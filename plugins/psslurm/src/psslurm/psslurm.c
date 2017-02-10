/*
 * ParaStation
 *
 * Copyright (C) 2014-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
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
#include "psslurmgres.h"
#include "psslurmenv.h"
#include "psslurmpelogue.h"
#include "slurmcommon.h"
#include "psslurmspawn.h"

#include "pluginmalloc.h"
#include "pluginlog.h"
#include "pluginhelper.h"
#include "pluginfrag.h"
#include "pspluginprotocol.h"
#include "psdaemonprotocol.h"
#include "psidplugin.h"
#include "psidhook.h"
#include "psidnodes.h"
#include "plugin.h"
#include "timer.h"
#include "psaccounthandles.h"
#include "peloguehandles.h"
#include "psmungehandles.h"
#include "pspamhandles.h"
#include "psexechandles.h"
#include "pspmihandles.h"

#include "psslurm.h"

#define PSSLURM_CONFIG_FILE  PLUGINDIR "/psslurm.conf"
#define PSSLURM_SLURMD_PORT 6818
#define PSSLURM_SLURMCTLD_PORT 6817

/** the job cleanup timer */
static int cleanupTimerID = -1;

/** the maximal number of seconds to wait for all jobs to exit. */
static int obitTime = 10;

static int isInit = 0;

int confAccPollTime;

uid_t slurmUserID = 495;

time_t start_time;

/** hash value of the SLURM config file */
uint32_t configHash;

PSnodes_ID_t slurmController;
PSnodes_ID_t slurmBackupController;

handlerFunc_t oldChildBornHandler = NULL;
handlerFunc_t oldCCMsgHandler = NULL;
handlerFunc_t oldSpawnFailedHandler = NULL;
handlerFunc_t oldSpawnReqHandler = NULL;

/** psid plugin requirements */
char name[] = "psslurm";
int version = 115;
int requiredAPI = 117;
plugin_dep_t dependencies[7];

void startPsslurm(void)
{
    dependencies[0].name = "psmunge";
    dependencies[0].version = 3;
    dependencies[1].name = "psaccount";
    dependencies[1].version = 25;
    dependencies[2].name = "pelogue";
    dependencies[2].version = 6;
    dependencies[3].name = "pspam";
    dependencies[3].version = 3;
    dependencies[4].name = "psexec";
    dependencies[4].version = 1;
    dependencies[5].name = "pspmi";
    dependencies[5].version = 4;
    dependencies[6].name = NULL;
    dependencies[6].version = 0;
}

void stopPsslurm(void)
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

    /* unregister various messages */
    if (oldChildBornHandler) {
	PSID_registerMsg(PSP_DD_CHILDBORN, (handlerFunc_t) oldChildBornHandler);
    }

    if (oldCCMsgHandler) {
	PSID_registerMsg(PSP_CC_MSG, (handlerFunc_t) oldCCMsgHandler);
    }

    if (oldSpawnFailedHandler) {
	PSID_registerMsg(PSP_CD_SPAWNFAILED,
			(handlerFunc_t) oldSpawnFailedHandler);
    }

    if (oldSpawnReqHandler) {
	PSID_registerMsg(PSP_CD_SPAWNREQ,
			(handlerFunc_t) oldSpawnReqHandler);
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

    if (!(PSIDhook_del(PSIDHOOK_EXEC_CLIENT, handleExecClient))) {
	if (verbose) mlog("unregister 'PSIDHOOK_EXEC_CLIENT' failed\n");
    }

    if (!(PSIDhook_del(PSIDHOOK_EXEC_CLIENT_USER, handleExecClientUser))) {
	if (verbose) mlog("unregister 'PSIDHOOK_EXEC_CLIENT_USER' failed\n");
    }

    if (!(PSIDhook_del(PSIDHOOK_FRWRD_INIT, handleForwarderInit))) {
	if (verbose) mlog("unregister 'PSIDHOOK_FRWRD_INIT' failed\n");
    }

    if (!(PSIDhook_del(PSIDHOOK_FRWRD_CLIENT_STAT, handleForwarderClientStatus))) {
	if (verbose) mlog("unregister 'PSIDHOOK_FRWRD_CLIENT_STAT' failed\n");
    }

    if (!(PSIDhook_del(PSIDHOOK_PELOGUE_FINISH, handleLocalPElogueFinish))) {
	if (verbose) mlog("unregister 'PSIDHOOK_PELOGUE_FINISH' failed\n");
    }
}

