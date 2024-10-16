/*
 * ParaStation
 *
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "rrcomm.h"

/* rank that delays call of RRC_init() or -1 for no delay */
#define DELAY_RANK -1

/* amount of delay before calling RRC_init on rank DELAY_RANK */
#define DELAY 3

/* number of bytes to check in buffer */
#define NUM_CHECK 5

/* check validity of the first NUM_CHECK bytes of rBuf */
bool checkRecvBuf(char *rBuf, size_t size, int32_t srcRank)
{
    bool difference = false;
    for (size_t i = 0; i < size; i++) {
	char expected = 32 + (i+srcRank)%(128-32);
	if (rBuf[i] != expected) {
	    printf("unexpected data at %zd: %d vs %d\n", i, rBuf[i], expected);
	    difference = true;
	    if (i > NUM_CHECK) break;
	}
    }

    return !difference;
}

/* send bufSize bytes of buf to dest */
void sendBuf(int32_t dest, char *buf, size_t bufSize)
{
    ssize_t ret = RRC_send(dest, buf, bufSize);
    if (ret < 0) {
	printf("RRC_send(): %m\n");
	exit(0);
    }
    printf("sent %zd to %d\n", ret, dest);
    fflush(stdout);
}

int main(void)
{
    /* total number of resends */
    int tries = 0;

    char *envStr = getenv("__RRCOMM_SOCKET");
    printf("RRComm socket expected at '%s'\n", envStr ? envStr : "???");

    envStr = getenv("PS_JOB_RANK");
    if (!envStr) envStr = "0";
    int rank = atoi(envStr);
    envStr = getenv("PS_JOB_SIZE");
    if (!envStr) envStr = "1";
    int num = atoi(envStr);

    struct utsname uBuf;
    uname(&uBuf);

    if (rank == DELAY_RANK) {
	printf("\ndelay RRC_init() on %s (rank %d) by %d sec\n\n",
	       uBuf.nodename, rank, DELAY);
	fflush(stdout);
	sleep(DELAY);
    }

    int fd = RRC_init();
    if (fd < 0) {
	printf("RRC_init(): %m\n");
	return 0;
    }

    printf("Connected at %s on fd %d, my rank is %d of %d\n", uBuf.nodename,
	   fd, rank, num);

    /* setup data to send */
    char sBuf[64*1024];
    for (size_t i = 0; i < sizeof(sBuf); i++) sBuf[i] = 32+(i+rank)%(128-32);

    /* choose two destinations */
    int32_t dest1 = (rank + 1) % num, dest2 = (rank + num - 1) % num;

    sendBuf(dest1, sBuf, sizeof(sBuf));

    /* prepare for receive */
    char rBuf[64*1024];
    int32_t srcRank;
    ssize_t got;

recv1Again:
    got = RRC_recv(&srcRank, rBuf, sizeof(rBuf));
    if (got < 0) {
	if (!errno) {
	    printf("RRC_send(%d) failed; try again\n", srcRank);
	    sendBuf(srcRank, sBuf, sizeof(sBuf));
	    if (tries++ < 5) goto recv1Again;
	}
	printf("RRC_recv(): %m\n");
	exit(0);
    }
    printf("got %zd from %d to %p\n", got, srcRank, rBuf);

    /* Compare received data to sent data */
    checkRecvBuf(rBuf, got, srcRank);
    fflush(stdout);

    /* Second round */

    sendBuf(dest2, sBuf, sizeof(sBuf)/2);

recv2Again:
    got = RRC_recv(&srcRank, rBuf, sizeof(rBuf));
    if (got < 0) {
	if (!errno) {
	    printf("RRC_send(%d) failed; try again\n", srcRank);
	    sendBuf(srcRank, sBuf, sizeof(sBuf) / (srcRank == dest2 ? 2 : 1));
	    if (tries++ < 5) goto recv2Again;
	}
	printf("RRC_recv(): %m\n");
	exit(0);
    }
    printf("got %zd from %d to %p\n", got, srcRank, rBuf);

    /* Compare received data to sent data */
    checkRecvBuf(rBuf, got, srcRank);

    RRC_finalize();

    printf("done\n");
    fflush(stdout);

    return 0;
}
