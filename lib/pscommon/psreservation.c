/*
 * ParaStation
 *
 * Copyright (C) 2015 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdlib.h>

#include "pscommon.h"
#include "list.h"

#include "psreservation.h"

/**
 * Number of reservation structures allocated at once. Ensure this
 * chunk is larger than 128 kB to force it into mmap()ed memory
 */
#define RESERVATION_CHUNK (int)((128*1024)/sizeof(PSrsrvtn_t) + 1)

/**
 * Single chunk of reservation structures allocated at once within
 * incFreeList()
 */
typedef struct {
    list_t next;                        /**< Use to put into chunkList */
    PSrsrvtn_t ress[RESERVATION_CHUNK]; /**< the reservation structures */
} res_chunk_t;

/**
 * Pool of reservation strucures ready to use. Initialized by @ref
 * incFreeList(). To get a buffer from this pool, use @ref
 * Psrsrvtn_get(), to put it back into it use @ref Psrsrvtn_put().
 */
static LIST_HEAD(resFreeList);

/** List of chunks of reservation structures */
static LIST_HEAD(chunkList);

/** Number of reservation structures currently in use */
static unsigned int usedRess = 0;

/** Total number of reservation structures currently available */
static unsigned int availRess = 0;

/**
 * @brief Increase reservation structures
 *
 * Increase the number of available reservation structures. For that,
 * a chunk of @ref RESERVATION_CHUNK reservation structures is
 * allocated. All reservation structures are appended to the list of
 * free reservation structures @ref resFreeList. Additionally, the
 * chunk is registered within @ref chunkList. Chunks might be released
 * within @ref PSrsrvtn_gc() as soon as enough reservation structures
 * are available again.
 *
 * return On success, 1 is returned. Or 0, if allocating the required
 * memory failed. In the latter case errno is set appropriately.
 */
static int incFreeList(void)
{
    res_chunk_t *chunk = malloc(sizeof(*chunk));
    unsigned int i;

    if (!chunk) return 0;

    list_add_tail(&chunk->next, &chunkList);

    for (i=0; i<RESERVATION_CHUNK; i++) {
	chunk->ress[i].state = RES_UNUSED;
	list_add_tail(&chunk->ress[i].next, &resFreeList);
    }

    availRess += RESERVATION_CHUNK;
    PSC_log(PSC_LOG_TASK, "%s: now used %d.\n", __func__, availRess);

    return 1;
}

PSrsrvtn_t *PSrsrvtn_get(void)
{
    PSrsrvtn_t *rp;

    if (list_empty(&resFreeList)) {
	PSC_log(PSC_LOG_TASK, "%s: no more elements\n", __func__);
	if (!incFreeList()) {
	    PSC_log(-1, "%s: no memory\n", __func__);
	    return NULL;
	}
    }

    /* get list's first usable element */
    rp = list_entry(resFreeList.next, PSrsrvtn_t, next);
    if (rp->state != RES_UNUSED) {
	PSC_log(-1, "%s: %s reservation. Never be here.\n", __func__,
		(rp->state == RES_USED) ? "USED" : "DRAINED");
	return NULL;
    }

    list_del(&rp->next);

    INIT_LIST_HEAD(&rp->next);
    rp->task = 0;
    rp->requester = 0;
    rp->nMin = 0;
    rp->nMax = 0;
    rp->tpp = 1;
    rp->hwType = 0;
    rp->options = 0;
    rp->rid = 0;
    rp->firstRank = 0;
    rp->nSlots = 0;
    rp->slots = NULL;
    rp->nextSlot = 0;
    rp->checked = 0;
    rp->dynSent = 0;
    rp->state = RES_USED;

    usedRess++;

    return rp;
}

