/*
 * ParaStation
 *
 * Copyright (C) 2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <assert.h>
#include <errno.h>
#include <linux/limits.h>
#include <pmix.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

#include "rrcomm.h"
#undef MIN
#include "pscommon.h"

#define DEBUG 0

#define SLEEP 1
#define EXTRA_SLEEP 1

/* Number of executable the spawned world will be split into */
#define N_EX 1

/* Switch between direct and twisted ping-pong across jobs*/
#define TWISTED_PINGPONG false

bool verbose = false;

static pmix_proc_t myproc;

/* Global coordinates */
int depth = 1;
int wSize = -1;
int rank = -1;

#define clog(fmt, ...)					\
    {							\
	printf("(%d/%d(%d)) ", depth, rank, wSize);	\
	printf(fmt __VA_OPT__(,) __VA_ARGS__);		\
	fflush(stdout);					\
    }

#define cdbg(...) if (DEBUG || !rank) clog(__VA_ARGS__);

#define mlog(fmt, ...)				\
    printf(fmt __VA_OPT__(,) __VA_ARGS__);

#define mdbg(...) if (DEBUG || !rank) mlog(__VA_ARGS__)


void jobBarrier(void)
{
    pmix_proc_t proc;
    PMIX_PROC_CONSTRUCT(&proc);
    PMIX_LOAD_PROCID(&proc, myproc.nspace, PMIX_RANK_WILDCARD);
    int rc = PMIx_Fence(&proc, 1, NULL, 0);
    if (rc != PMIX_SUCCESS) {
	clog("PMIx_Fence failed: %s\n", PMIx_Error_string(rc));
	PMIx_Abort(-1, __func__, NULL, 0);
    }
}

int jobSize(void)
{
    pmix_proc_t proc;
    PMIX_PROC_CONSTRUCT(&proc);
    PMIX_LOAD_PROCID(&proc, myproc.nspace, PMIX_RANK_WILDCARD);

    pmix_value_t *val = NULL;
    int rc = PMIx_Get(&proc, PMIX_JOB_SIZE, NULL, 0, &val);
    if (rc != PMIX_SUCCESS) {
	clog("PMIx_Get(job size) failed: %s\n", PMIx_Error_string(rc));
	PMIx_Abort(-1, __func__, NULL, 0);
    }

    uint32_t size = val->data.uint32;
    PMIX_VALUE_RELEASE(val);

    return size;
}

/**
 * Check answer content for consistency
 */
static bool checkContent(char *msgBuf, PStask_ID_t xpctdJob, int32_t xpctdRank)
{
    int32_t cntntRank, cntntWorld;
    PStask_ID_t cntntJob;
    sscanf(msgBuf, "%ld %d %d", &cntntJob, &cntntRank, &cntntWorld);
    if (xpctdJob != cntntJob || xpctdRank != cntntRank || wSize != cntntWorld) {
	clog("content mismatch got %s/%d/%d",
	     PSC_printTID(cntntJob), cntntRank, cntntWorld);
	mlog(" expected %s/%d/%d\n", PSC_printTID(xpctdJob), xpctdRank, wSize);
	return false;
    }

    return true;
}

/**
 * Check answer for consistency
 */
static bool checkAnswer(ssize_t recvd, int eno, char *msgBuf, size_t bufSize,
			PStask_ID_t recvJob, int32_t recvRank,
			PStask_ID_t xpctdJob, int32_t xpctdRank, bool content)
{
    if (recvd < 0) {
	if (eno) {
	    errno = eno;
	    clog("RRC_recv[X](): %m\n");
	    PMIx_Abort(-1, __func__, NULL, 0);
	} else {
	    clog("RRC_recv[X]: error reported on %s/%d\n",
		 PSC_printTID(recvJob), recvRank);
	    return false;
	}
    }
    if ((size_t)recvd > bufSize) {
	clog("huge message (%zd > %zd) from %s/%d\n", recvd, bufSize,
	     PSC_printTID(recvJob), recvRank);
	PMIx_Abort(-1, __func__, NULL, 0);
    }
    if (recvJob != xpctdJob || recvRank != xpctdRank) {
	clog("unexpected message from %s/%d", PSC_printTID(recvJob), recvRank);
	mlog(" expected from %s/%d\n", PSC_printTID(xpctdJob), xpctdRank);
	return false;
    }
    /* check content size*/
    if ((size_t)recvd != strlen(msgBuf)+1) {
	clog("unexpected content size (%zd vs %zd) from %s/%d\n",
	     recvd, strlen(msgBuf), PSC_printTID(recvJob), recvRank);
	return false;
    }
    /* check content */
    return content ? checkContent(msgBuf, xpctdJob, xpctdRank) : true;
}

