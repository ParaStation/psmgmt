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
#include <signal.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <pwd.h>

#include "psslurmlog.h"
#include "psslurmlimits.h"
#include "psslurmpscomm.h"
#include "psslurmconfig.h"
#include "psslurmjob.h"
#include "psslurmforwarder.h"
#include "psslurmcomm.h"
#include "psslurmproto.h"
#include "slurmcommon.h"

#include "pluginmalloc.h"
#include "pluginlog.h"

#include "pspluginprotocol.h"
#include "psdaemonprotocol.h"
#include "psidplugin.h"
#include "psidhook.h"
#include "plugin.h"
#include "timer.h"
#include "psaccfunc.h"

#include "psslurm.h"

// set default dir from my config / build system
//#define SPOOL_DIR "/var/spool/parastation/scripts"

#define PSSLURM_CONFIG_FILE  PLUGINDIR "/psslurm.conf"
#define PSSLURM_SLURMD_PORT 6818
#define PSSLURM_SLURMCTLD_PORT 6817

/** the job cleanup timer */
static int cleanupTimerID = -1;

/** the maximal number of seconds to wait for all jobs to exit. */
static int obitTime = 10;

static int isInit = 0;

uid_t slurmUserID = 495;

time_t start_time;

handlerFunc_t oldChildBornHandler = NULL;

/** psid plugin requirements */
char name[] = "psslurm";
int version = 12;
int requiredAPI = 109;
plugin_dep_t dependencies[4];

void startPsslurm()
{
    dependencies[0].name = "psmunge";
    dependencies[0].version = 1;
    dependencies[1].name = "psaccount";
    dependencies[1].version = 21;
    dependencies[2].name = "pelogue";
    dependencies[2].version = 4;
    dependencies[3].name = NULL;
    dependencies[3].version = 0;
}

void stopPsslurm()
{
    /* release the logger */
    logger_finalize(psslurmlogger);
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
    /* unregister psslurm msg */
    PSID_clearMsg(PSP_CC_PLUG_PSSLURM);

    if (oldChildBornHandler) {
	PSID_registerMsg(PSP_DD_CHILDBORN, (handlerFunc_t) oldChildBornHandler);
    }

    /* unregister msg drop handler */
    PSID_clearDropper(PSP_CC_PLUG_PSSLURM);

    /* unregister hooks */
    if (!(PSIDhook_del(PSIDHOOK_NODE_DOWN, handleNodeDown))) {
	if (verbose) mlog("unregister 'PSIDHOOK_NODE_DOWN' failed\n");
    }

    if (!(PSIDhook_del(PSIDHOOK_CREATEPART, handleCreatePart))) {
	if (verbose) mlog("unregister 'PSIDHOOK_CREATEPART' failed\n");
    }

    if (!(PSIDhook_del(PSIDHOOK_CREATEPARTNL, handleCreatePartNL))) {
	if (verbose) mlog("unregister 'PSIDHOOK_CREATEPARTNL' failed\n");
    }

    if (!(PSIDhook_del(PSIDHOOK_EXEC_CLIENT_USER, handleExecClient))) {
	if (verbose) mlog("unregister 'PSIDHOOK_EXEC_CLIENT_USER' failed\n");
    }

    if (!(PSIDhook_del(PSIDHOOK_FRWRD_INIT, handleForwarderInit))) {
	if (verbose) mlog("unregister 'PSIDHOOK_FRWRD_INIT' failed\n");
    }
}

