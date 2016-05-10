/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdbool.h>
#include <limits.h>

#include "plugin.h"
#include "psidhook.h"
#include "psidplugin.h"

#include "pluginmalloc.h"
#include "pluginlog.h"

#include "cgrouplog.h"
#include "cgroupconfig.h"

#define CGROUP_CONFIG "cgroup.conf"

/** psid plugin requirements */
char name[] = "cgroup";
int version = 1;
int requiredAPI = 115;
plugin_dep_t dependencies[] = { { NULL, 0 } };

/** Root of the cgroup hierarchy */
static char *cgroupRoot = NULL;

/** Name of psmgmt's private cgroup */
static char *cgroupName = NULL;

/** Memory limit of psmgmt's memory cgroup */
static long memLim = -1;

/** Memory+Swap limit of psmgmt's memory cgroup */
static long memSwLim = -1;

/** Actual pid-file to jail processes */
static char *pidFile = NULL;

static int jailProcess(void *info)
{
    cglog(-1, "%s: called\n", __func__);

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
    if (!(PSIDhook_del(PSIDHOOK_JAIL_CHILD, jailProcess))) {
	if (verbose) cglog(-1, "removing PSIDHOOK_JAIL_CHILD failed\n");
    }
}

int initialize(void)
{
    int debugMask;
    char configFile[PATH_MAX];

    /* init logging facility */
    initCgLogger(NULL);
    initPluginLogger(NULL);

    /* init the config facility */
    snprintf(configFile, sizeof(configFile), "%s/%s", PLUGINDIR, CGROUP_CONFIG);

    initCgConfig(configFile);

    /* adapt the debug mask */
    debugMask = getConfValueI(&cgroupConfig, "DEBUG_MASK");
    maskCgLogger(debugMask);
    cglog(CG_LOG_VERBOSE, "%s: debugMask set to %#x\n", __func__, debugMask);

    cgroupRoot = ustrdup(getConfValueC(&cgroupConfig, "CGROUP_ROOT"));
    if (!cgroupRoot) {
	cglog(-1, "%s: CGROUP_ROOT not found in '%s'\n", __func__, configFile);
	return 1;
    }
    cglog(CG_LOG_VERBOSE, "%s: cgroupRoot set to '%s'\n", __func__, cgroupRoot);

    cgroupName = ustrdup(getConfValueC(&cgroupConfig, "CGROUP_NAME"));
    if (!cgroupName) {
	cglog(-1, "%s: CGROUP_NAME not found in '%s'\n", __func__, configFile);
	return 1;
    }
    cglog(CG_LOG_VERBOSE, "%s: cgroupName set to '%s'\n", __func__, cgroupName);

    memLim = getConfValueL(&cgroupConfig, "MEM_LIMIT");
    cglog(CG_LOG_VERBOSE, "%s: memLim set to %ld\n", __func__, memLim);

    memSwLim = getConfValueL(&cgroupConfig, "MEMSW_LIMIT");
    cglog(CG_LOG_VERBOSE, "%s: memSwLim set to %ld\n", __func__, memSwLim);

    /* @todo identify memory cgroup */
    /* @todo try to create our cgroup */
    /* @todo enforce the limits */

    if (!(PSIDhook_add(PSIDHOOK_JAIL_CHILD, jailProcess))) {
	cglog(-1, "%s: register PSIDHOOK_JAIL_CHILD failed\n", __func__);
	goto INIT_ERROR;
    }

    cglog(-1, "(%i) successfully started\n", version);

    return 0;

INIT_ERROR:
    unregisterHooks(false);
    return 1;
}

void finalize(void)
{
    /* @todo kill all processes within the cgroup */

    PSIDplugin_unload(name);
}

void cleanup(void)
{
    unregisterHooks(true);
    freeConfig(&cgroupConfig);
    if (cgroupRoot) ufree(cgroupRoot);
    if (cgroupName) ufree(cgroupName);
    if (pidFile) ufree(pidFile);
}

