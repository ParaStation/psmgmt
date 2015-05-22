/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdlib.h>
#include <errno.h>

#include "selector.h"

#include "pscommon.h"
#include "psdaemonprotocol.h"

#include "psidnodes.h"
#include "psidcomm.h"
#include "psidutil.h"
#include "psidtask.h"
#include "psidclient.h"

#include "psidflowcontrol.h"

/** StopTID structure */
typedef struct {
    list_t next;              /**< used to put into hashtable-lists */
    PStask_ID_t tid;          /**< unique task identifier */
} stopTID_t;

/* ==================== Chunk management for stopTIDs ==================== */

/**
 * Number of stopTID structures allocated at once. Ensure this chunk is
 * larger than 128 kB to force it into mmap()ed memory
 */
#define STOPTID_CHUNK 8192

/** Some magic values */
#define UNUSED -1   /**< stopTID is unused, i.e. no TID is stored */
#define DRAINED -2  /**< stopTID is drained, i.e. not available */

/** Single chunk of stopTID structs allocated at once within incFreeList() */
typedef struct {
    list_t next;                      /**< Use to put into chunkList */
    stopTID_t stids[STOPTID_CHUNK];   /**< the stopTID structures */
} stopTID_chunk_t;

/**
 * Pool of stopTID structures ready to use. Initialized by @ref
 * initStopTIDList(). To get a buffer from this pool, use @ref
 * getStopTID(), to put it back into it use @ref putStopTID().
 */
static LIST_HEAD(stopTIDFreeList);

/** List of chunks of stopTID structures */
static LIST_HEAD(chunkList);

/** Number of stopTID structures currently in use */
static unsigned int usedStopTIDs = 0;

/** Total number of stopTID structures currently available */
static unsigned int availStopTIDs = 0;

/**
 * @brief Increase stopTID structures
 *
 * Increase the number of available stopTID structures. For that, a
 * chunk of @ref STOPTID_CHUNK stopTID structures is allocated. All
 * stopTID structures are appended to the list of free stopTID
 * structures @ref sigFreeList. Additionally, the chunk is registered
 * within @ref chunkList. Chunks might be released within @ref
 * PSIDFlwCntrl_gc() as soon as enough stopTID structures are available
 * again.
 *
 * return On success, 1 is returned. Or 0, if allocating the required
 * memory failed. In the latter case errno is set appropriately.
 */
static int incFreeList(void)
{
    stopTID_chunk_t *chunk = malloc(sizeof(*chunk));
    unsigned int i;

    if (!chunk) return 0;

    list_add_tail(&chunk->next, &chunkList);

    for (i=0; i<STOPTID_CHUNK; i++) {
	chunk->stids[i].tid = UNUSED;
	list_add_tail(&chunk->stids[i].next, &stopTIDFreeList);
    }

    availStopTIDs += STOPTID_CHUNK;
    PSID_log(PSID_LOG_FLWCNTRL, "%s: now used %d.\n", __func__, availStopTIDs);

    return 1;
}

/**
 * @brief Get stopTID structure from pool
 *
 * Get a stopTID structure from the pool of free stopTID structures. If
 * there is no structure left in the pool, this will be extended by
 * @ref STOPTID_CHUNK structures via calling @ref incFreeList().
 *
 * The stopTID structure returned will be prepared, i.e. the
 * list-handle @a next is initialized, the tid is set to UNUSED, etc.
 *
 * @return On success, a pointer to the new stopTID structure is
 * returned. Or NULL, if an error occurred.
 */