/**
 * RRComm tests inside a single job
 *
 * Check RRComm functionality within a single job. For this messages
 * are sent around the (cyclic) ring of processes forming the job
 */
static void testInsideJob(void)
{
    PStask_ID_t jobID = RRC_getJobID(), recvJob;
    int32_t recvRank;

    cdbg("in-job ring test\n");

    char sendBuf[128], recvBuf[128];
    snprintf(sendBuf, sizeof(sendBuf), "%ld %d %d", jobID, rank, wSize);

    // right with compatibility: RRC_send() -> RRC_recv()
    if (verbose) cdbg("test 1\n");
    RRC_send((rank + 1) % wSize, sendBuf, strlen(sendBuf) + 1);
    ssize_t recvd = RRC_recv(&recvRank, recvBuf, sizeof(recvBuf));
    if (!checkAnswer(recvd, errno, recvBuf, sizeof(recvBuf), jobID, recvRank,
		     jobID, (rank + wSize - 1) % wSize, true)) {
	clog("test 1 failed\n");
	clog("RRC_send to %d RRC_recv from %d\n", (rank + 1) % wSize,
	     (rank + wSize - 1) % wSize);
    }

    // second right with mixed I: RRC_send() -> RRC_recvX()
    if (verbose) cdbg("test 3\n");
    RRC_send((rank + 1) % wSize, sendBuf, strlen(sendBuf) + 1);
    recvd = RRC_recvX(&recvJob, &recvRank, recvBuf, sizeof(recvBuf));
    if (!checkAnswer(recvd, errno, recvBuf, sizeof(recvBuf), recvJob, recvRank,
		     jobID, (rank + wSize - 1) % wSize, true)) {
	clog("test 3 failed\n");
	clog("RRC_send to %d RRC_recvX from %d\n", (rank + 1) % wSize,
	     (rank + wSize - 1) % wSize);
    }


    jobBarrier();

    // left with new version: RRC_sendX() -> RRC_recvX()
    if (verbose) cdbg("test 2\n");
    RRC_sendX(0, (rank + wSize - 1) % wSize, sendBuf, strlen(sendBuf) + 1);
    recvd = RRC_recvX(&recvJob, &recvRank, recvBuf, sizeof(recvBuf));
    if (!checkAnswer(recvd, errno, recvBuf, sizeof(recvBuf), recvJob, recvRank,
		     jobID, (rank + 1) % wSize, true)) {
	clog("test 2 failed\n");
	clog("RRC_sendX to %d RRC_recvX from %d\n", (rank + wSize - 1) % wSize,
	     (rank + 1) % wSize);
    }

    // second left with mixed II: RRC_sendX() -> RRC_recv()
    if (verbose) cdbg("test 4\n");
    RRC_sendX(jobID, (rank + wSize - 1) % wSize, sendBuf, strlen(sendBuf) + 1);
    recvd = RRC_recv(&recvRank, recvBuf, sizeof(recvBuf));
    if (!checkAnswer(recvd, errno, recvBuf, sizeof(recvBuf), jobID, recvRank,
		     jobID, (rank + 1) % wSize, true)) {
	clog("test 4 failed\n");
	clog("RRC_sendX to %d RRC_recv from %d\n", (rank + wSize - 1) % wSize,
	     (rank + 1) % wSize);
    }

    jobBarrier();

    cdbg("done\n\n");

    return;
}

/**
 * RRComm tests across jobs -- server part
 *
 * Check RRComm functionality across jobs. For this messages are sent
 * in between the (cyclic) rings of processes forming the two job.
 *
 * This is the "server" part of the communication called within the
 * ancestor job. Since there is no knowledge on the descendants it
 * must be waited to get contacted by the descendant job to handle.
 *
 * The communication strategy is to first expect a message from the
 * previous rank in the other job and then to send the received
 * message content to the next rank in the other job.
 */
