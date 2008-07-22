/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>

#include "psidutil.h"

#include "psidmsgbuf.h"

/**
 * Pool of message buffers ready to use. Initialized by initMsgList().
 * To get a buffer from this pool, use getMsg(), to put it back into
 * it use putMsg().
 */
static msgbuf_t *msgFreeList;

#define NUM_MESSAGES 1024

static msgbuf_t *getInitializedList(size_t size)
{
    msgbuf_t *buf = malloc(sizeof(msgbuf_t) * size);

    if (buf) {
	unsigned int i;

	for (i=0; i<size; i++) {
	    buf[i].msg = NULL;
	    buf[i].offset = 0;
	    buf[i].next = &buf[i+1];
	}
	buf[size-1].next = NULL;
    }

    return buf;
}

void initMsgList(void)
{
    msgFreeList = getInitializedList(NUM_MESSAGES);

    if (!msgFreeList) {
	PSID_log(-1, "%s: no memory\n", __func__);
	exit(0);
    }

    return;
}

msgbuf_t *getMsg(void)
{
    msgbuf_t *mp = msgFreeList;

    if (!mp) {
	PSID_log(PSID_LOG_COMM, "%s: no more elements\n", __func__);

	msgFreeList = getInitializedList(NUM_MESSAGES);

	if (!msgFreeList) {
	    PSID_log(-1, "%s: no memory\n", __func__);
	    return NULL;
	}

	mp = msgFreeList;
    }

    msgFreeList = msgFreeList->next;
    mp->msg = NULL;
    mp->offset = 0;
    mp->next = NULL;

    return mp;
}

void putMsg(msgbuf_t *mp)
{
    mp->next = msgFreeList;
    msgFreeList = mp;
    return;
}

void freeMsg(msgbuf_t *mp)
{
    if (mp->msg) free(mp->msg);
    putMsg(mp);
}