static int registerHooks()
{
    /* register psslurm msg */
    PSID_registerMsg(PSP_CC_PLUG_PSSLURM, (handlerFunc_t) handlePsslurmMsg);

    /* register various messages */
    oldChildBornHandler = PSID_registerMsg(PSP_DD_CHILDBORN,
					    (handlerFunc_t) handleChildBornMsg);

    oldCCMsgHandler = PSID_registerMsg(PSP_CC_MSG, (handlerFunc_t) handleCCMsg);

    oldSpawnFailedHandler = PSID_registerMsg(PSP_CD_SPAWNFAILED,
					    (handlerFunc_t) handleSpawnFailed);

    oldSpawnReqHandler = PSID_registerMsg(PSP_CD_SPAWNREQ,
					    (handlerFunc_t) handleSpawnReq);

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

    if (!(PSIDhook_add(PSIDHOOK_EXEC_CLIENT, handleExecClient))) {
	mlog("register 'PSIDHOOK_EXEC_CLIENT' failed\n");
	return 0;
    }

    if (!(PSIDhook_add(PSIDHOOK_EXEC_CLIENT_USER, handleExecClientUser))) {
	mlog("register 'PSIDHOOK_EXEC_CLIENT_USER' failed\n");
	return 0;
    }

    if (!(PSIDhook_add(PSIDHOOK_FRWRD_INIT, handleForwarderInit))) {
	mlog("register 'PSIDHOOK_FRWRD_INIT' failed\n");
	return 0;
    }

    if (!(PSIDhook_add(PSIDHOOK_FRWRD_CLIENT_STAT, handleForwarderClientStatus))) {
	mlog("register 'PSIDHOOK_FRWRD_CLIENT_STAT' failed\n");
	return 0;
    }

    if (!(PSIDhook_add(PSIDHOOK_PELOGUE_FINISH, handleLocalPElogueFinish))) {
	mlog("register 'PSIDHOOK_PELOGUE_FINISH' failed\n");
	return 0;
    }

    return 1;
}

static int regPsAccountHandles()
{
    void *pluginHandle = NULL;

    /* get psaccount function handles */
    if (!(pluginHandle = PSIDplugin_getHandle("psaccount"))) {
	mlog("%s: getting psaccount handle failed\n", __func__);
	return 0;
    }

    if (!(psAccountSignalSession = dlsym(pluginHandle,
	    "psAccountSignalSession"))) {
	mlog("%s: loading function psAccountSignalSession() failed\n",
		__func__);
	return 0;
    }

    if (!(psAccountRegisterJob = dlsym(pluginHandle,
            "psAccountRegisterJob"))) {
        mlog("%s: loading function psAccountRegisterJob() failed\n",
                __func__);
        return 0;
    }

    if (!(psAccountUnregisterJob = dlsym(pluginHandle,
            "psAccountUnregisterJob"))) {
        mlog("%s: loading function psAccountUnregisterJob() failed\n",
                __func__);
        return 0;
    }

    if (!(psAccountSetGlobalCollect = dlsym(pluginHandle,
            "psAccountSetGlobalCollect"))) {
        mlog("%s: loading function psAccountSetGlobalCollect() failed\n",
                __func__);
        return 0;
    }

    if (!(psAccountGetDataByJob = dlsym(pluginHandle,
	    "psAccountGetDataByJob"))) {
        mlog("%s: loading function psAccountGetDataByJob() failed\n", __func__);
        return 0;
    }

    if (!(psAccountGetDataByLogger = dlsym(pluginHandle,
	    "psAccountGetDataByLogger"))) {
        mlog("%s: loading function psAccountGetDataByLogger() failed\n",
		__func__);
        return 0;
    }

    if (!(psAccountGetPidsByLogger = dlsym(pluginHandle,
	    "psAccountGetPidsByLogger"))) {
        mlog("%s: loading function psAccountGetPidsByLogger() failed\n",
		__func__);
        return 0;
    }

    if (!(psAccountDelJob = dlsym(pluginHandle,
	    "psAccountDelJob"))) {
        mlog("%s: loading function psAccountDelJob() failed\n",
		__func__);
        return 0;
    }
    return 1;
}

