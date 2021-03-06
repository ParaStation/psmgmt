/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#define _GNU_SOURCE
#include <fenv.h>

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
#include "psslurmpin.h"
#ifdef HAVE_SPANK
#include "psslurmspank.h"
#endif

#include "pluginmalloc.h"
#include "pluginlog.h"
#include "pluginhelper.h"
#include "pspluginprotocol.h"
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
#define MEMORY_DEBUG 0

/** the job cleanup timer */
static int cleanupTimerID = -1;

/** the maximal number of seconds to wait for all jobs to exit. */
static int obitTime = 5;

bool isInit = false;

int confAccPollTime;

uid_t slurmUserID = 495;

time_t start_time;

bool pluginShutdown = false;

/** hash value of the SLURM config file */
uint32_t configHash;

/** psid plugin requirements */
char name[] = "psslurm";
int version = 117;
int requiredAPI = 132;
plugin_dep_t dependencies[] = {
    { .name = "psmunge", .version = 4 },
    { .name = "psaccount", .version = 29 },
    { .name = "pelogue", .version = 9 },
    { .name = "pspam", .version = 3 },
    { .name = "psexec", .version = 2 },
    { .name = "pspmi", .version = 4 },
    { .name = "nodeinfo", .version = 1 },
    { .name = NULL, .version = 0 } };

static void cleanupJobs(void)
{
    static int obitTimeCounter = 0;
    int jcount = countJobs() + countAllocs();

    /* check if we are waiting for jobs to exit */
    obitTimeCounter++;

    if (!jcount) {
	/* all jobs and allocs are gone */
	Timer_remove(cleanupTimerID);
	cleanupTimerID = -1;

	closeSlurmdSocket();
	PSIDplugin_unload(name);

	return;
    }

    if (obitTimeCounter >= obitTime) {
	mlog("sending SIGKILL to %i remaining jobs\n", jcount);
	signalJobs(SIGKILL);
    }
}

/**
 * @brief Unregister all hooks
 *
 * @param verbose If set to true an error message will be displayed
 * when unregistering a hook or a message handle fails.
 */
