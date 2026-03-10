/*
 * ParaStation
 *
 * Copyright (C) 2026 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "plugin.h"
#include "pluginpsconfig.h"
#include "psenv.h"
#include "psstrbuf.h"
#include "psidhook.h"

#include "qproxyconfig.h"
#include "qproxylog.h"

/** psid plugin requirements */
char name[] = "qproxy";
int version = 1;
int requiredAPI = 148;
plugin_dep_t dependencies[] = { { NULL, 0 } };

static bool evalValue(const char *key, const pluginConfigVal_t *val,
		      const void *info)
{
    if (!strcmp(key, "DebugMask")) {
	uint32_t mask = val ? val->val.num : 0;
	maskQProxyLogger(mask);
	mdbg(QPROXY_LOG_VERBOSE, "debugMask set to %#.8x\n", mask);
    } else if (!strcmp(key, "SchedulerURL")) {
	if (!val || !strlen(val->val.str)) mlog("no schedulerURL given\n");
    } else {
	mlog("unknown key '%s'\n", key);
    }

    return true;
}

static int hookPsslurmEnv(void *data)
{
    env_t env = data;
    char *schedURL = pluginConfig_getStr(QProxyConfig, "SchedulerURL");

    if (schedURL && strlen(schedURL)) envSet(env, "PSQSCHED_BASEPATH", schedURL);

    return 0;
}

#define addHook(hookName, hookFunc)			\
    if (!PSIDhook_add(hookName, hookFunc)) {		\
	flog("attaching " #hookName " failed\n");	\
	return false;					\
    }

static bool attachQProxyHooks(void)
{
    addHook(PSIDHOOK_PSSLURM_ENV, hookPsslurmEnv);

    return true;
}

#define relHook(hookName, hookFunc)			\
    if (!PSIDhook_del(hookName, hookFunc) && verbose)	\
	mlog("detaching " #hookName " failed\n");

static void detachQProxyHooks(bool verbose)
{
    relHook(PSIDHOOK_PSSLURM_ENV, hookPsslurmEnv);
}

int initialize(FILE *logfile)
{
    /* init logging facility */
    initQProxyLogger(name, logfile);

    /* init configuration (depends on psconfig) */
    initQProxyConfig();

    /* Activate configuration values */
    pluginConfig_traverse(QProxyConfig, evalValue, NULL);

    if (!attachQProxyHooks()) return 1;

    mlog("(%i) successfully started\n", version);

    return 0;
}

void cleanup(void)
{
    detachQProxyHooks(true);
    finalizeQProxyConfig();

    mlog("...Bye.\n");

    /* release the logger */
    finalizeQProxyLogger();
}

char *help(char *key)
{
    strbuf_t buf = strbufNew(NULL);

    strbufAdd(buf, "\tImplement proxy function for connecting to PSQSched\n\n");
    strbufAdd(buf, "# configuration options #\n");

    pluginConfig_helpDesc(QProxyConfig, buf);

    return strbufSteal(buf);
}

char *set(char *key, char *val)
{
    const pluginConfigDef_t *thisDef = pluginConfig_getDef(QProxyConfig, key);

    if (!thisDef) return strdup("\tunknown option\n");

    if (!pluginConfig_addStr(QProxyConfig, key, val)
	|| !evalValue(key, pluginConfig_get(QProxyConfig, key), NULL)) {
	return strdup("\tillegal value\n");
    }

    return NULL;
}

char *unset(char *key)
{
    if (!pluginConfig_remove(QProxyConfig, key)) {
	return strdup("\tunknown option\n");
    }
    evalValue(key, NULL, QProxyConfig);

    return NULL;
}

char *show(char *key)
{
    strbuf_t buf = strbufNew(NULL);

    if (!key) {
	/* Show the whole configuration */
	strbufAdd(buf, "\n");
	pluginConfig_traverse(QProxyConfig, pluginConfig_showVisitor, buf);
    } else if (!pluginConfig_showKeyVal(QProxyConfig, key, buf)) {
	return strdup("\tunknown option\n");
    }

    return strbufSteal(buf);
}