static void handleDescendant(void)
{
    cdbg("waiting for descendant\n");

    int32_t destRank = TWISTED_PINGPONG ? (rank + 1) % wSize : rank;
    int32_t xpctdRank = TWISTED_PINGPONG ? (rank + wSize - 1) % wSize : rank;

    /* waiting to get contacted */
    PStask_ID_t recvJob;
    int32_t recvRank;
    char recvBuf[128];
    ssize_t recvd = RRC_recvX(&recvJob, &recvRank, recvBuf, sizeof(recvBuf));
    if (!checkAnswer(recvd, errno, recvBuf, sizeof(recvBuf), recvJob, recvRank,
		     recvJob, xpctdRank, true))
	clog("%s failed\n", __func__);

    /* send the received message to next remote rank */
    RRC_sendX(recvJob, destRank, recvBuf, recvd);
}

/**
 * RRComm tests across jobs -- client part
 *
 * Check RRComm functionality across jobs. For this messages are sent
 * in between the (cyclic) rings of processes forming the two job.
 *
 * This is the "client" part of the communication called within the
 * descendant job knowing the ancestor job @a remoteJob to contact.
 *
 * The communication strategy is to first send a message to the next
 * rank in the other job and then to to expect a message from the
 * previous rank in the other job
 */
static void contactAncestor(PStask_ID_t ancestor)
{
    cdbg("contacting ancestor %s\n", PSC_printTID(ancestor));

    int32_t destRank = TWISTED_PINGPONG ? (rank + 1) % wSize : rank;
    int32_t xpctdRank = TWISTED_PINGPONG ? (rank + wSize - 1) % wSize : rank;
    int32_t cntndRank = TWISTED_PINGPONG ? (rank + wSize - 2) % wSize : rank;

    char buf[128];
    snprintf(buf, sizeof(buf), "%ld %d %d", RRC_getJobID(), rank, wSize);

    /* send message of next remote rank */
    RRC_sendX(ancestor, destRank, buf, strlen(buf) + 1);

    /* waiting for answer */
    PStask_ID_t recvJob;
    int32_t recvRank;
    ssize_t recvd = RRC_recvX(&recvJob, &recvRank, buf, sizeof(buf));
    /* message content expected from previous but one rank in local job */
    if (!checkAnswer(recvd, errno, buf, sizeof(buf), recvJob, recvRank,
		     ancestor, xpctdRank, false)
	|| !checkContent(buf, RRC_getJobID(), cntndRank))
	clog("%s failed\n", __func__);
}

