/*
 * ParaStation
 *
 * Copyright (C) 2012-2019 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <string.h>

#include "pluginlog.h"
#include "pluginmalloc.h"
#include "pluginconfig.h"
#include "plugin.h"

#include "psaccountclient.h"
#include "psaccountconfig.h"
#include "psaccountlog.h"
#include "psaccountenergy.h"

#include "psaccountkvs.h"

FILE *memoryDebug = NULL;

static char line[256];

/**
 * @brief Show current configuration
 *
 * Print the current configuration of the plugin to the buffer @a buf
 * of current length @a length. The buffer might be dynamically
 * extended if required.
 *
 * @param buf Buffer to write information to
 *
 * @param bufSize Size of the buffer
 *
 * @return Returns the buffer with the updated configuration information.
 */
static char *showConfig(char *buf, size_t *bufSize)
{
    int maxKeyLen = getMaxKeyLen(confDef);
    int i;

    str2Buf("\n", &buf, bufSize);

    for (i = 0; confDef[i].name; i++) {
	char *name = confDef[i].name;
	char *val = getConfValueC(&config, name);

	snprintf(line, sizeof(line), "%*s = %s\n", maxKeyLen+2, name, val);
	str2Buf(line, &buf, bufSize);
    }

    return buf;
}

static char *showEnergy(char *buf, size_t *bufSize)
{
    psAccountEnergy_t *e = energyGetData();

    str2Buf("\n", &buf, bufSize);
    snprintf(line, sizeof(line), "power cur: %u avg: %u min: %u max: %u "
	     "(watt) \n", e->powerCur, e->powerAvg, e->powerMin, e->powerMax);
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "energy base: %zu consumed: %zu (joules)\n",
	     e->energyBase, e->energyCur);
    str2Buf(line, &buf, bufSize);

    return buf;
}

char *show(char *key)
{
    char *buf = NULL;
    size_t bufSize = 0;

    if (!key) {
	str2Buf("use key [clients|dclients|jobs|config|energy]\n",
		&buf, &bufSize);
	return buf;
    }

    /* show current clients */
    if (!strcmp(key, "clients")) {
	return listClients(buf, &bufSize, false);
    }

    /* show current clients in detail */
    if (!strcmp(key, "dclients")) {
	return listClients(buf, &bufSize, true);
    }

    /* show current jobs */
    if (!strcmp(key, "jobs")) {
	return listJobs(buf, &bufSize);
    }

    /* show current config */
    if (!strcmp(key, "config")) {
	return showConfig(buf, &bufSize);
    }

    /* show nodes energy/power consumption */
    if (!strcmp(key, "energy")) {
	return showEnergy(buf, &bufSize);
    }

    str2Buf("invalid key, use [clients|dclients|jobs|config|energy]\n",
	    &buf, &bufSize);
    return buf;
}

char *set(char *key, char *val)
{
    const ConfDef_t *thisConfDef = getConfigDef(key, confDef);
    char *buf = NULL;
    size_t bufSize = 0;

    /* search in config for given key */
    if (thisConfDef) {
	int verRes = verifyConfigEntry(confDef, key, val);
	if (verRes) {
	    if (verRes == 1) {
		str2Buf("\nInvalid key '", &buf, &bufSize);
		str2Buf(key, &buf, &bufSize);
		str2Buf("' for cmd set : use 'plugin help psaccount' "
			"for help.\n", &buf, &bufSize);
	    } else if (verRes == 2) {
		str2Buf("\nThe value '", &buf, &bufSize);
		str2Buf(val, &buf, &bufSize);
		str2Buf("' for cmd 'set ", &buf, &bufSize);
		str2Buf(key, &buf, &bufSize);
		str2Buf("' has to be numeric.\n", &buf,	&bufSize);
	    }
	} else {
	    /* save new config value */
	    addConfigEntry(&config, key, val);

	    snprintf(line, sizeof(line), "\nsaved '%s = %s'\n", key, val);
	    str2Buf(line, &buf, &bufSize);

	    if (!strcmp(key, "DEBUG_MASK")) {
		int debugMask = getConfValueI(&config, "DEBUG_MASK");
		maskLogger(debugMask);
	    }
	}
    } else if (!strcmp(key, "memdebug")) {
	if (memoryDebug) fclose(memoryDebug);
	memoryDebug = fopen(val, "w+");
	if (memoryDebug) {
	    finalizePluginLogger();
	    initPluginLogger(NULL, memoryDebug);
	    maskPluginLogger(PLUGIN_LOG_MALLOC);
	    str2Buf("\nmemory logging to '", &buf, &bufSize);
	    str2Buf(val, &buf, &bufSize);
	    str2Buf("'\n", &buf, &bufSize);
	} else {
	    str2Buf("\nopening file '", &buf, &bufSize);
	    str2Buf(val, &buf, &bufSize);
	    str2Buf("' for writing failed\n", &buf, &bufSize);
	}
    } else {
	str2Buf("\nInvalid key '", &buf, &bufSize);
	str2Buf(key, &buf, &bufSize);
	str2Buf("' for cmd set : use 'plugin help psaccount' for help.\n",
		&buf, &bufSize);
    }

    return buf;
}

char *unset(char *key)
{
    char *buf = NULL;
    size_t bufSize = 0;

    /* search in config for given key */
    if (getConfValueC(&config, key)) {
	unsetConfigEntry(&config, confDef, key);
    } else if (!strcmp(key, "memdebug")) {
	if (memoryDebug) {
	    finalizePluginLogger();
	    fclose(memoryDebug);
	    memoryDebug = NULL;
	    initPluginLogger(NULL, psaccountlogfile);
	}
	str2Buf("Stopped memory debugging\n", &buf, &bufSize);
    } else {
	str2Buf("\nInvalid key '", &buf, &bufSize);
	str2Buf(key, &buf, &bufSize);
	str2Buf("' for cmd unset : use 'plugin help psaccount' for help.\n",
		&buf, &bufSize);
    }

    return buf;
}

char *help(void)
{
    char *buf = NULL;
    size_t bufSize = 0;
    int maxKeyLen = getMaxKeyLen(confDef);
    int i;

    str2Buf("\n# configuration options #\n\n", &buf, &bufSize);

    for (i = 0; confDef[i].name; i++) {
	char type[10];
	snprintf(type, sizeof(type), "<%s>", confDef[i].type);
	snprintf(line, sizeof(line), "%*s %8s  %s\n", maxKeyLen+2,
		 confDef[i].name, type, confDef[i].desc);
	str2Buf(line, &buf, &bufSize);
    }
    str2Buf("\nuse show [clients|dclients|jobs|config]\n", &buf, &bufSize);

    return buf;
}
