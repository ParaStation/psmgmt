/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <pwd.h>
#include <signal.h>

#include "pspluginprotocol.h"
#include "psidcomm.h"
#include "psidhook.h"
#include "psidplugin.h"
#include "plugin.h"
#include "timer.h"
#include "psaccounthandles.h"

#include "pluginfrag.h"
#include "pluginlog.h"
#include "pluginfrag.h"
#include "pluginhelper.h"

#include "peloguelog.h"
#include "peloguecomm.h"
#include "peloguescript.h"
#include "pelogueconfig.h"
#include "peloguechild.h"

#include "pelogue.h"

/** set to the home directory of root */
char rootHome[100];

/** the job cleanup timer */
static int cleanupTimerID = -1;

/** the maximal number of seconds to wait for all jobs to exit. */
static int obitTime = 10;

/** psid plugin requirements */
char name[] = "pelogue";
int version = 6;
int requiredAPI = 114;
plugin_dep_t dependencies[] = {
    { .name = "psaccount", .version = 21 },
    { .name = NULL, .version = 0 } };

static void cleanupJobs(void)
{
    static int obitTimeCounter = 0;
    int njobs;

    /* check if we are waiting for jobs to exit */
    obitTimeCounter++;

    if ((njobs = countJobs()) == 0) {
	Timer_remove(cleanupTimerID);
	cleanupTimerID = -1;
	return;
    }

    if (obitTime == obitTimeCounter) {
	mlog("sending SIGKILL to %i remaining jobs\n", njobs);
	signalAllJobs(SIGKILL, "shutdown");
    }

    /* all jobs are gone */
    PSIDplugin_unload("pelogue");
}

static void shutdownJobs(void)
{
    struct timeval cleanupTimer = {1,0};

    mlog("shutdown jobs\n");

    if (countJobs() > 0) {
	mlog("sending SIGTERM to %i remaining jobs\n", countJobs());

	signalAllJobs(SIGTERM, "shutdown");

	if ((cleanupTimerID = Timer_register(&cleanupTimer,
							cleanupJobs)) == -1) {
	    mlog("registering cleanup timer failed\n");
	}
	return;
    }

    /* all jobs are gone */
    PSIDplugin_unload("pelogue");
}

static bool nodeDownVisitor(Job_t *job, const void *info)
{
    PSnodes_ID_t id = *(PSnodes_ID_t *)info;
    int i;

    if (job->state != JOB_PROLOGUE && job->state != JOB_EPILOGUE) return false;
    if (!findChild(job->plugin, job->id)) return false;

    for (i=0; i<job->nrOfNodes; i++) {
	if (job->nodes[i].id == id) {
	    const char *hname = getHostnameByNodeId(id);

	    mlog("%s: node %s(%i) running job '%s' jstate '%s' is down\n",
		 __func__, hname, id, job->id, jobState2String(job->state));

	    if (job->state == JOB_PROLOGUE) {
		job->nodes[i].prologue = 2;
		job->state = JOB_CANCEL_PROLOGUE;
	    } else {
		job->nodes[i].epilogue = 2;
		job->state = JOB_CANCEL_EPILOGUE;
	    }

	    /* stop pelogue scripts on all nodes */
	    sendPElogueSignal(job, SIGTERM, "node down");
	    stopJobExecution(job);
	    break;
	}
    }
    return false;
}

static int handleNodeDown(void *nodeID)
{
    traverseJobs(nodeDownVisitor, nodeID);
    return 1;
}

static int handleShutdown(void *data)
{
    obitTime = 2;
    shutdownJobs();

    return 0;
}

/**
 * @brief Unregister all hooks and message handler.
 *
 * @param verbose If set to true an error message will be displayed
 * when unregistering a hook or a message handle fails.
 *
 * @return No return value.
 */
static void unregisterHooks(bool verbose)
{
    /* unregister pelogue msg */
    PSID_clearMsg(PSP_CC_PLUG_PELOGUE);

    /* unregister msg drop handler */
    PSID_clearDropper(PSP_CC_PLUG_PELOGUE);

    /* unregister hooks */
    if (!PSIDhook_del(PSIDHOOK_NODE_DOWN, handleNodeDown)) {
	if (verbose) mlog("unregister 'PSIDHOOK_NODE_DOWN' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_SHUTDOWN, handleShutdown)) {
	if (verbose) mlog("unregister 'PSIDHOOK_SHUTDOWN' failed\n");
    }
}

static bool setRootHome(void)
{
    struct passwd *spasswd = getpwnam("root");

    if (!spasswd) {
	mlog("%s: getpwnam(root) failed\n", __func__);
	return false;
    }

    snprintf(rootHome, sizeof(rootHome), "%s", spasswd->pw_dir);
    return true;
}

int initialize(void)
{
    void *accHandle = PSIDplugin_getHandle("psaccount");

    /* init the logger (log to syslog) */
    initLogger(NULL);

    /*
    FILE *lfile = fopen("/tmp/malloc", "w+");
    initPluginLogger(NULL, lfile);
    maskPluginLogger(PLUGIN_LOG_MALLOC);
    */

    /* we need to have root privileges */
    if (getuid() != 0) {
	fprintf(stderr, "%s: pelogue must have root privileges\n", __func__);
	return 1;
    }

    initConfig();
    initFragComm();

    /* register pelogue msg */
    PSID_registerMsg(PSP_CC_PLUG_PELOGUE, (handlerFunc_t) handlePelogueMsg);

    /* register handler for dropped msgs */
    PSID_registerDropper(PSP_CC_PLUG_PELOGUE, (handlerFunc_t) handleDroppedMsg);

    /* get psaccount function handles */
    if (!accHandle) {
	mlog("%s: getting psaccount handle failed\n", __func__);
	goto INIT_ERROR;
    }

    psAccountSignalSession = dlsym(accHandle, "psAccountSignalSession");
    if (!psAccountSignalSession) {
	mlog("%s: loading function psAccountSignalSession() failed\n",
		__func__);
	goto INIT_ERROR;
    }

    /* register needed hooks */
    if (!PSIDhook_add(PSIDHOOK_NODE_DOWN, handleNodeDown)) {
	mlog("register 'PSIDHOOK_NODE_DOWN' failed\n");
	goto INIT_ERROR;
    }

    if (!PSIDhook_add(PSIDHOOK_SHUTDOWN, handleShutdown)) {
	mlog("register 'PSIDHOOK_SHUTDOWN' failed\n");
	goto INIT_ERROR;
    }

    /* make sure timer facility is ready */
    if (!Timer_isInitialized()) {
	mdbg(PELOGUE_LOG_WARN, "timer facility not ready, trying to initialize"
	    " it\n");
	Timer_init(NULL);
    }

    setRootHome();

    maskLogger(PELOGUE_LOG_PROCESS | PELOGUE_LOG_WARN);

    mlog("(%i) successfully started\n", version);
    return 0;

INIT_ERROR:
    unregisterHooks(false);
    return 1;
}

void finalize(void)
{
    /* kill all running jobs */
    shutdownJobs();
}

void cleanup(void)
{
    /* TODO: kill all remaining children */

    /* remove all registered hooks and msg handler */
    unregisterHooks(true);

    /* TODO free all malloced memory */
    clearJobList();
    clearChildList();
    clearConfig();
    finalizeFragComm();

    mlog("...Bye.\n");

    /* release the logger */
    logger_finalize(peloguelogger);
}
