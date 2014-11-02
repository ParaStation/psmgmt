/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2014 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>

#include "rdp.h"

#include "psidutil.h"
#include "psidtimer.h"

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
} msgbuf_small_t;

/**
 * Number of small msgbufs allocated at once. Ensure this chunk is
 * larger than 128 kB to put it into mmap()ed memory
 */
#define MSGBUF_SCHUNK 2048

/** Single chunk of small msgbufs allocated at once within incFreeList() */
typedef struct {
    list_t next;                         /**< Use to put into chunkList */
    msgbuf_small_t bufs[MSGBUF_SCHUNK];  /**< the message buffers */
} msgbuf_schunk_t;

/**
 * Pool of small message buffers ready to use. Initialized by
 * @ref initMsgbufList(). To get a buffer from this pool, use @ref
 * getSmallMsgbuf(), to put it back into it use @ref putSmallMsgbuf().
 */
static LIST_HEAD(msgbufFreeList);

/** List of chunks of small msgbufs */
static LIST_HEAD(chunkList);

/** Number of messages-buffers currently in use */
static unsigned int usedBufs = 0;

/** Total number of small message-buffers currently available */
static unsigned int smallBufs = 0;

/** Number of small message-buffers currently in use */
static unsigned int usedSmallBufs = 0;

#define UNUSED -1
#define DRAINED -2

/**
 * @brief Increase small msgbufs
 *
 * Increase the number of available small msgbufs. For that, a chunk
 * of @ref MSGBUF_SCHUNK small msgbufs is allocated. All msgbufs are
 * appended to the list of free small msgbufs @ref
 * msgbufFreeList. Additionally, the chunk is registered within @ref
 * chunkList. Chunks might be released within @ref PSIDMsgbuf_gc() as
 * soon as enough small msgbufs are again available.
 *
 * return On success, 1 is returned. Or 0, if allocation the required
 * memory failed. In the latter case errno is set appropriately.
 */
static int incFreeList(void)
{
    msgbuf_schunk_t *chunk = malloc(sizeof(*chunk));
    unsigned int i;

    if (!chunk) return 0;

    list_add_tail(&chunk->next, &chunkList);

    for (i=0; i<MSGBUF_SCHUNK; i++) {
	chunk->bufs[i].offset = UNUSED;
	list_add_tail(&chunk->bufs[i].next, &msgbufFreeList);
    }

    smallBufs += MSGBUF_SCHUNK;

    return 1;
}

/**
 * @brief Get small msgbuf from pool
 *
 * Get a small msgbuf from the pool of free msgbufs. If there is no
 * msgbuf left in the pool, it will be extended by @ref MSGBUF_SCHUNK
 * msgbufs via calling @ref incFreeList().
 *
 * The msgbuf returned will be prepared, i.e. offset is set to 0, the
 * list-handle @a next is initialized, etc.
 *
 * @return On success, a pointer to the new msgbuf is returned. Or
 * NULL, if an error occurred.
 */
static msgbuf_t *getSmallMsgbuf(void)
{
    msgbuf_t *mp;

    if (list_empty(&msgbufFreeList)) {
	PSID_log(PSID_LOG_COMM, "%s: no more elements\n", __func__);
	if (!incFreeList()) {
	    PSID_log(-1, "%s: no memory\n", __func__);
	    return NULL;
	}
    }

    /* get list's first usable element */
    mp = list_entry(msgbufFreeList.next, msgbuf_t, next);
    list_del(&mp->next);

    if (mp->offset == DRAINED) {
	PSID_log(-1, "%s: DRAINED msgbuf. Never be here.\n", __func__);
	return NULL;
    }

    INIT_LIST_HEAD(&mp->next);
    mp->offset = 0;

    usedSmallBufs++;

    return mp;
}

/**
 * @brief Put small msgbuf back into pool
 *
 * Put the small msgbuf @a mp back into the pool of free msgbufs. The
 * msgbuf might get reused and handed back to the application by
 * calling @ref getSmallMsgbuf().
 *
 * @param mp Pointer to the small msgbuf to be put back into the pool.
 *
 * @return No return value
 */
static void putSmallMsgbuf(msgbuf_t *mp)
{
    mp->offset = UNUSED;
    list_add_tail(&mp->next, &msgbufFreeList);

    usedSmallBufs--;
}

