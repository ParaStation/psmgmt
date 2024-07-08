/*
 * ParaStation
 *
 * Copyright (C) 2012-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psaccountkvs.h"

#include <stdbool.h>
#include <string.h>

#include "psstrbuf.h"

#include "plugin.h"
#include "pluginconfig.h"
#include "pluginlog.h"

#include "psaccount.h"
#include "psaccountclient.h"
#include "psaccountconfig.h"
#include "psaccountenergy.h"
#include "psaccountjob.h"
#include "psaccountlog.h"
#include "psaccounttypes.h"

FILE *memoryDebug = NULL;

static char line[256];

/**
 * @brief Show current configuration
 *
 * Print the current configuration of the plugin.
 *
 * @return Returns a buffer with the updated configuration information
 */
static char *showConfig(void)
{
    int maxKeyLen = getMaxKeyLen(confDef);

    strbuf_t buf = strbufNew("\n");
    for (int i = 0; confDef[i].name; i++) {
	char *cName = confDef[i].name;
	char *cVal = getConfValueC(config, cName);
	snprintf(line, sizeof(line), "%*s = %s\n", maxKeyLen+2, cName, cVal);
	strbufAdd(buf, line);
    }

    return strbufSteal(buf);
}

static char *showEnergy(void)
{
    psAccountEnergy_t *e = Energy_getData();

    strbuf_t buf = strbufNew("\n");
    snprintf(line, sizeof(line), "power cur: %u avg: %u min: %u max: %u "
	     "(watt) \n", e->powerCur, e->powerAvg, e->powerMin, e->powerMax);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "energy base: %lu consumed: %lu (joules)\n",
	     e->energyBase, e->energyCur);
    strbufAdd(buf, line);

    return strbufSteal(buf);
}

char *show(char *key)
{

    if (!key) {
	strbuf_t buf = strbufNew(NULL);
	strbufAdd(buf, "use key [clients|dclients|jobs|config|energy]\n");
	return strbufSteal(buf);
    }

    /* show current clients */
    if (!strcmp(key, "clients")) return listClients(false);

    /* show current clients in detail */
    if (!strcmp(key, "dclients")) return listClients(true);

    /* show current jobs */
    if (!strcmp(key, "jobs")) return listJobs();

    /* show current config */
    if (!strcmp(key, "config")) return showConfig();

    /* show nodes energy/power consumption */
    if (!strcmp(key, "energy")) return showEnergy();

    strbuf_t buf = strbufNew(NULL);
    strbufAdd(buf, "invalid key, use [clients|dclients|jobs|config|energy]\n");
    return strbufSteal(buf);
}

char *set(char *key, char *val)
{
    strbuf_t buf = strbufNew(NULL);

    /* search in config for given key */
    const ConfDef_t *thisConfDef = getConfigDef(key, confDef);
    if (thisConfDef) {
	int verRes = verifyConfigEntry(confDef, key, val);
	if (verRes) {
	    if (verRes == 1) {
		strbufAdd(buf, "\nInvalid key '");
		strbufAdd(buf, key);
		strbufAdd(buf, "' for cmd set : use 'plugin help psaccount' "
			"for help.\n");
	    } else if (verRes == 2) {
		strbufAdd(buf, "\nThe value '");
		strbufAdd(buf, val);
		strbufAdd(buf, "' for cmd 'set ");
		strbufAdd(buf, key);
		strbufAdd(buf, "' has to be numeric.\n");
	    }
	} else {
	    /* save new config value */
	    addConfigEntry(config, key, val);

	    snprintf(line, sizeof(line), "\nsaved '%s = %s'\n", key, val);
	    strbufAdd(buf, line);

	    if (!strcmp(key, "DEBUG_MASK")) {
		int debugMask = getConfValueI(config, "DEBUG_MASK");
		maskLogger(debugMask);
	    } else if (!strcmp(key, "POLL_INTERVAL")) {
		int poll = getConfValueI(config, "POLL_INTERVAL");
		if (poll >= 0) setMainTimer(poll);
	    }
	}
    } else if (!strcmp(key, "memdebug")) {
	if (memoryDebug) fclose(memoryDebug);
	memoryDebug = fopen(val, "w+");
	if (memoryDebug) {
	    finalizePluginLogger();
	    initPluginLogger(NULL, memoryDebug);
	    maskPluginLogger(PLUGIN_LOG_MALLOC);
	    strbufAdd(buf, "\nmemory logging to '");
	    strbufAdd(buf, val);
	    strbufAdd(buf, "'\n");
	} else {
	    strbufAdd(buf, "\nopening file '");
	    strbufAdd(buf, val);
	    strbufAdd(buf, "' for writing failed\n");
	}
    } else {
	strbufAdd(buf, "\nInvalid key '");
	strbufAdd(buf, key);
	strbufAdd(buf, "' for cmd set : use 'plugin help psaccount' for help.\n");
    }

    return strbufSteal(buf);
}

char *unset(char *key)
{
    strbuf_t buf = strbufNew(NULL);

    /* search in config for given key */
    if (getConfValueC(config, key)) {
	unsetConfigEntry(config, confDef, key);

	if (!strcmp(key, "DEBUG_MASK")) {
	    int debugMask = getConfValueI(config, "DEBUG_MASK");
	    maskLogger(debugMask);
	} else if (!strcmp(key, "POLL_INTERVAL")) {
	    int poll = getConfValueI(config, "POLL_INTERVAL");
	    if (poll >= 0) setMainTimer(poll);
	}
    } else if (!strcmp(key, "memdebug")) {
	if (memoryDebug) {
	    finalizePluginLogger();
	    fclose(memoryDebug);
	    memoryDebug = NULL;
	    initPluginLogger(NULL, psaccountlogfile);
	}
	strbufAdd(buf, "Stopped memory debugging\n");
    } else {
	strbufAdd(buf, "\nInvalid key '");
	strbufAdd(buf, key);
	strbufAdd(buf, "' for cmd unset : use 'plugin help psaccount' for help.\n");
    }

    return strbufSteal(buf);
}

char *help(char *key)
{
    int maxKeyLen = getMaxKeyLen(confDef);

    strbuf_t buf = strbufNew("\n# configuration options #\n\n");
    for (int i = 0; confDef[i].name; i++) {
	char type[10];
	snprintf(type, sizeof(type), "<%s>", confDef[i].type);
	snprintf(line, sizeof(line), "%*s %8s  %s\n", maxKeyLen+2,
		 confDef[i].name, type, confDef[i].desc);
	strbufAdd(buf, line);
    }
    strbufAdd(buf, "\nuse show [clients|dclients|jobs|config]\n");

    return strbufSteal(buf);
}
