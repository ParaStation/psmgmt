/*
 * ParaStation
 *
 * Copyright (C) 2014 - 2015 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pspamssh.h"
#include "pluginmalloc.h"

#include "pspamuser.h"


void initUserList()
{
    INIT_LIST_HEAD(&UserList.list);
}

void addUser(char *username, char *plugin, int state)
{
    User_t *user;

    if (findUser(username, plugin)) return;

    user = (User_t *) umalloc(sizeof(User_t));
    user->name = ustrdup(username);
    user->plugin = ustrdup(plugin);
    user->state = state;

    list_add_tail(&(user->list), &UserList.list);
}

User_t *findUser(char *username, char *plugin)
{
    list_t *pos, *tmp;
    User_t *user;

    if (!username) return NULL;

    list_for_each_safe(pos, tmp, &UserList.list) {
	if (!(user = list_entry(pos, User_t, list))) return NULL;
	if (plugin && !!strcmp(user->plugin, plugin)) continue;
	if (!strcmp(user->name, username)) {
	    return user;
	}
    }
    return NULL;
}

void deleteUser(char *username, char *plugin)
{
    list_t *pos, *tmp;
    User_t *user;

    if (!username || !plugin) return;

    list_for_each_safe(pos, tmp, &UserList.list) {
	if (!(user = list_entry(pos, User_t, list))) break;
	if (!(strcmp(user->name, username)) &&
	    !(strcmp(user->plugin, plugin))) {

	    ufree(user->name);
	    ufree(user->plugin);

	    list_del(&user->list);
	    ufree(user);
	}
    }

    if (!(findUser(username, NULL))) delSSHSessions(username);
}

void setState(char *username, char *plugin, int state)
{
    list_t *pos, *tmp;
    User_t *user;

    if (!username || !plugin) return;

    list_for_each_safe(pos, tmp, &UserList.list) {
	if (!(user = list_entry(pos, User_t, list))) break;
	if (!(strcmp(user->name, username)) &&
	    !(strcmp(user->plugin, plugin))) {
	    user->state = state;
	}
    }
}

void clearUserList()
{
    list_t *pos, *tmp;
    User_t *user;

    list_for_each_safe(pos, tmp, &UserList.list) {
	if (!(user = list_entry(pos, User_t, list))) return;
	deleteUser(user->name, user->plugin);
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
