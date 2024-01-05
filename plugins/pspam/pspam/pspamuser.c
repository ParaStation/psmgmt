/*
 * ParaStation
 *
 * Copyright (C) 2014-2017 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pspamuser.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pluginmalloc.h"

#include "pspamlog.h"
#include "pspamssh.h"

/** List holding all users */
static LIST_HEAD(userList);

bool addUser(char *username, char *jobID, PSPAMState_t state)
{
    User_t *user = findUser(username, jobID);

    if (user) return true;

    user = malloc(sizeof(*user));
    if (!user) return false;

    user->name = strdup(username);
    if (!user->name) {
	free(user);
	return false;
    }
    user->jobID = ustrdup(jobID);
    if (!user->jobID) {
	free(user->name);
	free(user);
	return false;
    }
    user->state = state;

    list_add_tail(&user->next, &userList);
    return true;
}

User_t *findUser(char *username, char *jobID)
{
    if (!username) return NULL;

    list_t *u;
    list_for_each(u, &userList) {
	User_t *user = list_entry(u, User_t, next);
	if (jobID && strcmp(user->jobID, jobID)) continue;
#ifndef __clang_analyzer__
	if (!strcmp(user->name, username)) return user;
#endif
    }
    return NULL;
}

static void doDelete(User_t *user)
{
    if (!user) return;

    free(user->name);
    free(user->jobID);
    list_del(&user->next);
    free(user);
}

void setState(char *username, char *jobID, PSPAMState_t state)
{
    User_t *user = findUser(username, jobID);

    if (!jobID || !user) {
	if (jobID) mlog("%s: no entry for %s/%s\n", __func__, username, jobID);
	return;
    }

    user->state = state;
}

void deleteUser(char *username, char *jobID)
{
    User_t *user = findUser(username, jobID);

    if (!jobID || !user) {
	if (jobID) mdbg(PSPAM_LOG_DEBUG, "%s: no entry for %s/%s\n", __func__,
			username, jobID);
	return;
    }

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

const char *state2Str(PSPAMState_t state)
{
    switch(state) {
    case PSPAM_STATE_PROLOGUE:
	return "PSPAM_STATE_PROLOGUE";
    case PSPAM_STATE_JOB:
	return "PSPAM_STATE_JOB";
    default:
	return "unknown";
    }
}

char *listUsers(char *buf, size_t *bufSize)
{
    char l[160];
    list_t *u;

    if (list_empty(&userList)) {
	return str2Buf("\nNo current users.\n", &buf, bufSize);
    }

    str2Buf("\nusers:\n", &buf, bufSize);

    list_for_each(u, &userList) {
	User_t *user = list_entry(u, User_t, next);

	snprintf(l, sizeof(l), "%12s %24s %s\n", user->name, user->jobID,
		 state2Str(user->state));
	str2Buf(l, &buf, bufSize);
    }

    return buf;
}
