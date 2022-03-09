/*
 * ParaStation
 *
 * Copyright (C) 2022 ParTec AG, Munich
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

#include "list.h"
#include "psnodes.h"
#include "psreservation.h"

/** Single node part of a reservation */
typedef struct {
    PSnodes_ID_t node;       /**< node ID */
    int32_t firstrank;       /**< first rank designated to this node */
    int32_t lastrank;        /**< last rank designated to this node */
} PSresinfoentry_t;

/** Compact reservation information structure, used in non-logger deamons */
typedef struct {
    list_t next;               /**< used to put into reservation-lists */
    PSrsrvtn_ID_t resID;       /**< unique reservation identifier */
    uint32_t nEntries;         /**< Number of entries in @ref entries */
    PSresinfoentry_t *entries; /**< Slots forming the reservation */
} PSresinfo_t;

/** Job: reservations involving this node requested by the same spawner */
typedef struct {
    list_t next;             /**< used to put into PSsession_t.jobs */
    PStask_ID_t spawnertid;  /**< spawner's tid, unique job identifier */
    list_t resInfos;         /**< reservations in this job (PSresinfo_t) */
} PSjob_t;

/** Session: jobs running on this node with a common logger */
typedef struct {
    list_t next;             /**< used to put into localSessions */
    PStask_ID_t loggertid;   /**< logger's tid, unique session identifier */
    list_t jobs;             /**< sets of reservations (PSspawnblock_t) */
} PSsession_t;

/**
 * @brief Find local session by logger TID
 *
 * Find information on a local session by its logger's task ID @a logger.
 *
 * @param loggerTID Task ID of the logger identifying the session
 *
 * @return Return pointer to the session information or NULL if none was found
 */
PSsession_t* PSID_findSessionByLoggerTID(PStask_ID_t loggerTID);

/**
 * @brief Initialize session stuff
 *
 * Initialize the session framework. This registers
 * the necessary message handlers.
 *
 * @return No return value.
 */
void PSIDsession_init(void);

/**
 * @brief Memory cleanup
 *
 * Cleanup all dynamic memory currently retained in session structures
 * collected in the @ref localSessions list. This will very aggressively
 * free() all allocated memory destroying all information on sessions, jobs,
 * and reservation.
 *
 * The purpose of this function is to cleanup before a fork()ed
 * process is handling other businesses, e.g. becoming a forwarder.
 *
 * @return No return value
 */
void PSIDsession_clearMem(void);

#endif /* __PSIDSESSION_H */
