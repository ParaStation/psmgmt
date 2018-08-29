/*
 * ParaStation
 *
 * Copyright (C) 2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "pscommon.h"

#include "psitems.h"

#define CHUNK_SIZE (128*1024)

/** Sub-chunk holding the actual items */
typedef struct {
    list_t next;    /**< Use to put into list of chunks */
    char itemBuf[]; /**< Space for actual items */
} chunk_t;

void PSitems_init(PSitems_t *items, size_t itemSize, char *name)
{
    if (!items) return;

    INIT_LIST_HEAD(&items->chunks);
    INIT_LIST_HEAD(&items->idleItems);
    items->name = strdup(name);
    items->itemSize = itemSize;
    items->avail = 0;
    items->used = 0;
    items->iPC = (CHUNK_SIZE - sizeof(list_t)) / itemSize + 1;
}

uint32_t PSitems_getAvail(PSitems_t *items)
{
    return items->avail;
}

uint32_t PSitems_getUsed(PSitems_t *items)
{
    return items->used;
}

/** Stub of the actual item type */
typedef struct {
    list_t next;    /**< Used to put item into some list */
    int32_t state;  /**< State of the item (USED/IDLE/DRAINED) */
} item_t;

/**
 * @brief Grow chunk of items
 *
 * Grow the chunk of items represented by @a items. The number of
 * added items depends on the size of the individual items such that
 * the amount of memory allocated is at least 128 kB. This means each
 * sub-chunk is at least of size 128 kB.
 *
 * All new items are added to the pool of idle items ready to be
 * consumed by @ref PSitems_getItem().
 *
 * @param items Structure holding all information on the chunk of items
 *
 * @return If new items were added successfully, the number of new
 * items is returned or 0 in case of error.
 */
static int growItems(PSitems_t *items)
{
    chunk_t *newChunk = malloc(sizeof(list_t) + items->itemSize * items->iPC);
    uint32_t i;

    if (!newChunk) {
	PSC_warn(-1, errno, "%s(%s)", __func__, items->name);
	return 0;
    }

    list_add_tail(&newChunk->next, &items->chunks);
    memset(&newChunk->itemBuf, 0, items->itemSize * items->iPC);

    for (i = 0; i < items->iPC; i++) {
	item_t *item = (item_t *)&newChunk->itemBuf[i * items->itemSize];
	item->state = PSITEM_IDLE;
	list_add_tail(&item->next, &items->idleItems);
    }

    items->avail += items->iPC;

    return items->iPC;
}

void * PSitems_getItem(PSitems_t *items)
{
    if (list_empty(&items->idleItems)) {
	PSC_log(PSC_LOG_VERB, "%s(%s): no more items\n", __func__, items->name);
	if (!growItems(items)) {
	    PSC_log(-1, "%s(%s): no memory\n", __func__, items->name);
	    return NULL;
	}
    }

    /* get list's first usable element */
    item_t *item = list_entry(items->idleItems.next, item_t, next);
    if (item->state != PSITEM_IDLE) {
	PSC_log(-1, "%s(%s): item is %s. Never be here.\n", __func__,
		items->name,
		(item->state == PSITEM_DRAINED) ? "DRAINED" : "BUSY");
	return NULL;
    }

    list_del(&item->next);
    INIT_LIST_HEAD(&item->next);

    items->used++;

    return item;
}

void PSitems_putItem(PSitems_t *items, void *item)
{
    item_t *i = (item_t *)item;

    i->state = PSITEM_IDLE;
    list_add_tail(&i->next, &items->idleItems);

    items->used--;
}

bool PSitems_gcRequired(PSitems_t *chunk)
{
    return chunk->avail > chunk->iPC
	&& chunk->used < (chunk->avail - chunk->iPC)/2;
}

/**
 * @brief Free a chunk of items
 *
 * Free the sub-chunk of items @a chunk part of the chunk of items @a
 * items. For that, all empty items from this sub-chunk are removed
 * from the list of idle items and marked as drained. All items still
 * in use are replaced by calling @a relocItem().
 *
 * Once all items of the sub-chunk are empty, the whole chunk is free()ed.
 *
 * @param items Structure holding all information on the chunk of items
 *
 * @param chunk Sub-chunk of items to free
 *
 * @param relocItem Function to relocate single items
 *
 * @return No return value
 */
static void freeChunk(PSitems_t *items, chunk_t *chunk, bool(*relocItem)(void*))
{
    uint32_t i;

    if (!items || !chunk) return;

    /* First round: remove items from s */
    for (i = 0; i < items->iPC; i++) {
	item_t *item = (item_t *)&chunk->itemBuf[i * items->itemSize];
	if (item->state == PSITEM_IDLE) {
	    list_del(&item->next);
	    item->state = PSITEM_DRAINED;
	}
    }

    /* Second round: now relocate and release all used items */
    for (i = 0; i < items->iPC; i++) {
	item_t *item = (item_t *)&chunk->itemBuf[i * items->itemSize];

	if (item->state == PSITEM_DRAINED) continue;

	if (item->state == PSITEM_IDLE) {
	    /* item might got idle in the meantime */
	    list_del(&item->next);
	} else {
	    if (!relocItem(item)) {
		PSC_log(-1, "%s(%s): reloc(%d) failed\n", __func__,
			items->name, i);
		return;
	    }
	    items->used--;
	}
	item->state = PSITEM_DRAINED;
    }

    /* Now that the sub-chunk is completely empty, free() it */
    list_del(&chunk->next);
    free(chunk);
    items->avail -= items->iPC;
}

void PSitems_gc(PSitems_t *items, bool (*relocItem)(void *))
{
    list_t *c, *tmp;
    bool first = true;
    uint32_t i;

    if (!PSitems_gcRequired(items)) return;

    list_for_each_safe(c, tmp, &items->chunks) {
	chunk_t *chunk = list_entry(c, chunk_t, next);
	uint32_t unused = 0;

	/* always keep the first one */
	if (first) {
	    first = false;
	    continue;
	}

	for (i = 0; i < items->iPC; i++) {
	    item_t *item = (item_t *)&chunk->itemBuf[i * items->itemSize];
	    if (item->state == PSITEM_IDLE
		|| item->state == PSITEM_DRAINED) unused++;
	}

	if (unused > items->iPC / 2) freeChunk(items, chunk, relocItem);

	if (!PSitems_gcRequired(items)) break;
    }
}

void PSitems_clearMem(PSitems_t *items)
{
    list_t *c, *tmp;
    list_for_each_safe(c, tmp, &items->chunks) {
	chunk_t *chunk = list_entry(c, chunk_t, next);

	list_del(&chunk->next);
	free(chunk);
    }

    INIT_LIST_HEAD(&items->idleItems);
    items->used = items->avail = 0;
}
