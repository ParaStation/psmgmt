/*
 * ParaStation
 *
 * Copyright (C) 2015-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * Helper structures and functions to store signals in lists.
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSSIGNAL_H
#define __PSSIGNAL_H

#include <stdint.h>
#include <stdbool.h>

#include "list_t.h"

#include "pstask.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** Internal state of PSsignal_t structure */
typedef enum {
    SIG_USED,           /**< In use */
    SIG_UNUSED,         /**< Unused and ready for re-use */
    SIG_DRAINED,        /**< Unused and ready for discard */
} PSsignal_state_t;

/** Signal structure */
typedef struct {
    list_t next;              /**< used to put into signal-lists */
    PStask_ID_t tid;          /**< unique task identifier */
    int32_t signal;           /**< signal to send, or -1 for child-signal */
    bool deleted;             /**< flag to mark deleted signal structs.
				 Will be removed later when save. */
    PSsignal_state_t state;   /**< flag internal state of structure */
} PSsignal_t;

/**
 * @brief Get signal structure from pool
 *
 * Get a signal structure from the pool of free signal structures. If
 * there is no structure left in the pool, this will be extended by
 * @ref SIGNAL_CHUNK structures via calling @ref incFreeList().
 *
 * The signal structure returned will be prepared, i.e. the
 * list-handle @a next is initialized, the deleted flag is cleared, it
 * is marked as SIG_USED, etc.
 *
 * @return On success, a pointer to the new signal structure is
 * returned. Or NULL if an error occurred.
 */
PSsignal_t *PSsignal_get(void);

/**
 * @brief Put signal structure back into pool
 *
 * Put the signal structure @a sp back into the pool of free signal
 * structures. The signal structure might get reused and handed back
 * to the application by calling @ref PSsignal_get().
 *
 * @param sp Pointer to the signal structure to be put back into the
 * pool.
 *
 * @return No return value
 */
void PSsignal_put(PSsignal_t *sp);

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
 *
 * @see Pssignal_gcRequired()
 */
void PSsignal_gc(void);

/**
 * @brief Garbage collection required?
 *
 * Find out if a call to PSsignal_gc() will have any effect, i.e. if
 * sufficiently many unused signal structures are available to free().
 *
 * @return If enough signal structure to free() are available, 1 is
 * returned. Otherwise 0 is given back.
 *
 * @see Pssignal_gc()
 */
int PSsignal_gcRequired(void);

/**
 * @brief Print statistics
 *
 * Print statistics concerning the usage of signal structures.
 *
 * @return No return value.
 */
void PSsignal_printStat(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSSIGNAL_H */
