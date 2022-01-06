/*
 * ParaStation
 *
 * Copyright (C) 2020-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <security/pam_appl.h>
#include <security/pam_modules.h>

#include "pscommon.h"
#include "plugin.h"
#include "pluginmalloc.h"

#include "psidhook.h"

#include "pamservice_log.h"

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

    mdbg(PAMSERVICE_LOG_DEBUG, "%s: start PAM service for %s\n",
	 __func__, user);

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

    mdbg(PAMSERVICE_LOG_DEBUG, "%s: start PAM session for %s\n",
	 __func__, user);

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

    mdbg(PAMSERVICE_LOG_DEBUG, "%s: stop PAM service\n", __func__);

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
    char *user = PSC_userFromUID(task->uid);
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
    char *user = PSC_userFromUID(task->uid);
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

int initialize(FILE *logfile)
{
    initLogger(name, logfile);

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
