/*
 * ParaStation
 *
 * Copyright (C) 2018-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <dlfcn.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#include "logging.h"
#include "plugin.h"
#include "pluginconfig.h"
#include "pspluginprotocol.h"
#include "psidcomm.h"
#include "psidhook.h"
#include "psidplugin.h"

#include "peloguehandles.h"
#include "psaccounthandles.h"
#include "psexechandles.h"

#include "psgwcomm.h"
#include "psgwconfig.h"
#include "psgwlog.h"
#include "psgwpart.h"
#include "psgwrequest.h"
#include "psgwres.h"

#define PSGW_CONFIG_FILE  PLUGINDIR "/psgw.conf"

/** flag set to true if psgw was successfully initialized */
static bool isInit = false;

/** psid plugin requirements */
char name[] = "psgw";
int version = 2;
int requiredAPI = 122;
plugin_dep_t dependencies[] = {
    { .name = "pelogue", .version = 7 },
    { .name = "psexec", .version = 2 },
    { .name = "psaccount", .version = 21 },
    { .name = NULL, .version = 0 } };

/**
 * @brief Verify file permissions of the routing script
 */
static bool checkRouteScript(void)
{
    char rScript[PATH_MAX];
    char *dir = getConfValueC(config, "DIR_ROUTE_SCRIPTS");
    char *script = getConfValueC(config, "ROUTE_SCRIPT");
    struct stat sb;

    snprintf(rScript, sizeof(rScript), "%s/%s", dir, script);

    if (stat(rScript, &sb) < 0) {
	mlog("%s: routing script %s not found\n", __func__, rScript);
	return false;
    }

    int strict = getConfValueI(config, "STRICT_MODE");
    if (strict) {
	/* readable and executable by root and NOT writable by anyone
	 * besides root */
	if (sb.st_uid != 0) {
	    mlog("%s: root must be owner of routing script %s\n",
		 __func__, rScript);
	    return false;
	}
	if (!S_ISREG(sb.st_mode) ||
	    ((sb.st_mode & (S_IRUSR | S_IXUSR)) != (S_IRUSR | S_IXUSR)) ||
	    (sb.st_mode & (S_IWGRP | S_IWOTH))) {
	    mlog("%s: invalid permissions for routing script %s\n",
		 __func__, rScript);
	    return false;
	}
    }

    /* test default routing plugin */
    script = getConfValueC(config, "DEFAULT_ROUTE_PLUGIN");
    if (stat(script, &sb) < 0) {
	mlog("%s: routing plugin %s not found\n", __func__, script);
	return false;
    }

    return true;
}

/**
 * @brief Register pelogue plugin handles
 */
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

/**
 * @brief Register psexec plugin handles
 */
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

    psExecStartScriptEx = dlsym(pluginHandle, "psExecStartScriptEx");
    if (!psExecStartScriptEx) {
	mlog("%s: loading psExecStartScriptEx() failed\n", __func__);
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

/**
 * @brief Register psaccount plugin handles
 */
static bool regPsAccountHandles(void)
{
    void *accHandle = PSIDplugin_getHandle("psaccount");
    if (!accHandle) {
	flog("getting psaccount handle failed\n");
	return false;
    }

    psAccountSignalSession = dlsym(accHandle, "psAccountSignalSession");
    if (!psAccountSignalSession) {
	flog("loading function psAccountSignalSession() failed\n");
	return false;
    }

    return true;
}

static bool initPluginHandles(void)
{
    if (!regPElogueHandles()) {
	flog("register pelogue handles failed\n");
	return false;
    }

    if (!regPsExecHandles()) {
	flog("register psexec handles failed\n");
	return false;
    }

    if (!regPsAccountHandles()) {
	flog("register psaccount handles failed\n");
	return false;
    }

    return true;
}

int initialize(FILE *logfile)
{
    /* init the logger */
    initLogger(name, logfile);

    /* we need to have root privileges */
    if (getuid()) {
	mlog("%s: psgw must have root privileges\n", __func__);
	return 1;
    }

    if (!initPluginHandles()) {
	flog("initialize plugin handles failed\n");
	return 1;
    }

    /* init the configuration */
    if (!initPSGWConfig(PSGW_CONFIG_FILE)) {
	mlog("%s: init of the configuration failed\n", __func__);
	return 1;
    }

    /* psgw debug */
    int32_t mask = getConfValueI(config, "DEBUG_MASK");
    if (mask) {
	mlog("%s: set psgw debug mask '%i'\n", __func__, mask);
	maskLogger(mask);
    }

    /* verify routing script */
    if (!checkRouteScript()) {
	mlog("%s: invalid routing script\n", __func__);
	return 1;
    }

    psPelogueAddPluginConfig("psgw", config);

    if (!PSIDhook_add(PSIDHOOK_PELOGUE_RES, handlePElogueRes)) {
	mlog("register 'PSIDHOOK_PELOGUE_RES' failed\n");
	return 1;
    }

    if (!PSIDhook_add(PSIDHOOK_PSSLURM_FINALLOC, handleFinAlloc)) {
	mlog("register 'PSIDHOOK_PSSLURM_FINALLOC' failed\n");
	return 1;
    }

    if (!PSIDhook_add(PSIDHOOK_PELOGUE_OE, handlePelogueOE)) {
	mlog("register 'PSIDHOOK_PELOGUE_OE' failed\n");
	return false;
    }

    /* register to psgw PSP_PLUG_PSGW message */
    PSID_registerMsg(PSP_PLUG_PSGW, (handlerFunc_t)handlePSGWmsg);

    regPartMsg();

    isInit = true;

    mlog("(%i) successfully started\n", version);
    return 0;
}

void cleanup(void)
{
    if (!isInit) return;

    if (!PSIDhook_del(PSIDHOOK_PELOGUE_RES, handlePElogueRes)) {
	mlog("unregister 'PSIDHOOK_PELOGUE_RES' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_PSSLURM_FINALLOC, handleFinAlloc)) {
	mlog("unregister 'PSIDHOOK_PSSLURM_FINALLOC' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_PELOGUE_OE, handlePelogueOE)) {
	mlog("unregister 'PSIDHOOK_PELOGUE_OE' failed\n");
    }

    /* unregister psgw message */
    PSID_clearMsg(PSP_PLUG_PSGW, (handlerFunc_t)handlePSGWmsg);

    clrPartMsg();

    Request_clear();

    psPelogueDelPluginConfig("psgw");
    freeConfig(config);

    mlog("...Bye.\n");
    finalizeLogger();
}
