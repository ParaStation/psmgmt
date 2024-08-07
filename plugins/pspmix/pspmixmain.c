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
#include <stdio.h>
#include <limits.h>
#include <string.h>

#include "plugin.h"
#include "psstrbuf.h"

#include "pluginconfig.h"

#include "pspmixlog.h"
#include "pspmixconfig.h"
#include "pspmixdaemon.h"
#include "pspmixforwarder.h"

#include "pmix_version.h"

#define PSPMIX_CONFIG "pspmix.conf"

/** psid plugin requirements */
char name[] = "pspmix";
int version = 3;
int requiredAPI = 137;
plugin_dep_t dependencies[] = {
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

    mlog("Using PMIx %ld.%ld.%ld\n", PMIX_VERSION_MAJOR, PMIX_VERSION_MINOR,
	 PMIX_VERSION_RELEASE);

    /* adapt the debug mask */
    debugMask = getConfValueI(config, "DEBUG_MASK");
    pspmix_maskLogger(debugMask);
/*    pspmix_maskLogger(PSPMIX_LOG_CALL | PSPMIX_LOG_ENV | PSPMIX_LOG_COMM
		    | PSPMIX_LOG_LOCK | PSPMIX_LOG_FENCE | PSPMIX_LOG_VERBOSE);

    PSC_setDebugMask(PSC_LOG_COMM);
*/
    mdbg(PSPMIX_LOG_VERBOSE, "%s: debugMask set to %#x\n", __func__, debugMask);

    /* initialize all modules */
    pspmix_initDaemonModule();
    pspmix_initForwarderModule();

    mlog("(%i) successfully started\n", version);

    return 0;
}

void cleanup(void)
{
    /* remove registered hooks */
    pspmix_finalizeForwarderModule();
    pspmix_finalizeDaemonModule();

//    if (memoryDebug) fclose(memoryDebug); XXX wozu ist das gut?
    freeConfig(config);
    pspmix_finalizeLogger();
}

char *help(char *key)
{
    int maxKeyLen = getMaxKeyLen(confDef);

    strbuf_t buf = strbufNew("\tProvide PMIx interface to executed programs\n");
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
    if (!getConfigDef(key, confDef)) return strdup("\nUnknown key\n");

    if (verifyConfigEntry(confDef, key, val)) return strdup("\tIllegal value\n");

    addConfigEntry(config, key, val);

    if (!strcmp(key, "DEBUG_MASK")) {
	int dbgMask = getConfValueI(config, key);
	pspmix_maskLogger(dbgMask);
	mlog("debugMask set to %#x\n", dbgMask);
    }

    return NULL;
}

char *unset(char *key)
{
    if (!getConfigDef(key, confDef)) return strdup("\nUnknown key\n");

    unsetConfigEntry(config, confDef, key);

    if (!strcmp(key, "DEBUG_MASK")) {
	int dbgMask = getConfValueI(config, key);
	pspmix_maskLogger(dbgMask);
	mlog("debugMask set to %#x\n", dbgMask);
    }

    return NULL;
}

char *show(char *key)
{
    char *val;

    strbuf_t buf = strbufNew(NULL);
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
    } else if ((val = getConfValueC(config, key))) {
	strbufAdd(buf, "\t");
	strbufAdd(buf, key);
	strbufAdd(buf, " = ");
	strbufAdd(buf, val);
	strbufAdd(buf, "\n");
    }

    return strbufSteal(buf);
}
/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