static void unregisterHooks(bool verbose)
{
    if (!PSIDhook_del(PSIDHOOK_EXEC_CLIENT, handleExecClient)) {
	if (verbose) mlog("unregister 'PSIDHOOK_EXEC_CLIENT' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_EXEC_CLIENT_USER, handleExecClientUser)) {
	if (verbose) mlog("unregister 'PSIDHOOK_EXEC_CLIENT_USER' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_FRWRD_INIT, handleForwarderInit)) {
	if (verbose) mlog("unregister 'PSIDHOOK_FRWRD_INIT' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_FRWRD_CLNT_RLS, handleForwarderClientStatus)){
	if (verbose) mlog("unregister 'PSIDHOOK_FRWRD_CLNT_RLS' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_PELOGUE_START, handleLocalPElogueStart)) {
	if (verbose) mlog("unregister 'PSIDHOOK_PELOGUE_START' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_PELOGUE_FINISH, handleLocalPElogueFinish)) {
	if (verbose) mlog("unregister 'PSIDHOOK_PELOGUE_FINISH' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_PELOGUE_PREPARE, handlePEloguePrepare)) {
	if (verbose) mlog("unregister 'PSIDHOOK_PELOGUE_PREPARE' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_FRWRD_CLNT_RES, handleFwRes)) {
	if (verbose) mlog("unregister 'PSIDHOOK_FRWRD_CLNT_RES' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_PELOGUE_OE, handlePelogueOE)) {
	if (verbose) mlog("unregister 'PSIDHOOK_PELOGUE_OE' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_PELOGUE_GLOBAL, handlePelogueGlobal)) {
	if (verbose) mlog("unregister 'PSIDHOOK_PELOGUE_GLOBAL' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_EXEC_FORWARDER, handleHookExecFW)) {
	if (verbose) mlog("unregister 'PSIDHOOK_EXEC_FORWARDER' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_EXEC_CLIENT_PREP, handleExecClientPrep)) {
	if (verbose) mlog("unregister 'PSIDHOOK_EXEC_CLIENT_PREP' failed\n");
    }
}

/**
* @brief Register various hooks
*/
static bool registerHooks(void)
{
    if (!PSIDhook_add(PSIDHOOK_EXEC_CLIENT, handleExecClient)) {
	mlog("register 'PSIDHOOK_EXEC_CLIENT' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_EXEC_CLIENT_USER, handleExecClientUser)) {
	mlog("register 'PSIDHOOK_EXEC_CLIENT_USER' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_FRWRD_INIT, handleForwarderInit)) {
	mlog("register 'PSIDHOOK_FRWRD_INIT' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_FRWRD_CLNT_RLS, handleForwarderClientStatus)){
	mlog("register 'PSIDHOOK_FRWRD_CLNT_RLS' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_PELOGUE_START, handleLocalPElogueStart)) {
	mlog("register 'PSIDHOOK_PELOGUE_START' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_PELOGUE_FINISH, handleLocalPElogueFinish)) {
	mlog("register 'PSIDHOOK_PELOGUE_FINISH' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_PELOGUE_PREPARE, handlePEloguePrepare)) {
	mlog("register 'PSIDHOOK_PELOGUE_PREPARE' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_FRWRD_CLNT_RES, handleFwRes)) {
	mlog("register 'PSIDHOOK_FRWRD_CLNT_RES' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_PELOGUE_OE, handlePelogueOE)) {
	mlog("register 'PSIDHOOK_PELOGUE_OE' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_PELOGUE_GLOBAL, handlePelogueGlobal)) {
	mlog("register 'PSIDHOOK_PELOGUE_GLOBAL' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_EXEC_FORWARDER, handleHookExecFW)) {
	mlog("register 'PSIDHOOK_EXEC_FORWARDER' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_EXEC_CLIENT_PREP, handleExecClientPrep)) {
	mlog("register 'PSIDHOOK_EXEC_CLIENT_PREP' failed\n");
	return false;
    }

    return true;
}

static bool regPsAccountHandles(void)
{
    void *pluginHandle = PSIDplugin_getHandle("psaccount");

    if (!pluginHandle) {
	mlog("%s: getting psaccount handle failed\n", __func__);
	return false;
    }

    psAccountSignalSession = dlsym(pluginHandle, "psAccountSignalSession");
    if (!psAccountSignalSession) {
	mlog("%s: loading psAccountSignalSession() failed\n", __func__);
	return false;
    }

    psAccountRegisterJob = dlsym(pluginHandle, "psAccountRegisterJob");
    if (!psAccountRegisterJob) {
	mlog("%s: loading psAccountRegisterJob() failed\n", __func__);
	return false;
    }

    psAccountUnregisterJob = dlsym(pluginHandle, "psAccountUnregisterJob");
    if (!psAccountUnregisterJob) {
	mlog("%s: loading psAccountUnregisterJob() failed\n", __func__);
	return false;
    }

    psAccountSetGlobalCollect = dlsym(pluginHandle,
				      "psAccountSetGlobalCollect");
    if (!psAccountSetGlobalCollect) {
	mlog("%s: loading psAccountSetGlobalCollect() failed\n", __func__);
	return false;
    }

    psAccountGetDataByJob = dlsym(pluginHandle, "psAccountGetDataByJob");
    if (!psAccountGetDataByJob) {
	mlog("%s: loading psAccountGetDataByJob() failed\n", __func__);
	return false;
    }

    psAccountGetDataByLogger = dlsym(pluginHandle, "psAccountGetDataByLogger");
    if (!psAccountGetDataByLogger) {
	mlog("%s: loading psAccountGetDataByLogger() failed\n", __func__);
	return false;
    }

    psAccountGetPidsByLogger = dlsym(pluginHandle, "psAccountGetPidsByLogger");
    if (!psAccountGetPidsByLogger) {
	mlog("%s: loading psAccountGetPidsByLogger() failed\n", __func__);
	return false;
    }

    psAccountDelJob = dlsym(pluginHandle, "psAccountDelJob");
    if (!psAccountDelJob) {
	mlog("%s: loading psAccountDelJob() failed\n", __func__);
	return false;
    }

    psAccountGetEnergy = dlsym(pluginHandle, "psAccountGetEnergy");
    if (!psAccountGetEnergy) {
	mlog("%s: loading psAccountGetEnergy() failed\n", __func__);
	return false;
    }

    psAccountGetPoll = dlsym(pluginHandle, "psAccountGetPoll");
    if (!psAccountGetPoll) {
	mlog("%s: loading psAccountGetPoll() failed\n", __func__);
	return false;
    }

    psAccountSetPoll = dlsym(pluginHandle, "psAccountSetPoll");
    if (!psAccountSetPoll) {
	mlog("%s: loading psAccountSetPoll() failed\n", __func__);
	return false;
    }

    return true;
}

static bool regPElogueHandles(void)
{
    void *pluginHandle = PSIDplugin_getHandle("pelogue");

    if (!pluginHandle) {
	mlog("%s: getting pelogue handle failed\n", __func__);
	return false;
    }

    psPelogueAddPluginConfig = dlsym(pluginHandle, "psPelogueAddPluginConfig");
    if (!psPelogueAddPluginConfig) {
	mlog("%s: loading psPelogueAddPluginConfig() failed\n", __func__);
	return false;
    }

    psPelogueDelPluginConfig = dlsym(pluginHandle, "psPelogueDelPluginConfig");
    if (!psPelogueDelPluginConfig) {
	mlog("%s: loading psPelogueDelPluginConfig() failed\n", __func__);
	return false;
    }

    psPelogueAddJob = dlsym(pluginHandle, "psPelogueAddJob");
    if (!psPelogueAddJob) {
	mlog("%s: loading psPelogueAddJob() failed\n", __func__);
	return false;
    }

    psPelogueDeleteJob = dlsym(pluginHandle, "psPelogueDeleteJob");
    if (!psPelogueDeleteJob) {
	mlog("%s: loading psPelogueDeleteJob() failed\n", __func__);
	return false;
    }

    psPelogueStartPE = dlsym(pluginHandle, "psPelogueStartPE");
    if (!psPelogueStartPE) {
	mlog("%s: loading psPelogueStartPE() failed\n", __func__);
	return false;
    }

    psPelogueSignalPE = dlsym(pluginHandle, "psPelogueSignalPE");
    if (!psPelogueSignalPE) {
	mlog("%s: loading psPelogueSignalPE() failed\n", __func__);
	return false;
    }
    return true;
}

static bool regMungeHandles(void)
{
    void *pluginHandle = PSIDplugin_getHandle("psmunge");

    /* get pelogue function handles */
    if (!pluginHandle) {
	mlog("%s: getting psmunge handle failed\n", __func__);
	return false;
    }

    psMungeEncode = dlsym(pluginHandle, "psMungeEncode");
    if (!psMungeEncode) {
	mlog("%s: loading psMungeEncode() failed\n", __func__);
	return false;
    }

    psMungeDecode = dlsym(pluginHandle, "psMungeDecode");
    if (!psMungeDecode) {
	mlog("%s: loading psMungeDecode() failed\n", __func__);
	return false;
    }

    psMungeDecodeBuf = dlsym(pluginHandle, "psMungeDecodeBuf");
    if (!psMungeDecodeBuf) {
	mlog("%s: loading psMungeDecodeBuf() failed\n", __func__);
	return false;
    }

    psMungeMeasure = dlsym(pluginHandle, "psMungeMeasure");
    if (!psMungeMeasure) {
	mlog("%s: loading psMungeMeasure() failed\n", __func__);
	return false;
    }

    return true;
}

static bool regPsPAMHandles(void)
{
    void *pluginHandle = PSIDplugin_getHandle("pspam");

    if (!pluginHandle) {
	mlog("%s: getting pspam handle failed\n", __func__);
	return false;
    }

    psPamAddUser = dlsym(pluginHandle, "psPamAddUser");
    if (!psPamAddUser) {
	mlog("%s: loading psPamAddUser() failed\n", __func__);
	return false;
    }

    psPamDeleteUser = dlsym(pluginHandle, "psPamDeleteUser");
    if (!psPamDeleteUser) {
	mlog("%s: loading psPamDeleteUser() failed\n", __func__);
	return false;
    }

    psPamSetState = dlsym(pluginHandle, "psPamSetState");
    if (!psPamSetState) {
	mlog("%s: loading psPamSetState() failed\n", __func__);
	return false;
    }

    return true;
}

static bool regPsExecHandles(void)
{
    void *pluginHandle = PSIDplugin_getHandle("psexec");

    if (!pluginHandle) {
	mlog("%s: getting psexec handle failed\n", __func__);
	return false;
    }

    psExecStartScript = dlsym(pluginHandle, "psExecStartScript");
    if (!psExecStartScript) {
	mlog("%s: loading psExecStartScript() failed\n", __func__);
	return false;
    }

    psExecSendScriptStart = dlsym(pluginHandle, "psExecSendScriptStart");
    if (!psExecSendScriptStart) {
	mlog("%s: loading psExecSendScriptStart() failed\n", __func__);
	return false;
    }

    psExecStartLocalScript = dlsym(pluginHandle, "psExecStartLocalScript");
    if (!psExecStartLocalScript) {
	mlog("%s: loading psExecStartLocalScript() failed\n", __func__);
	return false;
    }

    return true;
}

static bool regPsPMIHandles(void)
{
    void *pluginHandle = PSIDplugin_getHandle("pspmi");

    if (!pluginHandle) {
	mlog("%s: getting pspmi handle failed\n", __func__);
	return false;
    }

    psPmiSetFillSpawnTaskFunction =
	dlsym(pluginHandle, "psPmiSetFillSpawnTaskFunction");
    if (!psPmiSetFillSpawnTaskFunction) {
	mlog("%s: loading psPmiSetFillSpawnTaskFunction() failed\n", __func__);
	return false;
    }

    psPmiResetFillSpawnTaskFunction =
	dlsym(pluginHandle, "psPmiResetFillSpawnTaskFunction");
    if (!psPmiResetFillSpawnTaskFunction) {
	mlog("%s: loading psPmiResetFillSpawnTaskFunction() failed\n",__func__);
	return false;
    }

    return true;
}

static bool initPluginHandles(void)
{
    /* get psaccount function handles */
    if (!regPsAccountHandles()) return false;

    /* get pelogue function handles */
    if (!regPElogueHandles()) return false;

    /* get psmunge function handles */
    if (!regMungeHandles()) return false;

    /* get pspam function handles */
    if (!regPsPAMHandles()) return false;

    /* get psexec function handles */
    if (!regPsExecHandles()) return false;

    /* get pspmi function handles */
    if (!regPsPMIHandles()) return false;

    return true;
}

static void setConfOpt(void)
{
    /* plugin library debug */
    int mask = getConfValueI(&Config, "PLUGIN_DEBUG_MASK");
    if (mask) {
	mlog("%s: set plugin debug mask '%i'\n", __func__, mask);
	maskPluginLogger(mask);
    }

    /* glib malloc checking */
    int mCheck = getConfValueI(&Config, "MALLOC_CHECK");
    if (mCheck) {
	mlog("%s: enable memory checking\n", __func__);
	setenv("MALLOC_CHECK_", "2", 1);
    }

    /* measure libmunge */
    int measure = getConfValueI(&Config, "MEASURE_MUNGE");
    if (measure) {
	mlog("%s: measure libmunge executing times\n", __func__);
	psMungeMeasure(true);
    }

    /* measure RPC calls */
    measure = getConfValueI(&Config, "MEASURE_RPC");
    if (measure) {
	mlog("%s: measure Slurm RPC calls\n", __func__);
	measureRPC = true;
    }
}

/**
 * @brief Enable libc FPE exception traps
 *
 * Enable the FE_DIVBYZERO, FE_INVALID, FE_OVERFLOW,
 * FE_UNDERFLOW libc exception traps.
 */
static void enableFPEexceptions(void)
{
    int ret;

    if (!getConfValueI(&Config, "ENABLE_FPE_EXCEPTION")) return;

    ret = feenableexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW | FE_UNDERFLOW);

    if (ret == -1) {
	mlog("%s: feenableexcept() failed\n", __func__);
    } else {
	mlog("%s: old exception mask: %s %s %s %s\n", __func__,
	     (ret & FE_DIVBYZERO) ? "FE_DIVBYZERO :" : "",
	     (ret & FE_INVALID) ? "FE_INVALID :" : "",
	     (ret & FE_OVERFLOW) ? "FE_OVERFLOW :" : "",
	     (ret & FE_UNDERFLOW) ? "FE_UNDERFLOW" : "");
    }
}

/**
 * @doctodo
 */
static bool needConfUpdate(char *confDir)
{
    int forceUpdate = getConfValueI(&Config, "SLURM_UPDATE_CONF_AT_STARTUP");
    if (forceUpdate) return true;

    char buf[1024];
    snprintf(buf, sizeof(buf), "%s/slurm.conf", confDir);

    struct stat sbuf;
    if (stat(buf, &sbuf) != -1) {
	flog("Using available configuration cache\n");
	return false;
    }

    return true;
}

/**
 * @doctodo
 */
static bool requestConfig(void)
{
    /* request Slurm configuration files from slurmctld */
    char *server = getConfValueC(&Config, "SLURM_CONF_SERVER");
    if (!server) {
	flog("SLURM_CONF_SERVER not set\n");
	return false;
    }

    if (!sendConfigReq(server, CONF_ACT_STARTUP)) {
	server = getConfValueC(&Config, "SLURM_CONF_BACKUP_SERVER");
	if (server) {
	    flog("requesting config from backup server %s\n", server);
	    if (!sendConfigReq(server, CONF_ACT_STARTUP)) {
		flog("requesting Slurm configuration from %s failed\n", server);
		return false;
	    }
	} else {
	    flog("requesting Slurm configuration failed\n");
	    return false;
	}
    }
    flog("waiting for Slurm configuration from %s\n", server);

    return true;
}

/**
 * @brief Initialize Slurm options
 *
 * Initialize Slurm options from various configuration files. On
 * success the communication facility is started and the node is
 * registered to the slurmctld. Additionally all spank plugins are
 * initialized and the global spank API is loaded.
 *
 * @return Returns true on success or false otherwise
 */
static bool initSlurmOpt(void)
{
    struct passwd *pw;

    if (!initPScomm()) goto INIT_ERROR;
    if (!initLimits()) goto INIT_ERROR;

    /* save the Slurm user ID */
    char *slurmUser = getConfValueC(&SlurmConfig, "SlurmUser");
    if (slurmUser) {
	if ((pw = getpwnam(slurmUser))) {
	    slurmUserID = pw->pw_uid;
	} else {
	    mwarn(errno, "%s: getting user ID of Slurm user '%s' failed: ",
		    __func__, slurmUser);
	    goto INIT_ERROR;
	}
    }

#ifdef HAVE_SPANK
    /* load global spank symbols */
    if (!SpankInitGlobalSym()) goto INIT_ERROR;

    /* verify all configured spank plugins */
    if (!SpankInitPlugins()) goto INIT_ERROR;

    struct spank_handle spank = {
	.task = NULL,
	.alloc = NULL,
	.job = NULL,
	.step = NULL,
	.hook = SPANK_SLURMD_INIT
    };
    SpankCallHook(&spank);
#endif

    /* initialize Slurm communication */
    if (!initSlurmCon()) goto INIT_ERROR;

    return true;

INIT_ERROR:
    return false;
}

bool finalizeInit(void)
{
    if (!initSlurmOpt()) return false;

    /* initialize pinning defaults */
    if (!initPinning()) return false;

    isInit = true;

    mlog("(%i) successfully started, protocol '%s (%i)'\n", version,
	 slurmProtoStr, slurmProto);

    return true;
}

int initialize(FILE *logfile)
{
    start_time = time(NULL);

    /* init the logger (log to syslog) */
    initLogger("psslurm", logfile);

    if (MEMORY_DEBUG) {
	FILE *lfile = fopen("/tmp/malloc", "w+");
	initPluginLogger(NULL, lfile);
	maskPluginLogger(PLUGIN_LOG_MALLOC);
    } else {
	initPluginLogger("psslurm", logfile);
    }

    /* we need to have root privileges */
    if(getuid() != 0) {
	mlog("%s: psslurm must have root privileges\n", __func__);
	return 1;
    }

    /* init the configuration */
    int confRes = initPSSlurmConfig(PSSLURM_CONFIG_FILE, &configHash);
    if (confRes == CONFIG_ERROR) {
	mlog("%s: init of the configuration failed\n", __func__);
	return 1;
    }

    /* init plugin handles, *has* to be called before using INIT_ERROR */
    if (!initPluginHandles()) return 1;

    /* set various psslurm config options */
    setConfOpt();

    if (!registerHooks()) goto INIT_ERROR;
    if (!(initSlurmdProto())) goto INIT_ERROR;

    enableFPEexceptions();

    if (!initEnvFilter()) goto INIT_ERROR;

    /* we want to have periodic updates on used resources */
    if (!psAccountGetPoll()) psAccountSetPoll(30);

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
    if ((confAccPollTime = psAccountGetPoll()) <= 0) confAccPollTime = 30;

    if (confRes == CONFIG_SERVER) {
	char *confDir = getConfValueC(&Config, "SLURM_CONF_DIR");
	if (needConfUpdate(confDir)) {
	    /* wait for config response from slurmctld */
	    if (!requestConfig()) goto INIT_ERROR;
	    return 0;
	}

	/* update configuration file defaults */
	activateConfigCache(confDir);

	/* parse configuration files */
	if (!parseSlurmConfigFiles(&configHash)) goto INIT_ERROR;
    }

    /* all further initialisation which requires Slurm configuration files *has*
     * to be done in finalizeInit() */
    if (!finalizeInit()) goto INIT_ERROR;

    return 0;

INIT_ERROR:
    psPelogueDelPluginConfig("psslurm");
    unregisterHooks(false);
    finalizePScomm(false);
    return 1;
}

void finalize(void)
{
    int jcount = countJobs() + countAllocs();

    pluginShutdown = true;

    if (jcount) {
	struct timeval cleanupTimer = {1,0};

	mlog("sending SIGTERM to %i remaining jobs\n", jcount);
	signalJobs(SIGTERM);

	/* re-investigate every second */
	cleanupTimerID = Timer_register(&cleanupTimer, cleanupJobs);
	if (cleanupTimerID == -1)  mlog("timer registration failed\n");

	return;
    }

#ifdef HAVE_SPANK
    struct spank_handle spank = {
	.task = NULL,
	.alloc = NULL,
	.job = NULL,
	.step = NULL,
	.hook = SPANK_SLURMD_EXIT
    };
    SpankCallHook(&spank);
#endif

    closeSlurmdSocket();
    PSIDplugin_unload(name);
}

void cleanup(void)
{
    if (!isInit) return;

    /* close all remaining connections */
    clearSlurmCon();

    /* free config in pelogue plugin */
    psPelogueDelPluginConfig("psslurm");

    /* unregister timer */
    if (cleanupTimerID != -1) Timer_remove(cleanupTimerID);

    /* unregister PS messages */
    finalizePScomm(true);

    /* remove all registered hooks and msg handler */
    unregisterHooks(true);

    /* reset collect mode in psaccount */
    if (psAccountSetGlobalCollect) psAccountSetGlobalCollect(false);

    /* free all malloced memory */
    clearJobList();
    clearGresConf();
    clearSlurmdProto();
    clearMsgBuf();
    freeConfig(&Config);
    freeConfig(&SlurmConfig);
    freeEnvFilter();
#ifdef HAVE_SPANK
    SpankFinalize();
#endif

    mlog("...Bye.\n");

    /* release the logger */
    logger_finalize(psslurmlogger);
}
