/*
 * ParaStation
 *
 * Copyright (C) 2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "plugin.h"
#include "pscommon.h"
#include "psidhook.h"

//#include "psidutil.h"

#include "pluginmalloc.h"
#include "pluginlog.h"

#include "jaillog.h"
#include "jailconfig.h"

#define JAIL_CONFIG "jail.conf"

/** psid plugin requirements */
char name[] = "jail";
int version = 1;
int requiredAPI = 118;
plugin_dep_t dependencies[] = { { NULL, 0 } };

/** Name of the script to use for jailing */
static char *jailScript = NULL;

static char *checkScript(char *script)
{
    char *fName;

    if (script[0] == '/') {
	fName = strdup(script);
    } else {
	fName = PSC_concat(PLUGINDIR, "/", script, NULL);
    }

    struct stat sb;
    if (stat(fName, &sb) == -1) {
	jwarn(-1, errno, "%s: stat(%s)", __func__, fName);
	free(fName);
	return NULL;
    }

    if (!S_ISREG(sb.st_mode) || !(sb.st_mode & S_IXUSR)) {
	jlog(errno, "%s: stat(%s): %s", __func__, fName,
	     (!S_ISREG(sb.st_mode)) ? "S_ISREG error" :
	     (sb.st_mode & S_IXUSR) ? "" : "S_IXUSR error");
	free(fName);
	return NULL;
    }

    return fName;
}

static int jailProcess(void *info)
{
    pid_t pid = *(pid_t *)info;

    jlog(J_LOG_VERBOSE, "%s: called for %d\n", __func__, pid);

    if (!jailScript) {
	jlog(-1, "%s: no script provided\n", __func__);
	return 0;
    }

    char argument[64];
    snprintf(argument, sizeof(argument), " %d", pid);
    char *command = PSC_concat(jailScript, argument, NULL);

    if (command) {
	int ret = system(command);
	if (ret == -1) jlog(-1, "%s: system(%s) failed\n", __func__, command);
	free(command);
    }

    return 0;
}

int initialize(void)
{
    int debugMask;
    char configFile[PATH_MAX];

    /* init logging facility */
    initLogger(NULL);

    /* init the config facility */
    snprintf(configFile, sizeof(configFile), "%s/%s", PLUGINDIR, JAIL_CONFIG);

    initConfig(configFile);

    /* adapt the debug mask */
    debugMask = getConfValueI(&config, "DEBUG_MASK");
    maskLogger(debugMask);
    jlog(J_LOG_VERBOSE, "%s: debugMask set to %#x\n", __func__, debugMask);

    char *script = getConfValueC(&config, "JAIL_SCRIPT");
    jailScript = checkScript(script);

    if (!jailScript) {
	jlog(-1, "(%i) no script defined\n", version);
    } else {
	jlog(J_LOG_VERBOSE, "script set to '%s'\n", jailScript);
    }

    if (!PSIDhook_add(PSIDHOOK_JAIL_CHILD, jailProcess)) {
	jlog(-1, "%s: register PSIDHOOK_JAIL_CHILD failed\n", __func__);
	return 1;
    }

    if (jailScript) jlog(-1 , "(%i) successfully started\n", version);

    return 0;
}

void cleanup(void)
{
    if (!PSIDhook_del(PSIDHOOK_JAIL_CHILD, jailProcess)) {
	jlog(-1, "removing PSIDHOOK_JAIL_CHILD failed\n");
    }

    if (jailScript) free(jailScript);

    jlog(-1, "...Bye.\n");

    /* release the logger */
    logger_finalize(jaillogger);
}

