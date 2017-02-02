/*
 * ParaStation
 *
 * Copyright (C) 2015-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSPAM_TYPES
#define __PSPAM_TYPES

#include <stdbool.h>
#include <sys/types.h>

/** Phases a user's job is currently in */
typedef enum {
    PSPAM_STATE_PROLOGUE,   /**< Job still in prologue phase */
    PSPAM_STATE_JOB,        /**< Job entered actual execution stage */
} PSPAMState_t;

/**
 * @brief Register user
 *
 * Register a new user identified by the username @a username and the
 * job ID @a jobID. The new user's initial jobstate is set to @a
 * state.
 *
 * @param username Name of the user to register
 *
 * @param jobID ID of the job the user is running
 *
 * @param state Current state of the user's job
 *
 * @return No return value
 */
typedef void (psPamAddUser_t)(char *username, char *jobID, PSPAMState_t state);

/**
 * @brief Set user's jobstate
 *
 * Set jobstate of the user job identified by the username @a username
 * and the job ID @a jobID to @a state.
 *
 * @param username Name of the user whose jobstate to update
 *
 * @param jobID ID of the job to be updated
 *
 * @param state Updated state of the user's job
 *
 * @return No return value
 */
typedef void (psPamSetState_t)(char *username, char *jobID, PSPAMState_t state);

/**
 * @brief Delete user
 *
 * Delete the user identified by the username @a username and the
 * job ID @a jobID.
 *
 * @param username Name of the user to delete
 *
 * @param jobID ID of the job to be removed
 *
 * @return No return value
 */
typedef void (psPamDeleteUser_t)(char *username, char *jobID);

/**
 * @brief Search for SSH session PID belongs to
 *
 * Identify if the process with ID @a pid belongs to a SSH session
 * registered within pspam.
 *
 * @param pid Process ID of the process the check
 *
 * @return If a session was found, true is returned. Otherwise false
 * is returned.
 */
typedef bool (psPamFindSessionForPID_t)(pid_t pid);

#endif /* __PSPAM_TYPES */