static int registerHooks()
{
    /* register psslurm msg */
    PSID_registerMsg(PSP_CC_PLUG_PSSLURM, (handlerFunc_t) handlePsslurmMsg);

    oldChildBornHandler = PSID_registerMsg(PSP_DD_CHILDBORN,
					    (handlerFunc_t) handleChildBornMsg);

    /* register handler for dropped msgs */
    PSID_registerDropper(PSP_CC_PLUG_PSSLURM, (handlerFunc_t) handleDroppedMsg);

    /* register various hooks */
    if (!(PSIDhook_add(PSIDHOOK_NODE_DOWN, handleNodeDown))) {
	mlog("register 'PSIDHOOK_NODE_DOWN' failed\n");
	return 0;
    }

    if (!(PSIDhook_add(PSIDHOOK_CREATEPART, handleCreatePart))) {
	mlog("register 'PSIDHOOK_CREATEPART' failed\n");
	return 0;
    }

    if (!(PSIDhook_add(PSIDHOOK_CREATEPARTNL, handleCreatePartNL))) {
	mlog("register 'PSIDHOOK_CREATEPARTNL' failed\n");
	return 0;
    }

    if (!(PSIDhook_add(PSIDHOOK_EXEC_CLIENT_USER, handleExecClient))) {
	mlog("register 'PSIDHOOK_EXEC_CLIENT_USER' failed\n");
	return 0;
    }

    if (!(PSIDhook_add(PSIDHOOK_FRWRD_INIT, handleForwarderInit))) {
	mlog("register 'PSIDHOOK_FRWRD_INIT' failed\n");
	return 0;
    }

    return 1;
}

static int initPluginHandles()
{
    void *pluginHandle = NULL;

    /* get psaccount function handles */
    if (!(pluginHandle = PSIDplugin_getHandle("psaccount"))) {
	mlog("%s: getting psaccount handle failed\n", __func__);
	return 0;
    }

    if (!(psAccountsendSignal2Session = dlsym(pluginHandle,
	    "psAccountsendSignal2Session"))) {
	mlog("%s: loading function psAccountsendSignal2Session() failed\n",
		__func__);
	return 0;
    }

    if (!(psAccountRegisterJob = dlsym(pluginHandle,
            "psAccountRegisterMOMJob"))) {
        mlog("%s: loading function psAccountRegisterMOMJob() failed\n",
                __func__);
        return 0;
    }

    if (!(psAccountUnregisterJob = dlsym(pluginHandle,
            "psAccountUnregisterMOMJob"))) {
        mlog("%s: loading function psAccountUnregisterMOMJob() failed\n",
                __func__);
        return 0;
    }

    if (!(psAccountSetGlobalCollect = dlsym(pluginHandle,
            "psAccountSetGlobalCollect"))) {
        mlog("%s: loading function psAccountSetGlobalCollect() failed\n",
                __func__);
        return 0;
    }

    if (!(psAccountGetJobData = dlsym(pluginHandle, "psAccountGetJobData"))) {
        mlog("%s: loading function psAccountGetJobData() failed\n", __func__);
        return 0;
    }

    if (!(pluginHandle = PSIDplugin_getHandle("pelogue"))) {
	mlog("%s: getting pelogue handle failed\n", __func__);
	return 0;
    }

    /* get pelogue function handles */
    if (!(psPelogueAddPluginConfig = dlsym(pluginHandle,
	    "psPelogueAddPluginConfig"))) {
	mlog("%s: loading function psPelogueAddPluginConfig() failed\n",
		__func__);
	return 0;
    }

    if (!(psPelogueAddJob = dlsym(pluginHandle, "psPelogueAddJob"))) {
	mlog("%s: loading function psPelogueAddJob() failed\n", __func__);
	return 0;
    }

    if (!(psPelogueDeleteJob = dlsym(pluginHandle, "psPelogueDeleteJob"))) {
	mlog("%s: loading function psPelogueDeleteJob() failed\n", __func__);
	return 0;
    }

    if (!(psPelogueStartPE = dlsym(pluginHandle, "psPelogueStartPE"))) {
	mlog("%s: loading function psPelogueStartPE() failed\n", __func__);
	return 0;
    }

    if (!(psPelogueSignalPE = dlsym(pluginHandle, "psPelogueSignalPE"))) {
	mlog("%s: loading function psPelogueSignalPE() failed\n", __func__);
	return 0;
    }

    /* get psmunge function handles */
    if (!(pluginHandle = PSIDplugin_getHandle("psmunge"))) {
	mlog("%s: getting psmunge handle failed\n", __func__);
	return 0;
    }

    if (!(psMungeEncode = dlsym(pluginHandle, "mungeEncode"))) {
	mlog("%s: loading function mungeEncode() failed\n", __func__);
	return 0;
    }

    if (!(psMungeDecode = dlsym(pluginHandle, "mungeDecode"))) {
	mlog("%s: loading function mungeDecode() failed\n", __func__);
	return 0;
    }

    if (!(psMungeDecodeBuf = dlsym(pluginHandle, "mungeDecodeBuf"))) {
	mlog("%s: loading function mungeDecodeBuf() failed\n", __func__);
	return 0;
    }

    return 1;
}

