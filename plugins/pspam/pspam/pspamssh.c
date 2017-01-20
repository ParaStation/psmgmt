/*
 * ParaStation
 *
 * Copyright (C) 2011-2017 ParTec Cluster Competence Center GmbH, Munich
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

#include "list.h"

/** Structure holding all information concerning a specific ssh session */
typedef struct {
    list_t next;       /**< used to put into list of sessions */
    char *user;        /**< username of the ssh-session owner */
    char *rhost;       /**< remote host the ssh-session came from */
    pid_t pid;         /**< pid of the SSH forwarder */
    pid_t sid;         /**< session ID of the SSH forwarder */
    time_t startTime;  /**< time when the ssh-session was started */
} Session_t;

/* list holding all ssh sessions */
LIST_HEAD(sshList);

bool addSession(char *user, char *rhost, pid_t sshPid, pid_t sshSid)
{
    Session_t *ssh = malloc(sizeof(*ssh));
    if (!ssh) return false;

    ssh->user = ustrdup(user);
    if (!ssh->user) {
	free(ssh);
	return false;
    }

    ssh->rhost = ustrdup(rhost);
    if (!ssh->rhost) {
	free(ssh->user);
	free(ssh);
	return false;
    }

    ssh->pid = sshPid;
    ssh->sid = sshSid;
    ssh->startTime = time(NULL);

    list_add_tail(&ssh->next, &sshList);

    return true;
}

static Session_t *findSession(char *user, char *rhost, pid_t sshPid)
{
    list_t *s;
    list_for_each(s, &sshList) {
	Session_t *ssh = list_entry(s, Session_t, next);
	if (user && !strcmp(ssh->user, user)
	    && rhost && !strcmp(ssh->rhost, rhost)
	    && ssh->pid == sshPid) return ssh;
    }
    return NULL;
}

static void doDelete(Session_t *ssh)
{
    if (!ssh) return;

    free(ssh->user);
    free(ssh->rhost);
    list_del(&ssh->next);
    free(ssh);
}

void rmSession(char *user, char *rhost, pid_t sshPid)
{
    Session_t *ssh = findSession(user, rhost, sshPid);

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
