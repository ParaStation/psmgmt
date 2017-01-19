/*
 * ParaStation
 *
 * Copyright (C) 2014-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSPAM_USER
#define __PSPAM_USER

#include <stdbool.h>

#include "list.h"
#include "pspamdef.h"

/** Structure holding all information concerning a specifig user */
typedef struct {
    list_t next;        /**< used to put into list of users */
    char *name;         /**< username of the registered user */
    char *plugin;       /**< name of the plugin registering the user */
    PSPAMState_t state; /**< current state the user's job is in */
} User_t;

/**
 * @brief Register user
 *
 * Register a new user identified by the username @a username and the
 * registering plugin @a plugin. The new user's jobstate is set to @a
 * state.
 *
 * @param username Name of the user to register
 *
 * @param plugin Name of the registering plugin
 *
 * @param state Current state of the user's job
 *
 * @return If a new user was added or a corresponding user entry
 * already exists true is returned. Otherwise false is returned.
 */
bool addUser(char *username, char *plugin, PSPAMState_t state);

/**
 * @brief Find user
 *
 * Find the user identified by the username @a username and the
 * registering plugin @a plugin. If plugin is NULL users registered by
 * any plugin might be returned.
 *
 * @param username Name of the user to find
 *
 * @param plugin Name of the registering plugin to search for or NULL
 * to match users registered by any plugin.
 *
 * @return Return a pointer to the found user structure or NULL
 */
User_t *findUser(char *username, char *plugin);

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
void setState(char *username, char *plugin, int state);

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
void deleteUser(char *username, char *plugin);

/**
 * @brief Clear all users
 *
 * Clear all users and clear the used memory
 *
 * @return No return value
 */
void clearUserList(void);

void psPamAddUser(char *username, char *plugin, int state);
void psPamDeleteUser(char *username, char *plugin);
void psPamSetState(char *username, char *plugin, int state);

#endif /* __PSPAM_USER */
