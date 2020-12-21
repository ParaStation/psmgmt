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
#include "pscommon.h"
#include "psprotocol.h"
#include "pspluginprotocol.h"
#include "psserial.h"

#include "psidcomm.h"
#include "psidhook.h"
#include "psidnodes.h"
#include "psidplugin.h"
#include "psidutil.h"

#include "pluginmalloc.h"

#include "nodeinfolog.h"
#include "nodeinfoconfig.h"
#include "nodeinfotypes.h"

/** psid plugin requirements */
char name[] = "nodeinfo";
int version = 1;
int requiredAPI = 129;
plugin_dep_t dependencies[] = { { NULL, 0 } };

/** Backup of handler for PSP_PLUG_NODEINFO messages */
static handlerFunc_t handlerBackup = NULL;

static void addCPUMapData(PS_SendDB_t *data)
{
    int16_t numThrds = PSIDnodes_getNumThrds(PSC_getMyID());
    if (numThrds <= 0) return;

    addUint8ToMsg(PSP_NODEINFO_CPUMAP, data);
    addUint16ToMsg(numThrds, data);
    for (uint16_t thrd = 0; thrd < numThrds; thrd++) {
	addInt16ToMsg(PSIDnodes_mapCPU(PSC_getMyID(), thrd), data);
    }
}

bool handleCPUMapData(char **ptr, PSnodes_ID_t sender)
{
    uint16_t numThrds;
    getUint16(ptr, &numThrds);

    if (PSIDnodes_clearCPUMap(sender) < 0) return false;

    for (uint16_t thrd = 0; thrd < numThrds; thrd++) {
	int16_t mappedThrd;
	getInt16(ptr, &mappedThrd);
	if (PSIDnodes_appendCPUMap(sender, mappedThrd) < 0) return false;
    }

    return true;
}

static void addSetsData(PSP_NodeInfo_t type, PSCPU_set_t *sets,
			int16_t setSize, PS_SendDB_t *data)
{
    uint16_t numNUMA = PSIDnodes_numNUMADoms(PSC_getMyID());
    if (!numNUMA || setSize <= 0 || !sets) return;

    addUint8ToMsg(type, data);
    addUint16ToMsg(numNUMA, data);
    addInt16ToMsg(setSize, data);
    uint16_t nBytes = PSCPU_bytesForCPUs(setSize);
    for (uint16_t dom = 0; dom < numNUMA; dom++) {
	uint8_t setBuf[nBytes];
	PSCPU_extract(setBuf, sets[dom], nBytes);
	for (uint16_t b = 0; b < nBytes; b++) addUint8ToMsg(setBuf[b], data);
    }
}

static bool handleSetData(char **ptr, PSnodes_ID_t sender,
			  int setSetSize(PSnodes_ID_t, short),
			  int setSets(PSnodes_ID_t, PSCPU_set_t *))
{
    uint16_t numNUMA;
    getUint16(ptr, &numNUMA);
    if (!PSIDnodes_numNUMADoms(sender)) {
	PSIDnodes_setNumNUMADoms(sender, numNUMA);
    } else if (PSIDnodes_numNUMADoms(sender) != numNUMA) {
	mlog("%s: mismatch in numNUMA %d/%d\n", __func__, numNUMA,
	     PSIDnodes_numNUMADoms(sender));
	return false;
    }

    int16_t setSize;
    getInt16(ptr, &setSize);
    if (setSetSize) setSetSize(sender, setSize);

    PSCPU_set_t *sets = malloc(numNUMA * sizeof(*sets));
    uint16_t nBytes = PSCPU_bytesForCPUs(setSize);
    for (uint16_t dom = 0; dom < numNUMA; dom++) {
	uint8_t setBuf[nBytes];
	for (uint16_t b = 0; b < nBytes; b++) getUint8(ptr, &setBuf[b]);
	PSCPU_clrAll(sets[dom]);
	PSCPU_inject(sets[dom], setBuf, nBytes);
    }
    setSets(sender, sets);

    return true;
}

/**
 * @brief Send nodeinfo data
 *
 * Send the nodeinfo data of the local node to node @a node.
 *
 * @param node Destination node
 *
 * @return No return value
 */
