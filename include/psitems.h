/*
 * ParaStation
 *
 * Copyright (C) 2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Functions for dynamic handling of a pool of items
 *
 * Items are dynamically allocated in chunks of size 128 kB and are
 * managed within this modules. This includes holding a reservoir of
 * idle items, garbage collection, etc.
 *
 * Each item is expected to be of the form:
 * struct {
 *     list_t next;
 *     int32_t someInt;
 *     ...
 * }
 * For details refer to @ref PSitems_init().
 */
#ifndef __PSITEMS_H
#define __PSITEMS_H

#include <stdbool.h>
#include <stdint.h>
#include "list.h"

/** Structure holding all information on a pool of items */
typedef struct {
    list_t chunks;     /**< List of actual chunks of items */
    list_t idleItems;  /**< List of idle items for PSitems_getItem() */
    char *name;        /**< Name of the items to handle */
    bool initialized;  /**< Track pool of items initialization */
    size_t itemSize;   /**< Size of a single item (including its list_t) */
    uint32_t avail;    /**< Number of items available (used + unused) */
    uint32_t used;     /**< Number of used items */
    uint32_t iPC;      /**< Number of items per chunk */
} PSitems_t;

/** Magic value to mark idle items */
#define PSITEM_IDLE -1

/** Magic value to mark drained */
#define PSITEM_DRAINED -2

/**
 * @brief Initialize pool of items
 *
 * Initialize the pool of items represented by @a items in order to
 * hold items of size @a itemSize. The pool is given the name @a name.
 *
 * New items are added to the pool automatically while getting new
 * items via PSitems_getItem(). The item are added in chunks. The
 * number of items to be added is determined by @a itemSize in a way
 * that at least 128 kB of memory is allocated per chunk. This ensures
 * that memory is provided via mmap() avoiding to pollute of the
 * process' address space.
 *
 * Each item is expected to be of the form:
 * struct {
 *     list_t next;
 *     int32_t someInt;
 *     ...
 * }
 *
 * someInt must be set to PSITEM_IDLE if and only if the item is
 * unused. Furthermore, someInt might be changed to PSITEM_DRAINED
 * during garbage collection if it had the value PSITEM_IDLE
 * before. If this happens, the item is removed from the list it was
 * member of via @ref list_del().
 *
 * @param items Structure holding all information on the pool of items
 *
 * @param itemSize Size of the single items to handle
 *
 * @param name Name given to the pool of items
 *
 * @return No return value
 */
void PSitems_init(PSitems_t *items, size_t itemSize, char *name);

/**
 * @brief Check pool of items for initialization
 *
 * Check if the pool of items represented by @a items is initialized,
 * i.e. if @ref PSitems_init() was called for this pool before.
 *
 * @param items Structure holding all information on the pool of items
 *
 * @return Return true if the pool of items is initialized or false
 * otherwise
 */
bool PSitems_isInitialized(PSitems_t *items);

/**
 * @brief Get number of available items
 *
 * Get the total number of available items within the pool of items
 * @a items. The includes both, active and idle items.
 *
 * @param items Structure holding all information on the pool of items
 *
 * @return Return the total number of available items
 */
uint32_t PSitems_getAvail(PSitems_t *items);

/**
 * @brief Get single item
 *
 * Get a single item from the pool of items @a items. The pool might
 * be extended in the way described in @ref PSitems_init().
 *
 * @param items Structure holding all information on the pool of items
 *
 * #return Return a pointer to an idle item or NULL if an error occurred
 */
void * PSitems_getItem(PSitems_t *items);

/**
 * @brief Put single item
 *
 * Put a single item back into the pool items @a items. The item is
 * expected to be idle and ready for reuse.
 *
 * @param items Structure holding all information on the pool of items
 *
 * #return No return value
 */
void PSitems_putItem(PSitems_t *items, void *item);

/**
 * @brief Get number of used items
 *
 * Get the number of active items within the pool of items @a items.
 *
 * @param items Structure holding all information on the pool of items
 *
 * @return Return the number of used items
 */
uint32_t PSitems_getUsed(PSitems_t *items);

/**
 * @brief Check if garbage collection is required
 *
 * Check if a call of the garbage collector is required for the pool
 * of items represented by @a items. The criterion is fulfilled if at
 * most half of the available items are used. Furthermore, at least
 * one chunk of items is kept for optimization reasons.
 *
 * The actual garbage collection is executed by calling @ref
 * PSitems_gc().
 *
 * @param items Structure holding all information on the pool of items
 *
 * @return If garbage collection is required, true is returned. Or
 * false otherwise.
 */
bool PSitems_gcRequired(PSitems_t *items);

/**
 * @brief Collect garbage
 *
 * Run the garbage collector for the pool of items represented by @a
 * items.
 *
 * The garbage collector tries to free chunks in order to reduce the
 * memory footprint of the process. For this it might be necessary to
 * empty partially filled chunks, thus, creating the necessity to
 * relocate the leftover items within this chunk. For each item to be
 * relocated @a relocItem() is called with a pointer to the item to be
 * relocated as the argument.
 *
 * The @a relocItem function is expected to:
 *  - fetch a new item from the list of unused items using PSitems_getItem()
 *  - setup the new item according to the content of the one to be relocated
 *  - replace the one to be relocated by the new one in the list if necessary
 *  - return true if the relocation happened or false if it failed
 *
 * @param items Structure holding all information on the pool of items
 *
 * @param relocItem Function called for every item to be relocated
 *
 * @return No return value
 */
void PSitems_gc(PSitems_t *items, bool (*relocItem)(void *item));

/**
 * @brief Memory cleanup
 *
 * Cleanup all dynamic memory currently used by the pool of items @a
 * items. It will very aggressively free() all allocated memory most
 * likely destroying all lists the items are used in.
 *
 * The purpose of this function is to cleanup before a fork()ed
 * process is handling other tasks, e.g. becoming a forwarder.
 *
 * @param items Structure holding all information on the pool of items
 *
 * @return No return value.
 */
void PSitems_clearMem(PSitems_t *items);

#endif /* __PSITEMS_H */