static stopTID_t *getStopTID(void)
{
    stopTID_t *sp;

    if (list_empty(&stopTIDFreeList)) {
	PSID_log(PSID_LOG_FLWCNTRL, "%s: no more elements\n", __func__);
	if (!incFreeList()) {
	    PSID_log(-1, "%s: no memory\n", __func__);
	    return NULL;
	}
    }

    /* get list's first usable element */
    sp = list_entry(stopTIDFreeList.next, stopTID_t, next);
    if (sp->tid != UNUSED) {
	PSID_log(-1, "%s: TID is %s. Never be here.\n", __func__,
		 (sp->tid == DRAINED) ? "DRAINED" : PSC_printTID(sp->tid));
	return NULL;
    }

    list_del(&sp->next);

    INIT_LIST_HEAD(&sp->next);

    usedStopTIDs++;

    return sp;
}

/**
 * @brief Put stopTID structure back into pool
 *
 * Put the stopTID structure @a sp back into the pool of free stopTID
 * structures. The stopTID structure might get reused and handed back
 * to the application by calling @ref getStopTID().
 *
 * @param sp Pointer to the stopTID structure to be put back into the
 * pool.
 *
 * @return No return value
 */
static void putStopTID(stopTID_t *sp)
{
    sp->tid = UNUSED;
    list_add_tail(&sp->next, &stopTIDFreeList);

    usedStopTIDs--;
}

/**
 * @brief Free a chunk of stopTID structures
 *
 * Free the chunk of stopTID structures @a chunk. For that, all empty
 * stopTID structures from this chunk are removed from @ref
 * stopTIDFreeList and marked as drained. All stopTID structures still
 * in use are replaced by using other free stopTID structure from @ref
 * stopTIDFreeList.
 *
 * Once all stopTID structures of the chunk are empty, the whole chunk
 * is free()ed.
 *
 * @param chunk The chunk of stopTID structures to free.
 *
 * @return No return value.
 */
static void freeChunk(stopTID_chunk_t *chunk)
{
    unsigned int i;

    if (!chunk) return;

    /* First round: remove stopTID structs from stopTIDFreeList */
    for (i=0; i<STOPTID_CHUNK; i++) {
	if (chunk->stids[i].tid == UNUSED) {
	    list_del(&chunk->stids[i].next);
	    chunk->stids[i].tid = DRAINED;
	}
    }

    /* Second round: now copy and release all used stopTID structs */
    for (i=0; i<STOPTID_CHUNK; i++) {
	stopTID_t *old = &chunk->stids[i], *new;

	if (old->tid == DRAINED) continue;

	if (old->tid == UNUSED) {
	    list_del(&old->next);
	    old->tid = DRAINED;
	} else {
	    new = getStopTID();
	    if (!new) {
		PSID_log(-1, "%s: new is NULL\n", __func__);
		return;
	    }

	    /* copy stopTID struct's content */
	    new->tid = old->tid;

	    /* tweak the list */
	    __list_add(&new->next, old->next.prev, old->next.next);

	    old->tid = DRAINED;
	}
	usedStopTIDs--;
    }

    /* Now that the chunk is completely empty, free() it */
    list_del(&chunk->next);
    free(chunk);
    availStopTIDs -= STOPTID_CHUNK;
    PSID_log(PSID_LOG_FLWCNTRL, "%s: now used %d.\n", __func__, availStopTIDs);
}

/**
 * @brief Garbage collection
 *
 * Do garbage collection on unused stopTID structures. Since this
 * module will keep pre-allocated buffers for stopTID structures its
 * memory-footprint might have grown after phases of heavy
 * usage. Thus, this function shall be called regularly in order to
 * free() stopTID structures no longer required.
 *
 * @return No return value.
 */
static void PSIDFlwCntrl_gc(void)
{
    list_t *c, *tmp;
    unsigned int i;
    int first = 1;

    if ((int)usedStopTIDs > (int)availStopTIDs/2 - STOPTID_CHUNK) return;

    PSID_log(PSID_LOG_FLWCNTRL, "%s()\n", __func__);

    list_for_each_safe(c, tmp, &chunkList) {
	stopTID_chunk_t *chunk = list_entry(c, stopTID_chunk_t, next);
	int unused = 0;

	if (first) {
	    first = 0;
	    continue;
	}

	for (i=0; i<STOPTID_CHUNK; i++) {
	    if (chunk->stids[i].tid == UNUSED
		|| chunk->stids[i].tid == DRAINED) {
		unused++;
	    }
	}

	if (unused > STOPTID_CHUNK/2) freeChunk(chunk);

	if (availStopTIDs == STOPTID_CHUNK) break; /* keep the last one */
    }
}