static void sendNodeInfoData(PSnodes_ID_t node)
{
    PS_SendDB_t data;

    if (!PSC_validNode(node)) {
	mlog("%s: invalid node id %i\n", __func__, node);
	return;
    }

    initFragBuffer(&data, PSP_PLUG_NODEINFO, 0);
    setFragDest(&data, PSC_getTID(node, 0));

    addCPUMapData(&data);
    addSetsData(PSP_NODEINFO_NUMANODES, PSIDnodes_CPUSets(PSC_getMyID()),
		PSIDnodes_getNumThrds(PSC_getMyID()), &data);
    addSetsData(PSP_NODEINFO_GPU, PSIDnodes_GPUSets(PSC_getMyID()),
		PSIDnodes_numGPUs(PSC_getMyID()), &data);
    addSetsData(PSP_NODEINFO_NIC, PSIDnodes_NICSets(PSC_getMyID()),
		PSIDnodes_numNICs(PSC_getMyID()), &data);
    addUint8ToMsg(0, &data); // declare end of message

    /* send the messages */
    sendFragMsg(&data);
}

static void broadcastMapData(void)
{
    PS_SendDB_t data;

    mdbg(NODEINFO_LOG_VERBOSE,"%s: distribute map informaton\n", __func__);

    initFragBuffer(&data, PSP_PLUG_NODEINFO, 0);
    for (PSnodes_ID_t n = 0; n < PSC_getNrOfNodes(); n++) {
	if (n == PSC_getMyID() || !PSIDnodes_isUp(n)) continue;
	setFragDest(&data, PSC_getTID(n, 0));
    }
    addCPUMapData(&data);
    addUint8ToMsg(0, &data); // declare end of message

    /* send the messages */
    sendFragMsg(&data);
}

static void handleNodeInfoData(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *rData)
{
    PSnodes_ID_t sender = PSC_getID(msg->header.sender);
    char *ptr = rData->buf;
    PSP_NodeInfo_t type;

    getUint8(&ptr, &type);
    while (type) {
	switch (type) {
	case PSP_NODEINFO_CPUMAP:
	    if (!handleCPUMapData(&ptr, sender)) return;
	    break;
	case PSP_NODEINFO_NUMANODES:
	    if (!handleSetData(&ptr, sender, NULL,
			       PSIDnodes_setCPUSets)) return;
	    break;
	case PSP_NODEINFO_GPU:
	    if (!handleSetData(&ptr, sender, PSIDnodes_setNumGPUs,
			       PSIDnodes_setGPUSets)) return;
	    break;
	case PSP_NODEINFO_NIC:
	    if (!handleSetData(&ptr, sender, PSIDnodes_setNumNICs,
			       PSIDnodes_setNICSets)) return;
	    break;
	case PSP_NODEINFO_REQ:
	    sendNodeInfoData(sender);
	    break;
	default:
	    mlog("%s: unknown type %d\n", __func__, type);
	    return;
	}
	/* Peek into next type */
	getUint8(&ptr, &type);
    }
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

    sendNodeInfoData(id);

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
    PSIDnodes_setCPUSets(id, NULL);
    PSIDnodes_setNumGPUs(id, 0);
    PSIDnodes_setGPUSets(id, NULL);
    PSIDnodes_setNumNICs(id, 0);
    PSIDnodes_setNICSets(id, NULL);

    return 1;
}

static int handleDistInfo(void *infoType)
{
    if (!infoType) return 1;

    PSP_Optval_t type = *(PSP_Optval_t *) infoType;

    switch (type) {
    case PSP_OP_CPUMAP:
	broadcastMapData();
	break;
    default:
	mlog("%s: unsupported option type %#04x\n", __func__, type);
    }


    return 1;
}

