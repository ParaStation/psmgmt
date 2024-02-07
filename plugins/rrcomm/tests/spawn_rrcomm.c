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

#define SLEEP 1
#define EXTRA_SLEEP 1

bool verbose = false;

/**
 * Check answer content for consistency
 */
static void checkContent(int32_t rank, int32_t world, int32_t depth,
			 char *msgBuf, PStask_ID_t xpctdJob, int32_t xpctdRank)
{
    int32_t cntntRank, cntntWorld;
    PStask_ID_t cntntJob;
    sscanf(msgBuf, "%d %d %d", &cntntJob, &cntntRank, &cntntWorld);
    if (xpctdJob != cntntJob || xpctdRank != cntntRank
	|| world != cntntWorld) {
	printf("(%d/%d/%d) content mismatch got %s/%d/%d",
	       rank, world, depth, PSC_printTID(cntntJob), cntntRank, cntntWorld);
	printf(" expected %s/%d/%d\n", PSC_printTID(xpctdJob), xpctdRank, world);
	return;
    }

    return;
}

/**
 * Check answer for consistency
 */
static void checkAnswer(int32_t rank, int32_t world, int32_t depth,
			ssize_t recvd, int eno, char *msgBuf, size_t bufSize,
			PStask_ID_t recvJob, int32_t recvRank,
			PStask_ID_t xpctdJob, int32_t xpctdRank, bool content)
{
    if (recvd < 0) {
	if (eno) {
	    errno = eno;
	    printf("(%d/%d/%d) RRC_recv[X](): %m\n", rank, world, depth);
	    MPI_Abort(MPI_COMM_WORLD, -1);
	} else {
	    printf("(%d/%d/%d) RRC_recv[X]: error from %s/%d\n", rank, world,
		   depth, PSC_printTID(recvJob), recvRank);
	    return;
	}
    }
    if ((size_t)recvd > bufSize) {
	printf("(%d/%d/%d) huge message (%zd > %zd) from %s/%d\n",
	       rank, world, depth, recvd, bufSize, PSC_printTID(recvJob),
	       recvRank);
	MPI_Abort(MPI_COMM_WORLD, -1);
    }
    if (recvJob != xpctdJob || recvRank != xpctdRank) {
	printf("(%d/%d/%d) unexpected message from %s/%d",
	       rank, world, depth, PSC_printTID(recvJob), recvRank);
	printf(" expected from %s/%d\n", PSC_printTID(xpctdJob), xpctdRank);
	return;
    }
    /* check content size*/
    if ((size_t)recvd != strlen(msgBuf)+1) {
	printf("(%d/%d/%d) unexpected content size (%zd vs %zd) from %s/%d\n",
	       rank, world, depth, recvd, strlen(msgBuf),
	       PSC_printTID(recvJob), recvRank);
	return;
    }
    /* check content */
    if (content) checkContent(rank, world, depth, msgBuf, xpctdJob, xpctdRank);
}

/**
 * RRComm tests inside a single job
 *
 * Check RRComm functionality within a single job. For this messages
 * are sent around the (cyclic) ring of processes forming the job
 */
