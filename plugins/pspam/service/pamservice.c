/*
 * ParaStation
 *
 * Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <security/pam_appl.h>

#include "plugin.h"
#include "psidplugin.h"
#include "psidhook.h"
#include "pluginmalloc.h"
#include "pluginhelper.h"

#include "pamservice_log.h"

#define DEBUG 0

/** psid plugin requirements */
char name[] = "pam_service";
int version = 1;
int requiredAPI = 127;
plugin_dep_t dependencies[] = {
    { .name = NULL, .version = 0 } };

static pam_handle_t *pamh = NULL;

static int retPAM = PAM_SUCCESS;

static void startPAMservice(char *user)
{
    const struct pam_conv conversation;
    const char serviceName[] = "psid";

    if (DEBUG) mlog("%s: start pam serivce for %s\n", __func__, user);

    retPAM = pam_start(serviceName, user, &conversation, &pamh);
    if (retPAM != PAM_SUCCESS) {
	mlog("%s: starting PAM for %s failed : %s\n", __func__, user,
	     pam_strerror(pamh, retPAM));
    }
}

static void startPAMsession(char *user)
{
    if (!pamh) {
	mlog("%s: invalid PAM handle for %s\n", __func__, user);
        return;
    }

    if (DEBUG) mlog("%s: start pam session for %s\n", __func__, user);

    retPAM = pam_open_session(pamh, PAM_SILENT);
    if (retPAM != PAM_SUCCESS) {
	mlog("%s: open PAM session for %s failed : %s\n", __func__,
	     user, pam_strerror(pamh, retPAM));
    }
}

static int finishPAMservice(void *unused)
{
    if (!pamh) {
	mlog("%s: invalid PAM handle\n", __func__);
	return 0;
    }

    if (DEBUG) mlog("%s: stop pam service\n", __func__);

    retPAM = pam_close_session(pamh, 0);
    if (retPAM != PAM_SUCCESS) {
	mlog("%s: closing PAM session failed : %s\n", __func__,
	     pam_strerror(pamh, retPAM));
    }

    int ret = pam_end(pamh, retPAM);
    if (ret != PAM_SUCCESS) {
	mlog("%s: ending PAM failed : %s\n", __func__, pam_strerror(pamh, ret));
    }
    return 0;
}

static int handleExecForwarder(void *data)
{
    PStask_t *task = data;
    char *user = uid2String(task->uid);
    if (!user) {
	mlog("getting username for uid %i failed\n", task->uid);
	return 0;
    }

    startPAMservice(user);
    ufree(user);
    return 0;
}

static int handlePsslurmFWinit(void *data)
{
    char *user = data;
    startPAMservice(user);
    return 0;
}

static int handleExecClient(void *data)
{
    PStask_t *task = data;
    char *user = uid2String(task->uid);
    if (!user) {
	mlog("getting username for uid %i failed\n", task->uid);
	return 0;
    }

    startPAMsession(user);
    ufree(user);
    return 0;
}

static int handlePsslurmJobExec(void *data)
{
    char *user = data;
    startPAMsession(user);
    return 0;
}

int initialize(void)
{
    if (!PSIDhook_add(PSIDHOOK_EXEC_FORWARDER, handleExecForwarder)) {
	mlog("register 'PSIDHOOK_EXEC_FORWARDER' failed\n");
	return 1;
    }

    if (!PSIDhook_add(PSIDHOOK_EXEC_CLIENT, handleExecClient)) {
	mlog("register 'PSIDHOOK_EXEC_CLIENT' failed\n");
	return 1;
    }

    if (!PSIDhook_add(PSIDHOOK_FRWRD_EXIT, finishPAMservice)) {
	mlog("register 'PSIDHOOK_FRWRD_EXIT' failed\n");
	return 1;
    }

    if (!PSIDhook_add(PSIDHOOK_PSSLURM_JOB_FWINIT, handlePsslurmFWinit)) {
	mlog("register 'PSIDHOOK_PSSLURM_JOB_FWINIT' failed\n");
	return 1;
    }

    if (!PSIDhook_add(PSIDHOOK_PSSLURM_JOB_EXEC, handlePsslurmJobExec)) {
	mlog("register 'PSIDHOOK_PSSLURM_JOB_EXEC' failed\n");
	return 1;
    }

    if (!PSIDhook_add(PSIDHOOK_PSSLURM_JOB_FWFIN, finishPAMservice)) {
	mlog("register 'PSIDHOOK_PSSLURM_JOB_FWFIN' failed\n");
	return 1;
    }

    mlog("(%i) successfully started\n", version);
    return 0;
}

void cleanup(void)
{
    if (!PSIDhook_del(PSIDHOOK_EXEC_FORWARDER, handleExecForwarder)) {
	mlog("unregister 'PSIDHOOK_EXEC_FORWARDER' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_EXEC_CLIENT, handleExecClient)) {
	mlog("unregister 'PSIDHOOK_EXEC_CLIENT' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_FRWRD_EXIT, finishPAMservice)) {
	mlog("unregister 'PSIDHOOK_FRWRD_EXIT' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_PSSLURM_JOB_FWINIT, handlePsslurmFWinit)) {
	mlog("unregister 'PSIDHOOK_PSSLURM_JOB_FWINIT' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_PSSLURM_JOB_EXEC, handlePsslurmJobExec)) {
	mlog("unregister 'PSIDHOOK_PSSLURM_JOB_EXEC' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_PSSLURM_JOB_FWFIN, finishPAMservice)) {
	mlog("unregister 'PSIDHOOK_PSSLURM_JOB_FWFIN' failed\n");
    }
}
