/*
 * ParaStation
 *
 * Copyright (C) 2020-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <errno.h>
#include <pty.h>
#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "pscommon.h"
#include "plugin.h"
#include "pluginmalloc.h"

#include "psidhook.h"

#include "pamservice_types.h"
#include "pamservice_log.h"

/** psid plugin requirements */
char name[] = "pam_service";
int version = 1;
int requiredAPI = 127;
plugin_dep_t dependencies[] = {
    { .name = NULL, .version = 0 } };

static pam_handle_t *pamh = NULL;

/** pty filedescriptors only used outside the main psid */
static int ptyFDs[2] = {-1, -1};

static bool startPAMservice(char *user, char *ptyName)
{
    const struct pam_conv conversation;
    const char serviceName[] = "psid";

    fdbg(PAMSERVICE_LOG_DEBUG, "start PAM service for %s\n", user);

    int ret = pam_start(serviceName, user, &conversation, &pamh);
    if (ret != PAM_SUCCESS) {
	flog("failed to start PAM for %s: %s\n", user, pam_strerror(pamh, ret));
	return false;
    }

    ret = pam_set_item(pamh, PAM_USER, user);
    if (ret != PAM_SUCCESS) {
	flog("failed to set PAM_USER %s: %s\n", user, pam_strerror(pamh, ret));
	return false;
    }

    ret = pam_set_item(pamh, PAM_RUSER, user);
    if (ret != PAM_SUCCESS) {
	flog("failed to set PAM_RUSER %s: %s\n", user, pam_strerror(pamh, ret));
	return false;
    }

    ret = pam_set_item(pamh, PAM_RHOST, "localhost");
    if (ret != PAM_SUCCESS) {
	flog("failed to set PAM_RHOST: %s\n", pam_strerror(pamh, ret));
	return false;
    }

    if (ptyName) {
	ret = pam_set_item(pamh, PAM_TTY, ptyName);
	if (ret != PAM_SUCCESS) {
	    flog("failed to set PAM_TTY %s: %s\n", ptyName,
		 pam_strerror(pamh, ret));
	    return false;
	}
	fdbg(PAMSERVICE_LOG_DEBUG, "set PAM_TTY to %s for user %s\n",
	     ptyName, user);
    }

    ret = pam_setcred(pamh, PAM_ESTABLISH_CRED);
    if (ret != PAM_SUCCESS) {
	flog("failed to set PAM_ESTABLISH_CRED %s: %s\n", user,
	     pam_strerror(pamh, ret));
	return false;
    }

    return true;
}

pamserviceOpenSession_t pamserviceOpenSession;

bool pamserviceOpenSession(char *user)
{
    if (!pamh) {
	flog("invalid PAM handle for %s\n", user);
	return false;
    }

    fdbg(PAMSERVICE_LOG_DEBUG, "start PAM session for %s\n", user);

    int ret = pam_open_session(pamh, PAM_SILENT);
    if (ret != PAM_SUCCESS) {
	flog("no PAM session for %s: %s\n", user, pam_strerror(pamh, ret));
	return false;
    }

    /* systemd might move the child to its own cgroup, we have to
     * move it back after the session was started */
    pid_t pid = getpid();
    if (!getenv("USER")) setenv("USER", user, 1);
    if (PSIDhook_call(PSIDHOOK_JAIL_CHILD, &pid) < 0) {
	flog("PSIDHOOK_JAIL_CHILD for %i failed\n", pid);
	return false;
    }

    return true;
}

pamserviceStopService_t pamserviceStopService;

bool pamserviceStopService(void)
{
    if (!pamh) {
	flog("invalid PAM handle\n");
	return false;
    }

    fdbg(PAMSERVICE_LOG_DEBUG, "stop PAM service\n");

    int ret = pam_close_session(pamh, 0);
    if (ret != PAM_SUCCESS) {
	flog("closing PAM session failed: %s\n", pam_strerror(pamh, ret));
    }

    ret = pam_setcred(pamh, PAM_DELETE_CRED);
    if (ret != PAM_SUCCESS) {
	flog("removing PAM credentials failed: %s\n", pam_strerror(pamh, ret));
    }

    ret = pam_end(pamh, ret);
    if (ret != PAM_SUCCESS) {
	flog("ending PAM failed: %s\n", pam_strerror(pamh, ret));
    }

    if (ptyFDs[0] >= 0) close(ptyFDs[0]);
    if (ptyFDs[1] >= 0) close(ptyFDs[1]);

    return true;
}

static int finishPAMservice(void *unused)
{
    pamserviceStopService();
    return 0;
}

static int handleExecForwarder(void *data)
{
    PStask_t *task = data;
    char *user = PSC_userFromUID(task->uid);
    if (!user) {
	flog("getting username for uid %i failed\n", task->uid);
	return 0;
    }

    bool ret = startPAMservice(user, NULL);
    ufree(user);
    return ret ? 0 : -1;
}

pamserviceStartService_t pamserviceStartService;

bool pamserviceStartService(char *user)
{
    if (openpty(&ptyFDs[0], &ptyFDs[1], NULL, NULL, NULL)) {
	fwarn(errno, "openpty()");
	return false;
    }
    char *ptyName = ttyname(ptyFDs[1]);
    if (!ptyName) {
	fwarn(errno, "ttyname(%d)", ptyFDs[1]);
	return false;
    }

    /* PAM service and sessions have to be started in the forwarder */
    if (!startPAMservice(user, ptyName)) return false;

    return pamserviceOpenSession(user);
}

static int handleExecClient(void *data)
{
    PStask_t *task = data;
    char *user = PSC_userFromUID(task->uid);
    if (!user) {
	flog("getting username for uid %i failed\n", task->uid);
	return 0;
    }

    bool ret = pamserviceOpenSession(user);
    ufree(user);
    return ret ? 0 : -1;
}

#define addHook(hookName, hookFunc)			\
    if (!PSIDhook_add(hookName, hookFunc)) {		\
	mlog("register '" #hookName "' failed\n");      \
	return 1;                                       \
    }

int initialize(FILE *logfile)
{
    initLogger(name, logfile);

    addHook(PSIDHOOK_EXEC_FORWARDER, handleExecForwarder);
    addHook(PSIDHOOK_EXEC_CLIENT, handleExecClient);
    addHook(PSIDHOOK_FRWRD_EXIT, finishPAMservice);

    mlog("(%i) successfully started\n", version);
    return 0;
}

#define relHook(hookName, hookFunc)                    \
    if (!PSIDhook_del(hookName, hookFunc))             \
	mlog("unregister '" #hookName "' failed\n");

void cleanup(void)
{
    relHook(PSIDHOOK_EXEC_FORWARDER, handleExecForwarder);
    relHook(PSIDHOOK_EXEC_CLIENT, handleExecClient);
    relHook(PSIDHOOK_FRWRD_EXIT, finishPAMservice);

    mlog("...Bye.\n");

    finalizeLogger();
}
