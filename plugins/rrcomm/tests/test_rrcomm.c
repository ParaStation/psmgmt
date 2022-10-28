/*
 * ParaStation
 *
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "rrcomm.h"

bool checkRecvBuf(char *rBuf, size_t size, int32_t srcRank)
{
    bool difference = false;
    for (size_t i = 0; i < size; i++) {
	char expected = 32 + (i+srcRank)%(128-32);
	if (rBuf[i] != expected) {
	    printf("unexpected data at %zd: %d vs %d\n", i, rBuf[i], expected);
	    difference = true;
	    if (i > 20) break;
	}
    }

    return !difference;
}

int main(void)
{
    char *envStr = getenv("__RRCOMM_SOCKET");
    printf("RRComm socket expected at '%s'\n", envStr ? envStr : "???");

    int fd = RRC_init();
    if (fd < 0) {
	printf("RRC_init(): %m\n");
	return 0;
    }

    envStr = getenv("PMI_RANK");
    if (!envStr) envStr = "0";
    int rank = atoi(envStr);
    envStr = getenv("PMI_SIZE");
    if (!envStr) envStr = "1";
    int num = atoi(envStr);

    printf("Connected on fd %d, my rank is %d of %d\n", fd, rank, num);

    //sleep(1); // give the other processes time to connect

    char sBuf[64*1024];
    for (size_t i = 0; i < sizeof(sBuf); i++) sBuf[i] = 32+(i+rank)%(128-32);

    int32_t dest = (rank + 1) % num;
    ssize_t ret = RRC_send(dest, sBuf, sizeof(sBuf));
    if (ret < 0) {
	printf("RRC_send(): %m\n");
	return 0;
    }
    printf("sent %zd to %d\n", ret, dest);
    fflush(stdout);

    char rBuf[64*1024];
    int32_t srcRank;
    ssize_t got = RRC_recv(&srcRank, rBuf, sizeof(rBuf));
    printf("got %zd from %d to %p\n", got, srcRank, rBuf);

    /* Compare received data to sent data */
    checkRecvBuf(rBuf, got, srcRank);
    fflush(stdout);

    /* Second round */
    /* choose a new destination */
    dest = (rank + num - 1) % num;
    ret = RRC_send(dest, sBuf, sizeof(sBuf)/2);
    if (ret < 0) {
	printf("RRC_send(): %m\n");
	return 0;
    }
    printf("sent %zd to %d\n", ret, dest);
    fflush(stdout);

    got = RRC_recv(&srcRank, rBuf, sizeof(rBuf));
    printf("got %zd from %d to %p\n", got, srcRank, rBuf);

    /* Compare received data to sent data */
    checkRecvBuf(rBuf, got, srcRank);

    RRC_finalize();

    printf("done\n");
    fflush(stdout);

    return 0;
}
