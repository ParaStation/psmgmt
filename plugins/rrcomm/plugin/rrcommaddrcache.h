/*
 * ParaStation
 *
 * Copyright (C) 2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __RRCOMM_ADDRCACHE_H
#define __RRCOMM_ADDRCACHE_H

#include <stdint.h>

#include "pstaskid.h"

/**
 * @brief Initialize RRComm's address cache
 *
 * Initialize RRComm's address cache and register it to the local job
 * ID @a jobID. @a jobID will help to optimize job-local caching.
 *
 * @return No return value
 */
void initAddrCache(PStask_ID_t jobID);

/**
 * @brief Add entry to address cache
 *
 * Add or update an entry on the task identified by the rank @a rank
 * in job @a jobID to RRComm's address cache and set it to @a
 * taskID. If @a jobID is 0, the ID of the local job as defined via
 * @ref initAddrCache() is assumed.
 *
 * @param jobID Job ID part of the entry's key
 *
 * @param rank Rank part of the entry's key
 *
 * @param taskID Value of the entry to be created or updated
 *
 * @return No return value
 */
void updateAddrCache(PStask_ID_t jobID, int32_t rank, PStask_ID_t taskID);

/**
 * @brief Lookup entry in address cache
 *
 * Lookup the entry belonging to rank @a rank in the job with ID @a
 * jobID from RRComm's address cache.
 *
 * @param jobID Job ID part of the to be looked up entry's key
 *
 * @param rank Rank part of the to be looked up entry's key
 *
 * @return Return the value of the cache entry to look up or -1 if no
 * such entry exists
 */
PStask_ID_t getAddrFromCache(PStask_ID_t jobID, int32_t rank);

/**
 * @brief Destroy RRComm's address cache
 *
 * Destroy RRComm's address cache and free all allocated memory.
 *
 * @return No return value
 */
void clearAddrCache(void);

#endif  /* __RRCOMM_ADDRCACHE_H */
