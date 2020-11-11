/*
 * ParaStation
 *
 * Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include "plugin.h"
#include "psprotocol.h"

#include "psidhook.h"
#include "psidnodes.h"
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
{
    // @todo
}

static int handleNodeUp(void *nodeID)
{
    if (!nodeID) return 1;

    PSnodes_ID_t id = *(PSnodes_ID_t *)nodeID;

    if (!PSC_validNode(id)) {
	mlog("%s: invalid node id %i\n", __func__, id);
	return 1;
    }

    if (id == PSC_getMyID()) return 1;

    sendTopologyData(id);

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

    if (id == PSC_getMyID()) return 1;

    // reset/cleanup CPUmap and hardware topology data
    PSIDnodes_setNumNUMADoms(id, 0);
    PSIDnodes_setCPUSet(id, NULL);
    PSIDnodes_setNumGPUs(id, 0);
    PSIDnodes_setGPUSet(id, NULL);
    PSIDnodes_setNumNICs(id, 0);
    PSIDnodes_setNICSet(id, NULL);

    return 1;
}

static int handleDistInfo(void *infoType)
{
    if (!infoType) return 1;

    PSP_Optval_t type = *(PSP_Optval_t *) infoType;

    // @todo send info to be updated to all nodes
    mlog("%s: going to distribute info of type %d\n", __func__, type);

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

    /* init default configuration (no config file yet; wait for psconfig?) */
    initNodeInfoConfig();

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
    StrBuffer_t strBuf = {
	.buf = NULL,
	.bufSize = 0 };

    addStrBuf("\tDistribute, collect and store info on node configurations\n\n",
	      &strBuf);
    addStrBuf("\tHW threads are displayed under key 'cpu'\n", &strBuf);
    addStrBuf("\tGPUs are displayed under key 'gpu'\n", &strBuf);
    addStrBuf("\tNICs are displayed under key 'nic'\n", &strBuf);
    addStrBuf("\tCPU-maps are displayed under key 'map'\n", &strBuf);
    addStrBuf("\tTo display all HW information use key 'all'\n", &strBuf);
    addStrBuf("\n# configuration options #\n", &strBuf);

    int maxKeyLen = getMaxKeyLen(confDef);
    for (int i = 0; confDef[i].name; i++) {
	char type[10], line[160];
	snprintf(type, sizeof(type), "<%s>", confDef[i].type);
	snprintf(line, sizeof(line), "%*s %10s  %s\n",
		 maxKeyLen+2, confDef[i].name, type, confDef[i].desc);
	addStrBuf(line, &strBuf);
    }

    return strBuf.buf;
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

static char *showMap(char *key)
{
    StrBuffer_t strBuf = {
	.buf = NULL,
	.bufSize = 0 };
    PSnodes_ID_t node = getNode(key);
    char line[80];

    addStrBuf("\nMap", &strBuf);
    if (node != PSC_getMyID()) {
	snprintf(line, sizeof(line), " (for node %d)", node);
	addStrBuf(line, &strBuf);
    }
    addStrBuf(":\n\t", &strBuf);
    for (uint16_t thrd = 0; thrd < PSIDnodes_getNumThrds(node); thrd++) {
	snprintf(line, sizeof(line), " %d->%d", thrd,
		 PSIDnodes_mapCPU(node, thrd));
	addStrBuf(line, &strBuf);
    }
    addStrBuf("\n", &strBuf);

    return strBuf.buf;
}


char *show(char *key)
{
    StrBuffer_t strBuf = {
	.buf = NULL,
	.bufSize = 0 };
    char *val;

    if (!key) {
	/* Show the whole configuration */
	int maxKeyLen = getMaxKeyLen(confDef);

	addStrBuf("\n", &strBuf);
	for (int i = 0; confDef[i].name; i++) {
	    char *cName = confDef[i].name, line[160];
	    val = getConfValueC(&nodeInfoConfig, cName);
	    snprintf(line, sizeof(line), "%*s = %s\n", maxKeyLen+2, cName, val);
	    addStrBuf(line, &strBuf);
	}
    } else if (!(strncmp(key, "cpu", strlen("cpu")))) {
	PSnodes_ID_t node = getNode(key);
	return printSets(node, "HW threads", PSIDnodes_numNUMADoms(node),
			 PSIDnodes_CPUSet(node), PSIDnodes_getNumThrds(node));
    } else if (!(strncmp(key, "gpu", strlen("gpu")))) {
	PSnodes_ID_t node = getNode(key);
	return printSets(node, "GPUs", PSIDnodes_numNUMADoms(node),
			 PSIDnodes_GPUSet(node), PSIDnodes_numGPUs(node));
    } else if (!(strncmp(key, "nic", strlen("nic")))) {
	PSnodes_ID_t node = getNode(key);
	return printSets(node, "NICs", PSIDnodes_numNUMADoms(node),
			 PSIDnodes_NICSet(node), PSIDnodes_numNICs(node));
    } else if (!(strncmp(key, "map", strlen("map")))) {
	return showMap(key);
    } else if (!(strncmp(key, "all", strlen("all")))) {
	PSnodes_ID_t node = getNode(key);
	char *tmp;
	tmp = printSets(node, "HW threads", PSIDnodes_numNUMADoms(node),
			PSIDnodes_CPUSet(node), PSIDnodes_getNumThrds(node));
	addStrBuf(tmp, &strBuf);
	free(tmp);
	tmp = showMap(key);
	addStrBuf(tmp, &strBuf);
	free(tmp);
	tmp = printSets(node, "GPUs", PSIDnodes_numNUMADoms(node),
			PSIDnodes_GPUSet(node), PSIDnodes_numGPUs(node));
	addStrBuf(tmp, &strBuf);
	free(tmp);
	tmp = printSets(node, "NICs", PSIDnodes_numNUMADoms(node),
			PSIDnodes_NICSet(node), PSIDnodes_numNICs(node));
	addStrBuf(tmp, &strBuf);
	free(tmp);
    } else if ((val = getConfValueC(&nodeInfoConfig, key))) {
	addStrBuf("\n", &strBuf);
	addStrBuf(key, &strBuf);
	addStrBuf(" = ", &strBuf);
	addStrBuf(val, &strBuf);
	addStrBuf("\n", &strBuf);
    }

    return strBuf.buf;
}
