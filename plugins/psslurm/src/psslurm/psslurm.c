/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#define _GNU_SOURCE
#include "psslurm.h"

#include <fenv.h>

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "pscommon.h"
#include "timer.h"

#include "plugin.h"
#include "pluginconfig.h"
#include "pluginlog.h"

#include "psidhook.h"
#include "psidplugin.h"
#include "psidutil.h"

#include "jailhandles.h"
#include "pamservice_handles.h"
#include "peloguehandles.h"
#include "psaccounthandles.h"
#include "psexechandles.h"
#include "psmungehandles.h"
#include "pspamhandles.h"
#include "pspmihandles.h"
#include "pspmixhandles.h"

#include "psslurmalloc.h"
#include "psslurmauth.h"
#include "psslurmbcast.h"
#include "psslurmcomm.h"
#include "psslurmconfig.h"
#include "psslurmenv.h"
#include "psslurmforwarder.h"
#include "psslurmgres.h"
#include "psslurmjob.h"
#include "psslurmlimits.h"
#include "psslurmlog.h"
#include "psslurmmsg.h"
#include "psslurmpelogue.h"
#include "psslurmpin.h"
#include "psslurmproto.h"
#include "psslurmpscomm.h"
#ifdef HAVE_SPANK
#include "psslurmspank.h"
#endif
#include "psslurmstep.h"
#include "psslurmaccount.h"

#include "plugincpufreq.h"

#define PSSLURM_CONFIG_FILE  PLUGINDIR "/psslurm.conf"
#define MEMORY_DEBUG 0

/** the job cleanup timer */
static int cleanupTimerID = -1;

/** the maximal number of seconds to wait for all jobs to exit. */
static int obitTime = 5;

bool isInit = false;

uid_t slurmUserID = 495;

time_t start_time;

bool pluginShutdown = false;

int oldExceptions = -1;

Init_Flags_t initFlags;

/** psid plugin requirements */
char name[] = "psslurm";
int version = 118;
int requiredAPI =146;
plugin_dep_t dependencies[] = {
    { .name = "psmunge", .version = 5 },
    { .name = "psaccount", .version = 30 },
    { .name = "pelogue", .version = 10 },
    { .name = "pspam", .version = 3 },
    { .name = "psexec", .version = 2 },
    { .name = "pspmi", .version = 4 },
    { .name = "pspmix", .version = 3 },
    { .name = "nodeinfo", .version = 1 },
    { .name = "jail", .version = 3 },
    { .name = NULL, .version = 0 } };

static void cleanupJobs(void)
{
    static int obitTimeCounter = 0;
    int jcount = Job_count() + Alloc_count();
    bool stopHC = stopHealthCheck(SIGTERM);

    /* check if we are waiting for jobs to exit */
    obitTimeCounter++;

    if (!jcount && stopHC) {
	/* all jobs and allocs are gone */
	Timer_remove(cleanupTimerID);
	cleanupTimerID = -1;

	closeSlurmdSocket();
	PSIDplugin_unload(name);

	return;
    }

    if (obitTimeCounter >= obitTime && jcount) {
	mlog("sending SIGKILL to %i remaining jobs\n", jcount);
	Job_signalAll(SIGKILL);
    }
    if (obitTimeCounter >= obitTime && !stopHC) {
	stopHealthCheck(SIGKILL);
    }
}

static bool regPAMServiceHandles(bool verbose);
static void dropPAMServiceHandles(void);

static int handlePluginLoaded(void *data)
{
    char *name = data;
    if (name && !strcmp(name, "pam_service")) regPAMServiceHandles(true);

    return 0;
}

static int handlePluginFinalize(void *data)
{
    char *name = data;
    if (name && !strcmp(name, "pam_service")) dropPAMServiceHandles();

    return 0;
}

/** Flag hook de/registration */
static bool hooksRegistered = false;

