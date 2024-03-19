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
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/utsname.h>

#include "rrcomm.h"

/**
 * @brief rank in a parallel context
 *
 * Set from PS_JOB_RANK in initCoordinates() (default: assume to be alone)
 */
int rank = 0;

/**
 * @brief number of processes in a parallel context
 *
 * Set from PS_JOB_SIZE in initCoordinates() (default: assume to be alone)
 */
int num = 1;

/**
 * @brief local nodename
 *
 * Set utilizing uname(2) in initCoordinates()
 */
char *nodeName = NULL;

/** maxumim RRComm message size we want to test */
#define BUFSIZE 128*1024*1024

/**
 * @brief Init process' coordinates
 *
 * Set @ref rank, @ref num, and @ref nodeName from environment and
 * @ref uname.
 */
void initCoordinates(void)
{
    char *envStr = getenv("PS_JOB_RANK");
    if (envStr) rank = atoi(envStr);
    envStr = getenv("PS_JOB_SIZE");
    if (envStr) num = atoi(envStr);

    static struct utsname uBuf;
    uname(&uBuf);
    nodeName = uBuf.nodename;
}

static inline char date(int32_t rank, int32_t pos)
{
    const uint32_t min = 32, max = 128;
    return min + (rank + pos) % (max - min);
}

/** number of bytes to check in buffer */
#define NUM_CHECK 8

/**
 * @brief Check validity of received data
 *
 * Check the validity of the first and last @ref NUM_CHECK bytes of
 * the buffer @a rBuf of size @a size assuming the remote side at @a
 * srcRank uses the same data systematics as we do.
 *
 * @return Return true if data is as expected or false otherwise
 */
bool checkRecvBuf(char *rBuf, size_t size, int32_t srcRank)
{
    bool diffDetect = false;

    /* Check first elements */
    for (size_t i = 0; i < size && i < NUM_CHECK; i++) {
	char expect = date(srcRank, i);
	if (rBuf[i] != expect) {
	    printf("unexpected data at %zd: %d vs %d\n", i, rBuf[i], expect);
	    diffDetect = true;
	}
    }

    /* Check last elements if different from first */
    if (size < NUM_CHECK) return !diffDetect;
    for (size_t i = size - NUM_CHECK; i < size; i++) {
	char expect = date(srcRank, i);
	if (rBuf[i] != expect) {
	    printf("unexpected data at %zd: %d vs %d\n", i, rBuf[i], expect);
	    diffDetect = true;
	}
    }

    return !diffDetect;
}

/**
 * @brief Send some data
 *
 * Send some data from the buffer @a buf of size @a bufSize bytes to
 * @a dest. The amount of data sent depends on the destination rank @a
 * dest in the following way:
 *
 * numBytes = bufSize - 16 * dest
 *
 * but will be at least one byte to send
 *
 * @return No return value
 */
void sendBuf(int32_t dest, char *buf, size_t bufSize)
{
    ssize_t ret = RRC_send(dest, buf, bufSize);
    if (ret < 0) {
	printf("RRC_send(%d, %zd): %m\n", dest, bufSize);
    } else {
	printf("RRC_send(%d, %zd) sent %zd\n", dest, bufSize, ret);
    }
    fflush(stdout);
}

/** Buffer containing local data to send */
char dataBuf[BUFSIZE];

static char termBuf[] = "T.E.R.M.I.N.A.T.E";

static ssize_t allowed = 0;