char *help(void)
{
    char *buf = NULL;
    size_t bufSize = 0;
    int maxKeyLen = getMaxKeyLen(cgConfDef);
    int i;

    buf = str2Buf("\nSimple plugin to jail all psid's client processes into"
		  " a single cgroup\n\n", buf, &bufSize);
    buf = str2Buf("\n# configuration options #\n\n", buf, &bufSize);

    for (i = 0; cgConfDef[i].name; i++) {
	char type[10], line[160];
	snprintf(type, sizeof(type), "<%s>", cgConfDef[i].type);
	snprintf(line, sizeof(line), "%*s %10s  %s\n",
		 maxKeyLen, cgConfDef[i].name, type, cgConfDef[i].desc);
	buf = str2Buf(line, buf, &bufSize);
    }

    return buf;
}

char *set(char *key, char *val)
{
    const ConfDef_t *thisConfDef = getConfigDef(key, cgConfDef);

    if (!thisConfDef) return ustrdup("\nUnknown option\n");

    if (verifyConfigEntry(cgConfDef, key, val))
	return ustrdup("\nIllegal value\n");

    if (!strcmp(key, "MEM_LIMIT")) {
	addConfigEntry(&cgroupConfig, key, val);
	memLim = getConfValueL(&cgroupConfig, key);
	cglog(CG_LOG_VERBOSE, "%s: memLim set to %ld\n", __func__, memLim);

	/* @todo set new memLim */
    } else if (!strcmp(key, "MEMSW_LIMIT")) {
	addConfigEntry(&cgroupConfig, key, val);
	memSwLim = getConfValueL(&cgroupConfig, key);
	cglog(CG_LOG_VERBOSE, "%s: memSwLim set to %ld\n", __func__, memSwLim);

	/* @todo set new memSwLim */
    } else if (!strcmp(key, "DEBUG_MASK")) {
	int dbgMask;
	addConfigEntry(&cgroupConfig, key, val);
	dbgMask = getConfValueI(&cgroupConfig, key);
	maskCgLogger(dbgMask);
	cglog(CG_LOG_VERBOSE, "%s: debugMask set to %#x\n", __func__, dbgMask);
    } else {
	return ustrdup("\nPermission denied\n");
    }

    return NULL;
}

char *unset(char *key)
{
    if (!strcmp(key, "MEM_LIMIT")) {
	unsetConfigEntry(&cgroupConfig, cgConfDef, key);
	memLim = getConfValueL(&cgroupConfig, key);
	cglog(CG_LOG_VERBOSE, "%s: memLim set to %ld\n", __func__, memLim);

	/* @todo set new memLim */
    } else if (!strcmp(key, "MEMSW_LIMIT")) {
	unsetConfigEntry(&cgroupConfig, cgConfDef, key);
	memSwLim = getConfValueL(&cgroupConfig, key);
	cglog(CG_LOG_VERBOSE, "%s: memSwLim set to %ld\n", __func__, memSwLim);

	/* @todo set new memSwLim */
    } else if (!strcmp(key, "DEBUG_MASK")) {
	int dbgMask;
	unsetConfigEntry(&cgroupConfig, cgConfDef, key);
	dbgMask = getConfValueI(&cgroupConfig, key);
	maskCgLogger(dbgMask);
	cglog(CG_LOG_VERBOSE, "%s: debugMask set to %#x\n", __func__, dbgMask);
    } else {
	return ustrdup("Permission denied\n");
    }

    return NULL;
}

char *show(char *key)
{
    char *buf = NULL, *val;
    size_t bufSize = 0;

    if (!key) {
	/* Show the whole configration */
	int maxKeyLen = getMaxKeyLen(cgConfDef);
	int i;

	buf = str2Buf("\n", buf, &bufSize);
	for (i = 0; cgConfDef[i].name; i++) {
	    char *name = cgConfDef[i].name, line[160];
	    val = getConfValueC(&cgroupConfig, name);

	    snprintf(line, sizeof(line), "%*s = %s\n", maxKeyLen, name, val);
	    buf = str2Buf(line, buf, &bufSize);
	}
    } else if ((val = getConfValueC(&cgroupConfig, key))) {
	buf = str2Buf("\n", buf, &bufSize);
	buf = str2Buf(key, buf, &bufSize);
	buf = str2Buf(" = ", buf, &bufSize);
	buf = str2Buf(val, buf, &bufSize);
	buf = str2Buf("\n", buf, &bufSize);
    }

    return buf;
}
