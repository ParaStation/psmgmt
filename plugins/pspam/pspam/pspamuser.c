/*
 * ParaStation
 *
 * Copyright (C) 2014-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pspamssh.h"
#include "pluginmalloc.h"

#include "pspamuser.h"

/** List holding all users */
static LIST_HEAD(userList);

void initUserList(void) {}

bool addUser(char *username, char *plugin, PSPAMState_t state)
{
    User_t *user = findUser(username, plugin);

    if (user) return true;

    user = malloc(sizeof(*user));
    if (!user) return false;

    user->name = strdup(username);
    if (!user->name) {
	free(user);
	return false;
    }
    user->plugin = ustrdup(plugin);
    if (!user->plugin) {
	free(user->name);
	free(user);
	return false;
    }
    user->state = state;

    list_add_tail(&user->next, &userList);
    return true;
}

User_t *findUser(char *username, char *plugin)
{
    list_t *u;

    if (!username) return NULL;

    list_for_each(u, &userList) {
	User_t *user = list_entry(u, User_t, next);
	if (plugin && strcmp(user->plugin, plugin)) continue;
	if (!strcmp(user->name, username)) return user;
    }
    return NULL;
}

static void doDelete(User_t *user)
{
    if (!user) return;

    free(user->name);
    free(user->plugin);
    list_del(&user->next);
    free(user);
}

void setState(char *username, char *plugin, int state)
{
    User_t *user = findUser(username, plugin);

    if (!plugin || !user) return;

    user->state = state;
}

void deleteUser(char *username, char *plugin)
{
    User_t *user = findUser(username, plugin);

    if (!plugin || !user) return;

    doDelete(user);

    if (!findUser(username, NULL)) killSessions(username);
}

void clearUserList(void)
{
    list_t *u, *tmp;

    list_for_each_safe(u, tmp, &userList) {
	User_t *user = list_entry(u, User_t, next);
	doDelete(user);
    }
}

void psPamAddUser(char *username, char *plugin, int state)
{
    addUser(username, plugin, state);
}

void psPamDeleteUser(char *username, char *plugin)
{
    deleteUser(username, plugin);
}

void psPamSetState(char *username, char *plugin, int state)
{
    setState(username, plugin, state);
}
