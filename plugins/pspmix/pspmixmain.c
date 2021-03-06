/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdio.h>
#include <dlfcn.h>
#include <limits.h>

#include "plugin.h"
#include "pluginmalloc.h"
#include "psidplugin.h"
#include "pscommon.h"

#include "pspmixlog.h"
#include "pspmixconfig.h"
#include "pspmixdaemon.h"
#include "pspmixforwarder.h"

#if 0
#include "psaccounthandles.h"
#endif

#define PSPMIX_CONFIG "pspmix.conf"

/** psid plugin requirements */
char name[] = "pspmix";
int version = 1;
int requiredAPI = 110;
plugin_dep_t dependencies[] = {
#if 0
    { .name = "psaccount", .version = 24 },
#endif
    { .name = NULL, .version = 0 } };

int initialize(FILE *logfile)
{
    int debugMask;
    char configFile[PATH_MAX];

    /* init logging facility */
    pspmix_initLogger(name, logfile);

    /* init the config facility */
    snprintf(configFile, sizeof(configFile), "%s/%s", PLUGINDIR, PSPMIX_CONFIG);

    initPSPMIxConfig(configFile);

    /* adapt the debug mask */
    debugMask = getConfValueI(&config, "DEBUG_MASK");
    pspmix_maskLogger(debugMask);
/*    pspmix_maskLogger(PSPMIX_LOG_CALL | PSPMIX_LOG_ENV | PSPMIX_LOG_COMM
		    | PSPMIX_LOG_LOCK | PSPMIX_LOG_FENCE | PSPMIX_LOG_VERBOSE);

    PSC_setDebugMask(PSC_LOG_COMM);
*/
    mdbg(PSPMIX_LOG_VERBOSE, "%s: debugMask set to %#x\n", __func__, debugMask);

#if 0
    void *handle = PSIDplugin_getHandle("psaccount");
#endif

    /* initialize all modules */
    pspmix_initDaemonModule();
    pspmix_initForwarderModule();

#if 0
    /* get psaccount function handles */
    if (!handle) {
	psAccountSwitchAccounting = NULL;
	mlog("%s: getting psaccount handle failed\n", __func__);
    } else {
	psAccountSwitchAccounting = dlsym(handle, "psAccountSwitchAccounting");
	if (!psAccountSwitchAccounting) {
	    mlog("%s: loading function psAccountSwitchAccounting() failed\n",
		 __func__);
	}
    }
#endif

    mlog("(%i) successfully started\n", version);

    return 0;
}

void cleanup(void)
{
    /* remove registered hooks */
    pspmix_finalizeForwarderModule();
    pspmix_finalizeDaemonModule();

//    if (memoryDebug) fclose(memoryDebug); XXX wozu ist das gut?
    freeConfig(&config);

    logger_finalize(pmixlogger);
}

char *help(void)
{
    char *buf = NULL;
    size_t bufSize = 0;
    int maxKeyLen = getMaxKeyLen(confDef);
    int i;

    str2Buf("\tProvide PMIx interface to executed programs\n",
	    &buf, &bufSize);
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

    if (!strcmp(key, "DEBUG_MASK")) {
	int dbgMask;
	addConfigEntry(&config, key, val);
	dbgMask = getConfValueI(&config, key);
	pspmix_maskLogger(dbgMask);
	mdbg(PSPMIX_LOG_VERBOSE, "debugMask set to %#x\n", dbgMask);
    } else {
	return ustrdup("\nPermission denied\n");
    }

    return NULL;
}

char *unset(char *key)
{
    if (!strcmp(key, "DEBUG_MASK")) {
	int dbgMask;
	unsetConfigEntry(&config, confDef, key);
	dbgMask = getConfValueI(&config, key);
	pspmix_maskLogger(dbgMask);
	mdbg(PSPMIX_LOG_VERBOSE, "debugMask set to %#x\n", dbgMask);
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

	    char *cName = confDef[i].name, line[160];
	    val = getConfValueC(&config, cName);

	    snprintf(line, sizeof(line), "%*s = %s\n", maxKeyLen+2, cName, val);
	    str2Buf(line, &buf, &bufSize);
	}
	str2Buf("\n", &buf, &bufSize);
    } else if ((val = getConfValueC(&config, key))) {
	str2Buf("\t", &buf, &bufSize);
	str2Buf(key, &buf, &bufSize);
	str2Buf(" = ", &buf, &bufSize);
	str2Buf(val, &buf, &bufSize);
	str2Buf("\n", &buf, &bufSize);
    }

    return buf;
}
/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
