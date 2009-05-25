/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2009 ParTec Cluster Competence Center GmbH, Munich
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
static LIST_HEAD(msgFreeList);

#define MESSAGES_CHUNK 1024

static int incFreeList(size_t size)
{
    msgbuf_t *buf = malloc(sizeof(msgbuf_t) * size);
    unsigned int i;

    if (!buf) return 0;

    for (i=0; i<size; i++) {
	buf[i].msg = NULL;
	buf[i].offset = 0;
	list_add_tail(&buf[i].next, &msgFreeList);
    }

    return 1;
}

void initMsgList(void)
{
    if (!incFreeList(MESSAGES_CHUNK)) {
	PSID_log(-1, "%s: no memory\n", __func__);
	exit(0);
    }

    return;
}

msgbuf_t *getMsg(void)
{
    msgbuf_t *mp;

    if (list_empty(&msgFreeList)) {
	PSID_log(PSID_LOG_COMM, "%s: no more elements\n", __func__);
	if (!incFreeList(MESSAGES_CHUNK)) {
	    PSID_log(-1, "%s: no memory\n", __func__);
	    return NULL;
	}
    }

    /* get list's first element */
    mp = list_entry(msgFreeList.next, msgbuf_t, next);
    list_del(&mp->next);

    mp->msg = NULL;
    mp->offset = 0;

    return mp;
}

void putMsg(msgbuf_t *mp)
{
    list_add_tail(&mp->next, &msgFreeList);
}

void freeMsg(msgbuf_t *mp)
{
    if (mp->msg) free(mp->msg);
    putMsg(mp);
}
