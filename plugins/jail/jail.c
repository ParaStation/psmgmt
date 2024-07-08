/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "pscommon.h"
#include "psstrbuf.h"

#include "plugin.h"
#include "pluginconfig.h"
#include "psidhook.h"
#include "psidutil.h"

#include "jailconfig.h"
#include "jaillog.h"
#include "jailtypes.h"

#define JAIL_CONFIG "jail.conf"

/** psid plugin requirements */
char name[] = "jail";
int version = 3;
int requiredAPI = 131;
plugin_dep_t dependencies[] = { { NULL, 0 } };

/** Name of the script to use for jailing */
static char *jailScript = NULL;

/** Name of the script to use for terminating a jail */
static char *termScript = NULL;

/** Name of the script to initialize jail */
static char *initScript = NULL;

static char *checkScript(char *script)
{
    char *fName;

    if (script[0] == '/') {
	fName = strdup(script);
    } else {
	fName = PSC_concat(PLUGINDIR, "/", script);
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

static int execScript(pid_t child, char *script)
{
    char argument[64];
    snprintf(argument, sizeof(argument), " %d", child);
    char *command = PSC_concat(script, argument);

    if (command) {
	/* ensure system() is able to catch SIGCHLD */
	bool blocked = PSID_blockSig(SIGCHLD, false);

	int ret = system(command);
	if (ret == -1) jlog(-1, "%s: system(%s) failed\n", __func__, command);

	PSID_blockSig(SIGCHLD, blocked);
	free(command);

	if (ret != 0) return -1;
    }

    return 0;
}

static int jailProcess(void *info)
{
    pid_t pid = *(pid_t *)info;

    jlog(J_LOG_VERBOSE, "%s: called for %d\n", __func__, pid);

    if (!jailScript) {
	jlog(J_LOG_VERBOSE, "%s: no jail script provided\n", __func__);
	return 0;
    }
    return execScript(pid, jailScript);
}

static int jailTerminate(void *info)
{
    pid_t pid = *(pid_t *)info;

    jlog(J_LOG_VERBOSE, "%s: called for %d\n", __func__, pid);

    if (!termScript) {
	jlog(J_LOG_VERBOSE, "%s: no terminate script provided\n", __func__);
	return 0;
    }

    return execScript(pid, termScript);
}

jailGetScripts_t jailGetScripts;

void jailGetScripts(const char **jailScriptName, const char **termScriptName)
{
    if (jailScriptName) *jailScriptName = jailScript;
    if (termScriptName) *termScriptName = termScript;
}

int initialize(FILE *logfile)
{
    int debugMask;
    char configFile[PATH_MAX];

    /* init logging facility */
    initLogger(name, logfile);

    /* init the config facility */
    snprintf(configFile, sizeof(configFile), "%s/%s", PLUGINDIR, JAIL_CONFIG);

    initJailConfig(configFile);

    /* adapt the debug mask */
    debugMask = getConfValueI(config, "DEBUG_MASK");
    maskLogger(debugMask);
    jlog(J_LOG_VERBOSE, "%s: debugMask set to %#x\n", __func__, debugMask);

    char *script = getConfValueC(config, "JAIL_SCRIPT");
    jailScript = checkScript(script);

    if (!jailScript) {
	jlog(-1, "(%i) no jail script defined\n", version);
    } else {
	jlog(J_LOG_VERBOSE, "jail script set to '%s'\n", jailScript);
    }

    script = getConfValueC(config, "JAIL_TERM_SCRIPT");
    termScript = checkScript(script);

    if (!termScript) {
	jlog(-1, "(%i) no terminate script defined\n", version);
    } else {
	jlog(J_LOG_VERBOSE, "terminate script set to '%s'\n", termScript);
    }

    script = getConfValueC(config, "JAIL_INIT_SCRIPT");
    initScript = checkScript(script);
    if (initScript) {
	if (execScript(getpid(), initScript) != 0) {
	    jlog(-1, "%s: JAIL_INIT_SCRIPT failed\n", __func__);
	    return 1;
	}
    }

    if (!PSIDhook_add(PSIDHOOK_JAIL_CHILD, jailProcess)) {
	jlog(-1, "%s: register PSIDHOOK_JAIL_CHILD failed\n", __func__);
	return 1;
    }

    if (!PSIDhook_add(PSIDHOOK_JAIL_TERM, jailTerminate)) {
	jlog(-1, "%s: register PSIDHOOK_JAIL_TERM failed\n", __func__);
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

    if (!PSIDhook_del(PSIDHOOK_JAIL_TERM, jailTerminate)) {
	jlog(-1, "removing PSIDHOOK_JAIL_TERM failed\n");
    }

    free(jailScript);
    free(termScript);
    free(initScript);
    freeConfig(config);

    jlog(-1, "...Bye.\n");
    finalizeLogger();
}

char *help(char *key)
{
    strbuf_t buf = strbufNew(NULL);
    int maxKeyLen = getMaxKeyLen(confDef);

    strbufAdd(buf, "\tModify child process' cgroup setup while jailing\n");
    strbufAdd(buf, "\tAll cgroups are defined in the JAIL_SCRIPT\n\n");
    strbufAdd(buf, "\tThe destruction of cgroups is in the JAIL_TERM_SCRIPT\n\n");
    strbufAdd(buf, "# configuration options #\n");

    for (int i = 0; confDef[i].name; i++) {
	char type[10], line[160];
	snprintf(type, sizeof(type), "<%s>", confDef[i].type);
	snprintf(line, sizeof(line), "%*s %10s  %s\n",
		 maxKeyLen+2, confDef[i].name, type, confDef[i].desc);
	strbufAdd(buf, line);
    }

    return strbufSteal(buf);
}

char *set(char *key, char *val)
{
    strbuf_t buf = strbufNew(NULL);

    const ConfDef_t *thisConfDef = getConfigDef(key, confDef);
    if (!thisConfDef) {
	strbufAdd(buf, "\nUnknown option\n");
    } else if (verifyConfigEntry(confDef, key, val)) {
	strbufAdd(buf, "\tIllegal value\n");
    } else if (!strcmp(key, "JAIL_SCRIPT")) {
	addConfigEntry(config, key, val);
	free(jailScript);
	char *script = getConfValueC(config, "JAIL_SCRIPT");
	jailScript = checkScript(script);
	jlog(J_LOG_VERBOSE, "jailScript set to '%s'\n",
	     jailScript ? jailScript : "<invalid>");
	if (!jailScript) {
	    strbufAdd(buf, "\tscript '");
	    strbufAdd(buf, script);
	    strbufAdd(buf, "' is invalid\n");
	}
    } else if (!strcmp(key, "JAIL_TERM_SCRIPT")) {
	addConfigEntry(config, key, val);
	free(termScript);
	char *script = getConfValueC(config, "JAIL_TERM_SCRIPT");
	termScript = checkScript(script);
	jlog(J_LOG_VERBOSE, "termScript set to '%s'\n",
	     termScript ? termScript : "<invalid>");
	if (!termScript) {
	    strbufAdd(buf, "\tterm script '");
	    strbufAdd(buf, script);
	    strbufAdd(buf, "' is invalid\n");
	}
    } else if (!strcmp(key, "DEBUG_MASK")) {
	int dbgMask;
	addConfigEntry(config, key, val);
	dbgMask = getConfValueI(config, key);
	maskLogger(dbgMask);
	jlog(J_LOG_VERBOSE, "debugMask set to %#x\n", dbgMask);
    } else {
	strbufAdd(buf, "\nPermission denied\n");
    }

    return strbufSteal(buf);
}

char *unset(char *key)
{
    if (!strcmp(key, "JAIL_SCRIPT")) {
	unsetConfigEntry(config, confDef, key);
	free(jailScript);
	char *script = getConfValueC(config, "JAIL_SCRIPT");
	jailScript = checkScript(script);
	if (!jailScript) {
	    jlog(-1, "%s: no script defined\n", __func__);
	} else {
	    jlog(J_LOG_VERBOSE, "script set to '%s'\n", jailScript);
	}
    } else if (!strcmp(key, "JAIL_TERM_SCRIPT")) {
	unsetConfigEntry(config, confDef, key);
	free(termScript);
	char *script = getConfValueC(config, "JAIL_TERM_SCRIPT");
	termScript = checkScript(script);
	if (!termScript) {
	    jlog(-1, "%s: no term script defined\n", __func__);
	} else {
	    jlog(J_LOG_VERBOSE, "term script set to '%s'\n", termScript);
	}
    } else if (!strcmp(key, "DEBUG_MASK")) {
	int dbgMask;
	unsetConfigEntry(config, confDef, key);
	dbgMask = getConfValueI(config, key);
	maskLogger(dbgMask);
	jlog(J_LOG_VERBOSE, "debugMask set to %#x\n", dbgMask);
    } else {
	return strdup("Permission denied\n");
    }
    return NULL;
}

char *show(char *key)
{
    strbuf_t buf = strbufNew(NULL);

    char *val;
    if (!key) {
	/* Show the whole configuration */
	int maxKeyLen = getMaxKeyLen(confDef);

	strbufAdd(buf, "\n");
	for (int i = 0; confDef[i].name; i++) {
	    char *cName = confDef[i].name, line[160];
	    val = getConfValueC(config, cName);

	    snprintf(line, sizeof(line), "%*s = %s\n", maxKeyLen+2, cName, val);
	    strbufAdd(buf, line);
	}
	strbufAdd(buf, "\n");
	if (jailScript) {
	    strbufAdd(buf, "jail script in use: '");
	    strbufAdd(buf, jailScript);
	    strbufAdd(buf, "'\n");
	} else {
	    strbufAdd(buf, "no jail script defined!\n");
	}
	if (termScript) {
	    strbufAdd(buf, "term script in use: '");
	    strbufAdd(buf, termScript);
	    strbufAdd(buf, "'\n");
	} else {
	    strbufAdd(buf, "no term script defined!\n");
	}
	if (initScript) {
	    strbufAdd(buf, "init script in use: '");
	    strbufAdd(buf, initScript);
	    strbufAdd(buf, "'\n");
	} else {
	    strbufAdd(buf, "no init script defined!\n");
	}
    } else if ((val = getConfValueC(config, key))) {
	strbufAdd(buf, "\t");
	strbufAdd(buf, key);
	strbufAdd(buf, " = ");
	strbufAdd(buf, val);
	strbufAdd(buf, "\n");
    }

    return strbufSteal(buf);
}