#define relHook(hookName, hookFunc)			\
    if (!PSIDhook_del(hookName, hookFunc) && verbose)	\
	mlog("unregister '" #hookName "' failed\n");

/**
 * @brief Unregister all hooks
 *
 * @param verbose If set to true an error message will be displayed
 * when unregistering a hook or a message handle fails.
 */
static void unregisterHooks(bool verbose)
{
    if (!hooksRegistered) return;

    relHook(PSIDHOOK_EXEC_CLIENT, handleExecClient);
    relHook(PSIDHOOK_EXEC_CLIENT_USER, handleExecClientUser);
    relHook(PSIDHOOK_EXEC_FORWARDER, handleHookExecFW);
    relHook(PSIDHOOK_EXEC_CLIENT_PREP, handleExecClientPrep);
    relHook(PSIDHOOK_EXEC_CLIENT_EXEC, handleExecClientExec);
    relHook(PSIDHOOK_FRWRD_INIT, handleForwarderInit);
    relHook(PSIDHOOK_PRIV_FRWRD_INIT, handleForwarderInitPriv);
    relHook(PSIDHOOK_FRWRD_CLNT_RLS, handleForwarderClientStatus);
    relHook(PSIDHOOK_FRWRD_CLNT_RES, handleFwRes);
    relHook(PSIDHOOK_PRIV_FRWRD_CLNT_RES, handleFwResPriv);
    relHook(PSIDHOOK_LAST_CHILD_GONE, handleLastChildGone);
    relHook(PSIDHOOK_LAST_RESRELEASED, handleResReleased);
    relHook(PSIDHOOK_PELOGUE_START, handleLocalPElogueStart);
    relHook(PSIDHOOK_PELOGUE_FINISH, handleLocalPElogueFinish);
    relHook(PSIDHOOK_PELOGUE_PREPARE, handlePEloguePrepare);
    relHook(PSIDHOOK_PELOGUE_OE, handlePelogueOE);
    relHook(PSIDHOOK_PELOGUE_GLOBAL, handlePelogueGlobal);
    relHook(PSIDHOOK_PELOGUE_DROP, handlePelogueDrop);
    relHook(PSIDHOOK_PLUGIN_LOADED, handlePluginLoaded);
    relHook(PSIDHOOK_PLUGIN_FINALIZE, handlePluginFinalize);

    hooksRegistered = false;
}

#define addHook(hookName, hookFunc)			\
    if (!PSIDhook_add(hookName, hookFunc)) {		\
	mlog("register '" #hookName "' failed\n");	\
	return false;					\
    }

/**
 * @brief Register various hooks
 */
static bool registerHooks(void)
{
    addHook(PSIDHOOK_EXEC_CLIENT, handleExecClient);

    hooksRegistered = true;

    addHook(PSIDHOOK_EXEC_CLIENT_USER, handleExecClientUser);
    addHook(PSIDHOOK_EXEC_FORWARDER, handleHookExecFW);
    addHook(PSIDHOOK_EXEC_CLIENT_PREP, handleExecClientPrep);
    addHook(PSIDHOOK_EXEC_CLIENT_EXEC, handleExecClientExec);
    addHook(PSIDHOOK_FRWRD_INIT, handleForwarderInit);
    addHook(PSIDHOOK_PRIV_FRWRD_INIT, handleForwarderInitPriv);
    addHook(PSIDHOOK_FRWRD_CLNT_RLS, handleForwarderClientStatus);
    addHook(PSIDHOOK_FRWRD_CLNT_RES, handleFwRes);
    addHook(PSIDHOOK_PRIV_FRWRD_CLNT_RES, handleFwResPriv);
    addHook(PSIDHOOK_LAST_CHILD_GONE, handleLastChildGone);
    addHook(PSIDHOOK_LAST_RESRELEASED, handleResReleased);
    addHook(PSIDHOOK_PELOGUE_START, handleLocalPElogueStart);
    addHook(PSIDHOOK_PELOGUE_FINISH, handleLocalPElogueFinish);
    addHook(PSIDHOOK_PELOGUE_PREPARE, handlePEloguePrepare);
    addHook(PSIDHOOK_PELOGUE_OE, handlePelogueOE);
    addHook(PSIDHOOK_PELOGUE_GLOBAL, handlePelogueGlobal);
    addHook(PSIDHOOK_PELOGUE_DROP, handlePelogueDrop);
    addHook(PSIDHOOK_PLUGIN_LOADED, handlePluginLoaded);
    addHook(PSIDHOOK_PLUGIN_FINALIZE, handlePluginFinalize);

    return true;
}

#define loadHandle(pluginHandle, handleName)			\
    handleName = dlsym(pluginHandle, #handleName);		\
    if (!handleName) {						\
	flog("loading " #handleName "() failed\n");		\
	return false;						\
    }

static bool regPsAccountHandles(void)
{
    void *pluginHandle = PSIDplugin_getHandle("psaccount");
    if (!pluginHandle) {
	flog("getting psaccount handle failed\n");
	return false;
    }

    loadHandle(pluginHandle, psAccountSignalSession);
    loadHandle(pluginHandle, psAccountRegisterJob);
    loadHandle(pluginHandle, psAccountUnregisterJob);
    loadHandle(pluginHandle, psAccountSetGlobalCollect);
    loadHandle(pluginHandle, psAccountGetDataByJob);
    loadHandle(pluginHandle, psAccountGetDataByLogger);
    loadHandle(pluginHandle, psAccountGetPidsByLogger);
    loadHandle(pluginHandle, psAccountDelJob);
    loadHandle(pluginHandle, psAccountGetLocalInfo);
    loadHandle(pluginHandle, psAccountGetPoll);
    loadHandle(pluginHandle, psAccountSetPoll);
    loadHandle(pluginHandle, psAccountCtlScript);
    loadHandle(pluginHandle, psAccountScriptEnv);

    return true;
}

static bool regPElogueHandles(void)
{
    void *pluginHandle = PSIDplugin_getHandle("pelogue");
    if (!pluginHandle) {
	flog("getting pelogue handle failed\n");
	return false;
    }

    loadHandle(pluginHandle, psPelogueAddPluginConfig);
    loadHandle(pluginHandle, psPelogueDelPluginConfig);
    loadHandle(pluginHandle, psPelogueAddJob);
    loadHandle(pluginHandle, psPelogueDeleteJob);
    loadHandle(pluginHandle, psPelogueStartPE);
    loadHandle(pluginHandle, psPelogueSignalPE);
    loadHandle(pluginHandle, psPelogueCallPE);

    return true;
}

static bool regMungeHandles(void)
{
    void *pluginHandle = PSIDplugin_getHandle("psmunge");
    if (!pluginHandle) {
	flog("getting psmunge handle failed\n");
	return false;
    }

    loadHandle(pluginHandle, psMungeEncode);
    loadHandle(pluginHandle, psMungeEncodeRes);
    loadHandle(pluginHandle, psMungeDecode);
    loadHandle(pluginHandle, psMungeDecodeBuf);
    loadHandle(pluginHandle, psMungeMeasure);

    return true;
}

static bool regPsPAMHandles(void)
{
    void *pluginHandle = PSIDplugin_getHandle("pspam");

    if (!pluginHandle) {
	flog("getting pspam handle failed\n");
	return false;
    }

    loadHandle(pluginHandle, psPamAddUser);
    loadHandle(pluginHandle, psPamDeleteUser);
    loadHandle(pluginHandle, psPamSetState);

    return true;
}

static bool regPsExecHandles(void)
{
    void *pluginHandle = PSIDplugin_getHandle("psexec");
    if (!pluginHandle) {
	flog("getting psexec handle failed\n");
	return false;
    }

    loadHandle(pluginHandle, psExecStartScript);
    loadHandle(pluginHandle, psExecSendScriptStart);
    loadHandle(pluginHandle, psExecStartLocalScript);

    return true;
}

static bool regPsPMIHandles(void)
{
    void *pluginHandle = PSIDplugin_getHandle("pspmi");
    if (!pluginHandle) {
	flog("getting pspmi handle failed\n");
	return false;
    }

    loadHandle(pluginHandle, psPmiSetFillSpawnTaskFunction);
    loadHandle(pluginHandle, psPmiResetFillSpawnTaskFunction);

    return true;
}

static bool regPsPMIxHandles(void)
{
    void *pluginHandle = PSIDplugin_getHandle("pspmix");
    if (!pluginHandle) {
	flog("getting pspmix handle failed\n");
	return false;
    }

    loadHandle(pluginHandle, psPmixSetFillSpawnTaskFunction);
    loadHandle(pluginHandle, psPmixResetFillSpawnTaskFunction);

    return true;
}

static bool regJailHandles(void)
{
    void *pluginHandle = PSIDplugin_getHandle("jail");
    if (!pluginHandle) {
	flog("getting jail handle failed\n");
	return false;
    }

    loadHandle(pluginHandle, jailGetScripts);

    return true;
}

static bool regPAMServiceHandles(bool verbose)
{
    void *pluginHandle = PSIDplugin_getHandle("pam_service");
    if (!pluginHandle) {
	if (verbose) flog("getting pam_service handle failed\n");
	return false;
    }

    loadHandle(pluginHandle, pamserviceStartService);
    loadHandle(pluginHandle, pamserviceOpenSession);
    loadHandle(pluginHandle, pamserviceStopService);

    return true;
}

static void dropPAMServiceHandles(void)
{
    pamserviceStartService = NULL;
    pamserviceOpenSession = NULL;
    pamserviceStopService = NULL;
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

    /* get pspmix function handles */
    if (!regPsPMIxHandles()) return false;

    /* get jail function handles */
    if (!regJailHandles()) return false;

    /* get pam_service function handles; this might fail since it is optional */
    regPAMServiceHandles(false);

    return true;
}

static char *oldPElogues[] = { "prologue", "prologue.parallel", "epilogue",
    "epilogue.parallel", "epilogue.finalize", NULL };

static bool assertNoOldPElogues(char *dir)
{
    if (!dir) return false;
    fdbg(PSSLURM_LOG_PELOG, "checking directory '%s'\n", dir);

    for (char **f = oldPElogues; *f; f++) {
	char *fName = PSC_concat(dir, "/", *f);
	fdbg(PSSLURM_LOG_PELOG, "checking file '%s'\n", fName);
	struct stat sbuf;
	int ret = stat(fName, &sbuf);
	int eno = errno;
	if (ret == -1 && eno == ENOENT) {
	    /* check for dangling symlink */
	    ret = lstat(fName, &sbuf);
	    eno = errno;
	    free(fName);
	    if (ret == -1 && eno == ENOENT) continue;

	    flog("dangling symlink %s in %s\n", *f, dir);
	    return false;
	}
	free(fName);
	if (!ret) {
	    flog("found %s in %s\n", *f, dir);
	} else {
	    fwarn(eno, "%s/%s", dir, *f);
	}
	return false;
    }
    return true;
}

static void setConfOpt(void)
{
    /* plugin library debug */
    int mask = getConfValueI(Config, "PLUGIN_DEBUG_MASK");
    if (mask) {
	flog("set plugin debug mask '0x%x'\n", mask);
	maskPluginLogger(mask);
    }

    /* glib malloc checking */
    int mCheck = getConfValueI(Config, "MALLOC_CHECK");
    if (mCheck) {
	flog("enable memory checking\n");
	setenv("MALLOC_CHECK_", "2", 1);
    }

    /* measure libmunge */
    int measure = getConfValueI(Config, "MEASURE_MUNGE");
    if (measure) {
	flog("measure libmunge executing times\n");
	psMungeMeasure(true);
    }

    /* measure RPC calls */
    measure = getConfValueI(Config, "MEASURE_RPC");
    if (measure) {
	flog("measure Slurm RPC calls\n");
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
    if (!getConfValueI(Config, "ENABLE_FPE_EXCEPTION")) return;

    int ret = feenableexcept(FE_DIVBYZERO | FE_INVALID
			     | FE_OVERFLOW | FE_UNDERFLOW);
    if (ret == -1) {
	flog("feenableexcept() failed\n");
    } else {
	oldExceptions = ret;
	flog("old exception mask: %s %s %s %s\n",
	     (ret & FE_DIVBYZERO) ? "FE_DIVBYZERO :" : "",
	     (ret & FE_INVALID) ? "FE_INVALID :" : "",
	     (ret & FE_OVERFLOW) ? "FE_OVERFLOW :" : "",
	     (ret & FE_UNDERFLOW) ? "FE_UNDERFLOW" : "");
    }
}

/**
 * @brief Test if psslurm should request the Slurm configuration from slurmcltd
 *
 * If SLURM_UPDATE_CONF_AT_STARTUP is set than the configuration will be fetched
 * on every start of psslurm. Otherwise the configuration will only be fetched
 * if the local configuration cache is empty.
 *
 * @param confDir The path to the configuration cache
 */
static bool needConfUpdate(char *confDir)
{
    int forceUpdate = getConfValueI(Config, "SLURM_UPDATE_CONF_AT_STARTUP");
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
 * @brief Request Slurm configuration files from slurmcltd
 *
 * Request the Slurm configuration form slurmcltd by sending a
 * CONFIG_REQUEST_SLURMD RPC.
 */
static bool requestConfig(void)
{
    /* request Slurm configuration files from slurmctld */
    char *server = getConfValueC(Config, "SLURM_CONF_SERVER");
    if (!server || !strcmp(server, "none")) {
	flog("SLURM_CONF_SERVER not set\n");
	return false;
    }

    if (!sendConfigReq(server, CONF_ACT_STARTUP)) {
	server = getConfValueC(Config, "SLURM_CONF_BACKUP_SERVER");
	if (server && strcmp(server, "none")) {
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
    flog("waiting for Slurm configuration from %s proto %s (%i)\n", server,
	 slurmProtoStr, slurmProto);

    return true;
}

bool initSlurmOpt(void)
{
    if (!initPScomm()) goto INIT_ERROR;
    if (!initLimits()) goto INIT_ERROR;

    /* save the Slurm user ID */
    char *slurmUser = getConfValueC(SlurmConfig, "SlurmUser");
    if (slurmUser) {
	uid_t uid = PSC_uidFromString(slurmUser);
	if ((int) uid < 0) {
	    mwarn(errno, "%s: getting user ID of Slurm user '%s' failed: ",
		    __func__, slurmUser);
	    goto INIT_ERROR;
	}
	slurmUserID = uid;
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
	.hook = SPANK_SLURMD_INIT,
	.envSet = NULL,
	.envUnset = NULL
    };
    if (SpankCallHook(&spank) < 0) {
	flog("hook SPANK_INIT failed\n");
	return false;
    }
#endif

    /* initialize Slurm communication */
    if (!initSlurmCon()) goto INIT_ERROR;

    return true;

INIT_ERROR:
    return false;
}

static bool checkForCleanup(Alloc_t *alloc, const void *info)
{
    time_t bound = *(time_t *)info;

    if (alloc->state == A_PROLOGUE_FINISH && alloc->startTime < bound) {
	flog("cleanup stale allocation %d\n", alloc->id);
	Alloc_delete(alloc);
    }
    return false; // continue loop;
}

static void cleanupStaleAllocs(void)
{
    time_t bound = time(0);
    int timeout = getConfValueI(Config, "ALLOC_CLEANUP_TIMEOUT");
    if (timeout <= 0) return;
    bound -= timeout;
    Alloc_traverse(checkForCleanup, &bound);
}

bool accomplishInit(void)
{
    /* initialize pinning defaults */
    if (!initPinning()) return false;

    /* initialize accounting facility */
    if (!Acc_Init()) return false;

    /* start health-check script */
    char *script = getConfValueC(SlurmConfig, "HealthCheckProgram");
    bool run = getConfValueI(Config, "SLURM_HC_STARTUP");
    /* wait till health-check is complete to register to slurmctld */
    if (run && script) {
	flog("running health-check before registering to slurmctld\n");
	return runHealthCheck();
    }

    /* initialize Slurm options and register node to slurmctld */
    if (!initSlurmOpt()) return false;

    /* enable PAM user sessions */
    bool pam = getConfValueI(Config, "PAM_SESSION");
    char *runtime = getConfValueC(SlurmOCIConfig, "RunTimeRun");
    if (runtime && runtime[0] != '\0') {
	flog("oci.conf automatically enables PAM sessions\n");
	pam = true;
    }

    if (pam) {
	flog("enabling PAM session support\n");
	PSIDplugin_t trigger = PSIDplugin_find(name);
	if (!trigger) {
	    flog("failed to get plugin handle\n");
	    return false;
	}
	if (!PSIDplugin_load("pam_service", 1, trigger, NULL)) {
	    flog("failed to load plugin pam_service\n");
	    return false;
	}
    }

    PSID_registerLoopAct(cleanupStaleAllocs);

    isInit = true;

    mlog("(%i) successfully started, protocol '%s (%i)'\n", version,
	 slurmProtoStr, slurmProto);

    return true;
}

/** Track basic initalization of configurations */
static bool haveBasicConfig = false;

static void CPUfreqInitCB(bool result)
{
    flog("initialize CPU frequency facility %s\n",
	 (result ? "succeeded" : "failed"));

    initFlags &= ~INIT_CPU_FREQ;

    if (!initFlags && !accomplishInit()) {
	/* finalize psslurm's startup failed */
	flog("startup of psslurm failed\n");
	PSIDplugin_finalize(name);
    }
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
	flog("psslurm must have root privileges\n");
	return 1;
    }

    /* Must be called before using INIT_ERROR or handling the configuration */
    if (!initPluginHandles()) return 1;

    /* Refuse to start if there are PElogue scripts (prologue, epilogue, ...)
     * in /var/spool/parastation/scripts. Users shall be forced to move these
     * into new `.d` irectories so they cannot miss this change.
     * Also DIR_SCRIPTS in any *.conf will be rejected automatically
     */
    if (!assertNoOldPElogues(SPOOL_DIR "/scripts")) {
	flog("********************************************************\n");
	flog("use new /etc/parastation/<peType>.d/ directories instead\n");
	flog("********************************************************\n");
	return 1;
    }

    /* init the configurations */
    initSlurmConfig(&SlurmConfig); // allow for early cleanup()
    int confRes = initPSSlurmConfig(PSSLURM_CONFIG_FILE);
    haveBasicConfig = true;
    if (confRes == CONFIG_ERROR) {
	flog("init of the configuration failed\n");
	return 1;
    }

    /* set various psslurm config options */
    setConfOpt();

    if (!registerHooks()) goto INIT_ERROR;
    if (!initSlurmdProto()) goto INIT_ERROR;
    if (!Auth_init()) goto INIT_ERROR;

    enableFPEexceptions();

    if (!initEnvFilter()) goto INIT_ERROR;

    /* we use two configs in pelogue, one for psslurm's pelogues, one for the
     * prologue triggered by pspelogue from withing the slurmctld prologue */
    if (!psPelogueAddPluginConfig("psslurm", Config)
	|| !psPelogueAddPluginConfig("pspelogue", Config)) goto INIT_ERROR;

    /* make sure timer facility is ready */
    if (!Timer_isInitialized()) {
	mdbg(PSSLURM_LOG_WARN, "timer facility not ready, trying to initialize"
	    " it\n");
	Timer_init(NULL);
    }

    /* CPU frequency scaling is hardware dependent and might
     * not be available at all */
    CPUfreq_init(getConfValueC(Config, "CPU_SCALING_DIR"), &CPUfreqInitCB);
    initFlags |= INIT_CPU_FREQ;

    if (confRes == CONFIG_SERVER) {
	char *confCache = getConfValueC(Config, "SLURM_CONF_CACHE");
	if (needConfUpdate(confCache)) {
	    initFlags |= INIT_CONFIG_REQ;
	    /* wait for config response from slurmctld */
	    if (!requestConfig()) goto INIT_ERROR;
	    return 0;
	}

	/* update configuration directory */
	addConfigEntry(Config, "SLURM_CONFIG_DIR", confCache);

	/* parse configuration files */
	if (!parseSlurmConfigFiles()) goto INIT_ERROR;
    }

    /* All further initialisation which requires Slurm configuration files *has*
     * to be done in accomplishInit(). It will be called after CPUfreq
     * initialisation and a possible configuration request has finished. */

    return 0;

INIT_ERROR:
    psPelogueDelPluginConfig("psslurm");
    psPelogueDelPluginConfig("pspelogue");
    unregisterHooks(false);
    finalizePScomm(false);
    CPUfreq_finalize();
    return 1;
}

void finalize(void)
{
    pluginShutdown = true;

    bool stopHC = stopHealthCheck(SIGTERM);

    PSID_unregisterLoopAct(cleanupStaleAllocs);

    int jcount = Job_count() + Alloc_count();
    if (jcount || !stopHC) {
	struct timeval cleanupTimer = {1,0};

	mlog("sending SIGTERM to %i remaining jobs\n", jcount);
	Job_signalAll(SIGTERM);

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
	.hook = SPANK_SLURMD_EXIT,
	.envSet = NULL,
	.envUnset = NULL
    };

    if (SpankIsInitialized() && SpankCallHook(&spank) < 0) {
	flog("SPANK_SLURMD_EXIT failed\n");
    }
#endif

    closeSlurmdSocket();
    PSIDplugin_unload(name);
}

void cleanup(void)
{
    if (!haveBasicConfig) return;

    /* close all remaining connections */
    clearSlurmCon();

    /* free config in pelogue plugin */
    if (psPelogueDelPluginConfig) {
	psPelogueDelPluginConfig("psslurm");
	psPelogueDelPluginConfig("pspelogue");
    }

    /* unregister timer */
    if (cleanupTimerID != -1) Timer_remove(cleanupTimerID);

    /* unregister PS messages */
    finalizePScomm(true);

    /* remove all registered hooks and msg handler */
    unregisterHooks(true);

    /* reset collect mode in psaccount */
    Acc_Finalize();

    /* reset FPE exceptions mask */
    if (getConfValueI(Config, "ENABLE_FPE_EXCEPTION") &&
	oldExceptions != -1) {
	if (feenableexcept(oldExceptions) == -1) {
	    flog("warning: failed to reset exception mask\n");
	}
    }

    /* reset all CPU scaling parameters */
    if (CPUfreq_isInitialized()) CPUfreq_resetAll();

    /* free all malloced memory */
    CPUfreq_finalize();
    Job_destroyAll();
    Step_destroyAll();
    clearGresConf();
    clearSlurmdProto();
    clearMsgBufs(-1);
    BCast_clearList();
    Alloc_clearList();
    freeConfig(Config);
    freeConfig(SlurmConfig);
    freeEnvFilter();
    Auth_finalize();
#ifdef HAVE_SPANK
    SpankFinalize();
#endif

    mlog("...Bye.\n");
    finalizeLogger();
}