int initialize(void)
{
    int ctlPort;
    char *slurmUser, buf[256];
    struct passwd *pw;

    start_time = time(NULL);

    setenv("MALLOC_CHECK_", "2", 1);

    /* init the logger (log to syslog) */
    initLogger("psslurm", NULL);
    maskLogger(PSSLURM_LOG_PROTO);
    //maskLogger(PSSLURM_LOG_PROTO | PSSLURM_LOG_PART);

    /*
    FILE *lfile = fopen("/tmp/malloc", "w+");
    initPluginLogger(NULL, lfile);
    maskPluginLogger(PLUGIN_LOG_MALLOC);
    */

    /* init all data lists */
    initJobList();

    /* we need to have root privileges, or the pbs_server will refuse our
     * connection attempt.
     */
    if(getuid() != 0) {
	mlog("%s: psslurm must have root privileges\n", __func__);
	return 1;
    }

    /* init the configuration */
    if (!(initConfig(PSSLURM_CONFIG_FILE))) {
	mlog("%s: init of the configuration failed\n", __func__);
	return 1;
    }

    if (!(registerHooks())) goto INIT_ERROR;
    if (!(initPluginHandles())) goto INIT_ERROR;
    if (!(initLimits())) goto INIT_ERROR;
    printLimits();

    /* set collect mode in psaccount */
    psAccountSetGlobalCollect(1);

    psPelogueAddPluginConfig("psslurm", &Config);

    /* make sure timer facility is ready */
    if (!Timer_isInitialized()) {
	mdbg(PSSLURM_LOG_WARN, "timer facility not ready, trying to initialize"
	    " it\n");
	Timer_init(NULL);
    }

    /* save the slurm user id */
    if ((slurmUser = getConfValueC(&SlurmConfig, "SlurmUser"))) {
	if ((pw = getpwnam(slurmUser))) {
	    slurmUserID = pw->pw_uid;
	} else {
	    mwarn(errno, "%s: getting userid of slurm user '%s' failed: ",
		    __func__, slurmUser);
	    goto INIT_ERROR;
	}
    }

    /* listening on slurmd port */
    getConfValueI(&SlurmConfig, "SlurmdPort", &ctlPort);
    if (ctlPort < 0) ctlPort = PSSLURM_SLURMD_PORT;
    if ((openSlurmdSocket(ctlPort)) < 0) goto INIT_ERROR;

    /* register to slurmctld */
    getConfValueI(&SlurmConfig, "SlurmctldPort", &ctlPort);
    if (ctlPort < 0) {
	snprintf(buf, sizeof(buf), "%u", PSSLURM_SLURMCTLD_PORT);
	addConfigEntry(&SlurmConfig, "SlurmctldPort", buf);
    }
    sendNodeRegStatus(-1, SLURM_SUCCESS, SLURM_14_03_PROTOCOL_VERSION);

    isInit = 1;

    mlog("(%i) successfully started: %u\n", version,
	    SLURM_14_03_PROTOCOL_VERSION);

    return 0;

INIT_ERROR:
    unregisterHooks(0);
    return 1;
}

void finalize(void)
{

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

void cleanup(void)
{
    if (!isInit) return;

    /* TODO: kill all jobs */
    shutdownJobs();

    /* remove all registered hooks and msg handler */
    unregisterHooks(1);

    /* free all malloced memory */
    clearJobList();
    freeConfig(&Config);

    mlog("...Bye.\n");
}
