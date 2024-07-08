/*
 * ParaStation
 *
 * Copyright (C) 2014-2017 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSPAM_USER
#define __PSPAM_USER

#include <stdbool.h>
#include <stddef.h>

#include "list.h"
#include "pspamtypes.h"

/** Structure holding all information concerning a specifig user */
typedef struct {
    list_t next;        /**< used to put into list of users */
    char *name;         /**< username of the registered user */
    char *jobID;        /**< name of the job the user is running */
    PSPAMState_t state; /**< current state the user's job */
} User_t;

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
 * @return If a new user was added or a corresponding user entry
 * already exists true is returned. Otherwise false is returned.
 */
bool addUser(char *username, char *jobID, PSPAMState_t state);

/**
 * @brief Find user
 *
 * Find the user identified by the username @a username and the job ID
 * @a jobID. If @a jobID is NULL only @a username will be taken into
 * account, i.e. users registered with any @a jobID might be returned.
 *
 * @param username Name of the user to find
 *
 * @param jobID ID of the job the user is running or NULL to match
 * users registered with any job ID.
 *
 * @return Return a pointer to the found user structure or NULL
 */
User_t *findUser(char *username, char *jobID);

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
void setState(char *username, char *jobID, PSPAMState_t state);

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
void deleteUser(char *username, char *jobID);

/**
 * @brief Clear all users
 *
 * Clear all users and clear the used memory
 *
 * @return No return value
 */
void clearUserList(void);

/**
 * @brief Convert state to string
 *
 * Convert the state @a state to a string constant.
 *
 * @param state State to convert
 *
 * @return Returns the requested state as string
 */
const char *state2Str( PSPAMState_t state);

/**
 * @brief List current users
 *
 * List current users and put all information into a string buffer.
 *
 * @return Pointer to buffer with updated user information
 */
char *listUsers(void);

#endif /* __PSPAM_USER */
