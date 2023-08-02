/*
 * ParaStation
 *
 * Copyright (C) 2022-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * Session, job and reservation information for the ParaStation daemon
 */
#ifndef __PSIDSESSION_H
#define __PSIDSESSION_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "list.h"
#include "psnodes.h"
#include "psreservation.h"
#include "pstask.h"
#include "pscpu.h"

/** Single node part of a reservation */
typedef struct {
    PSnodes_ID_t node;         /**< node ID */
    int32_t firstRank;         /**< first job rank designated to this node */
    int32_t lastRank;          /**< last job rank designated to this node */
} PSresinfoentry_t;

/** Single rank part of the local reservation information */
typedef struct {
    int32_t rank;              /**< job rank the reservation slot belongs to */
    PSCPU_set_t CPUset;        /**< set of CPUs the slot occupies */
} PSresslot_t;

/** Compact reservation information structure, used in non-logger deamons */
typedef struct {
    list_t next;               /**< used to put into PSjob_t.resInfos */
    PSrsrvtn_ID_t resID;       /**< unique reservation identifier */
    time_t creation;           /**< creation time of this info item */
    PStask_ID_t partHolder;    /**< task holding the associated partition part */
    uint32_t rankOffset;       /**< global rank offset for this reservation */
    int32_t minRank;           /**< minimum job rank in this reservation */
    int32_t maxRank;           /**< maximum job rank in this reservation */
    uint32_t nEntries;         /**< Number of entries in @ref entries */
    PSresinfoentry_t *entries; /**< slots forming the reservation */
    uint16_t nLocalSlots;      /**< number of entries in @ref localSlots */
    PSresslot_t *localSlots;   /**< local reservation information */
} PSresinfo_t;

/** Job: reservations involving this node requested by the same spawner */
typedef struct {
    list_t next;             /**< used to put into PSsession_t.jobs */
    PStask_ID_t ID;          /**< unique job identifier (spawner's TID) */
    time_t creation;         /**< creation time of this job */
    list_t resInfos;         /**< reservations in this job (PSresinfo_t) */
} PSjob_t;

/** Session: jobs involving this node with a common logger */
typedef struct {
    list_t next;             /**< used to put into localSessions */
    PStask_ID_t ID;          /**< unique session identifier (logger's TID) */
    time_t creation;         /**< creation time of this session */
    list_t jobs;             /**< jobs in this session (PSjob_t) */
} PSsession_t;

/**
 * @brief Find local session by ID
 *
 * Find a local session by its ID @a sessionID. A session is
 * identified by the task ID of the logger that is the root of all its
 * member processes.
 *
 * @param sessionID Task ID of logger identifying the session
 *
 * @return Return pointer to the session information or NULL if none was found
 */
PSsession_t* PSID_findSessionByID(PStask_ID_t sessionID);

/**
 * @brief Find job in session by ID
 *
 * Find a job within the session @a session by its ID @a jobID. A job
 * is identified by the task ID of the spawner that created its member
 * processes.
 *
 * @param session Session to search in
 *
 * @param jobID Task ID of spawner identifying the job
 *
 * @return Returns the job or NULL if none found
 */
PSjob_t* PSID_findJobInSession(PSsession_t *session, PStask_ID_t jobID);

/**
 * @brief Find reservation info by session and job identifier
 *
 * @param sessionID Task ID of logger identifying the session
 *
 * @param jobID Task ID of spawner identifying the job
 *
 * @param resID ID of the reservation to get the info object for
 *
 * @return Returns the reservation or NULL if none found
 */
PSresinfo_t* PSID_findResInfo(PStask_ID_t sessionID, PStask_ID_t jobID,
			      PSrsrvtn_ID_t resID);

/**
 * @brief Cleanup reservation infos by holder
 *
 * Cleanup all reservation infos from the session @a sessID that are
 * created from the partition managed by the task identified by @a
 * holderTID. For this all reservation infos within the session are
 * reviewed if they stem from the corresponding partition
 * holder. Those reservation infos will only removed if no other local
 * tasks still depend on this info, i.e. if no other local task
 * (usually a step forwarder) will hold a sister partition from the
 * same partition holder.
 *
 * @param sessID Task ID of logger identifying the session
 *
 * @param holderTID Task ID of partition holder
 *
 * @return No return value
 */
void PSIDsession_cleanupByHolder(PStask_ID_t sessID, PStask_ID_t holderTID);

/**
 * @brief Initialize session stuff
 *
 * Initialize the session framework. This registers the necessary
 * message handlers.
 *
 * @return On success true is returned; or false in case of error
 */
bool PSIDsession_init(void);

/**
 * @brief Print statistics
 *
 * Print statistics concerning the usage of internal pools for
 * sessions, jobs and resinfos.
 *
 * @return No return value
 */
void PSIDsession_printStat(void);

#endif /* __PSIDSESSION_H */
