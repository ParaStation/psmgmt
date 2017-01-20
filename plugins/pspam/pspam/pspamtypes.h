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

/** Phases a user's job might be in */
typedef enum {
    PSPAM_STATE_PROLOGUE,   /**< Job still in prologue phase */
    PSPAM_STATE_JOB,        /**< Job entered actual execution stage */
} PSPAMState_t;

/**
 * @brief Register user
 *
 * Register a new user identified by the username @a username and the
 * registering plugin @a plugin. The new user's initial jobstate is
 * set to @a state.
 *
 * @param username Name of the user to register
 *
 * @param plugin Name of the registering plugin
 *
 * @param state Current state of the user's job
 *
 * @return No return value
 */
typedef void (psPamAddUser_t)(char *username, char *plugin, PSPAMState_t state);

/**
 * @brief Set user's jobstate
 *
 * Set jobstate of the user identified by the username @a username and
 * the registering plugin @a plugin to @a state.
 *
 * @param username Name of the user whose jobstate to update
 *
 * @param plugin Name of the expected registering plugin
 *
 * @param state Updated state of the user's job
 *
 * @return No return value
 */
typedef void (psPamSetState_t)(char *username, char *plugin, PSPAMState_t state);

/**
 * @brief Delete user
 *
 * Delete the user identified by the username @a username and the
 * registering plugin @a plugin.
 *
 * @param username Name of the user to delete
 *
 * @param plugin Name of the registering plugin
 *
 * @return No return value
 */
typedef void (psPamDeleteUser_t)(char *username, char *plugin);

#endif /* __PSPAM_TYPES */
