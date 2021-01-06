/*
 * ParaStation
 *
 * Copyright (C) 2020-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pluginlog.h"
#include "pluginmalloc.h"
#include "pluginpsconfig.h"

#include "psidutil.h"
#include "psidnodes.h"
#include "psidplugin.h"

#include "plugin.h"

int requiredAPI = 129;

char name[] = "pluginpsconfig";

int version = 100;

plugin_dep_t dependencies[] = {
    { NULL, 0 } };

pluginConfig_t config;

/** Defintion of the configuration */
const pluginConfigDef_t confDef[] = {
    { "DebugMask", PLUGINCONFIG_VALUE_NUM, "Mask to steer debug output" },
    { "DebugMask2", PLUGINCONFIG_VALUE_NUM, "Mask to steer fine debug output" },
    { "GPUDevices", PLUGINCONFIG_VALUE_LST,
      "PCIe IDs of NICs (\"vendorID:deviceID[:subVendorID:subDeviceID]\"" },
    { "GPUSort", PLUGINCONFIG_VALUE_STR,
      "GPUs' sort order (\"BIOS\"|\"PCI\")" },
    { "NICDevices", PLUGINCONFIG_VALUE_LST,
      "PCIe IDs of NICs (\"vendorID:deviceID[:subVendorID:subDeviceID]\"" },
    { "NICSort", PLUGINCONFIG_VALUE_STR,
      "NICs' sort order (\"BIOS\"|\"PCI\")" },
    { NULL, PLUGINCONFIG_VALUE_NONE, NULL }
};

static void unregisterHooks(void)
{}

pluginConfig_t config = NULL;

int initialize(void)
{
    initPluginLogger(NULL, NULL);

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
    StrBuffer_t strBuf = {
	.buf = NULL,
	.bufSize = 0 };

    addStrBuf("\tSome dummy plugin mimicking psconfig usage.\n", &strBuf);
    addStrBuf("\n# configuration options #\n\n", &strBuf);

    int maxKeyLen = pluginConfig_maxKeyLen(config) + 2;
    char keyStr[maxKeyLen + 1];
    for (size_t i = 0; confDef[i].name; i++) {
	snprintf(keyStr, sizeof(keyStr), "%*s", maxKeyLen, confDef[i].name);
	addStrBuf(keyStr, &strBuf);
	char typeStr[16];
	snprintf(typeStr, sizeof(typeStr), "%10s",
		 pluginConfig_typeStr(confDef[i].type));
	addStrBuf(typeStr, &strBuf);
	addStrBuf("  ", &strBuf);
	addStrBuf(confDef[i].desc, &strBuf);
	addStrBuf("\n", &strBuf);
    }

    return strBuf.buf;
}

char *set(char *key, char *value)
{
    const pluginConfigDef_t *thisConfDef = pluginConfig_getDef(config, key);

    if (!strcmp(key, "config")) {
	pluginConfig_destroy(config);

	pluginConfig_new(&config);
	pluginConfig_setDef(config, confDef);
	pluginConfig_load(config, value);

	return NULL;
    }

    if (!thisConfDef) return strdup(" Unknown option\n");

    if (!strcmp(key, "DebugMask")) {
	if (pluginConfig_addStr(config, key, value)) {
	    long mask = pluginConfig_getNum(config, key);
	    PSID_log(-1, "%s: debugMask now %#lx\n", __func__, mask);
	} else {
	    return strdup(" Illegal value\n");
	}
    } else if (thisConfDef->type == PLUGINCONFIG_VALUE_LST) {
	if (*value == '+') {
	    value++;
	    pluginConfig_addToLst(config, key, value);
	} else {
	    pluginConfig_remove(config, key);
	    pluginConfig_addToLst(config, key, value);
	}
    } else if (!pluginConfig_addStr(config, key, value)) {
	return strdup(" Illegal value\n");
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
	pluginConfig_remove(config, key);
    }

    return NULL;
}

char *show(char *key)
{
    StrBuffer_t strBuf = {
	.buf = NULL,
	.bufSize = 0 };

    if (!key) {
	/* Show the whole configuration */
	addStrBuf("\n", &strBuf);
	pluginConfig_traverse(config, pluginConfig_showVisitor, &strBuf);
    } else if (!pluginConfig_showKeyVal(config, key, &strBuf)) {
	addStrBuf(" ", &strBuf);
	addStrBuf(key, &strBuf);
	addStrBuf(" is unknown\n", &strBuf);
    }

    return strBuf.buf;
}