bool recvBuf(int fd /* ignored */)
{
    /* prepare for receive */
    char buf[BUFSIZE];
    int32_t srcRank;
    ssize_t got = RRC_recv(&srcRank, buf, allowed);
    if (got > BUFSIZE) {
	printf("RRC_recv(%d) expects buffer of size %zd from %d\n", BUFSIZE,
	       got, srcRank);
	exit(1);
    } else if (got > allowed) {
	printf("RRC_recv(%zd) grow to %zd from %d\n", allowed, got, srcRank);
	allowed = got;
    } else if (got < 0) {
	if (!errno) {
	    printf("%s: RRC_send(%d) failed; try again\n", __func__, srcRank);
	    sendBuf(srcRank, termBuf, sizeof(termBuf));
	    return true;
	}
	printf("RRC_recv(): %m\n");
	exit(1);
    } else {
	printf("%s: got %zd from %d\n", __func__, got, srcRank);

	/* Check for terminate message */
	if (got == sizeof(termBuf) && !strncmp(termBuf, buf, got)) return false;

	/* Compare received data to sent data */
	checkRecvBuf(buf, got, srcRank);
    }
    return true;
}

/**
 * @brief Poll on RRC file descriptor
 *
 * Poll on the RRC file descriptor @a fd until the terminate message
 * @ref termBuf is received or the timeout of @a timeout milliseconds
 * has elapsed.
 *
 * @param fd File descriptor to poll on
 *
 * @param timeout Milliseconds of timeout
 *
 * @return No return value
 */
void pollRRC(int fd, int timeout)
{
    /* we'll poll on just one fd */
    struct pollfd pollfd = {.fd = fd, .events = POLLIN | POLLPRI, .revents = 0};

    /* prepare timeout helpers */
    struct timeval now, end = { .tv_sec = 0, .tv_usec = 0 };
    if (!(timeout < 0)) {
	struct timeval delta = { .tv_sec = timeout/1000,
				 .tv_usec = (timeout % 1000) * 1000 };
	gettimeofday(&now, NULL);
	timeradd(&now, &delta, &end);
    }

    while (timeout < 0 || timercmp(&now, &end, <)) {
	int tmout = timeout;
	if (!(timeout < 0)) {
	    struct timeval delta;
	    gettimeofday(&now, NULL);               /* get NEW starttime */
	    timersub(&end, &now, &delta);
	    if (delta.tv_sec < 0) timerclear(&delta);
	    tmout = delta.tv_sec * 1000 + delta.tv_usec / 1000 + 1;
	}

	int ret = poll(&pollfd, 1, tmout);

	if (ret < 0) {
	    printf("%s: poll(): %m\n", __func__);
	} else if (ret > 0) {
	    /* just one fd to control */
	    if (!recvBuf(fd)) {
		printf("%s: job done\n", __func__);
		return;
	    }
	} else {
	    printf("%s: poll() timed out\n", __func__);
	}
	fflush(stdout);
	gettimeofday(&now, NULL);  /* get NEW starttime */
    }
}

void runTest(void)
{
    int fd = RRC_init();
    if (fd < 0) {
	printf("RRC_init(): %m\n");
	return;
    }

    printf("rank %d of %d @ %s connected on fd %d\n", rank, num, nodeName, fd);
    fflush(stdout);

    if (rank < num - 1) {
	/* first num - 1 ranks receive */
	pollRRC(fd, -1);
    } else {
	/* last rank sends */
	for (int32_t r = 0; r < num - 1; r++) {
	    sendBuf(r, dataBuf, sizeof(dataBuf)/4);
	    sendBuf(r, NULL, 0);
	    sendBuf(r, dataBuf, sizeof(dataBuf)/2);
	    sendBuf(r, dataBuf, sizeof(dataBuf));
	    sendBuf(r, termBuf, sizeof(termBuf));
	}
	/* wait for possible errors */
	pollRRC(fd, 1000 /* 1 sec */);
    }

    RRC_finalize();
}

int main(void)
{
    initCoordinates();

    /* setup data to send */
    for (size_t i = 0; i < sizeof(dataBuf); i++) dataBuf[i] = date(rank, i);

    char *envStr = getenv("__RRCOMM_SOCKET");
    printf("RRComm socket expected at '%s'\n", envStr ? envStr : "???");
    fflush(stdout);

    runTest();

    printf("\nsecond round\n\n");
    fflush(stdout);

    runTest();

    printf("done\n");

    return 0;
}
