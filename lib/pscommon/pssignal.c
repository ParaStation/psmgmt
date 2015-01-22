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

#include "pssignal.h"

/**
 * Number of signal structures allocated at once. Ensure this chunk is
 * larger than 128 kB to force it into mmap()ed memory
 */
#define SIGNAL_CHUNK 8192

/** Single chunk of signal structures allocated at once within incFreeList() */
typedef struct {
    list_t next;                      /**< Use to put into chunkList */
    PSsignal_t sigs[SIGNAL_CHUNK];    /**< the signal structures */
} sig_chunk_t;

/**
 * Pool of signal strucures ready to use. Initialized by @ref
 * initSigList(). To get a buffer from this pool, use @ref
 * PSsignal_get(), to put it back into it use @ref PSsignal_put().
 */
static LIST_HEAD(sigFreeList);

/** List of chunks of signal structures */
static LIST_HEAD(chunkList);

/** Number of signal structures currently in use */
static unsigned int usedSigs = 0;

/** Total number of signal structures currently available */
static unsigned int availSigs = 0;

/**
 * @brief Increase signal structures
 *
 * Increase the number of available signal structures. For that, a
 * chunk of @ref SIGNAL_CHUNK signal structures is allocated. All
 * signal structures are appended to the list of free signal
 * structures @ref sigFreeList. Additionally, the chunk is registered
 * within @ref chunkList. Chunks might be released within @ref
 * PSsignal_gc() as soon as enough signal structures are available
 * again.
 *
 * return On success, 1 is returned. Or 0, if allocating the required
 * memory failed. In the latter case errno is set appropriately.
 */
static int incFreeList(void)
{
    sig_chunk_t *chunk = malloc(sizeof(*chunk));
    unsigned int i;

    if (!chunk) return 0;

    list_add_tail(&chunk->next, &chunkList);

    for (i=0; i<SIGNAL_CHUNK; i++) {
	chunk->sigs[i].state = SIG_UNUSED;
	list_add_tail(&chunk->sigs[i].next, &sigFreeList);
    }

    availSigs += SIGNAL_CHUNK;
    PSC_log(PSC_LOG_TASK, "%s: now used %d.\n", __func__, availSigs);

    return 1;
}

PSsignal_t *PSsignal_get(void)
{
    PSsignal_t *sp;

    if (list_empty(&sigFreeList)) {
	PSC_log(PSC_LOG_TASK, "%s: no more elements\n", __func__);
	if (!incFreeList()) {
	    PSC_log(-1, "%s: no memory\n", __func__);
	    return NULL;
	}
    }

    /* get list's first usable element */
    sp = list_entry(sigFreeList.next, PSsignal_t, next);
    if (sp->state != SIG_UNUSED) {
	PSC_log(-1, "%s: %s signal. Never be here.\n", __func__,
		(sp->state == SIG_USED) ? "USED" : "DRAINED");
	return NULL;
    }

    list_del(&sp->next);

    INIT_LIST_HEAD(&sp->next);
    sp->deleted = 0;
    sp->state = SIG_USED;

    usedSigs++;

    return sp;
}

void PSsignal_put(PSsignal_t *sp)
{
    sp->state = SIG_UNUSED;
    sp->deleted = 0;
    list_add_tail(&sp->next, &sigFreeList);

    usedSigs--;
}

/**
 * @brief Free a chunk of signal structures
 *
 * Free the chunk of signal structures @a chunk. For that, all empty
 * signal structures from this chunk are removed from @ref sigFreeList and
 * marked as drained. All signal structures still in use are replaced by
 * using other free signal structure from @ref sigFreeList.
 *
 * Once all signal structures of the chunk are empty, the whole chunk
 * is free()ed.
 *
 * @param chunk The chunk of signal structures to free.
 *
 * @return No return value.
 */
static void freeChunk(sig_chunk_t *chunk)
{
    unsigned int i;

    if (!chunk) return;

    /* First round: remove signal structs from sigFreeList */
    for (i=0; i<SIGNAL_CHUNK; i++) {
	if (chunk->sigs[i].state == SIG_UNUSED) {
	    list_del(&chunk->sigs[i].next);
	    chunk->sigs[i].state = SIG_DRAINED;
	}
    }

    /* Second round: now copy and release all used signal structs */
    for (i=0; i<SIGNAL_CHUNK; i++) {
	PSsignal_t *old = &chunk->sigs[i], *new;

	if (old->state == SIG_DRAINED) continue;

	if (old->deleted) {
	    list_del(&old->next);
	    old->state = SIG_DRAINED;
	} else {
	    new = (PSsignal_t*) PSsignal_get();
	    if (!new) {
		PSC_log(-1, "%s: new is NULL\n", __func__);
		return;
	    }

	    /* copy signal struct's content */
	    new->tid = old->tid;
	    new->signal = old->signal;
	    new->deleted = old->deleted;

	    /* tweak the list */
	    __list_add(&new->next, old->next.prev, old->next.next);

	    old->state = SIG_DRAINED;
	}
	usedSigs--;
    }

    /* Now that the chunk is completely empty, free() it */
    list_del(&chunk->next);
    free(chunk);
    availSigs -= SIGNAL_CHUNK;
    PSC_log(PSC_LOG_TASK, "%s: now used %d.\n", __func__, availSigs);
}

void PSsignal_gc(void)
{
    list_t *c, *tmp;
    unsigned int i;
    int first = 1;

    PSC_log(PSC_LOG_TASK, "%s()\n", __func__);

    if ((int)usedSigs > (int)availSigs/2 - SIGNAL_CHUNK) return;

    list_for_each_safe(c, tmp, &chunkList) {
	sig_chunk_t *chunk = list_entry(c, sig_chunk_t, next);
	int unused = 0;

	if (first) {
	    first = 0;
	    continue;
	}

	for (i=0; i<SIGNAL_CHUNK; i++) {
	    if (chunk->sigs[i].state != SIG_USED) unused++;
	}

	if (unused > SIGNAL_CHUNK/2) freeChunk(chunk);

	if (availSigs == SIGNAL_CHUNK) break; /* keep the last one */
    }
}

int PSsignal_gcRequired(void)
{
    PSC_log(PSC_LOG_TASK, "%s()\n", __func__);

    if ((int)usedSigs > (int)availSigs/2 - SIGNAL_CHUNK) return 0;

    return 1;
}

void PSsignal_printStat(void)
{
    PSC_log(-1, "%s: Signals %d/%d (used/avail)\n", __func__,
	    usedSigs, availSigs);
}