static void checkOtherNodes(void)
{
    PS_SendDB_t data;
    int numPartners = 0;

    initFragBuffer(&data, PSP_PLUG_NODEINFO, 0);
    for (PSnodes_ID_t n = 0; n < PSC_getNrOfNodes(); n++) {
	if (n == PSC_getMyID() || !PSIDnodes_isUp(n)) continue;
	setFragDest(&data, PSC_getTID(n, 0));
	numPartners++;
    }

    if (!numPartners) return; // no other nodes are up yet

    addUint8ToMsg(PSP_NODEINFO_REQ, &data);  // request info
    addCPUMapData(&data);
    addSetsData(PSP_NODEINFO_NUMANODES, PSIDnodes_CPUSets(PSC_getMyID()),
		PSIDnodes_getNumThrds(PSC_getMyID()), &data);
    addSetsData(PSP_NODEINFO_GPU, PSIDnodes_GPUSets(PSC_getMyID()),
		PSIDnodes_numGPUs(PSC_getMyID()), &data);
    addSetsData(PSP_NODEINFO_NIC, PSIDnodes_NICSets(PSC_getMyID()),
		PSIDnodes_numNICs(PSC_getMyID()), &data);
    addUint8ToMsg(0, &data);  // declare end of message

    /* send the messages */
    sendFragMsg(&data);
}

static void handleNodeInfoMsg(DDBufferMsg_t *msg)
{
    recvFragMsg((DDTypedBufferMsg_t *)msg, handleNodeInfoData);
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
	if (verbose) mlog("%s: unregister 'PSIDHOOK_NODE_UP' failed\n",
			  __func__);
    }
    if (!PSIDhook_del(PSIDHOOK_NODE_DOWN, handleNodeDown)) {
	if (verbose) mlog("%s: unregister 'PSIDHOOK_NODE_DOWN' failed\n",
			  __func__);
    }
    if (!PSIDhook_del(PSIDHOOK_DIST_INFO, handleDistInfo)) {
	if (verbose) mlog("%s: unregister 'PSIDHOOK_DIST_INFO' failed\n",
			  __func__);
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

    if (!initSerial(0, sendMsg)) {
	mlog("%s: initSerial() failed\n", __func__);
	goto INIT_ERROR;
    }

    handlerBackup = PSID_registerMsg(PSP_PLUG_NODEINFO, handleNodeInfoMsg);

    mlog("(%i) successfully started\n", version);

    /* Test if we were loaded late (far after psid startu) and send/req info */
    checkOtherNodes();

    return 0;

INIT_ERROR:
    PSID_clearMsg(PSP_PLUG_NODEINFO);
    unregisterHooks(false);
    finalizeSerial();

    return 1;
}

void cleanup(void)
{
    if (handlerBackup) {
	PSID_registerMsg(PSP_PLUG_NODEINFO, handlerBackup);
    } else {
	PSID_clearMsg(PSP_PLUG_NODEINFO);
    }
    unregisterHooks(true);
    finalizeSerial();
    freeConfig(&nodeInfoConfig);

    mlog("...Bye.\n");

    /* release the logger */
    logger_finalize(nodeInfoLogger);
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
			 PSIDnodes_CPUSets(node), PSIDnodes_getNumThrds(node));
    } else if (!(strncmp(key, "gpu", strlen("gpu")))) {
	PSnodes_ID_t node = getNode(key);
	return printSets(node, "GPUs", PSIDnodes_numNUMADoms(node),
			 PSIDnodes_GPUSets(node), PSIDnodes_numGPUs(node));
    } else if (!(strncmp(key, "nic", strlen("nic")))) {
	PSnodes_ID_t node = getNode(key);
	return printSets(node, "NICs", PSIDnodes_numNUMADoms(node),
			 PSIDnodes_NICSets(node), PSIDnodes_numNICs(node));
    } else if (!(strncmp(key, "map", strlen("map")))) {
	return showMap(key);
    } else if (!(strncmp(key, "all", strlen("all")))) {
	PSnodes_ID_t node = getNode(key);
	char *tmp;
	tmp = printSets(node, "HW threads", PSIDnodes_numNUMADoms(node),
			PSIDnodes_CPUSets(node), PSIDnodes_getNumThrds(node));
	addStrBuf(tmp, &strBuf);
	free(tmp);
	tmp = showMap(key);
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
    } else if ((val = getConfValueC(&nodeInfoConfig, key))) {
	addStrBuf("\n", &strBuf);
	addStrBuf(key, &strBuf);
	addStrBuf(" = ", &strBuf);
	addStrBuf(val, &strBuf);
	addStrBuf("\n", &strBuf);
    }

    return strBuf.buf;
}