static int regPElogueHandles()
{
    void *pluginHandle = NULL;

    /* get pelogue function handles */
    if (!(pluginHandle = PSIDplugin_getHandle("pelogue"))) {
	mlog("%s: getting pelogue handle failed\n", __func__);
	return 0;
    }

    if (!(psPelogueAddPluginConfig = dlsym(pluginHandle,
	    "psPelogueAddPluginConfig"))) {
	mlog("%s: loading function psPelogueAddPluginConfig() failed\n",
		__func__);
	return 0;
    }

    if (!(psPelogueDelPluginConfig = dlsym(pluginHandle,
	    "psPelogueDelPluginConfig"))) {
	mlog("%s: loading function psPelogueDelPluginConfig() failed\n",
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
    return 1;
}

static int initPluginHandles()
{
    void *pluginHandle = NULL;

    /* get psaccount function handles */
    if (!regPsAccountHandles()) return 0;

    /* get pelogue function handles */
    if (!regPElogueHandles()) return 0;

    /* get psmunge function handles */
    if (!(pluginHandle = PSIDplugin_getHandle("psmunge"))) {
	mlog("%s: getting psmunge handle failed\n", __func__);
	return 0;
    }

    if (!(psMungeEncode = dlsym(pluginHandle, "psMungeEncode"))) {
	mlog("%s: loading function psMungeEncode() failed\n", __func__);
	return 0;
    }

    if (!(psMungeDecode = dlsym(pluginHandle, "psMungeDecode"))) {
	mlog("%s: loading function psMungeDecode() failed\n", __func__);
	return 0;
    }

    if (!(psMungeDecodeBuf = dlsym(pluginHandle, "psMungeDecodeBuf"))) {
	mlog("%s: loading function psMungeDecodeBuf() failed\n", __func__);
	return 0;
    }

    /* get pspam function handles */
    if (!(pluginHandle = PSIDplugin_getHandle("pspam"))) {
	mlog("%s: getting pspam handle failed\n", __func__);
	return 0;
    }

    if (!(psPamAddUser = dlsym(pluginHandle, "psPamAddUser"))) {
	mlog("%s: loading function psPamAddUser() failed\n", __func__);
	return 0;
    }

    if (!(psPamDeleteUser = dlsym(pluginHandle, "psPamDeleteUser"))) {
	mlog("%s: loading function psPamDeleteUser() failed\n", __func__);
	return 0;
    }

    if (!(psPamSetState = dlsym(pluginHandle, "psPamSetState"))) {
	mlog("%s: loading function psPamSetState() failed\n", __func__);
	return 0;
    }

    /* get psexec function handles */
    if (!(pluginHandle = PSIDplugin_getHandle("psexec"))) {
	mlog("%s: getting psexec handle failed\n", __func__);
	return 0;
    }

    if (!(psExecStartScript = dlsym(pluginHandle, "psExecStartScript"))) {
	mlog("%s: loading function psExecStartScript() failed\n", __func__);
	return 0;
    }

    if (!(psExecSendScriptStart = dlsym(pluginHandle,
					    "psExecSendScriptStart"))) {
	mlog("%s: loading function psExecSendScriptStart() failed\n", __func__);
	return 0;
    }

    if (!(psExecStartLocalScript = dlsym(pluginHandle,
					    "psExecStartLocalScript"))) {
	mlog("%s: loading function psExecStartLocalScript() failed\n", __func__);
	return 0;
    }

    /* get pspmi function handles */
    if (!(pluginHandle = PSIDplugin_getHandle("pspmi"))) {
	mlog("%s: getting pspmi handle failed\n", __func__);
	return 0;
    }

    if (!(psPmiSetFillSpawnTaskFunction =
		dlsym(pluginHandle, "psPmiSetFillSpawnTaskFunction"))) {
	mlog("%s: loading function psPmiSetFillSpawnTaskFunction() failed\n",
		__func__);
	return 0;
    }

    if (!(psPmiResetFillSpawnTaskFunction =
		dlsym(pluginHandle, "psPmiResetFillSpawnTaskFunction"))) {
	mlog("%s: loading function psPmiResetFillSpawnTaskFunction() failed\n",
		__func__);
	return 0;
    }

    return 1;
}

static void setConfOpt()
{
    int mask, mCheck;

    /* psslurm debug */
    mask = getConfValueI(&Config, "DEBUG_MASK");
    if (mask) {
	mlog("%s: set psslurm debug mask '%i'\n", __func__, mask);
	maskLogger(mask);
    }

    /* plugin library debug */
    mask = getConfValueI(&Config, "PLUGIN_DEBUG_MASK");
    if (mask) {
	mlog("%s: set plugin debug mask '%i'\n", __func__, mask);
	maskPluginLogger(mask);
    }

    /* glib malloc checking */
    mCheck = getConfValueI(&Config, "MALLOC_CHECK");
    if (mCheck) {
	mlog("%s: enable memory checking\n", __func__);
	setenv("MALLOC_CHECK_", "2", 1);
    }
}

static int getControllerIDs()
{
    char *conAddr;

    /* resolve main controller */
    if (!(conAddr = getConfValueC(&SlurmConfig, "ControlMachine"))) {
	mlog("%s: invalid ControlMachine\n", __func__);
	return 0;
    }

    if ((slurmController = getNodeIDbyName(conAddr)) == -1) {
	mlog("%s: unable to resolve main controller '%s'\n", __func__, conAddr);
	return 0;
    }

    /* resolve backup controller */
    if (!(conAddr = getConfValueC(&SlurmConfig, "BackupController"))) {
	/* we could be running without backup controller */
	return 1;
    }

    if ((slurmBackupController = getNodeIDbyName(conAddr)) == -1) {
	mlog("%s: unable to resolve backup controller '%s'\n",
		__func__, conAddr);
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

    /* init the logger (log to syslog) */
    initLogger("psslurm", NULL);

    /*
    FILE *lfile = fopen("/tmp/malloc", "w+");
    initPluginLogger(NULL, lfile);
    maskPluginLogger(PLUGIN_LOG_MALLOC);
    */
    initPluginLogger("psslurm", NULL);

    /* init all data lists */
    initJobList();
    initGresConf();

    /* we need to have root privileges */
    if(getuid() != 0) {
	mlog("%s: psslurm must have root privileges\n", __func__);
	return 1;
    }

    /* init the configuration */
    if (!(initConfig(PSSLURM_CONFIG_FILE, &configHash))) {
	mlog("%s: init of the configuration failed\n", __func__);
	return 1;
    }

    /* set various config options */
    setConfOpt();

    initSlurmdProto();

    if (!(registerHooks())) goto INIT_ERROR;
    if (!(initPluginHandles())) goto INIT_ERROR;
    if (!(initLimits())) goto INIT_ERROR;
    if (!(initEnvFilter())) goto INIT_ERROR;
    if (!(initFragComm())) goto INIT_ERROR;

    /* we want to have periodic updates on used resources */
    if (!PSIDnodes_acctPollI(PSC_getMyID())) {
	PSIDnodes_setAcctPollI(PSC_getMyID(), 30);
    }

    /* set collect mode in psaccount */
    psAccountSetGlobalCollect(true);

    psPelogueAddPluginConfig("psslurm", &Config);

    /* make sure timer facility is ready */
    if (!Timer_isInitialized()) {
	mdbg(PSSLURM_LOG_WARN, "timer facility not ready, trying to initialize"
	    " it\n");
	Timer_init(NULL);
    }

    /* save default account poll time */
    if ((confAccPollTime = PSIDnodes_acctPollI(PSC_getMyID())) < 0) {
	confAccPollTime = 30;
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

    /* resolve controller IDs */
    if (!(getControllerIDs())) goto INIT_ERROR;

    /* listening on slurmd port */
    ctlPort = getConfValueI(&SlurmConfig, "SlurmdPort");
    if (ctlPort < 0) ctlPort = PSSLURM_SLURMD_PORT;
    if ((openSlurmdSocket(ctlPort)) < 0) goto INIT_ERROR;

    /* register to slurmctld */
    ctlPort = getConfValueI(&SlurmConfig, "SlurmctldPort");
    if (ctlPort < 0) {
	snprintf(buf, sizeof(buf), "%u", PSSLURM_SLURMCTLD_PORT);
	addConfigEntry(&SlurmConfig, "SlurmctldPort", buf);
    }
    sendNodeRegStatus(SLURM_SUCCESS, SLURM_CUR_PROTOCOL_VERSION);

    isInit = 1;

    mlog("(%i) successfully started, protocol '%s'\n", version,
	    SLURM_CUR_PROTOCOL_VERSION_STR);

    return 0;

INIT_ERROR:
    psPelogueDelPluginConfig("psslurm");
    unregisterHooks(0);
    finalizeFragComm();  /* needed for unregister hooks */
    return 1;
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
	return;
    }

    /* all jobs are gone */
    PSIDplugin_unload("psslurm");
}

void finalize(void)
{
    /* TODO: kill all jobs */
    shutdownJobs();
}

void cleanup(void)
{
    if (!isInit) return;

    /* free config in pelogue plugin */
    psPelogueDelPluginConfig("psslurm");

    /* unregister timer */
    if (cleanupTimerID != -1) Timer_remove(cleanupTimerID);

    /* remove all registered hooks and msg handler */
    unregisterHooks(1);

    /* reset collect mode in psaccount */
    if (psAccountSetGlobalCollect) psAccountSetGlobalCollect(false);

    /* free all malloced memory */
    clearJobList();
    clearGresConf();
    clearConnections();
    clearSlurmdProto();
    freeConfig(&Config);
    freeConfig(&SlurmConfig);
    freeConfig(&SlurmGresConfig);
    freeEnvFilter();
    finalizeFragComm();

    mlog("...Bye.\n");
}
