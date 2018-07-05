/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/types.h>

#include "pspluginprotocol.h"
#include "psidhook.h"
#include "psidplugin.h"
#include "plugin.h"
#include "timer.h"
#include "psaccounthandles.h"

#include "pluginhelper.h"

#include "peloguelog.h"
#include "peloguecomm.h"
#include "pelogueconfig.h"
#include "peloguechild.h"

/** the job cleanup timer */
static int cleanupTimerID = -1;

/** Number of seconds to wait for all jobs to end before sending SIGKILL */
static int obitTime = 2;

/** psid plugin requirements */
char name[] = "pelogue";
int version = 7;
int requiredAPI = 119;
plugin_dep_t dependencies[] = {
    { .name = "psaccount", .version = 21 },
    { .name = NULL, .version = 0 } };

static void cleanupJobs(void)
{
    static int obitTimeCounter = 0;
    int numJobs = countActiveJobs();

    /* check if we are waiting for jobs to exit */
    obitTimeCounter++;

    if (!numJobs) {
	/* all jobs are gone */
	Timer_remove(cleanupTimerID);
	cleanupTimerID = -1;

	PSIDplugin_unload(name);

	return;
    }

    if (obitTimeCounter >= obitTime) {
	mlog("sending SIGKILL to %i remaining jobs\n", numJobs);
	signalAllJobs(SIGKILL, "shutdown");
    }
}

static bool nodeDownVisitor(Job_t *job, const void *info)
{
    PSnodes_ID_t id = *(PSnodes_ID_t *)info;
    int i;

    if (job->state != JOB_PROLOGUE && job->state != JOB_EPILOGUE) return false;
    if (!findChild(job->plugin, job->id)) return false;

    for (i=0; i<job->numNodes; i++) {
	if (job->nodes[i].id == id) {
	    const char *hname = getHostnameByNodeId(id);

	    mlog("%s: node %s(%i) running job '%s' jstate '%s' is down\n",
		 __func__, hname, id, job->id, jobState2String(job->state));

	    if (job->state == JOB_PROLOGUE) {
		job->nodes[i].prologue = PELOGUE_TIMEDOUT;
		job->state = JOB_CANCEL_PROLOGUE;
	    } else {
		job->nodes[i].epilogue = PELOGUE_TIMEDOUT;
		job->state = JOB_CANCEL_EPILOGUE;
	    }

	    /* stop pelogue scripts on all nodes */
	    sendPElogueSignal(job, SIGTERM, "node down");
	    cancelJob(job);
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
    finalizeComm();

    /* unregister hooks */
    if (!PSIDhook_del(PSIDHOOK_NODE_DOWN, handleNodeDown)) {
	if (verbose) mlog("unregister 'PSIDHOOK_NODE_DOWN' failed\n");
    }
}

int initialize(void)
{
    void *accHandle = PSIDplugin_getHandle("psaccount");

    /* init the logger (log to syslog) */
    initLogger(NULL);

    /* we need to have root privileges */
    if (getuid() != 0) {
	mlog("%s: pelogue must have root privileges\n", __func__);
	return 1;
    }

    initPluginConfigs();
    initComm();

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

    /* make sure timer facility is ready */
    if (!Timer_isInitialized()) {
	mdbg(PELOGUE_LOG_WARN, "timer facility not yet ready, try to init\n");
	Timer_init(NULL);
    }

    maskLogger(PELOGUE_LOG_PROCESS | PELOGUE_LOG_WARN);

    mlog("(%i) successfully started\n", version);
    return 0;

INIT_ERROR:
    unregisterHooks(false);
    return 1;
}

void finalize(void)
{
    int remJobs = countActiveJobs();

    if (remJobs > 0) {
	struct timeval cleanupTimer = {1,0};

	mlog("sending SIGTERM to %i remaining jobs\n", remJobs);
	signalAllJobs(SIGTERM, "shutdown");

	/* re-investigate every second */
	cleanupTimerID = Timer_register(&cleanupTimer, cleanupJobs);
	if (cleanupTimerID == -1) mlog("timer registration failed\n");

	return;
    }

    PSIDplugin_unload(name);
}

void cleanup(void)
{
    /* unregister timer */
    if (cleanupTimerID != -1) Timer_remove(cleanupTimerID);

    clearChildList();
    clearJobList();
    clearAllPluginConfigs();
    unregisterHooks(true);

    mlog("...Bye.\n");

    /* release the logger */
    logger_finalize(peloguelogger);
}