char *help(void)
{
    char *buf = NULL;
    size_t bufSize = 0;
    int maxKeyLen = getMaxKeyLen(confDef);
    int i;

    str2Buf("\tModify child process' cgroup setup while jailing\n",
	    &buf, &bufSize);
    str2Buf("\tAll action takes place in the JAIL_SCRIPT\n\n", &buf, &bufSize);
    str2Buf("# configuration options #\n", &buf, &bufSize);

    for (i = 0; confDef[i].name; i++) {
	char type[10], line[160];
	snprintf(type, sizeof(type), "<%s>", confDef[i].type);
	snprintf(line, sizeof(line), "%*s %10s  %s\n",
		 maxKeyLen+2, confDef[i].name, type, confDef[i].desc);
	str2Buf(line, &buf, &bufSize);
    }

    return buf;
}

char *set(char *key, char *val)
{
    const ConfDef_t *thisConfDef = getConfigDef(key, confDef);

    if (!thisConfDef) return ustrdup("\nUnknown option\n");

    if (verifyConfigEntry(confDef, key, val))
	return ustrdup("\tIllegal value\n");

    if (!strcmp(key, "JAIL_SCRIPT")) {
	addConfigEntry(&config, key, val);
	if (jailScript) free(jailScript);
	char *script = getConfValueC(&config, "JAIL_SCRIPT");
	jailScript = checkScript(script);
	jlog(J_LOG_VERBOSE, "jailScript set to '%s'\n",
	     jailScript ? jailScript : "<invalid>");
	if (!jailScript) {
	    char *buf = NULL;
	    size_t bufSize = 0;
	    str2Buf("\tscript '", &buf, &bufSize);
	    str2Buf(script, &buf, &bufSize);
	    str2Buf("' is invalid\n", &buf, &bufSize);
	    return buf;
	}
    } else if (!strcmp(key, "DEBUG_MASK")) {
	int dbgMask;
	addConfigEntry(&config, key, val);
	dbgMask = getConfValueI(&config, key);
	maskLogger(dbgMask);
	jlog(J_LOG_VERBOSE, "debugMask set to %#x\n", dbgMask);
    } else {
	return ustrdup("\nPermission denied\n");
    }

    return NULL;
}

char *unset(char *key)
{
    if (!strcmp(key, "JAIL_SCRIPT")) {
	unsetConfigEntry(&config, confDef, key);
	if (jailScript) free(jailScript);
	char *script = getConfValueC(&config, "JAIL_SCRIPT");
	jailScript = checkScript(script);
	if (!jailScript) {
	    jlog(-1, "%s: no script defined\n", __func__);
	} else {
	    jlog(J_LOG_VERBOSE, "script set to '%s'\n", jailScript);
	}
    } else if (!strcmp(key, "DEBUG_MASK")) {
	int dbgMask;
	unsetConfigEntry(&config, confDef, key);
	dbgMask = getConfValueI(&config, key);
	maskLogger(dbgMask);
	jlog(J_LOG_VERBOSE, "debugMask set to %#x\n", dbgMask);
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
	/* Show the whole configuration */
	int maxKeyLen = getMaxKeyLen(confDef);
	int i;

	str2Buf("\n", &buf, &bufSize);
	for (i = 0; confDef[i].name; i++) {
	    char *name = confDef[i].name, line[160];
	    val = getConfValueC(&config, name);

	    snprintf(line, sizeof(line), "%*s = %s\n", maxKeyLen+2, name, val);
	    str2Buf(line, &buf, &bufSize);
	}
	str2Buf("\n", &buf, &bufSize);
	if (jailScript) {
	    str2Buf("script in use: '", &buf, &bufSize);
	    str2Buf(jailScript, &buf, &bufSize);
	    str2Buf("'\n", &buf, &bufSize);
	} else {
	    str2Buf("no script defined!\n", &buf, &bufSize);
	}
    } else if ((val = getConfValueC(&config, key))) {
	str2Buf("\t", &buf, &bufSize);
	str2Buf(key, &buf, &bufSize);
	str2Buf(" = ", &buf, &bufSize);
	str2Buf(val, &buf, &bufSize);
	str2Buf("\n", &buf, &bufSize);
    }

    return buf;
}