static void testInsideJob(int32_t rank, int32_t world, int32_t depth)
{
    PStask_ID_t jobID = RRC_getJobID(), recvJob;
    int32_t recvRank;

    printf("(%d/%d/%d) in job ring test\n", rank, world, depth);

    char sendBuf[128], recvBuf[128];
    snprintf(sendBuf, sizeof(sendBuf), "%d %d %d", jobID, rank, world);

    // right with compatibility: RRC_send() -> RRC_recv()
    if (verbose) printf("(%d/%d/%d) test 1\n", rank, world, depth);
    RRC_send((rank + 1) % world, sendBuf, strlen(sendBuf) + 1);
    ssize_t recvd = RRC_recv(&recvRank, recvBuf, sizeof(recvBuf));
    checkAnswer(rank, world, depth, recvd, errno, recvBuf, sizeof(recvBuf),
		jobID, recvRank, jobID, (rank + world - 1) % world, true);

    // second right with mixed I: RRC_send() -> RRC_recvX()
    if (verbose) printf("(%d/%d/%d) test 3\n", rank, world, depth);
    RRC_send((rank + 1) % world, sendBuf, strlen(sendBuf) + 1);
    recvd = RRC_recvX(&recvJob, &recvRank, recvBuf, sizeof(recvBuf));
    checkAnswer(rank, world, depth, recvd, errno, recvBuf, sizeof(recvBuf),
		recvJob, recvRank, jobID, (rank + world - 1) % world, true);

    MPI_Barrier(MPI_COMM_WORLD);

    // left with new version: RRC_sendX() -> RRC_recvX()
    if (verbose) printf("(%d/%d/%d) test 2\n", rank, world, depth);
    RRC_sendX(0, (rank + world - 1) % world, sendBuf, strlen(sendBuf) + 1);
    recvd = RRC_recvX(&recvJob, &recvRank, recvBuf, sizeof(recvBuf));
    checkAnswer(rank, world, depth, recvd, errno, recvBuf, sizeof(recvBuf),
		recvJob, recvRank, jobID, (rank + 1) % world, true);

    // second left with mixed II: RRC_sendX() -> RRC_recv()
    if (verbose) printf("(%d/%d/%d) test 4\n", rank, world, depth);
    RRC_sendX(jobID, (rank + world - 1) % world, sendBuf, strlen(sendBuf) + 1);
    recvd = RRC_recv(&recvRank, recvBuf, sizeof(recvBuf));
    checkAnswer(rank, world, depth, recvd, errno, recvBuf, sizeof(recvBuf),
		jobID, recvRank, jobID, (rank + 1) % world, true);

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
static void handleDescendant(int32_t rank, int32_t world, int32_t depth)
{
    PStask_ID_t recvJob;
    int32_t recvRank;

    printf("(%d/%d/%d) waiting for descendant\n", rank, world, depth);

    char recvBuf[128];

    /* waiting to get contacted */
    ssize_t recvd = RRC_recvX(&recvJob, &recvRank, recvBuf, sizeof(recvBuf));
    checkAnswer(rank, world, depth, recvd, errno, recvBuf, sizeof(recvBuf),
		recvJob, recvRank, recvJob, (rank + world - 1) % world, true);

    /* send the received message to next remote rank */
    RRC_sendX(recvJob, (rank + 1) % world, recvBuf, recvd);
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
static void contactAncestor(int32_t rank, int32_t world, int32_t depth,
			    PStask_ID_t ancestor)
{
    PStask_ID_t jobID = RRC_getJobID(), recvJob;
    int32_t recvRank;

    printf("(%d/%d/%d) contacting ancestor %s\n", rank, world, depth,
	   PSC_printTID(ancestor));

    char buf[128];
    snprintf(buf, sizeof(buf), "%d %d %d", jobID, rank, world);

    /* send message of next remote rank */
    RRC_sendX(ancestor, (rank + 1) % world, buf, strlen(buf) + 1);

    /* waiting for answer */
    ssize_t recvd = RRC_recvX(&recvJob, &recvRank, buf, sizeof(buf));
    /* message content expected from previous but one rank in local job */
    checkAnswer(rank, world, depth, recvd, errno, buf, sizeof(buf),
		recvJob, recvRank, ancestor, (rank + world - 1) % world, false);
    checkContent(rank, world, depth, buf, jobID, (rank + world - 2) % world);
}

int main( int argc, char *argv[] )
{
    MPI_Init(&argc, &argv);

    int depth = 1;
    if (argc > 1) depth = strtol(argv[1], NULL, 0);

    int worldSize;
    MPI_Comm_size(MPI_COMM_WORLD, &worldSize);
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int len;
    char name[MPI_MAX_PROCESSOR_NAME];
    MPI_Get_processor_name(name, &len);

    printf("(%d/%d/%d) pid %d on node '%s' pinned to %s\n",
	   rank, worldSize, depth, getpid(), name, getenv("__PINNING__"));
    if (depth > 0 && !rank) {
	printf("(%d/%d/%d): %d more levels\n", rank, worldSize, depth, depth);
    }

    printf("(%d/%d/%d) RRCOMM_SOCKET_ENV '%s'\n", rank, worldSize, depth,
	   getenv("__RRCOMM_SOCKET")); // @todo cleanup
    if (RRC_init() < 0) {
	printf("(%d/%d/%d) RRC_init() failed: %m\n", rank, worldSize, depth);
	MPI_Abort(MPI_COMM_WORLD, -1);
    }

    printf("(%d/%d/%d) RRComm version %d, jobID %s\n", rank, worldSize, depth,
	   RRC_getVersion(), PSC_printTID(RRC_getJobID()));


    MPI_Comm parent_comm;
    MPI_Comm_get_parent(&parent_comm);
    char rootJobStr[64];
    if (parent_comm == MPI_COMM_NULL) {
	/* root parent */
	snprintf(rootJobStr, sizeof(rootJobStr), "%d", RRC_getJobID());
    } else {
	if (argc < 4) {
	    printf("(%d/%d/%d) no root/parent job\n", rank, worldSize, depth);
	    MPI_Abort(MPI_COMM_WORLD, -1);
	}
	snprintf(rootJobStr, sizeof(rootJobStr), "%s", argv[2]);
    }

    /* first check ring communication within local job */
    testInsideJob(rank, worldSize, depth);

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
	    printf("(%d/%d) spawn_comm is NULL?!\n", rank, getpid());
	} else {
	    MPI_Comm_free(&spawn_comm);
	    //printf("(%d/%d) Freed spawn_comm\n", rank, getpid());
	}
	sleep(depth * SLEEP + EXTRA_SLEEP);
    }

    // Test connection between parent and child
    if (parent_comm != MPI_COMM_NULL) {
	printf("(%d/%d/%d) parent test\n", rank, worldSize, depth);
	PStask_ID_t parentJobID;
	sscanf(argv[3], "%d", &parentJobID);
	if (depth > 0) {
	    // not a leaf job: wait for message from children
	    handleDescendant(rank, worldSize, depth);
	}
	// non root job: contact parent job
	contactAncestor(rank, worldSize, depth, parentJobID);
    } else {
	// root job: only wait for message from children
	if (depth > 0) {
	    printf("(%d/%d/%d) parent test\n", rank, worldSize, depth);
	    handleDescendant(rank, worldSize, depth);
	}
    }

    // Test connection between root and descendant
    printf("(%d/%d/%d) root test\n", rank, worldSize, depth);
    if (parent_comm == MPI_COMM_NULL) {
	for (int d = 0; d < depth; d++) {
	    handleDescendant(rank, worldSize, depth);
	}
    } else {
	PStask_ID_t rootJobID;
	sscanf(argv[2], "%d", &rootJobID);
	contactAncestor(rank, worldSize, depth, rootJobID);

	//printf("(%d/%d) Freed spawn_comm\n", rank, getpid());
	MPI_Comm_free(&parent_comm);
    }

    MPI_Finalize();

    printf("(%d/%d/%d) Finalize done\n", rank, worldSize, depth);

    return 0;
}