int main( int argc, char *argv[] )
{
    int rc = PMIx_Init(&myproc, NULL, 0);
    if (rc != PMIX_SUCCESS) {
	clog("PMIx_Init failed: %s\n", PMIx_Error_string(rc));
	exit(0);
    }

    /* setup coordinates */
    rank = myproc.rank;
    wSize = jobSize();
    if (argc > 1) depth = strtol(argv[1], NULL, 0);

    char name[HOST_NAME_MAX+1];
    if (gethostname(name, sizeof(name)) != 0) {
	clog("gethostname failed: %m\n");
	PMIx_Abort(-1, __func__, NULL, 0);
    }

    if (wSize % N_EX) {
	cdbg("world size %d not multiple of N_EX %d\n", wSize, N_EX);
	PMIx_Abort(-1, __func__, NULL, 0);
    }

    clog("pid %d on node '%s' pinned to %s\n",
	 getpid(), name, getenv("__PINNING__"));
    if (depth > 0) cdbg("%d more level(s)\n", depth);

    clog("RRCOMM_SOCKET_ENV '%s'\n", getenv("__RRCOMM_SOCKET"));
    if (RRC_init() < 0) {
	clog("RRC_init() failed: %m\n");
	PMIx_Abort(-1, __func__, NULL, 0);
    }

    clog("RRComm version %d, jobID %s\n",
	 RRC_getVersion(), PSC_printTID(RRC_getJobID()));


    bool rootJob = false;

    char rootJobStr[64];
    pmix_value_t *val = NULL;
    /* get parent id (for non-spawned process: not found) */
    rc = PMIx_Get(&myproc, PMIX_PARENT_ID, NULL, 0, &val);
    if (rc == PMIX_ERR_NOT_FOUND) {
	/* root parent */
	rootJob = true;
	snprintf(rootJobStr, sizeof(rootJobStr), "%ld", RRC_getJobID());
    } else if (rc == PMIX_SUCCESS && val != NULL) {
	/* some spawned process */
	if (argc < 4) {
	    clog("no root/parent job\n");
	    PMIx_Abort(-1, __func__, NULL, 0);
	}
	snprintf(rootJobStr, sizeof(rootJobStr), "%s", argv[2]);
    } else {
	clog("PMIx_Get parent id failed: %s\n", PMIx_Error_string(rc));
	PMIx_Abort(-1, __func__, NULL, 0);
    }

    /* in contrast to MPI that does a synchronization in MPI_Init() an
     * explicit barrier is needed for PMIx before we can run the first
     * test */
    jobBarrier();

    /* first check ring communication within local job */
    testInsideJob();

    /* spawn the next level if required */
    if (myproc.rank == 0 && depth > 0) {
	char newDepth[16];
	snprintf(newDepth, sizeof(newDepth), "%d", depth - 1);
	char parentJobStr[64];
	snprintf(parentJobStr, sizeof(parentJobStr), "%ld", RRC_getJobID());

		/* Fill app data structure for spawning */
	pmix_app_t *app;
	PMIX_APP_CREATE(app, N_EX);
	for (int a = 0; a < N_EX; a++) {
	    app[a].maxprocs = wSize / N_EX;
	    app[a].cmd = strdup(argv[0]);
	    app[a].argv = (char **) malloc(5 * sizeof(char *));
	    app[a].argv[0] = strdup(argv[0]);
	    app[a].argv[1] = strdup(newDepth);
	    app[a].argv[2] = strdup(rootJobStr);
	    app[a].argv[3] = strdup(parentJobStr);
	    app[a].argv[4] = NULL;
	    app[a].env = NULL;
	    app[a].ninfo = 0;
	}

	char wDir[PATH_MAX];
	char *cwd = getcwd(wDir, sizeof(wDir));
	if (!cwd) {
	    clog("getcwd failed: %m\n");
	    PMIx_Abort(-1, __func__, NULL, 0);
	}

	pmix_info_t *job_info;
	PMIX_INFO_CREATE(job_info, 1);
	PMIX_INFO_LOAD(&(job_info[0]), PMIX_WDIR, wDir, PMIX_STRING);

	//log("Calling PMIx_Spawn\n");
	char nspace[PMIX_MAX_NSLEN + 1];
	rc = PMIx_Spawn(job_info, 1, app, N_EX, nspace);
	if (rc != PMIX_SUCCESS) {
	    clog("PMIx_Spawn failed: %s\n", PMIx_Error_string(rc));
	    PMIx_Abort(-1, __func__, NULL, 0);
	}
	PMIX_APP_FREE(app, N_EX);
	PMIX_INFO_FREE(job_info, 1);
    }

    // Test connection between parent and child
    cdbg("parent test\n");
    if (rootJob) {
	// root job: only wait for message from children if any
	if (depth > 0) handleDescendant();
    } else {
	// not a leaf job: wait for message from children
	if (depth > 0) handleDescendant();

	// non root job: contact parent job
	PStask_ID_t parentJobID;
	sscanf(argv[3], "%ld", &parentJobID);
	contactAncestor(parentJobID);
    }

    // Test connection between root and descendant
    cdbg("root test\n");
    if (rootJob) {
	for (int d = 0; d < depth; d++) handleDescendant();
    } else {
	PStask_ID_t rootJobID;
	sscanf(argv[2], "%ld", &rootJobID);
	contactAncestor(rootJobID);
    }

    // allow children's output to get forwarded
    if (myproc.rank == 0) usleep(depth * 100000);

    /* finalize */
    rc = PMIx_Finalize(NULL, 0);
    if (rc != PMIX_SUCCESS) {
	clog("PMIx_Finalize failed: %s\n", PMIx_Error_string(rc));
	PMIx_Abort(-1, __func__, NULL, 0);
    }

    clog("PMIx_Finalize succeeded\n");

    return 0;
}
