/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <pwd.h>
#include <signal.h>

#include "peloguelog.h"
#include "peloguecomm.h"
#include "peloguescript.h"
#include "pelogueconfig.h"
#include "peloguechild.h"

#include "pspluginprotocol.h"
#include "psidplugin.h"
#include "psidhook.h"
#include "plugin.h"
#include "timer.h"
#include "psaccfunc.h"
#include "pluginfrag.h"

#include "pelogue.h"

/** set to the home directory of root */
char rootHome[100];

/** the job cleanup timer */
static int cleanupTimerID = -1;

/** the maximal number of seconds to wait for all jobs to exit. */
static int obitTime = 10;

/** psid plugin requirements */
char name[] = "pelogue";
int version = 4;
int requiredAPI = 109;
plugin_dep_t dependencies[2];

void startPelogue()
{
    dependencies[0].name = "psaccount";
    dependencies[0].version = 21;
    dependencies[1].name = NULL;
    dependencies[1].version = 0;
}

void stopPelogue()
{
    /* release the logger */
    logger_finalize(peloguelogger);
}

static void cleanupJobs()
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
	signalJobs(SIGKILL, "shutdown");
    }
}

static void shutdownJobs()
{
    struct timeval cleanupTimer = {1,0};

    mlog("shutdown jobs\n");

    if (countJobs() > 0) {
	mlog("sending SIGTERM to %i remaining jobs\n", countJobs());

	signalJobs(SIGTERM, "shutdown");

	if ((cleanupTimerID = Timer_register(&cleanupTimer,
							cleanupJobs)) == -1) {
	    mlog("registering cleanup timer failed\n");
	}
    }
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
static void unregisterHooks(int verbose)
{
    /* unregister pelogue msg */
    PSID_clearMsg(PSP_CC_PLUG_PELOGUE);

    /* unregister msg drop handler */
    PSID_clearDropper(PSP_CC_PLUG_PELOGUE);

    /* unregister hooks */
    if (!(PSIDhook_del(PSIDHOOK_NODE_DOWN, handleNodeDown))) {
	if (verbose) mlog("unregister 'PSIDHOOK_NODE_DOWN' failed\n");
    }

    if (!(PSIDhook_del(PSIDHOOK_SHUTDOWN, handleShutdown))) {
	if (verbose) mlog("unregister 'PSIDHOOK_SHUTDOWN' failed\n");
    }
}

static int setRootHome()
{
    struct passwd *spasswd;

    if (!(spasswd = getpwnam("root"))) {
	mlog("%s: getpwnam(root) failed\n", __func__);
	return 0;
    }

    snprintf(rootHome, sizeof(rootHome), "%s", spasswd->pw_dir);
    return 1;
}

int initialize(void)
{
    void *accHandle = NULL, *mungeHandle;
    static struct timeval time_now;
    Get_Cred_Func_t *getCred = NULL;
    Test_Cred_Func_t *testCred = NULL;

    /* init the logger (log to syslog) */
    initLogger("pelogue", NULL);

    /* we need to have root privileges */
    if (getuid() != 0) {
	fprintf(stderr, "%s: pelogue must have root privileges\n", __func__);
	return 1;
    }

    initConfig();
    initJobList();
    initChildList();

    /* register pelogue msg */
    PSID_registerMsg(PSP_CC_PLUG_PELOGUE, (handlerFunc_t) handlePelogueMsg);

    /* register handler for dropped msgs */
    PSID_registerDropper(PSP_CC_PLUG_PELOGUE, (handlerFunc_t) handleDroppedMsg);

    /* get psaccount function handles */
    if (!(accHandle = PSIDplugin_getHandle("psaccount"))) {
	mlog("%s: getting psaccount handle failed\n", __func__);
	goto INIT_ERROR;
    }

    if (!(psAccountsendSignal2Session = dlsym(accHandle,
	    "psAccountsendSignal2Session"))) {
	mlog("%s: loading function psAccountsendSignal2Session() failed\n",
		__func__);
	goto INIT_ERROR;
    }

    if ((mungeHandle = PSIDplugin_getHandle("psmunge"))) {
	if (!(getCred = dlsym(mungeHandle, "mungeEncode"))) {
	    mlog("%s: loading function mungeEncode() failed\n", __func__);
	} else if (!(testCred = dlsym(mungeHandle, "mungeDecode"))) {
	    mlog("%s: loading function mungeDecode() failed\n", __func__);
	} else {
	    /*
	    setFragCredFunc(getCred, testCred);
	    mlog("%s: using munge auth\n", __func__);
	    */
	}
    }

    /* register needed hooks */
    if (!(PSIDhook_add(PSIDHOOK_NODE_DOWN, handleNodeDown))) {
	mlog("register 'PSIDHOOK_NODE_DOWN' failed\n");
	goto INIT_ERROR;
    }

    if (!(PSIDhook_add(PSIDHOOK_SHUTDOWN, handleShutdown))) {
	mlog("register 'PSIDHOOK_SHUTDOWN' failed\n");
	goto INIT_ERROR;
    }

    /* make sure timer facility is ready */
    if (!Timer_isInitialized()) {
	mdbg(PELOGUE_LOG_WARN, "timer facility not ready, trying to initialize"
	    " it\n");
	Timer_init(NULL);
    }

    gettimeofday(&time_now, NULL);
    srand(time_now.tv_usec);

    setRootHome();

    maskLogger(PELOGUE_LOG_PROCESS | PELOGUE_LOG_WARN);

    mlog("(%i) successfully started\n", version);
    return 0;

INIT_ERROR:
    unregisterHooks(0);
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
    unregisterHooks(1);

    /* TODO free all malloced memory */
    clearJobList();
    clearChildList();
    clearConfig();

    mlog("...Bye.\n");
}