/**
 * @brief Free a chunk of small msgbufs
 *
 * Free the chunk of small msgbufs @a chunk. For that, all empty
 * msgbufs from this chunk are removed from @ref msgbufFreeList and
 * marked as drained. All small msgbufs still in use are emptied by
 * using other free msgbufs from @ref msgbufFreeList.
 *
 * Once all msgbufs of the chunk are empty, the whole chunk is free()ed.
 *
 * @param chunk The chunk of small msgbufs to free.
 *
 * @return No return value.
 */
static void freeChunk(msgbuf_schunk_t *chunk)
{
    unsigned int i;

    if (!chunk) return;

    /* First round: remove msgbufs from msgbufFreeList */
    for (i=0; i<MSGBUF_SCHUNK; i++) {
	if (chunk->bufs[i].offset == UNUSED) {
	    list_del(&chunk->bufs[i].next);
	    chunk->bufs[i].offset = DRAINED;
	}
    }

    /* Second round: now copy and release all used msgbufs */
    for (i=0; i<MSGBUF_SCHUNK; i++) {
	msgbuf_small_t *old = &chunk->bufs[i], *new;

	if (old->offset == DRAINED) continue;

	new = (msgbuf_small_t*) getSmallMsgbuf();

	/* copy msgbuf's content */
	memcpy(&new->msg, &old->msg, sizeof(new->msg));
	new->offset = old->offset;

	/* tweak the list */
	__list_add(&new->next, old->next.prev, old->next.next);

	old->offset = DRAINED;
	usedSmallBufs--;
    }

    /* Now that the chunk is completely empty, free() it */
    list_del(&chunk->next);
    free(chunk);
    smallBufs -= MSGBUF_SCHUNK;
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
    list_t *c, *tmp;
    int blockedCHLD, blockedRDP, first = 1;
    unsigned int i;

    if ((int)usedSmallBufs > (int)smallBufs/2 - MSGBUF_SCHUNK) return;

    blockedCHLD = PSID_blockSIGCHLD(1);
    blockedRDP = RDP_blockTimer(1);

    list_for_each_safe(c, tmp, &chunkList) {
	msgbuf_schunk_t *chunk = list_entry(c, msgbuf_schunk_t, next);
	int unused = 0;

	if (first) {
	    first = 0;
	    continue;
	}

	for (i=0; i<MSGBUF_SCHUNK; i++) {
	    if (chunk->bufs[i].offset == UNUSED) unused++;
	}

	if (unused > MSGBUF_SCHUNK/2) freeChunk(chunk);

	if (smallBufs == MSGBUF_SCHUNK) break; /* keep the last one */
    }

    RDP_blockTimer(blockedRDP);
    PSID_blockSIGCHLD(blockedCHLD);
}

msgbuf_t *PSIDMsgbuf_get(size_t len)
{
    msgbuf_t *mp;
    DDMsg_t *msg;

    if (len <= MSGBUF_SMALLSIZE) {
	int blockedCHLD = PSID_blockSIGCHLD(1);
	int blockedRDP = RDP_blockTimer(1);

	mp = getSmallMsgbuf();

	RDP_blockTimer(blockedRDP);
	PSID_blockSIGCHLD(blockedCHLD);
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

    msg = (DDMsg_t *)mp->msg;
    msg->len = 0;

    return mp;
}

void PSIDMsgbuf_put(msgbuf_t *mp)
{
    DDMsg_t *msg = (DDMsg_t *)mp->msg;

    if (msg->len <= MSGBUF_SMALLSIZE) {
	putSmallMsgbuf(mp);
    } else {
	free(mp);
    }

    usedBufs--;
}

void PSIDMsgbuf_printStat(void)
{
    PSID_log(-1, "%s: Buffers %d\n", __func__, usedBufs);
    PSID_log(-1, "%s: Small buffers %d/%d (used/avail)\n", __func__,
	     usedSmallBufs, smallBufs);
}

void PSIDMsgbuf_init(void)
{
    if (!incFreeList()) PSID_exit(ENOMEM, "%s", __func__);

    PSID_registerLoopAct(PSIDMsgbuf_gc);

    return;
}

void PSIDMsgbuf_clearMem(void)
{}
