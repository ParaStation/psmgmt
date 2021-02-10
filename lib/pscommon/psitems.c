/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "list.h"
#include "pscommon.h"

#include "psitems.h"

/** Default chunk size utilizing the mmap() path of glibc's malloc() */
#define CHUNK_SIZE (128*1024)

#define PSITEMS_MAGIC 0x3141592653589793

/** Structure holding all information on a pool of items */
struct itemPool {
    long magic;
    list_t chunks;     /**< List of actual chunks of items */
    list_t idleItems;  /**< List of idle items for PSitems_getItem() */
    char *name;        /**< Name of the items to handle */
    size_t itemSize;   /**< Size of a single item (including its list_t) */
    uint32_t avail;    /**< Number of items available (used + unused) */
    uint32_t used;     /**< Number of used items */
    uint32_t iPC;      /**< Number of items per chunk */
    uint32_t numGet;   /**< Pool's utilization (number of getItem() calls) */
    uint32_t numGrow;  /**< Pool's dynamics (number of growItems() calls) */
};

/** Chunk holding the actual items */
typedef struct {
    list_t next;    /**< Use to put into list of chunks */
    char itemBuf[]; /**< Space for actual items */
} chunk_t;

PSitems_t PSitems_new(size_t itemSize, char *name)
{
    PSitems_t items = malloc(sizeof(*items));
    if (!items) return NULL;

    items->magic = PSITEMS_MAGIC;
    INIT_LIST_HEAD(&items->chunks);
    INIT_LIST_HEAD(&items->idleItems);
    items->name = strdup(name);
    items->itemSize = itemSize;
    items->avail = 0;
    items->used = 0;
    items->iPC = (CHUNK_SIZE - sizeof(list_t)) / itemSize + 1;
    items->numGet = 0;
    items->numGrow = 0;

    return items;
}

bool PSitems_isInitialized(PSitems_t items)
{
    return (items && items->magic == PSITEMS_MAGIC);
}

uint32_t PSitems_getAvail(PSitems_t items)
{
    return PSitems_isInitialized(items) ? items->avail : 0;
}

uint32_t PSitems_getUsed(PSitems_t items)
{
    return PSitems_isInitialized(items) ? items->used : 0;
}

uint32_t PSitems_getUtilization(PSitems_t items)
{
    return PSitems_isInitialized(items) ? items->numGet : 0;
}

uint32_t PSitems_getDynamics(PSitems_t items)
{
    return PSitems_isInitialized(items) ? items->numGrow : 0;
}

bool PSitems_setChunkSize(PSitems_t items, size_t chunkSize)
{
    if (!PSitems_isInitialized(items) || items->avail) return false;

    items->iPC = (chunkSize - sizeof(list_t)) / items->itemSize + 1;
    return true;
}

/** Stub of the actual item type */
typedef struct {
    list_t next;    /**< Used to put item into some list */
    int32_t state;  /**< State of the item (USED/IDLE/DRAINED) */
} item_t;

/**
 * @brief Grow pool of items
 *
 * Grow the pool of items represented by @a items. The number of added
 * items depends on the size of the individual items such that the
 * amount of memory allocated is at least 128 kB. This means each
 * chunk is at least of size 128 kB.
 *
 * All new items are added to the list of idle items ready to be
 * consumed by @ref PSitems_getItem().
 *
 * @param items Structure holding all information on the pool of items
 *
 * @return If new items were added successfully, the number of new
 * items is returned or 0 in case of error.
 */
static int growItems(PSitems_t items)
{
    chunk_t *newChunk = malloc(sizeof(list_t) + items->itemSize * items->iPC);
    if (!newChunk) {
	PSC_warn(-1, errno, "%s(%s)", __func__, items->name);
	return 0;
    }
    items->numGrow++;

    list_add_tail(&newChunk->next, &items->chunks);
    memset(&newChunk->itemBuf, 0, items->itemSize * items->iPC);

    for (uint32_t i = 0; i < items->iPC; i++) {
	item_t *item = (item_t *)&newChunk->itemBuf[i * items->itemSize];
	item->state = PSITEM_IDLE;
	list_add_tail(&item->next, &items->idleItems);
    }

    items->avail += items->iPC;

    return items->iPC;
}

