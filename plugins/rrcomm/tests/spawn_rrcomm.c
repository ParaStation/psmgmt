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
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rrcomm.h"
#include "pscommon.h"

#define DEBUG 0

#define SLEEP 1
#define EXTRA_SLEEP 1

bool verbose = false;

/* Global coordinates */
int depth = 1;
int worldSize = -1;
int rank = -1;

#define clog(fmt, ...)					\
    {							\
	printf("(%d/%d/%d) ", rank, worldSize, depth);	\
	printf(fmt __VA_OPT__(,) __VA_ARGS__);		\
    }

#define cdbg(...) if (DEBUG || !rank) clog(__VA_ARGS__);

#define mlog(fmt, ...)				\
    printf(fmt __VA_OPT__(,) __VA_ARGS__);

#define mdbg(...) if (DEBUG || !rank) mlog(__VA_ARGS__)

/**
 * Check answer content for consistency
 */
static void checkContent(char *msgBuf, PStask_ID_t xpctdJob, int32_t xpctdRank)
{
    int32_t cntntRank, cntntWorld;
    PStask_ID_t cntntJob;
    sscanf(msgBuf, "%d %d %d", &cntntJob, &cntntRank, &cntntWorld);
    if (xpctdJob != cntntJob || xpctdRank != cntntRank
	|| worldSize != cntntWorld) {
	clog("content mismatch got %s/%d/%d",
	     PSC_printTID(cntntJob), cntntRank, cntntWorld);
	mlog(" expected %s/%d/%d\n",
	     PSC_printTID(xpctdJob), xpctdRank, worldSize);
	return;
    }

    return;
}

/**
 * Check answer for consistency
 */
static void checkAnswer(ssize_t recvd, int eno, char *msgBuf, size_t bufSize,
			PStask_ID_t recvJob, int32_t recvRank,
			PStask_ID_t xpctdJob, int32_t xpctdRank, bool content)
{
    if (recvd < 0) {
	if (eno) {
	    errno = eno;
	    clog("RRC_recv[X](): %m\n");
	    MPI_Abort(MPI_COMM_WORLD, -1);
	} else {
	    clog("RRC_recv[X]: error from %s/%d\n",
		 PSC_printTID(recvJob), recvRank);
	    return;
	}
    }
    if ((size_t)recvd > bufSize) {
	clog("huge message (%zd > %zd) from %s/%d\n", recvd, bufSize,
	     PSC_printTID(recvJob), recvRank);
	MPI_Abort(MPI_COMM_WORLD, -1);
    }
    if (recvJob != xpctdJob || recvRank != xpctdRank) {
	clog("unexpected message from %s/%d", PSC_printTID(recvJob), recvRank);
	mlog(" expected from %s/%d\n", PSC_printTID(xpctdJob), xpctdRank);
	return;
    }
    /* check content size*/
    if ((size_t)recvd != strlen(msgBuf)+1) {
	clog("unexpected content size (%zd vs %zd) from %s/%d\n",
	     recvd, strlen(msgBuf), PSC_printTID(recvJob), recvRank);
	return;
    }
    /* check content */
    if (content) checkContent(msgBuf, xpctdJob, xpctdRank);
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
    snprintf(sendBuf, sizeof(sendBuf), "%d %d %d", jobID, rank, worldSize);

    // right with compatibility: RRC_send() -> RRC_recv()
    if (verbose) cdbg("test 1\n");
    RRC_send((rank + 1) % worldSize, sendBuf, strlen(sendBuf) + 1);
    ssize_t recvd = RRC_recv(&recvRank, recvBuf, sizeof(recvBuf));
    checkAnswer(recvd, errno, recvBuf, sizeof(recvBuf),	jobID, recvRank,
		jobID, (rank + worldSize - 1) % worldSize, true);

    // second right with mixed I: RRC_send() -> RRC_recvX()
    if (verbose) cdbg("test 3\n");
    RRC_send((rank + 1) % worldSize, sendBuf, strlen(sendBuf) + 1);
    recvd = RRC_recvX(&recvJob, &recvRank, recvBuf, sizeof(recvBuf));
    checkAnswer(recvd, errno, recvBuf, sizeof(recvBuf),	recvJob, recvRank,
		jobID, (rank + worldSize - 1) % worldSize, true);

    MPI_Barrier(MPI_COMM_WORLD);

    // left with new version: RRC_sendX() -> RRC_recvX()
    if (verbose) cdbg("test 2\n");
    RRC_sendX(0, (rank + worldSize - 1) % worldSize,
	      sendBuf, strlen(sendBuf) + 1);
    recvd = RRC_recvX(&recvJob, &recvRank, recvBuf, sizeof(recvBuf));
    checkAnswer(recvd, errno, recvBuf, sizeof(recvBuf), recvJob, recvRank,
		jobID, (rank + 1) % worldSize, true);

    // second left with mixed II: RRC_sendX() -> RRC_recv()
    if (verbose) cdbg("test 4\n");
    RRC_sendX(jobID, (rank + worldSize - 1) % worldSize,
	      sendBuf, strlen(sendBuf) + 1);
    recvd = RRC_recv(&recvRank, recvBuf, sizeof(recvBuf));
    checkAnswer(recvd, errno, recvBuf, sizeof(recvBuf),	jobID, recvRank,
		jobID, (rank + 1) % worldSize, true);

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
    PStask_ID_t recvJob;
    int32_t recvRank;

    cdbg("waiting for descendant\n");

    char recvBuf[128];

    /* waiting to get contacted */
    ssize_t recvd = RRC_recvX(&recvJob, &recvRank, recvBuf, sizeof(recvBuf));
    checkAnswer(recvd, errno, recvBuf, sizeof(recvBuf),	recvJob, recvRank,
		recvJob, (rank + worldSize - 1) % worldSize, true);

    /* send the received message to next remote rank */
    RRC_sendX(recvJob, (rank + 1) % worldSize, recvBuf, recvd);
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
    PStask_ID_t jobID = RRC_getJobID(), recvJob;
    int32_t recvRank;

    cdbg("contacting ancestor %s\n", PSC_printTID(ancestor));

    char buf[128];
    snprintf(buf, sizeof(buf), "%d %d %d", jobID, rank, worldSize);

    /* send message of next remote rank */
    RRC_sendX(ancestor, (rank + 1) % worldSize, buf, strlen(buf) + 1);

    /* waiting for answer */
    ssize_t recvd = RRC_recvX(&recvJob, &recvRank, buf, sizeof(buf));
    /* message content expected from previous but one rank in local job */
    checkAnswer(recvd, errno, buf, sizeof(buf),	recvJob, recvRank,
		ancestor, (rank + worldSize - 1) % worldSize, false);
    checkContent(buf, jobID, (rank + worldSize - 2) % worldSize);
}

