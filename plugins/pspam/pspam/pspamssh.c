/*
 * ParaStation
 *
 * Copyright (C) 2011-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include "pluginmalloc.h"
#include "psaccounthandles.h"

#include "pspamssh.h"

/* list holding all ssh sessions */
static LIST_HEAD(sshList);

Session_t *addSession(char *user, char *rhost, pid_t sshPid, pid_t sshSid)
{
    Session_t *ssh = malloc(sizeof(*ssh));
    if (!ssh) return NULL;

    ssh->user = ustrdup(user);
    if (!ssh->user) {
	free(ssh);
	return NULL;
    }

    ssh->rhost = ustrdup(rhost);
    if (!ssh->rhost) {
	free(ssh->user);
	free(ssh);
	return NULL;
    }

    ssh->pid = sshPid;
    ssh->sid = sshSid;
    ssh->startTime = time(NULL);

    list_add_tail(&ssh->next, &sshList);

    return ssh;
}

/**
 * @brief Find a session
 *
 * Find the session with user ID @a sshPid owned by user @a user
 * coming from the remote host @a rhost in the list of sessions. If @a
 * rhost is NULL sessions coming from any remote host might be
 * returned.
 *
 * @param user User owning the session to find
 *
 * @param rhost Remote host the session was initiated from. Any remote
 * host is accepted if NULL
 *
 * @param sshPid Process ID of the session the find
 *
 * @return If a session was found, a pointer to this session is
 * returned. Or NULL otherwise.
 */
static Session_t *findSession(char *user, char *rhost, pid_t sshPid)
{
    list_t *s;
    list_for_each(s, &sshList) {
	Session_t *ssh = list_entry(s, Session_t, next);
	if (user && !strcmp(ssh->user, user)
	    && (!rhost || !strcmp(ssh->rhost, rhost))
	    && ssh->pid == sshPid) return ssh;
    }
    return NULL;
}

bool findSessionForPID(pid_t pid)
{
    list_t *s;
    list_for_each(s, &sshList) {
	Session_t *ssh = list_entry(s, Session_t, next);
	if (ssh->pid == pid || psAccountIsDescendant(ssh->pid, pid)) {
	    return true;
	}
    }
    return false;
}

static void doDelete(Session_t *ssh)
{
    if (!ssh) return;

    free(ssh->user);
    free(ssh->rhost);
    list_del(&ssh->next);
    free(ssh);
}

void rmSession(char *user, pid_t sshPid)
{
    Session_t *ssh = findSession(user, NULL, sshPid);

    if (ssh) doDelete(ssh);
}

static void doExtinct(Session_t *ssh)
{
    if (!ssh) return;

    /* kill ssh session */
    psAccountSignalSession(ssh->sid, SIGTERM);
    psAccountSignalSession(ssh->sid, SIGKILL);

    doDelete(ssh);
}

void killSessions(char *user)
{
    list_t *s, *tmp;
    if (!user) return;

    list_for_each_safe(s, tmp, &sshList) {
	Session_t *ssh = list_entry(s, Session_t, next);
	if (!strcmp(ssh->user, user)) doExtinct(ssh);
    }
}

void clearSessionList(void)
{
    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &sshList) {
	Session_t *ssh = list_entry(s, Session_t, next);
	doExtinct(ssh);
    }
}

char *listSessions(char *buf, size_t *bufSize)
{
    char l[160];
    list_t *s;

    if (list_empty(&sshList)) {
	return str2Buf("\nNo current sessions.\n", &buf, bufSize);
    }

    str2Buf("\nsessions:\n", &buf, bufSize);

    list_for_each(s, &sshList) {
	Session_t *ssh = list_entry(s, Session_t, next);

	snprintf(l, sizeof(l), "%12s %20.20s %6d %6d %s", ssh->user,
		 ssh->rhost, ssh->pid, ssh->sid, ctime(&ssh->startTime));
	str2Buf(l, &buf, bufSize);
    }

    return buf;
}

bool verifySessionPtr(Session_t *sessPtr)
{
    list_t *s;
    list_for_each(s, &sshList) {
	Session_t *sess = list_entry(s, Session_t, next);
	if (sess == sessPtr) return true;
    }
    return false;
}