void PSrsrvtn_put(PSrsrvtn_t *rp)
{
    rp->state = RES_UNUSED;
    if (rp->slots) {
	PSC_log(-1, "%s: there are still slots in registration %#x\n", __func__,
		rp->rid);
	free(rp->slots);
    }
    rp->task = 0;
    rp->rid = 0;
    rp->firstRank = 0;
    rp->nSlots = 0;
    rp->slots = NULL;
    rp->nextSlot = 0;
    rp->checked = 0;
    rp->dynSent = 0;
    list_add_tail(&rp->next, &resFreeList);

    usedRess--;
}

/**
 * @brief Free a chunk of reservation structures
 *
 * Free the chunk of reservation structures @a chunk. For that, all
 * empty reservation structures from this chunk are removed from @ref
 * resFreeList and marked as drained. All reservation structures still
 * in use are replaced by using other free reservation structure from
 * @ref resFreeList.
 *
 * Once all reservation structures of the chunk are empty, the whole
 * chunk is free()ed.
 *
 * @param chunk The chunk of reservation structures to free.
 *
 * @return No return value.
 */
static void freeChunk(res_chunk_t *chunk)
{
    unsigned int i;

    if (!chunk) return;

    /* First round: remove reservation structs from resFreeList */
    for (i=0; i<RESERVATION_CHUNK; i++) {
	if (chunk->ress[i].state == RES_UNUSED) {
	    list_del(&chunk->ress[i].next);
	    chunk->ress[i].state = RES_DRAINED;
	}
    }

    /* Second round: now copy and release all used reservation structs */
    for (i=0; i<RESERVATION_CHUNK; i++) {
	PSrsrvtn_t *old = &chunk->ress[i], *new;

	if (old->state == RES_DRAINED) continue;

	new = (PSrsrvtn_t*) PSrsrvtn_get();
	if (!new) {
	    PSC_log(-1, "%s: new is NULL\n", __func__);
	    return;
	}

	/* copy reservation struct's content */
	new->task = old->task;
	new->requester = old->requester;
	new->nMin = old->nMin;
	new->nMax = old->nMax;
	new->tpp = old->tpp;
	new->hwType = old->hwType;
	new->options = old->options;
	new->rid = old->rid;
	new->firstRank = old->firstRank;
	new->nSlots = old->nSlots;
	new->slots = old->slots;
	old->slots = NULL;
	new->nextSlot = old->nextSlot;
	new->checked = old->checked;
	new->dynSent = old->dynSent;

	/* tweak the list */
	__list_add(&new->next, old->next.prev, old->next.next);

	old->state = RES_DRAINED;

	usedRess--;
    }

    /* Now that the chunk is completely empty, free() it */
    list_del(&chunk->next);
    free(chunk);
    availRess -= RESERVATION_CHUNK;
    PSC_log(PSC_LOG_TASK, "%s: now used %d.\n", __func__, availRess);
}

void PSrsrvtn_gc(void)
{
    list_t *c, *tmp;
    unsigned int i;
    int first = 1;

    PSC_log(PSC_LOG_TASK, "%s()\n", __func__);

    if ((int)usedRess > (int)availRess/2 - RESERVATION_CHUNK) return;

    list_for_each_safe(c, tmp, &chunkList) {
	res_chunk_t *chunk = list_entry(c, res_chunk_t, next);
	int unused = 0;

	if (first) {
	    first = 0;
	    continue;
	}

	for (i=0; i<RESERVATION_CHUNK; i++) {
	    if (chunk->ress[i].state != RES_USED) unused++;
	}

	if (unused > RESERVATION_CHUNK/2) freeChunk(chunk);

	if (availRess == RESERVATION_CHUNK) break; /* keep the last one */
    }
}

int PSrsrvtn_gcRequired(void)
{
    PSC_log(PSC_LOG_TASK, "%s()\n", __func__);

    if ((int)usedRess > (int)availRess/2 - RESERVATION_CHUNK) return 0;

    return 1;
}

void PSrsrvtn_printStat(void)
{
    PSC_log(-1, "%s: Reservations %d/%d (used/avail)\n", __func__,
	    usedRess, availRess);
}
