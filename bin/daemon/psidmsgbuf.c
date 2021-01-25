/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "rdp.h"
#include "psitems.h"
#include "psprotocol.h"

#include "psidutil.h"

#include "psidmsgbuf.h"

/** Amount of payload data available within a small msgbuf */
#define MSGBUF_SMALLSIZE 64

/**
 * Message buffer used to temporarily store a message that cannot be
 * delivered to its destination right now.
 *
 * This type is used to handle the pre-alocated msgbufs holding small
 * messages.
 */
typedef struct {
    list_t next;                   /**< Use to put into msgbufFreeList, etc. */
    int offset;                    /**< Number of bytes already sent */
    char msg[MSGBUF_SMALLSIZE];    /**< The actual message to store */
} smallMsgBuf_t;

/** data structure to handle a pool of small message buffers*/
static PSitems_t smallMsgBufs = NULL;

/** Number of messages-buffers currently in use */
static unsigned int usedBufs = 0;

static bool relocSmallMsgBuf(void *item)
{
    smallMsgBuf_t *orig = item, *repl = PSitems_getItem(smallMsgBufs);

    if (!repl) return false;

    /* copy msgbuf's content */
    memcpy(&repl->msg, &orig->msg, sizeof(repl->msg));
    repl->offset = orig->offset;

    /* tweak the list */
    __list_add(&repl->next, orig->next.prev, orig->next.next);

    return true;
}

/**
 * @brief Garbage collection
 *
 * Do garbage collection on unused message buffers. Since this module
 * will keep pre-allocated buffers for small messages its
 * memory-footprint might have grown after phases of heavy
 * usage. Thus, this function shall be called regularly in order to
 * free() buffers no longer required.
 *
 * @return No return value.
 */
static void PSIDMsgbuf_gc(void)
{
    if (!PSitems_gcRequired(smallMsgBufs)) return;

    int blockedRDP = RDP_blockTimer(1);

    PSitems_gc(smallMsgBufs, relocSmallMsgBuf);

    RDP_blockTimer(blockedRDP);
}

PSIDmsgbuf_t *PSIDMsgbuf_get(size_t len)
{
    PSIDmsgbuf_t *mp;

    if (len <= MSGBUF_SMALLSIZE) {
	int blockedRDP = RDP_blockTimer(1);

	mp = PSitems_getItem(smallMsgBufs);

	RDP_blockTimer(blockedRDP);
    } else {
	mp = malloc(sizeof(*mp) + len);
    }

    if (!mp) {
	PSID_warn(-1, errno, "%s: malloc()", __func__);
	return NULL;
    }

    usedBufs++;

    INIT_LIST_HEAD(&mp->next);
    mp->offset = 0;

    DDMsg_t *msg = (DDMsg_t *)mp->msg;
    msg->len = 0;

    return mp;
}

void PSIDMsgbuf_put(PSIDmsgbuf_t *mp)
{
    DDMsg_t *msg = (DDMsg_t *)mp->msg;

    if (msg->len <= MSGBUF_SMALLSIZE) {
	PSitems_putItem(smallMsgBufs, mp);
    } else {
	free(mp);
    }

    usedBufs--;
}

void PSIDMsgbuf_printStat(void)
{
    PSID_log(-1, "%s: Buffers %d\n", __func__, usedBufs);
    PSID_log(-1, "%s: Small buffers %d/%d (used/avail)\n", __func__,
	     PSitems_getUsed(smallMsgBufs), PSitems_getAvail(smallMsgBufs));
}

void PSIDMsgbuf_init(void)
{
    if (PSitems_isInitialized(smallMsgBufs)) return;
    smallMsgBufs = PSitems_new(sizeof(smallMsgBuf_t), "smallMsgBufs");

    PSID_registerLoopAct(PSIDMsgbuf_gc);

    return;
}

void PSIDMsgbuf_clearMem(void)
{
    PSitems_clearMem(smallMsgBufs);
    smallMsgBufs = NULL;
    usedBufs = 0;
}
