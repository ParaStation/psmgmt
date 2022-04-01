/*
 * ParaStation
 *
 * Copyright (C) 2015-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Helper structures and functions to store signals in lists
 */
#ifndef __PSSIGNAL_H
#define __PSSIGNAL_H

#include <stdint.h>

#include "list_t.h"
#include "pstask.h"

/** Signal structure */
typedef struct {
    list_t next;         /**< used to put into signal-lists */
    PStask_ID_t tid;     /**< unique task identifier */
    int32_t signal;      /**< signal to send, or -1 for child-signal */
} PSsignal_t;

/**
 * @brief Initialize the signal structure pool
 *
 * Initialize to pool of signal structures. Must be called before any
 * other function function in this module.
 *
 * @return No return value
 */
void PSsignal_init(void);

/**
 * @brief Get signal structure from pool
 *
 * Get a signal structure from the pool of idle signal structures.
 *
 * The signal structure returned will be prepared, i.e. the
 * list-handle @a next is initialized, the deleted flag is cleared,
 * etc.
 *
 * @return On success, a pointer to the new signal structure is
 * returned. Or NULL if an error occurred.
 */
PSsignal_t *PSsignal_get(void);

/**
 * @brief Put signal structure back into pool
 *
 * Put the signal structure @a sp back into the pool of idle signal
 * structures. The signal structure might get reused and handed back
 * to the application by calling @ref PSsignal_get().
 *
 * @warning The signal structure will not be removed from the list it
 * is stored to. Thus, if @a sp is still registerd to a list when
 * calling this function, the corresponding list will be messed up.
 *
 * @param sp Pointer to the signal structure to be put back into the
 * pool.
 *
 * @return No return value
 */
void PSsignal_put(PSsignal_t *sp);

/**
 * @brief Clone list of signal structures
 *
 * Create an exact clone of the list of signal structures @a origList
 * and store it to the new list @a cloneList.
 *
 * If this functions runs out of memory, i.e. if not enough signal
 * structures could be retrieved from the pool via @ref
 * PSsignal_get(), @a cloneList will be an empty list upon return.
 *
 * @param cloneList List-head of the cloned list to be created
 *
 * @param origList List-head of the original list to be cloned
 *
 * @return No return value
 */
void PSsignal_cloneList(list_t *cloneList, list_t *origList);

/**
 * @brief Clear list of signal structures
 *
 * Clear the list of signal structures @a list and put all structures
 * back into the pool via @ref PSsignal_put()
 *
 * @param list List-head of the list to be cleared
 *
 * @return No return value
 */
void PSsignal_clearList(list_t *list);

/**
 * @brief Garbage collection
 *
 * Do garbage collection on unused signal structures. Since this
 * module will keep pre-allocated buffers for signal structures its
 * memory-footprint might have grown after phases of heavy
 * usage. Thus, this function shall be called regularly in order to
 * free() signal structures no longer required.
 *
 * @return No return value.
 */
void PSsignal_gc(void);

/**
 * @brief Print statistics
 *
 * Print statistics concerning the usage of signal structures.
 *
 * @return No return value.
 */
void PSsignal_printStat(void);

#endif  /* __PSSIGNAL_H */
