/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/time.h>

#include "pscommon.h"
#include "plugin.h"
#include "timer.h"
#include "psidhook.h"
#include "psidplugin.h"
#include "psidutil.h"

#include "psaccounthandles.h"

#include "pluginhelper.h"

#include "peloguecomm.h"
#include "pelogueconfig.h"
#include "peloguechild.h"
#include "peloguejob.h"
#include "peloguelog.h"
#include "peloguetypes.h"

/** the job cleanup timer */
static int cleanupTimerID = -1;

/** Number of seconds to wait for all jobs to end before sending SIGKILL */
static int obitTime = 2;

/** psid plugin requirements */
char name[] = "pelogue";
int version = 10;
int requiredAPI = 136;
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
	flog("sending SIGKILL to %i remaining jobs\n", numJobs);
	signalAllJobs(SIGKILL, "shutdown");
    }
}

static bool nodeDownVisitor(Job_t *job, const void *info)
{
    PSnodes_ID_t id = *(PSnodes_ID_t *)info;
    const char *hname = NULL;

    for (int i=0; i<job->numNodes; i++) {
	if (job->nodes[i].id == id) {
	    /* update node result */
	    switch (job->state) {
	    case JOB_PROLOGUE:
		job->nodes[i].prologue = PELOGUE_NODEDOWN;
		job->state = JOB_CANCEL_PROLOGUE;
		break;
	    case JOB_EPILOGUE:
		job->nodes[i].epilogue = PELOGUE_NODEDOWN;
		job->state = JOB_CANCEL_EPILOGUE;
		break;
	    default:
		/* job is already cancelled, process next node */
		return false;
	    }

	    if (!hname) hname = getHostnameByNodeId(id);

	    flog("node %s(%i) running job '%s' jstate '%s' is down\n",
		 hname, id, job->id, jobState2String(job->state));
	    flog("suppressing further node down error messages for job %s\n",
		 job->id);

	    /* stop pelogue scripts on all nodes */
	    char buf[128];
	    snprintf(buf, sizeof(buf), "node %s is down", hname);
	    sendPElogueSignal(job, SIGTERM, buf);

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
    /* unregister hooks */
    if (!PSIDhook_del(PSIDHOOK_NODE_DOWN, handleNodeDown)) {
	if (verbose) mlog("unregister 'PSIDHOOK_NODE_DOWN' failed\n");
    }
}

int initialize(FILE *logfile)
{
    void *accHandle = PSIDplugin_getHandle("psaccount");

    /* init the logger (log to syslog) */
    initLogger(name, logfile);

    /* we need to have root privileges */
    if (getuid() != 0) {
	mlog("pelogue must have root privileges\n");
	return 1;
    }

    if (!initComm()) {
	mlog("failed to initialize communication\n");
	return 1;
    }

    /* get psaccount function handles */
    if (!accHandle) {
	mlog("getting psaccount handle failed\n");
	return 1;
    }

    psAccountSignalSession = dlsym(accHandle, "psAccountSignalSession");
    if (!psAccountSignalSession) {
	mlog("loading function psAccountSignalSession() failed\n");
	return 1;
    }

    /* register needed hooks */
    if (!PSIDhook_add(PSIDHOOK_NODE_DOWN, handleNodeDown)) {
	mlog("register PSIDHOOK_NODE_DOWN failed\n");
	return 1;
    }

    /* make sure timer facility is ready */
    if (!Timer_isInitialized()) {
	mdbg(PELOGUE_LOG_WARN, "timer facility not yet ready, try to init\n");
	Timer_init(NULL);
    }

    PSID_registerLoopAct(clearDeletedJobs);

    maskLogger(PELOGUE_LOG_PROCESS | PELOGUE_LOG_WARN);

    mlog("(%i) successfully started\n", version);
    return 0;
}

void finalize(void)
{
    int remJobs = countActiveJobs();

    if (remJobs > 0) {
	struct timeval cleanupTimer = {1,0};

	flog("sending SIGTERM to %i remaining jobs\n", remJobs);
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

    PSID_unregisterLoopAct(clearDeletedJobs);

    clearChildList();
    clearJobList();
    clearPluginConfigList();
    unregisterHooks(true);
    finalizeComm();

    mlog("...Bye.\n");
    finalizeLogger();
}
