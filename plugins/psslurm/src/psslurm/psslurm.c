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

#include "peloguehandles.h"
#include "psaccounthandles.h"
#include "psexechandles.h"
#include "psmungehandles.h"
#include "pspamhandles.h"
#include "pspmihandles.h"
#include "pspmixhandles.h"
#include "jailhandles.h"

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

/** psid plugin requirements */
char name[] = "psslurm";
int version = 118;
int requiredAPI =143;
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

/** Flag hook de/registration */
static bool hooksRegistered = false;

/**
 * @brief Unregister all hooks
 *
 * @param verbose If set to true an error message will be displayed
 * when unregistering a hook or a message handle fails.
 */
static void unregisterHooks(bool verbose)
{
    if (!hooksRegistered) return;

    if (!PSIDhook_del(PSIDHOOK_EXEC_CLIENT, handleExecClient)) {
	if (verbose) mlog("unregister 'PSIDHOOK_EXEC_CLIENT' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_EXEC_CLIENT_USER, handleExecClientUser)) {
	if (verbose) mlog("unregister 'PSIDHOOK_EXEC_CLIENT_USER' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_EXEC_FORWARDER, handleHookExecFW)) {
	if (verbose) mlog("unregister 'PSIDHOOK_EXEC_FORWARDER' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_EXEC_CLIENT_PREP, handleExecClientPrep)) {
	if (verbose) mlog("unregister 'PSIDHOOK_EXEC_CLIENT_PREP' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_EXEC_CLIENT_EXEC, handleExecClientExec)) {
	if (verbose) mlog("unregister 'PSIDHOOK_EXEC_CLIENT_EXEC' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_FRWRD_INIT, handleForwarderInit)) {
	if (verbose) mlog("unregister 'PSIDHOOK_FRWRD_INIT' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_PRIV_FRWRD_INIT, handleForwarderInitPriv)) {
	if (verbose) mlog("unregister 'PSIDHOOK_PRIV_FRWRD_INIT' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_FRWRD_CLNT_RLS, handleForwarderClientStatus)){
	if (verbose) mlog("unregister 'PSIDHOOK_FRWRD_CLNT_RLS' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_FRWRD_CLNT_RES, handleFwRes)) {
	if (verbose) mlog("unregister 'PSIDHOOK_FRWRD_CLNT_RES' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_PRIV_FRWRD_CLNT_RES, handleFwResPriv)) {
	if (verbose) mlog("unregister 'PSIDHOOK_PRIV_FRWRD_CLNT_RES' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_LAST_CHILD_GONE, handleLastChildGone)) {
	if (verbose) mlog("unregister 'PSIDHOOK_LAST_CHILD_GONE' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_LAST_RESRELEASED, handleResReleased)) {
	if (verbose) mlog("unregister 'PSIDHOOK_LAST_RESRELEASED' failed\n");
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

    if (!PSIDhook_del(PSIDHOOK_PELOGUE_OE, handlePelogueOE)) {
	if (verbose) mlog("unregister 'PSIDHOOK_PELOGUE_OE' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_PELOGUE_GLOBAL, handlePelogueGlobal)) {
	if (verbose) mlog("unregister 'PSIDHOOK_PELOGUE_GLOBAL' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_PELOGUE_DROP, handlePelogueDrop)) {
	if (verbose) mlog("unregister 'PSIDHOOK_PELOGUE_DROP' failed\n");
    }

    hooksRegistered = false;
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

    hooksRegistered = true;

    if (!PSIDhook_add(PSIDHOOK_EXEC_CLIENT_USER, handleExecClientUser)) {
	mlog("register 'PSIDHOOK_EXEC_CLIENT_USER' failed\n");
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

    if (!PSIDhook_add(PSIDHOOK_EXEC_CLIENT_EXEC, handleExecClientExec)) {
	mlog("register 'PSIDHOOK_EXEC_CLIENT_EXEC' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_FRWRD_INIT, handleForwarderInit)) {
	mlog("register 'PSIDHOOK_FRWRD_INIT' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_PRIV_FRWRD_INIT, handleForwarderInitPriv)) {
	mlog("register 'PSIDHOOK_PRIV_FRWRD_INIT' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_FRWRD_CLNT_RLS, handleForwarderClientStatus)){
	mlog("register 'PSIDHOOK_FRWRD_CLNT_RLS' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_FRWRD_CLNT_RES, handleFwRes)) {
	mlog("register 'PSIDHOOK_FRWRD_CLNT_RES' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_PRIV_FRWRD_CLNT_RES, handleFwResPriv)) {
	mlog("register 'PSIDHOOK_PRIV_FRWRD_CLNT_RES' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_LAST_CHILD_GONE, handleLastChildGone)) {
	mlog("register 'PSIDHOOK_LAST_CHILD_GONE' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_LAST_RESRELEASED, handleResReleased)) {
	mlog("register 'PSIDHOOK_LAST_RESRELEASED' failed\n");
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

    if (!PSIDhook_add(PSIDHOOK_PELOGUE_OE, handlePelogueOE)) {
	mlog("register 'PSIDHOOK_PELOGUE_OE' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_PELOGUE_GLOBAL, handlePelogueGlobal)) {
	mlog("register 'PSIDHOOK_PELOGUE_GLOBAL' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_PELOGUE_DROP, handlePelogueDrop)) {
	mlog("register 'PSIDHOOK_PELOGUE_DROP' failed\n");
	return false;
    }

    return true;
}

static bool regPsAccountHandles(void)
{
    void *pluginHandle = PSIDplugin_getHandle("psaccount");

    if (!pluginHandle) {
	flog("getting psaccount handle failed\n");
	return false;
    }

    psAccountSignalSession = dlsym(pluginHandle, "psAccountSignalSession");
    if (!psAccountSignalSession) {
	flog("loading psAccountSignalSession() failed\n");
	return false;
    }

    psAccountRegisterJob = dlsym(pluginHandle, "psAccountRegisterJob");
    if (!psAccountRegisterJob) {
	flog("loading psAccountRegisterJob() failed\n");
	return false;
    }

    psAccountUnregisterJob = dlsym(pluginHandle, "psAccountUnregisterJob");
    if (!psAccountUnregisterJob) {
	flog("loading psAccountUnregisterJob() failed\n");
	return false;
    }

    psAccountSetGlobalCollect = dlsym(pluginHandle,
				      "psAccountSetGlobalCollect");
    if (!psAccountSetGlobalCollect) {
	flog("loading psAccountSetGlobalCollect() failed\n");
	return false;
    }

    psAccountGetDataByJob = dlsym(pluginHandle, "psAccountGetDataByJob");
    if (!psAccountGetDataByJob) {
	flog("loading psAccountGetDataByJob() failed\n");
	return false;
    }

    psAccountGetDataByLogger = dlsym(pluginHandle, "psAccountGetDataByLogger");
    if (!psAccountGetDataByLogger) {
	flog("loading psAccountGetDataByLogger() failed\n");
	return false;
    }

    psAccountGetPidsByLogger = dlsym(pluginHandle, "psAccountGetPidsByLogger");
    if (!psAccountGetPidsByLogger) {
	flog("loading psAccountGetPidsByLogger() failed\n");
	return false;
    }

    psAccountDelJob = dlsym(pluginHandle, "psAccountDelJob");
    if (!psAccountDelJob) {
	flog("loading psAccountDelJob() failed\n");
	return false;
    }

    psAccountGetLocalInfo = dlsym(pluginHandle, "psAccountGetLocalInfo");
    if (!psAccountGetLocalInfo) {
	flog("loading psAccountGetLocalInfo() failed\n");
	return false;
    }

    psAccountGetPoll = dlsym(pluginHandle, "psAccountGetPoll");
    if (!psAccountGetPoll) {
	flog("loading psAccountGetPoll() failed\n");
	return false;
    }

    psAccountSetPoll = dlsym(pluginHandle, "psAccountSetPoll");
    if (!psAccountSetPoll) {
	flog("loading psAccountSetPoll() failed\n");
	return false;
    }

    psAccountCtlScript = dlsym(pluginHandle, "psAccountCtlScript");
    if (!psAccountCtlScript) {
	flog("loading psAccountCtlScript() failed\n");
	return false;
    }

    psAccountScriptEnv = dlsym(pluginHandle, "psAccountScriptEnv");
    if (!psAccountScriptEnv) {
	flog("loading psAccountScriptEnv() failed\n");
	return false;
    }

    return true;
}

static bool regPElogueHandles(void)
{
    void *pluginHandle = PSIDplugin_getHandle("pelogue");

    if (!pluginHandle) {
	flog("getting pelogue handle failed\n");
	return false;
    }

    psPelogueAddPluginConfig = dlsym(pluginHandle, "psPelogueAddPluginConfig");
    if (!psPelogueAddPluginConfig) {
	flog("loading psPelogueAddPluginConfig() failed\n");
	return false;
    }

    psPelogueDelPluginConfig = dlsym(pluginHandle, "psPelogueDelPluginConfig");
    if (!psPelogueDelPluginConfig) {
	flog("loading psPelogueDelPluginConfig() failed\n");
	return false;
    }

    psPelogueAddJob = dlsym(pluginHandle, "psPelogueAddJob");
    if (!psPelogueAddJob) {
	flog("loading psPelogueAddJob() failed\n");
	return false;
    }

    psPelogueDeleteJob = dlsym(pluginHandle, "psPelogueDeleteJob");
    if (!psPelogueDeleteJob) {
	flog("loading psPelogueDeleteJob() failed\n");
	return false;
    }

    psPelogueStartPE = dlsym(pluginHandle, "psPelogueStartPE");
    if (!psPelogueStartPE) {
	flog("loading psPelogueStartPE() failed\n");
	return false;
    }

    psPelogueSignalPE = dlsym(pluginHandle, "psPelogueSignalPE");
    if (!psPelogueSignalPE) {
	flog("loading psPelogueSignalPE() failed\n");
	return false;
    }

    psPelogueCallPE = dlsym(pluginHandle, "psPelogueCallPE");
    if (!psPelogueCallPE) {
	flog("loading psPelogueCallPE() failed\n");
	return false;
    }

    return true;
}

static bool regMungeHandles(void)
{
    void *pluginHandle = PSIDplugin_getHandle("psmunge");

    /* get pelogue function handles */
    if (!pluginHandle) {
	flog("getting psmunge handle failed\n");
	return false;
    }

    psMungeEncode = dlsym(pluginHandle, "psMungeEncode");
    if (!psMungeEncode) {
	flog("loading psMungeEncode() failed\n");
	return false;
    }

    psMungeEncodeRes = dlsym(pluginHandle, "psMungeEncodeRes");
    if (!psMungeEncodeRes) {
	flog("loading psMungeEncodeRes() failed\n");
	return false;
    }

    psMungeDecode = dlsym(pluginHandle, "psMungeDecode");
    if (!psMungeDecode) {
	flog("loading psMungeDecode() failed\n");
	return false;
    }

    psMungeDecodeBuf = dlsym(pluginHandle, "psMungeDecodeBuf");
    if (!psMungeDecodeBuf) {
	flog("loading psMungeDecodeBuf() failed\n");
	return false;
    }

    psMungeMeasure = dlsym(pluginHandle, "psMungeMeasure");
    if (!psMungeMeasure) {
	flog("loading psMungeMeasure() failed\n");
	return false;
    }

    return true;
}

static bool regPsPAMHandles(void)
{
    void *pluginHandle = PSIDplugin_getHandle("pspam");

    if (!pluginHandle) {
	flog("getting pspam handle failed\n");
	return false;
    }

    psPamAddUser = dlsym(pluginHandle, "psPamAddUser");
    if (!psPamAddUser) {
	flog("loading psPamAddUser() failed\n");
	return false;
    }

    psPamDeleteUser = dlsym(pluginHandle, "psPamDeleteUser");
    if (!psPamDeleteUser) {
	flog("loading psPamDeleteUser() failed\n");
	return false;
    }

    psPamSetState = dlsym(pluginHandle, "psPamSetState");
    if (!psPamSetState) {
	flog("loading psPamSetState() failed\n");
	return false;
    }

    return true;
}

static bool regPsExecHandles(void)
{
    void *pluginHandle = PSIDplugin_getHandle("psexec");

    if (!pluginHandle) {
	flog("getting psexec handle failed\n");
	return false;
    }

    psExecStartScript = dlsym(pluginHandle, "psExecStartScript");
    if (!psExecStartScript) {
	flog("loading psExecStartScript() failed\n");
	return false;
    }

    psExecSendScriptStart = dlsym(pluginHandle, "psExecSendScriptStart");
    if (!psExecSendScriptStart) {
	flog("loading psExecSendScriptStart() failed\n");
	return false;
    }

    psExecStartLocalScript = dlsym(pluginHandle, "psExecStartLocalScript");
    if (!psExecStartLocalScript) {
	flog("loading psExecStartLocalScript() failed\n");
	return false;
    }

    return true;
}

static bool regPsPMIHandles(void)
{
    void *pluginHandle = PSIDplugin_getHandle("pspmi");

    if (!pluginHandle) {
	flog("getting pspmi handle failed\n");
	return false;
    }

    psPmiSetFillSpawnTaskFunction =
	dlsym(pluginHandle, "psPmiSetFillSpawnTaskFunction");
    if (!psPmiSetFillSpawnTaskFunction) {
	flog("loading psPmiSetFillSpawnTaskFunction() failed\n");
	return false;
    }

    psPmiResetFillSpawnTaskFunction =
	dlsym(pluginHandle, "psPmiResetFillSpawnTaskFunction");
    if (!psPmiResetFillSpawnTaskFunction) {
	flog("loading psPmiResetFillSpawnTaskFunction() failed\n");
	return false;
    }

    return true;
}

static bool regPsPMIxHandles(void)
{
    void *pluginHandle = PSIDplugin_getHandle("pspmix");

    if (!pluginHandle) {
	flog("getting pspmix handle failed\n");
	return false;
    }

    psPmixSetFillSpawnTaskFunction =
	dlsym(pluginHandle, "psPmixSetFillSpawnTaskFunction");
    if (!psPmixSetFillSpawnTaskFunction) {
	flog("loading psPmixSetFillSpawnTaskFunction() failed\n");
	return false;
    }

    psPmixResetFillSpawnTaskFunction =
	dlsym(pluginHandle, "psPmixResetFillSpawnTaskFunction");
    if (!psPmixResetFillSpawnTaskFunction) {
	flog("loading psPmixResetFillSpawnTaskFunction() failed\n");
	return false;
    }

    return true;
}

static bool regJailHandles(void)
{
    void *pluginHandle = PSIDplugin_getHandle("jail");

    jailGetScripts = dlsym(pluginHandle, "jailGetScripts");
    if (!jailGetScripts) {
	flog("loading jailGetScripts() failed\n");
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

    /* get pspmix function handles */
    if (!regPsPMIxHandles()) return false;

    /* get jail function handles */
    if (!regJailHandles()) return false;

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

    isInit = true;

    mlog("(%i) successfully started, protocol '%s (%i)'\n", version,
	 slurmProtoStr, slurmProto);

    return true;
}

/** Track basic initalization of configurations */
static bool haveBasicConfig = false;

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

    psPelogueAddPluginConfig("psslurm", Config);

    /* make sure timer facility is ready */
    if (!Timer_isInitialized()) {
	mdbg(PSSLURM_LOG_WARN, "timer facility not ready, trying to initialize"
	    " it\n");
	Timer_init(NULL);
    }

    if (confRes == CONFIG_SERVER) {
	char *confCache = getConfValueC(Config, "SLURM_CONF_CACHE");
	if (needConfUpdate(confCache)) {
	    /* wait for config response from slurmctld */
	    if (!requestConfig()) goto INIT_ERROR;
	    return 0;
	}

	/* update configuration directory */
	addConfigEntry(Config, "SLURM_CONFIG_DIR", confCache);

	/* parse configuration files */
	if (!parseSlurmConfigFiles()) goto INIT_ERROR;
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
    pluginShutdown = true;

    bool stopHC = stopHealthCheck(SIGTERM);

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

    if (SpankIsInitialized()) SpankCallHook(&spank);
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
    if (psPelogueDelPluginConfig) psPelogueDelPluginConfig("psslurm");

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

    /* free all malloced memory */
    Job_destroyAll();
    Step_destroyAll();
    clearGresConf();
    clearSlurmdProto();
    clearMsgBuf();
    BCast_clearList();
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