/* ==================== Hash management for stopTIDs ==================== */

void PSIDFlwCntrl_initHash(PSIDFlwCntrl_hash_t hash)
{
    int i;

    for (i = 0; i < FLWCNTRL_HASH_SIZE; i++) INIT_LIST_HEAD(&hash[i]);
}

void PSIDFlwCntrl_emptyHash(PSIDFlwCntrl_hash_t hash)
{
    int i;

    for (i = 0; i < FLWCNTRL_HASH_SIZE; i++) {
	if (!list_empty(&hash[i])) {
	    list_t *s, *tmp;
	    list_for_each_safe(s, tmp, &hash[i]) {
		stopTID_t *stop = list_entry(s, stopTID_t, next);

		list_del(&stop->next);
		putStopTID(stop);
	    }
	}
    }
}

int PSIDFlwCntrl_addStop(PSIDFlwCntrl_hash_t table, PStask_ID_t key)
{
    list_t *t;
    stopTID_t *new;

    list_for_each(t, &table[key%FLWCNTRL_HASH_SIZE]) {
	stopTID_t *stid = list_entry(t, stopTID_t, next);

	if (stid->tid == key) return 0;
    }

    new = getStopTID();
    if (!new) return -1;

    new->tid = key;
    list_add_tail(&new->next, &table[key%FLWCNTRL_HASH_SIZE]);

    return 1;
}

int PSIDFlwCntrl_sendContMsgs(PSIDFlwCntrl_hash_t stops, PStask_ID_t sender)
{
    int idx, num = 0;
    list_t *s, *tmp;

    for (idx = 0; idx < FLWCNTRL_HASH_SIZE; idx++) {
	list_for_each_safe(s, tmp, &stops[idx]) {
	    stopTID_t *stop = list_entry(s, stopTID_t, next);
	    if (PSC_getPID(stop->tid) && PSIDnodes_isUp(PSC_getID(stop->tid))) {
		DDMsg_t contmsg = { .type = PSP_DD_SENDCONT,
				    .sender = sender,
				    .dest = stop->tid,
				    .len = sizeof(DDMsg_t) };
		sendMsg(&contmsg);
		num++;
	    }
	    list_del(&stop->next);
	    putStopTID(stop);
	}
    }

    return num;
}

int PSIDFlwCntrl_applicable(DDMsg_t *msg)
{
    return (PSC_getPID(msg->sender)
	    && msg->type != PSP_CD_ACCOUNT
	    && msg->type != PSP_CD_SENDCONT
	    && msg->type != PSP_CD_SENDSTOP
	    && msg->type != PSP_DD_SENDCONT
	    && msg->type != PSP_DD_SENDSTOP
	    && msg->type != PSP_DD_SENDSTOPACK);
}

/**
 * @brief Handle PSP_DD_SENDSTOP message
 *
 * Handle the PSP_DD_SENDSTOP message @a msg.
 *
 * By sending such message the sending process requests the receiving
 * process to stop sending further messages to this
 * destination. Depending on a additional flag within @a msg the
 * sending process might expect an acknowledgment of the SENDSTOP to
 * take effect.
 *
 * This mechanism is used in order to implement a flow control on the
 * communication path between client processes or a client process and
 * a remote daemon process.
 *
 * @param msg The message to handle.
 *
 * @return No return value.
 */