int main( int argc, char *argv[] )
{
    MPI_Init(&argc, &argv);

    /* setup coordinates */
    if (argc > 1) depth = strtol(argv[1], NULL, 0);
    MPI_Comm_size(MPI_COMM_WORLD, &worldSize);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int len;
    char name[MPI_MAX_PROCESSOR_NAME];
    MPI_Get_processor_name(name, &len);

    clog("pid %d on node '%s' pinned to %s\n",
	 getpid(), name, getenv("__PINNING__"));
    if (depth > 0) cdbg("%d more level(s)\n", depth);

    clog("RRCOMM_SOCKET_ENV '%s'\n", getenv("__RRCOMM_SOCKET"));
    if (RRC_init() < 0) {
	clog("RRC_init() failed: %m\n");
	MPI_Abort(MPI_COMM_WORLD, -1);
    }

    clog("RRComm version %d, jobID %s\n",
	 RRC_getVersion(), PSC_printTID(RRC_getJobID()));


    MPI_Comm parent_comm;
    MPI_Comm_get_parent(&parent_comm);
    char rootJobStr[64];
    if (parent_comm == MPI_COMM_NULL) {
	/* root parent */
	snprintf(rootJobStr, sizeof(rootJobStr), "%d", RRC_getJobID());
    } else {
	if (argc < 4) {
	    clog("no root/parent job\n");
	    MPI_Abort(MPI_COMM_WORLD, -1);
	}
	snprintf(rootJobStr, sizeof(rootJobStr), "%s", argv[2]);
    }

    /* first check ring communication within local job */
    testInsideJob();

    /* spawn the next level if required */
    if (depth > 0) {
	char newDepth[16];
	snprintf(newDepth, sizeof(newDepth), "%d", depth - 1);
	char parentJobStr[64];
	snprintf(parentJobStr, sizeof(parentJobStr), "%d", RRC_getJobID());
	char *newArgV[4] = { newDepth, rootJobStr, parentJobStr, NULL };

	MPI_Info info;
	MPI_Info_create(&info);
	char wdir[PATH_MAX];
	char *cwd = getcwd(wdir, sizeof(wdir));
	MPI_Info_set(info, "wdir", cwd);

	MPI_Comm spawn_comm;
	int errcodes[worldSize];
	MPI_Comm_spawn(argv[0], newArgV, worldSize, info,
		       0, MPI_COMM_WORLD, &spawn_comm, errcodes);
	if (spawn_comm == MPI_COMM_NULL) {
	    clog("spawn_comm is NULL?!\n");
	} else {
	    MPI_Comm_disconnect(&spawn_comm);
	}
	//sleep(depth * SLEEP + EXTRA_SLEEP);
    }

    // Test connection between parent and child
    cdbg("parent test\n");
    if (parent_comm != MPI_COMM_NULL) {
	// not a leaf job: wait for message from children
	if (depth > 0) handleDescendant();

	// non root job: contact parent job
	PStask_ID_t parentJobID;
	sscanf(argv[3], "%d", &parentJobID);
	contactAncestor(parentJobID);
    } else {
	// root job: only wait for message from children
	if (depth > 0) handleDescendant();
    }

    // Test connection between root and descendant
    cdbg("root test\n");
    if (parent_comm == MPI_COMM_NULL) {
	for (int d = 0; d < depth; d++) handleDescendant();
    } else {
	PStask_ID_t rootJobID;
	sscanf(argv[2], "%d", &rootJobID);
	contactAncestor(rootJobID);
	MPI_Comm_disconnect(&parent_comm);
    }

    MPI_Finalize();

    clog("Finalize done\n");

    return 0;
}
