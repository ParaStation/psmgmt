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
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>

#include "plugin.h"
#include "psprotocol.h"

#include "psidhook.h"
#include "psidplugin.h"
#include "psidutil.h"

#include "pluginmalloc.h"

#include "nodeinfolog.h"
#include "nodeinfoconfig.h"

/** psid plugin requirements */
char name[] = "nodeinfo";
int version = 1;
int requiredAPI = 129;
plugin_dep_t dependencies[] = { { NULL, 0 } };

static void sendTopologyData(PSnodes_ID_t id)
{}

static int handleNodeUp(void *nodeID)
{
    if (!nodeID) return 1;

    PSnodes_ID_t id = *(PSnodes_ID_t *)nodeID;

    if (id == PSC_getMyID()) return 1;
    if (!PSC_validNode(id)) {
	mlog("%s: invalid node id %i\n", __func__, id);
	return 1;
    }

    if (id < PSC_getMyID()) {
	sendTopologyData(id);
    } else {
	//nodeStatus[id].state = NResUnknown;
    }

    return 1;
}

static int handleNodeDown(void *nodeID)
{
    if (!nodeID) return 1;

    PSnodes_ID_t id = *(PSnodes_ID_t *)nodeID;

    if (!PSC_validNode(id)) {
	mlog("%s: invalid node id %i\n", __func__, id);
	return 1;
    }

    // @todo cleanup topology data

    return 1;
}

static int handleDistInfo(void *infoType)
{
    if (!infoType) return 1;

    PSP_Optval_t type = *(PSP_Optval_t *) infoType;

    // @todo send info to be updated to all nodes
    mlog("%s: goind to distribute info of type %d\n", __func__, type);

    return 1;
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
    if (!PSIDhook_del(PSIDHOOK_NODE_UP, handleNodeUp)) {
	mlog("%s: unregister 'PSIDHOOK_NODE_UP' failed\n", __func__);
    }
    if (!PSIDhook_del(PSIDHOOK_NODE_DOWN, handleNodeDown)) {
	mlog("%s: unregister 'PSIDHOOK_NODE_DOWN' failed\n", __func__);
    }
    if (!PSIDhook_del(PSIDHOOK_DIST_INFO, handleDistInfo)) {
	mlog("%s: unregister 'PSIDHOOK_DIST_INFO' failed\n", __func__);
    }
}

int initialize(void)
{
    /* init logging facility */
    initNodeInfoLogger(name);

    /* adapt the debug mask */
    int mask = getConfValueI(&nodeInfoConfig, "DEBUG_MASK");
    maskNodeInfoLogger(mask);
    mdbg(NODEINFO_LOG_VERBOSE, "%s: debugMask set to %#x\n", __func__, mask);

    // @todo setup structures to store remote topology data

    // @todo setup data to send

    if (!PSIDhook_add(PSIDHOOK_NODE_UP, handleNodeUp)) {
	mlog("%s: register 'PSIDHOOK_NODE_UP' failed\n", __func__);
	goto INIT_ERROR;
    }
    if (!PSIDhook_add(PSIDHOOK_NODE_DOWN, handleNodeDown)) {
	mlog("%s: register 'PSIDHOOK_NODE_DOWN' failed\n", __func__);
	goto INIT_ERROR;
    }
    if (!PSIDhook_add(PSIDHOOK_DIST_INFO, handleDistInfo)) {
	mlog("%s: register 'PSIDHOOK_DIST_INFO' failed\n", __func__);
	goto INIT_ERROR;
    }

    // @todo register message handler

    mlog("(%i) successfully started\n", version);

    return 0;

INIT_ERROR:
    unregisterHooks(false);
    return 1;
}

void finalize(void)
{
    PSIDplugin_unload(name);
}

void cleanup(void)
{
    unregisterHooks(true);

    // @todo unregister message handler
}

char *help(void)
{
    char *buf = NULL;
    size_t bufSize = 0;
    int maxKeyLen = getMaxKeyLen(confDef);

    str2Buf("\tDistribute, collect and store info on node configurations\n\n",
	    &buf, &bufSize);
    str2Buf("# configuration options #\n", &buf, &bufSize);

    for (int i = 0; confDef[i].name; i++) {
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
	return ustrdup("\nIllegal value\n");

    if (!strcmp(key, "DEBUG_MASK")) {
	addConfigEntry(&nodeInfoConfig, key, val);
	int mask = getConfValueI(&nodeInfoConfig, key);
	maskNodeInfoLogger(mask);
	mlog("%s: debugMask now %#x\n", __func__, mask);
    } else {
	return ustrdup("\nPermission denied\n");
    }

    return NULL;
}

char *unset(char *key)
{
    if (!strcmp(key, "DEBUG_MASK")) {
	unsetConfigEntry(&nodeInfoConfig, confDef, key);
	int mask = getConfValueI(&nodeInfoConfig, key);
	maskNodeInfoLogger(mask);
	mlog("%s: debugMask now %#x\n", __func__, mask);
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
	    val = getConfValueC(&nodeInfoConfig, cName);

	    snprintf(line, sizeof(line), "%*s = %s\n", maxKeyLen+2, cName, val);
	    str2Buf(line, &buf, &bufSize);
	}
    } else if ((val = getConfValueC(&nodeInfoConfig, key))) {
	str2Buf("\n", &buf, &bufSize);
	str2Buf(key, &buf, &bufSize);
	str2Buf(" = ", &buf, &bufSize);
	str2Buf(val, &buf, &bufSize);
	str2Buf("\n", &buf, &bufSize);
    }

    return buf;
}
