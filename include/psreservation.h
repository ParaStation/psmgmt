/*
 * ParaStation
 *
 * Copyright (C) 2015-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Helper structures and functions to put resevations into lists
 */
#ifndef __PSRESERVATION_H
#define __PSRESERVATION_H

#include <stdbool.h>
#include <stdint.h>

#include "list_t.h"
#include "psitems.h"

#include "pstaskid.h"
#include "pspartition.h"

/** Internal state of PSrsrvtn_t structure */
typedef enum {
    RES_DRAINED = PSITEM_DRAINED,  /**< Unused and ready for discard */
    RES_UNUSED = PSITEM_IDLE,      /**< Unused and ready for re-use */
    RES_USED,                      /**< In use */
} PSrsrvtn_state_t;

typedef int32_t PSrsrvtn_ID_t;

/** Reservation structure */
typedef struct {
    list_t next;              /**< used to put into reservation-lists */
    PSrsrvtn_state_t state;   /**< flag internal state of structure */
    PStask_ID_t task;         /**< task holding the associated partition */
    PStask_ID_t requester;    /**< task requesting the registration */
    uint32_t nMin;            /**< minimum number of slots requested */
    uint32_t nMax;            /**< maximum number of slots requested */
    uint16_t ppn;             /**< maximum number of processes per node */
    uint16_t tpp;             /**< number of HW-threads per slot */
    uint32_t hwType;          /**< HW-type to be supported by the HW-threads */
    PSpart_option_t options;  /**< options steering reservation creation */
    PSrsrvtn_ID_t rid;        /**< unique reservation identifier */
    uint32_t firstRank;       /**< first (job-)rank foreseen to spawn */
    uint32_t rankOffset;      /**< global rank offset for this reservation */
    uint32_t nSlots;          /**< number of slots in @ref slots */
    PSpart_slot_t *slots;     /**< slots forming the reservation */
    uint32_t nextSlot;        /**< number of next slot to use */
    uint32_t relSlots;        /**< number of slots already released */
    bool checked;             /**< was checked to be completable */
    bool dynSent;             /**< dynamic request was sent */
} PSrsrvtn_t;

/** Structure used for the PSIDHOOK_RELS_PART_DYNAMIC hook */
typedef struct{
    PSrsrvtn_ID_t rid;        /**< Unique reservation identifier */
    PSpart_slot_t slot;       /**< Slot to be released */
} PSrsrvtn_dynRes_t;

/**
 * @brief Initialize the reservation structure pool
 *
 * Initialize to pool of reservation structure. Must be called before
 * any other function of this module.
 *
 * @return No return value
 */
void PSrsrvtn_init(void);

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
 * returned. Or NULL if an error occurred.
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
 * @warning The reservation structure will not be removed from the
 * list it is stored to. Thus, if @a rp is still registerd to a list
 * when calling this function, the corresponding list will be messed
 * up.
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
 * @brief Print statistics
 *
 * Print statistics concerning the usage of reservation structures.
 *
 * @return No return value.
 */
void PSrsrvtn_printStat(void);

/**
 * @brief Memory cleanup
 *
 * Cleanup all memory currently used by the module. It will very
 * aggressively free all allocated memory most likely destroying
 * existing reservations. Thus, these should have been cleaned up
 * earlier. Currently this requires PSIDtask to be cleaned up.
 *
 * The purpose of this function is to cleanup before a fork()ed
 * process is handling other tasks, e.g. becoming a forwarder.
 *
 * @return No return value.
 */
void PSrsrvtn_clearMem(void);

#endif  /* __PSRESERVATION_H */
