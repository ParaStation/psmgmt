/*
 * ParaStation
 *
 * Copyright (C) 2024-2025 ParTec AG, Munich
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

/*
 * Various tests of RRComm across jobs with re-spawned processes. This
 * is an MPI version, i.e. it requires MPI for building and running.
 *
 * The test works as follows if started with N processes:
 * - test ring communication within N processes of a generation
 * - spawn N child processes if next generation was requested
 * - wait for message from child process of same rank if any
 * - send message to parent process of same rank if any
 * - (if not root) send message to root process of same rank
 * - (if root) wait for message from all descendant processes of same rank
 *
 * Thus, all "generations" of processes consists of the same number of
 * processes (N) as started via srun. The number of generations might be
 * set on the command line; the default number of generations is 1
 *
 * In order to reserve sufficient resources for multilple generations
 * in Slurm an allocation has to be created, e.g. to get 4 nodes:
 *
 *  salloc -N 4
 *
 * To start an actual experiment with 5 processes per generation and a
 * total of 3 generations (root plus 2 descendant generations) use:
 *
 *  srun -n 5 --exact ./spawn_rrcomm 2
 */

#define DEBUG 0

/* Number of executable the spawned world will be split into */
#define N_EX 1

/* Switch between direct and twisted ping-pong across jobs*/
#define TWISTED_PINGPONG false

bool verbose = false;

/* Global coordinates */
int depth = 1;
int wSize = -1;
int rank = -1;

#define clog(fmt, ...)					\
    {							\
	printf("(%d/%d(%d)) ", depth, rank, wSize);	\
	printf(fmt __VA_OPT__(,) __VA_ARGS__);		\
    }

#define cdbg(...) if (DEBUG || !rank) clog(__VA_ARGS__);

#define mlog(fmt, ...)				\
    printf(fmt __VA_OPT__(,) __VA_ARGS__);

#define mdbg(...) if (DEBUG || !rank) mlog(__VA_ARGS__)

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
	    MPI_Abort(MPI_COMM_WORLD, -1);
	} else {
	    clog("RRC_recv[X]: error from %s/%d\n",
		 PSC_printTID(recvJob), recvRank);
	    return false;
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
		     jobID, (rank + wSize - 1) % wSize, true))
	clog("test 1 failed\n");

    // second right with mixed I: RRC_send() -> RRC_recvX()
    if (verbose) cdbg("test 3\n");
    RRC_send((rank + 1) % wSize, sendBuf, strlen(sendBuf) + 1);
    recvd = RRC_recvX(&recvJob, &recvRank, recvBuf, sizeof(recvBuf));
    if (!checkAnswer(recvd, errno, recvBuf, sizeof(recvBuf), recvJob, recvRank,
		     jobID, (rank + wSize - 1) % wSize, true))
	clog("test 3 failed\n");

    MPI_Barrier(MPI_COMM_WORLD);

    // left with new version: RRC_sendX() -> RRC_recvX()
    if (verbose) cdbg("test 2\n");
    RRC_sendX(0, (rank + wSize - 1) % wSize, sendBuf, strlen(sendBuf) + 1);
    recvd = RRC_recvX(&recvJob, &recvRank, recvBuf, sizeof(recvBuf));
    if (!checkAnswer(recvd, errno, recvBuf, sizeof(recvBuf), recvJob, recvRank,
		     jobID, (rank + 1) % wSize, true))
	clog("test 2 failed\n");

    // second left with mixed II: RRC_sendX() -> RRC_recv()
    if (verbose) cdbg("test 4\n");
    RRC_sendX(jobID, (rank + wSize - 1) % wSize, sendBuf, strlen(sendBuf) + 1);
    recvd = RRC_recv(&recvRank, recvBuf, sizeof(recvBuf));
    if (!checkAnswer(recvd, errno, recvBuf, sizeof(recvBuf), jobID, recvRank,
		     jobID, (rank + 1) % wSize, true))
	clog("test 4 failed\n");

    MPI_Barrier(MPI_COMM_WORLD);

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
    MPI_Init(&argc, &argv);

    /* setup coordinates */
    if (argc > 1) depth = strtol(argv[1], NULL, 0);
    MPI_Comm_size(MPI_COMM_WORLD, &wSize);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int len;
    char name[MPI_MAX_PROCESSOR_NAME];
    MPI_Get_processor_name(name, &len);

    if (wSize % N_EX) {
	cdbg("world size %d not multiple of N_EX %d\n", wSize, N_EX);
	MPI_Abort(MPI_COMM_WORLD, -1);
    }

    clog("pid %d on node '%s' pinned to %s\n",
	 getpid(), name, getenv("__PINNING__"));
    if (depth > 0) cdbg("%d more level(s)\n", depth);

    clog(" PMI_ID is %s\n", getenv("PMI_ID"));
    clog(" PMI_RANK is %s\n", getenv("PMI_RANK"));
    clog(" PMIX_NAMESPACE is %s\n", getenv("PMIX_NAMESPACE"));
    clog(" PMIX_RANK is %s\n", getenv("PMIX_RANK"));
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
	snprintf(rootJobStr, sizeof(rootJobStr), "%ld", RRC_getJobID());
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
	snprintf(parentJobStr, sizeof(parentJobStr), "%ld", RRC_getJobID());

	char *cmds[N_EX];
	for (int i = 0; i < N_EX; i++) cmds[i] = argv[0];
	char **argmnts[N_EX];
	for (int i = 0; i < N_EX; i++) {
	    argmnts[i] = malloc(sizeof(char *) * 4);
	    argmnts[i][0] = newDepth;
	    argmnts[i][1] = rootJobStr;
	    argmnts[i][2] = parentJobStr;
	    argmnts[i][3] = NULL;
	}
	int nps[N_EX];
	for (int i = 0; i < N_EX; i++) nps[i] = wSize / N_EX;

	char wdir[PATH_MAX];
	char *cwd = getcwd(wdir, sizeof(wdir));
	MPI_Info infos[N_EX];
	for (int i = 0; i < N_EX; i++) {
	    MPI_Info_create(&infos[i]);
	    MPI_Info_set(infos[i], "wdir", cwd);
	}

	MPI_Comm spawn_comm;
	int errcds[wSize];
	MPI_Comm_spawn_multiple(N_EX, cmds, argmnts, nps, infos, 0,
				MPI_COMM_WORLD, &spawn_comm, errcds);
	if (spawn_comm == MPI_COMM_NULL) {
	    clog("spawn_comm is NULL?!\n");
	} else {
	    MPI_Comm_disconnect(&spawn_comm);
	}
    }

    // Test connection between parent and child
    cdbg("parent test\n");
    if (parent_comm != MPI_COMM_NULL) {
	// not a leaf job: wait for message from children
	if (depth > 0) handleDescendant();

	// non root job: contact parent job
	PStask_ID_t parentJobID;
	sscanf(argv[3], "%ld", &parentJobID);
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
	sscanf(argv[2], "%ld", &rootJobID);
	contactAncestor(rootJobID);
	MPI_Comm_disconnect(&parent_comm);
    }

    MPI_Finalize();

    clog("Finalize done\n");

    return 0;
}