static void msg_SENDSTOP(DDTypedMsg_t *msg)
{
    PStask_t *task = PStasklist_find(&managedTasks, msg->header.dest);
    DDMsg_t ackmsg = { .type = PSP_DD_SENDSTOPACK,
		       .sender = msg->header.dest,
		       .dest = msg->header.sender,
		       .len = sizeof(DDMsg_t) };

    if (!task) return;

    PSID_log(PSID_LOG_FLWCNTRL, "%s: from %s\n",
	     __func__, PSC_printTID(msg->header.sender));

    if (task->group == TG_LOGGER) {
	msg->header.type = PSP_CD_SENDSTOP;
	sendMsg(msg);
    } else if (task->fd != -1) {
	PSID_log(PSID_LOG_FLWCNTRL,
		 "%s: client %s at %d temporarily disabled\n", __func__,
		 PSC_printTID(msg->header.dest), task->fd);

	if (!task->activeStops) Selector_disable(task->fd);
	task->activeStops++;
    }
    if (msg->header.len > sizeof(DDMsg_t) && msg->type) sendMsg(&ackmsg);
}

/**
 * @brief Handle PSP_DD_SENDSTOPACK message
 *
 * Handle the PSP_DD_SENDSTOPACK message @a msg.
 *
 * This type of message is sent on request of the originator of the
 * corresponding SENDSTOP message. It acknowledges that the request to
 * stop sending further messages has taken effect.
 *
 * This mechanism is used in order to implement a flow control on the
 * communication path between client processes or a client process and
 * a remote daemon process.
 *
 * @param msg The message to handle.
 *
 * @return No return value.
 */
static void msg_SENDSTOPACK(DDMsg_t *msg)
{
    PStask_t *task = PStasklist_find(&managedTasks, msg->dest);

    if (!task) return;

    PSID_log(PSID_LOG_FLWCNTRL, "%s: from %s\n",
	     __func__, PSC_printTID(msg->sender));

    if (task->fd != -1) releaseACKClient(task->fd);
}

/**
 * @brief Handle PSP_DD_SENDCONT message
 *
 * Handle the PSP_DD_SENDCONT message @a msg.
 *
 * By sending such message the sending process requests the receiving
 * process to continue sending messages to this destination.
 *
 * This mechanism is used in order to implement a flow control on the
 * communication path between client processes or a client process and
 * a remote daemon process.
 *
 * @param msg The message to handle.
 *
 * @return No return value.
 */
static void msg_SENDCONT(DDMsg_t *msg)
{
    PStask_t *task = PStasklist_find(&managedTasks, msg->dest);

    if (!task) return;

    PSID_log(PSID_LOG_FLWCNTRL, "%s: from %s\n",
	     __func__, PSC_printTID(msg->sender));

    if (task->group == TG_LOGGER) {
	msg->type = PSP_CD_SENDCONT;
	sendMsg(msg);
    } else if (task->fd != -1) {
	PSID_log(PSID_LOG_FLWCNTRL,
		 "%s: client %s at %d re-enabled\n", __func__,
		 PSC_printTID(msg->dest), task->fd);

	task->activeStops--;
	if (!task->activeStops) Selector_enable(task->fd);

    }
}

void PSIDFlwCntrl_printStat(void)
{
    PSID_log(-1, "%s: Stops %d/%d (used/avail)\n", __func__,
	     usedStopTIDs, availStopTIDs);
}


void PSIDFlwCntrl_init(void)
{
    if (availStopTIDs) return;

    if (!incFreeList()) PSID_exit(ENOMEM, "%s", __func__);

    PSID_registerMsg(PSP_DD_SENDSTOP, (handlerFunc_t) msg_SENDSTOP);
    PSID_registerMsg(PSP_DD_SENDCONT, (handlerFunc_t) msg_SENDCONT);
    PSID_registerMsg(PSP_DD_SENDSTOPACK, (handlerFunc_t) msg_SENDSTOPACK);

    PSID_registerLoopAct(PSIDFlwCntrl_gc);

    return;
}
