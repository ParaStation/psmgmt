/*
 * ParaStation
 *
 * Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pluginpsconfig.h"
#include "pluginmalloc.h"

#include "psidutil.h"
#include "psidnodes.h"
#include "psidplugin.h"

#include "plugin.h"

/* We'll need the psconfig configuration stuff */
int requiredAPI = 130;

char name[] = "pluginpsconfig";

int version = 100;

plugin_dep_t dependencies[] = {
    { NULL, 0 } };

pluginConfig_t config;

/** Defintion of the configuration */
const pluginConfigDef_t confDef[] = {
    { "DebugMask", PLUGINCONFIG_VALUE_NUM, "Mask to steer debug output" },
    { "DebugMask2", PLUGINCONFIG_VALUE_NUM, "Mask to steer fine debug output" },
    { "GPUPCIs", PLUGINCONFIG_VALUE_LST, "Bla" }
};

static void unregisterHooks(void)
{}

pluginConfig_t config = NULL;

int initialize(void)
{
    pluginConfig_new(&config);
    pluginConfig_setDef(config, confDef);

    pluginConfig_load(config, "pluginConfig");
    pluginConfig_verify(config);

    PSID_log(-1, "%s: (%i) successfully started\n", name, version);
    return 0;

/* INIT_ERROR: */
/*     unregisterHooks(); */
/*     return 1; */
}

void cleanup(void)
{
    PSID_log(-1, "%s: %s\n", name, __func__);
    unregisterHooks();
    PSID_log(-1, "%s: Done\n", name);
}

char * help(void)
{
    char *helpText = "\tSome dummy plugin mimicking psconfig usage.\n";

    return strdup(helpText);
}

char *set(char *key, char *value)
{
    const pluginConfigDef_t *thisConfDef = pluginConfig_getDef(config, key);

    if (!thisConfDef) return strdup("\nUnknown option\n");

    if (pluginConfig_getDef(config, key))
	return strdup("\nIllegal value\n");

    if (!strcmp(key, "DebugMask") && pluginConfig_addStr(config, key, value)) {
	long mask = pluginConfig_getNum(config, key);
	PSID_log(-1, "%s: debugMask now %#lx\n", __func__, mask);
    } else {
	return strdup("\nPermission denied\n");
    }

    return NULL;
}

char *unset(char *key)
{
    if (!strcmp(key, "DebugMask")) {
	pluginConfig_remove(config, key);
	long mask = 0;
	PSID_log(-1, "%s: debugMask now %#lx\n", __func__, mask);
    } else {
	return strdup("Permission denied\n");
    }

    return NULL;
}

static char *printSets(PSnodes_ID_t node, char *tag, uint16_t numNUMA,
		       PSCPU_set_t *sets, uint16_t setSize)
{
    StrBuffer_t strBuf = {
	.buf = NULL,
	.bufSize = 0 };
    char line[80];

    addStrBuf("\n", &strBuf);
    addStrBuf(tag, &strBuf);
    if (node != PSC_getMyID()) {
	snprintf(line, sizeof(line), " (for node %d)", node);
	addStrBuf(line, &strBuf);
    }
    snprintf(line, sizeof(line), ": %d devices\n", setSize);
    addStrBuf(line, &strBuf);

    if (!sets) {
	addStrBuf("\t<none>\n", &strBuf);
	return strBuf.buf;
    }

    for (uint16_t dom = 0; dom < numNUMA; dom++) {
	snprintf(line, sizeof(line), "\t%d\t%s\n", dom,
		 PSCPU_print_part(sets[dom], PSCPU_bytesForCPUs(setSize)));
	addStrBuf(line, &strBuf);
    }

    return strBuf.buf;
}

static PSnodes_ID_t getNode(char *key)
{
    PSnodes_ID_t node = -1;
    if (strlen(key) > 3 && key[3] == '_') {
	sscanf(key + 4, "%hd", &node);
    }
    if (!PSC_validNode(node)) node = PSC_getMyID();

    return node;
}

char *show(char *key)
{
    StrBuffer_t strBuf = {
	.buf = NULL,
	.bufSize = 0 };
    char *val;

    if (!key) {
	/* Show the whole configuration */
	int maxKeyLen = pluginConfig_maxKeyLen(config);

	addStrBuf("\n", &strBuf);
	for (int i = 0; confDef[i].name; i++) {
	    const char *cName = confDef[i].name;
	    char line[160];
	    val = pluginConfig_getStr(config, cName);
	    snprintf(line, sizeof(line), "%*s = %s\n", maxKeyLen+2, cName, val);
	    addStrBuf(line, &strBuf);
	}
    } else if (!(strncmp(key, "cpu", strlen("cpu")))) {
	PSnodes_ID_t node = getNode(key);
	return printSets(node, "HW threads", PSIDnodes_numNUMADoms(node),
			 PSIDnodes_CPUSets(node), PSIDnodes_getNumThrds(node));
    } else if (!(strncmp(key, "gpu", strlen("gpu")))) {
	PSnodes_ID_t node = getNode(key);
	return printSets(node, "GPUs", PSIDnodes_numNUMADoms(node),
			 PSIDnodes_GPUSets(node), PSIDnodes_numGPUs(node));
    } else if (!(strncmp(key, "nic", strlen("nic")))) {
	PSnodes_ID_t node = getNode(key);
	return printSets(node, "NICs", PSIDnodes_numNUMADoms(node),
			 PSIDnodes_NICSets(node), PSIDnodes_numNICs(node));
    } else if (!(strncmp(key, "all", strlen("all")))) {
	PSnodes_ID_t node = getNode(key);
	char *tmp;
	tmp = printSets(node, "HW threads", PSIDnodes_numNUMADoms(node),
			PSIDnodes_CPUSets(node), PSIDnodes_getNumThrds(node));
	addStrBuf(tmp, &strBuf);
	free(tmp);
	tmp = printSets(node, "GPUs", PSIDnodes_numNUMADoms(node),
			PSIDnodes_GPUSets(node), PSIDnodes_numGPUs(node));
	addStrBuf(tmp, &strBuf);
	free(tmp);
	tmp = printSets(node, "NICs", PSIDnodes_numNUMADoms(node),
			PSIDnodes_NICSets(node), PSIDnodes_numNICs(node));
	addStrBuf(tmp, &strBuf);
	free(tmp);
    } else if ((val = pluginConfig_getStr(config, key))) {
	addStrBuf("\n", &strBuf);
	addStrBuf(key, &strBuf);
	addStrBuf(" = ", &strBuf);
	addStrBuf(val, &strBuf);
	addStrBuf("\n", &strBuf);
    }

    return strBuf.buf;
}
