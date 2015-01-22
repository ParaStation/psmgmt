/*
 * ParaStation
 *
 * Copyright (C) 2015 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * Helper structures and functions to put resevations into lists.
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSRESERVATION_H
#define __PSRESERVATION_H

#include <stdint.h>

#include "list_t.h"

#include "pspartition.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** Internal state of PSrsrvtn_t structure */
typedef enum {
    RES_USED,           /**< In use */
    RES_UNUSED,         /**< Unused and ready for re-use */
    RES_DRAINED,        /**< Unused and ready for discard */
} PSrsrvtn_state_t;

typedef int32_t PSrsrvtn_ID_t;

/** Reservation structure */
typedef struct {
    list_t next;              /**< used to put into reservation-lists */
    PStask_ID_t requester;    /**< The task requesting the registration */
    uint32_t nMin;            /**< The minimum number of slots requested */
    uint32_t nMax;            /**< The maximum number of slots requested */
    uint16_t tpp;             /**< Number of HW-threads per slot */
    uint32_t hwType;          /**< HW-type to be supported by the HW-threads */
    PSpart_option_t options;  /**< Options steering reservation creation */
    PSrsrvtn_ID_t rid;        /**< unique reservation identifier */
    int firstRank;            /**< The first rank foreseen to spawn */
    int numSlots;             /**< Number of slots in @ref slots */
    PSpart_slot_t *slots;     /**< Slots forming the reservation */
    int nextSlot;             /**< Number of next slot to use */
    PSrsrvtn_state_t state;   /**< flag internal state of structure */
} PSrsrvtn_t;

/**
 * @brief Get reservation structure from pool
 *
 * Get a reservation structure from the pool of free reservation
 * structures. If there is no structure left in the pool, this will be
 * extended by @ref RESERVATION_CHUNK structures.
 *
 * The reservation structure returned will be prepared, i.e. the
 * list-handle @a next is initialized, the deleted flag is cleared, it
 * is marked as RES_USED, etc.
 *
 * @return On success, a pointer to the new reservation structure is
 * returned. Or NULL, if an error occurred.
 */
PSrsrvtn_t *PSrsrvtn_get(void);

/**
 * @brief Put reservation structure back into pool
 *
 * Put the reservation structure @a rp back into the pool of free
 * reservation structures. The reservation structure might get reused
 * and handed back to the application by calling @ref PSrsrvtn_get().
 *
 * Before putting the reservation back it has to be ensured that the
 * slot-list @ref slot is removed from the structure and free()ed in
 * order to avoid memory leaks. To signal this cleanup to the function
 * the corresponding entry in the structure must be set to NULL.
 *
 * @param rp Pointer to the reservation structure to be put back into
 * the pool.
 *
 * @return No return value
 */
void PSrsrvtn_put(PSrsrvtn_t *rp);

/**
 * @brief Garbage collection
 *
 * Do garbage collection on unused reservation structures. Since this
 * module will keep pre-allocated buffers for reservation structures
 * its memory-footprint might have grown after phases of heavy
 * usage. Thus, this function shall be called regularly in order to
 * free() reservation structures no longer required.
 *
 * @return No return value.
 *
 * @see Psreservation_gcRequired()
 */
void PSrsrvtn_gc(void);

/**
 * @brief Garbage collection required?
 *
 * Find out, if a call to PSrsrvtn_gc() will have any effect, i.e. if
 * sufficiently many unused reservation structures are available to
 * free().
 *
 * @return If enough reservation structure to free() are available, 1 is
 * returned. Otherwise 0 is given back.
 *
 * @see Psreservation_gc()
 */
int PSrsrvtn_gcRequired(void);

/**
 * @brief Print statistics
 *
 * Print statistics concerning the usage of reservation structures.
 *
 * @return No return value.
 */
void PSrsrvtn_printStat(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSRESERVATION_H */