void * __PSitems_getItem(PSitems_t items, const char *caller, const int line)
{
    if (!PSitems_isInitialized(items)) {
	PSC_log(-1, "%s: not initialized and called by %s:%i\n", __func__,
		caller, line);
	return NULL;
    }
    items->numGet++;

    if (list_empty(&items->idleItems)) {
	PSC_log(PSC_LOG_VERB, "%s(%s): no more items\n", __func__, items->name);
	if (!growItems(items)) {
	    PSC_log(-1, "%s(%s): no memory for caller %s:%i\n", __func__,
		    items->name, caller, line);
	    return NULL;
	}
    }

    /* get list's first usable element */
    item_t *item = list_entry(items->idleItems.next, item_t, next);
    if (item->state != PSITEM_IDLE) {
	PSC_log(-1, "%s(%s): item is %s. Never be here for caller %s:%i.\n",
		__func__, items->name,
		(item->state == PSITEM_DRAINED) ? "DRAINED" : "BUSY",
		caller, line);
	return NULL;
    }

    list_del(&item->next);
    INIT_LIST_HEAD(&item->next);

    items->used++;

    return item;
}

void PSitems_putItem(PSitems_t items, void *item)
{
    if (!PSitems_isInitialized(items)) {
	PSC_log(-1, "%s: initialize before!\n", __func__);
	return;
    }

    item_t *i = (item_t *)item;
    i->state = PSITEM_IDLE;
    list_add_tail(&i->next, &items->idleItems);

    items->used--;
}

bool PSitems_gcRequired(PSitems_t items)
{
    if (!PSitems_isInitialized(items)) return false;
    return items->avail > items->iPC
	&& items->used < (items->avail - items->iPC)/2;
}

/**
 * @brief Free a chunk of items
 *
 * Free the chunk of items @a chunk part of the pool of items @a
 * items. For that, all idle items from this chunk are removed from
 * the list of idle items and marked as drained. All items still in
 * use are replaced by calling @a relocItem().
 *
 * Once all items of the chunk are drained, the whole chunk is free()ed.
 *
 * @param items Structure holding all information on the pool of items
 *
 * @param chunk Chunk of items to free
 *
 * @param relocItem Function to relocate a single item
 *
 * @return No return value
 */
static void freeChunk(PSitems_t items, chunk_t *chunk, bool(*relocItem)(void*))
{
    if (!items || !chunk) return;

    /* First round: remove items from s */
    for (uint32_t i = 0; i < items->iPC; i++) {
	item_t *item = (item_t *)&chunk->itemBuf[i * items->itemSize];
	if (item->state == PSITEM_IDLE) {
	    list_del(&item->next);
	    item->state = PSITEM_DRAINED;
	}
    }

    /* Second round: now relocate and release all used items */
    for (uint32_t i = 0; i < items->iPC; i++) {
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

    /* Now that the chunk is completely empty, free() it */
    list_del(&chunk->next);
    free(chunk);
    items->avail -= items->iPC;
}

void PSitems_gc(PSitems_t items, bool (*relocItem)(void *))
{
    if (!PSitems_gcRequired(items)) return;

    bool first = true;
    list_t *c, *tmp;
    list_for_each_safe(c, tmp, &items->chunks) {
	chunk_t *chunk = list_entry(c, chunk_t, next);

	/* always keep the first one */
	if (first) {
	    first = false;
	    continue;
	}

	uint32_t unused = 0;
	for (uint32_t i = 0; i < items->iPC; i++) {
	    item_t *item = (item_t *)&chunk->itemBuf[i * items->itemSize];
	    if (item->state == PSITEM_IDLE
		|| item->state == PSITEM_DRAINED) unused++;
	}

	if (unused > items->iPC / 2) freeChunk(items, chunk, relocItem);

	if (!PSitems_gcRequired(items)) break;
    }
}

void PSitems_clearMem(PSitems_t items)
{
    if (!PSitems_isInitialized(items)) return;

    list_t *c, *tmp;
    list_for_each_safe(c, tmp, &items->chunks) {
	chunk_t *chunk = list_entry(c, chunk_t, next);

	list_del(&chunk->next);
	free(chunk);
    }

    INIT_LIST_HEAD(&items->idleItems);
    items->used = items->avail = 0;
    free(items->name);
    items->magic = 0;
    free(items);
}
