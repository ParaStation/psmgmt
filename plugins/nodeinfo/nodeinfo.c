/*
 * ParaStation
 *
 * Copyright (C) 2020-2021 ParTec Cluster Competence Center GmbH, Munich
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
#include "psidhw.h"
#include "psidnodes.h"
#include "psidplugin.h"
#include "psidutil.h"

#include "pluginmalloc.h"

#include "nodeinfolog.h"
#include "nodeinfoconfig.h"
#include "nodeinfotypes.h"

#include "nodeinfo.h"

/** psid plugin requirements */
char name[] = "nodeinfo";
int version = 2;
int requiredAPI = 130;
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
    if (!numNUMA || setSize < 0 || !sets) return;

    addUint8ToMsg(type, data);
    addUint16ToMsg(numNUMA, data);
    addInt16ToMsg(setSize, data);
    if (!setSize) return;
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

    if (!setSize) {
	setSets(sender, NULL);
	return true;
    }

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

void sendNodeInfoData(PSnodes_ID_t node)
{
    PS_SendDB_t data;

    if (!PSC_validNode(node)) {
	mlog("%s: invalid node id %i\n", __func__, node);
	return;
    }

    initFragBuffer(&data, PSP_PLUG_NODEINFO, 0);
    if (node == PSC_getMyID()) {
	// broadcast to all nodes
	for (PSnodes_ID_t n = 0; n < PSC_getNrOfNodes(); n++) {
	    if (n == PSC_getMyID() || !PSIDnodes_isUp(n)) continue;
	    setFragDest(&data, PSC_getTID(n, 0));
	}
    } else {
	setFragDest(&data, PSC_getTID(node, 0));
    }

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

/** List of PCIe IDs identifying GPU devices */
static PCI_ID_t *GPU_IDs = NULL;

static bool GPU_PCIeOrder = true;

void updateGPUInfo(void)
{
    uint16_t numGPUs = GPU_IDs ? PSIDhw_getNumPCIDevs(GPU_IDs) : 0;
    PSIDnodes_setNumGPUs(PSC_getMyID(), numGPUs);
    if (numGPUs) {
	PSCPU_set_t *GPUsets = PSIDhw_getPCISets(GPU_PCIeOrder, GPU_IDs);
	PSIDnodes_setGPUSets(PSC_getMyID(), GPUsets);
    }
}

/** List of PCIe IDs identifying high performance NIC devices */
static PCI_ID_t *NIC_IDs = NULL;

static bool NIC_PCIeOrder = false; // Use BIOS order

void updateNICInfo(void)
{
    uint16_t numNICs = NIC_IDs ? PSIDhw_getNumPCIDevs(NIC_IDs) : 0;
    PSIDnodes_setNumNICs(PSC_getMyID(), numNICs);
    if (numNICs) {
	PSCPU_set_t *NICsets = PSIDhw_getPCISets(NIC_PCIeOrder, NIC_IDs);
	PSIDnodes_setNICSets(PSC_getMyID(), NICsets);
    }
}

static inline size_t lstLen(char **lst)
{
    size_t len = 0;
    for (char **l = lst; l && *l; l++) len++;
    return len;
}

static bool pairFromStr(char *str, uint16_t *val1, uint16_t *val2)
{
    char *end;
    /* first element */
    long v1 = strtol(str, &end, 16);
    if (*end) {
	mlog("%s: illegal value '%s'\n", __func__, str);
	return false;
    }
    if (v1 > UINT16_MAX) {
	mlog("%s: value %s too large\n", __func__, str);
	return false;
    }
    /* second element */
    str = end + 1;
    long v2 = strtol(str, &end, 16);
    if (*end) {
	mlog("%s: illegal value '%s'\n", __func__, str);
	return false;
    }
    if (v2 > UINT16_MAX) {
	mlog("%s: value %s too large\n", __func__, str);
	return false;
    }
    /* now that both elements are valid do the assignment */
    *val1 = v1;
    *val2 = v2;

    return true;
}

/**
 * @brief Convert string into PCIe ID
 *
 * Convert the character array @a IDStr formatted according to
 * vendorID:deviceID[:subVendorID:subDeviceID] and into a corresponding
 * PCIe ID and store the result to @a id.
 *
 * @param id Pointer to the PCIe ID to store the result
 *
 * @param IDStr Character array to convert
 *
 * @return On success return true; or false in case of error
 */
static bool IDFromStr(PCI_ID_t *id, char *IDStr)
{
    char *myStr = strdup(IDStr);
    if (!myStr) {
	mlog("%s: no memory\n", __func__);
	return false;
    }
    /* prepare first pair */
    char *colon = strchr(myStr, ':');
    if (!colon) {
	mlog("%s: wrong format\n", __func__);
	goto error;
    }
    *colon = '\0';
    colon++;
    colon = strchr(colon, ':');
    if (colon) {
	*colon = '\0';
	colon++;
    }
    if (!pairFromStr(myStr, &id->vendor_id, &id->device_id)) goto error;
    if (colon) {
	char *subStr = colon;
	colon = strchr(subStr, ':');
	if (!colon) {
	    mlog("%s: wrong subsystem format\n", __func__);
	    goto error;
	}
	*colon = '\0';
	if (!pairFromStr(subStr, &id->subvendor_id, &id->subdevice_id)) {
	    goto error;
	}
    } else {
	id->subvendor_id = 0;
	id->subdevice_id = 0;
    }

    free(myStr);
    return true;

error:
    free(myStr);
    return false;
}

static void PCIIDsFromLst(PCI_ID_t **PCI_IDs, const pluginConfigVal_t *val)
{
    if (!PCI_IDs) return;
    if (!val) {
	// unset
	if (*PCI_IDs) free(*PCI_IDs);
	*PCI_IDs = NULL;
    }
    if (val->type != PLUGINCONFIG_VALUE_LST) {
	mlog("%s: value not list\n", __func__);
	return;
    }

    size_t len = lstLen(val->val.lst);
    PCI_ID_t *newIDs = realloc(*PCI_IDs, (len + 1) * sizeof(**PCI_IDs));
    if (!newIDs) {
	mlog("%s: no memory\n", __func__);
	return;
    }
    size_t id = 0;
    for (char **idStr = val->val.lst; idStr && *idStr; idStr++) {
	if (IDFromStr(&newIDs[id], *idStr)) id++;
    }
    newIDs[id] = (PCI_ID_t){ 0, 0, 0, 0 };

    *PCI_IDs = newIDs;
}

static bool getOrder(const pluginConfigVal_t *val)
{
    if (val->type != PLUGINCONFIG_VALUE_STR) {
	mlog("%s: value not string\n", __func__);
	return false;
    }

    if (!strcasecmp(val->val.str, "PCI")) return true;
    if (!strcasecmp(val->val.str, "BIOS")) return false;

    mlog("%s: Illegal value '%s'\n", __func__, val->val.str);
    return false;
}

static bool evalValue(const char *key, const pluginConfigVal_t *val,
		      const void *info)
{
    if (!strcmp(key, "DebugMask")) {
	uint32_t mask = val ? val->val.num : 0;
	maskNodeInfoLogger(mask);
	mdbg(NODEINFO_LOG_VERBOSE, "debugMask set to %#x\n", mask);
    } else if (!strcmp(key, "GPUDevices")) {
	PCIIDsFromLst(&GPU_IDs, val);
	updateGPUInfo();
    } else if (!strcmp(key, "GPUSort")) {
	GPU_PCIeOrder = val ? getOrder(val) : true;
	updateGPUInfo();
    } else if (!strcmp(key, "NICDevices")) {
	PCIIDsFromLst(&NIC_IDs, val);
	updateNICInfo();
    } else if (!strcmp(key, "NICSort")) {
	NIC_PCIeOrder = val ? getOrder(val) : false;
	updateNICInfo();
    } else {
	mlog("%s: unknown key '%s'\n", __func__, key);
    }

    return true;
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

    /* init configuration (depends on psconfig) */
    initNodeInfoConfig();

    /* Activate configuration values */
    pluginConfig_traverse(nodeInfoConfig, evalValue, NULL);

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

    /* Test if we were loaded late (far after psid startup) and send/req info */
    checkOtherNodes();

    return 0;

INIT_ERROR:
    PSID_clearMsg(PSP_PLUG_NODEINFO);
    unregisterHooks(false);
    finalizeSerial();
    finalizeNodeInfoConfig();
    finalizeNodeInfoLogger();

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
    finalizeNodeInfoConfig();

    mlog("...Bye.\n");

    /* release the logger */
    finalizeNodeInfoLogger();
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
    addStrBuf("\n# configuration options #\n\n", &strBuf);

    pluginConfig_helpDesc(nodeInfoConfig, &strBuf);

    return strBuf.buf;
}

char *set(char *key, char *val)
{
    const pluginConfigDef_t *thisDef = pluginConfig_getDef(nodeInfoConfig, key);

    if (!strcmp(key, "update")) {
	// broadcast my info again
	sendNodeInfoData(PSC_getMyID());
	return NULL;
    }

    if (!thisDef) return strdup(" Unknown option\n");

    if (thisDef->type == PLUGINCONFIG_VALUE_LST) {
	if (*val == '+') {
	    val++;
	    pluginConfig_addToLst(nodeInfoConfig, key, val);
	} else {
	    pluginConfig_remove(nodeInfoConfig, key);
	    pluginConfig_addToLst(nodeInfoConfig, key, val);
	}
    } else if (!pluginConfig_addStr(nodeInfoConfig, key, val)) {
	return strdup(" Illegal value\n");
    }
    if (!evalValue(key, pluginConfig_get(nodeInfoConfig, key), NULL)) {
	return strdup(" Illegal value\n");
    }

    return NULL;
}

char *unset(char *key)
{
    pluginConfig_remove(nodeInfoConfig, key);
    evalValue(key, NULL, nodeInfoConfig);

    return NULL;
}

static void printSets(PSnodes_ID_t node, char *tag, uint16_t numNUMA,
		      PSCPU_set_t *sets, uint16_t setSize, StrBuffer_t *strBuf)
{
    char line[80];

    addStrBuf("\n", strBuf);
    addStrBuf(tag, strBuf);
    if (node != PSC_getMyID()) {
	snprintf(line, sizeof(line), " (for node %d)", node);
	addStrBuf(line, strBuf);
    }
    snprintf(line, sizeof(line), ": %d devices\n", setSize);
    addStrBuf(line, strBuf);

    if (!sets) {
	addStrBuf("\t<none>\n", strBuf);
	return;
    }

    for (uint16_t dom = 0; dom < numNUMA; dom++) {
	snprintf(line, sizeof(line), "\t%d\t%s\n", dom,
		 PSCPU_print_part(sets[dom], PSCPU_bytesForCPUs(setSize)));
	addStrBuf(line, strBuf);
    }
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

static void showMap(char *key, StrBuffer_t *strBuf)
{
    PSnodes_ID_t node = getNode(key);
    char line[80];

    addStrBuf("\nMap", strBuf);
    if (node != PSC_getMyID()) {
	snprintf(line, sizeof(line), " (for node %d)", node);
	addStrBuf(line, strBuf);
    }
    addStrBuf(":\n\t", strBuf);
    for (uint16_t thrd = 0; thrd < PSIDnodes_getNumThrds(node); thrd++) {
	snprintf(line, sizeof(line), " %d->%d", thrd,
		 PSIDnodes_mapCPU(node, thrd));
	addStrBuf(line, strBuf);
    }
    addStrBuf("\n", strBuf);
}

void printPCIIDs(PCI_ID_t *id, StrBuffer_t *strBuf)
{
    if (!id) return;
    for (size_t d = 0; id[d].vendor_id; d++) {
	char devStr[80];
	snprintf(devStr, sizeof(devStr),
		 " %04x:%04x", id[d].vendor_id, id[d].device_id);
	if (id[d].subvendor_id || id[d].subdevice_id) {
	    snprintf(devStr + strlen(devStr), sizeof(devStr) - strlen(devStr),
		     ":%04x:%04x", id[d].subvendor_id, id[d].subdevice_id);
	}
	addStrBuf(devStr, strBuf);
    }
    addStrBuf("\n", strBuf);
}

char *show(char *key)
{
    StrBuffer_t strBuf = {
	.buf = NULL,
	.bufSize = 0 };

    if (!key) {
	/* Show the whole configuration */
	addStrBuf("\n", &strBuf);
	pluginConfig_traverse(nodeInfoConfig, pluginConfig_showVisitor,&strBuf);
    } else if (!strncmp(key, "cpu", strlen("cpu"))) {
	PSnodes_ID_t node = getNode(key);
	printSets(node, "HW threads", PSIDnodes_numNUMADoms(node),
		  PSIDnodes_CPUSets(node), PSIDnodes_getNumThrds(node),&strBuf);
    } else if (!strncmp(key, "gpu", strlen("gpu"))) {
	PSnodes_ID_t node = getNode(key);
	printSets(node, "GPUs", PSIDnodes_numNUMADoms(node),
		  PSIDnodes_GPUSets(node), PSIDnodes_numGPUs(node), &strBuf);
    } else if (!strncmp(key, "nic", strlen("nic"))) {
	PSnodes_ID_t node = getNode(key);
	printSets(node, "NICs", PSIDnodes_numNUMADoms(node),
		  PSIDnodes_NICSets(node), PSIDnodes_numNICs(node), &strBuf);
    } else if (!strncmp(key, "map", strlen("map"))) {
	showMap(key, &strBuf);
    } else if (!strncmp(key, "all", strlen("all"))) {
	PSnodes_ID_t node = getNode(key);
	printSets(node, "HW threads", PSIDnodes_numNUMADoms(node),
		  PSIDnodes_CPUSets(node), PSIDnodes_getNumThrds(node),&strBuf);
	showMap(key, &strBuf);
	printSets(node, "GPUs", PSIDnodes_numNUMADoms(node),
		  PSIDnodes_GPUSets(node), PSIDnodes_numGPUs(node), &strBuf);
	printSets(node, "NICs", PSIDnodes_numNUMADoms(node),
		  PSIDnodes_NICSets(node), PSIDnodes_numNICs(node), &strBuf);
    } else if (!strncmp(key, "pci", strlen("pci"))) {
	addStrBuf("\n", &strBuf);
	addStrBuf("PCIe IDs for GPUs:\n", &strBuf);
	printPCIIDs(GPU_IDs, &strBuf);
	addStrBuf("PCIe IDs for NICs:\n", &strBuf);
	printPCIIDs(NIC_IDs, &strBuf);
    } else if (!pluginConfig_showKeyVal(nodeInfoConfig, key, &strBuf)) {
	addStrBuf(" '", &strBuf);
	addStrBuf(key, &strBuf);
	addStrBuf("' is unknown\n", &strBuf);
    }

    return strBuf.buf;
}
