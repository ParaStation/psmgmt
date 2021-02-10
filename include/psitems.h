/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
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
 * For details refer to @ref PSitems_new().
 */
#ifndef __PSITEMS_H
#define __PSITEMS_H

#include <stdbool.h>
#include <stdint.h>

/** Opaque structure holding all information on a pool of items */
struct itemPool;

/** Item pool context to be created via @ref PSitems_new() */
typedef struct itemPool * PSitems_t;

/** Magic value to mark idle items */
#define PSITEM_IDLE -1

/** Magic value to mark drained items */
#define PSITEM_DRAINED -2

/**
 * @brief Create pool of items
 *
 * Create a pool of items in order to hold items of size @a
 * itemSize. The pool is given the name @a name.
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
 * @param itemSize Size of the single items to handle
 *
 * @param name Name given to the pool of items
 *
 * @return Handle to the pool of items or NULL if creating the pool
 * was impossible
 */
PSitems_t PSitems_new(size_t itemSize, char *name);

/**
 * @brief Check pool of items for initialization
 *
 * Check if the pool of items represented by @a items is initialized,
 * i.e. if @ref PSitems_new() was called for this pool before.
 *
 * @param items Structure holding all information on the pool of items
 *
 * @return Return true if the pool of items is initialized; or false
 * otherwise
 */
bool PSitems_isInitialized(PSitems_t items);

/**
 * @brief Set chunk size
 *
 * Set the chunk size of the pool of items represented by @a items to
 * @a chunkSize.
 *
 * The default chunk size for pools is 128 kB utilizing the mmap()
 * path of glibc's malloc() implementation. To set a smaller chunk
 * size might still make sense if the pool usage is less dynamic and a
 * hard upper bund of the number of items can be established, thus,
 * reducing the overhead of unused items.
 *
 * The chunk size must be set before any chunk is allocated for the
 * pool of items and must not be changed once a chunk was
 * allocated. If either of these rules is violated setting the chunk
 * size for the pool of items will fail.
 *
 * @param items Structure holding all information on the pool of items
 *
 * @param chunkSize Chunk size to be established
 *
 * #return If the chunk size was changed, true is returned; or false
 * in case of failure
 */
bool PSitems_setChunkSize(PSitems_t items, size_t chunkSize);

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
uint32_t PSitems_getAvail(PSitems_t items);

/**
 * @brief Get single item
 *
 * Get a single item from the pool of items @a items. The pool might
 * be extended in the way described in @ref PSitems_new().
 *
 * @param items Structure holding all information on the pool of items
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * #return Return a pointer to an idle item or NULL if an error occurred
 */
void * __PSitems_getItem(PSitems_t items, const char *caller, const int line);

#define PSitems_getItem(items) \
    __PSitems_getItem(items, __func__, __LINE__)

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
void PSitems_putItem(PSitems_t items, void *item);

/**
 * @brief Get number of used items
 *
 * Get the number of active items within the pool of items @a items.
 *
 * @param items Structure holding all information on the pool of items
 *
 * @return Return the number of used items
 */
uint32_t PSitems_getUsed(PSitems_t items);

/**
 * @brief Get pool's utilization
 *
 * Get the utilization of the pool of items @a items. For this, the
 * total number of call to @ref PSitems_getItem() is traced.
 *
 * @param items Structure holding all information on the pool of items
 *
 * @return Return total number calls to @ref PSitems_getItem() for the pool
 */
uint32_t PSitems_getUtilization(PSitems_t items);

/**
 * @brief Get pool's dynamics
 *
 * Get the dynamics of the pool of items @a items. For this, the
 * total number of times the pool is extended is traced.
 *
 * @param items Structure holding all information on the pool of items
 *
 * @return Return total number extensions of the pool
 */
uint32_t PSitems_getDynamics(PSitems_t items);

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
 * @return If garbage collection is required, true is returned; or
 * false otherwise
 */
bool PSitems_gcRequired(PSitems_t items);

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
void PSitems_gc(PSitems_t items, bool (*relocItem)(void *item));

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
 * @return No return value
 */
void PSitems_clearMem(PSitems_t items);

#endif /* __PSITEMS_H */
