/*
 * ParaStation
 *
 * Copyright (C) 2012-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>

#include "pscommon.h"
#include "pspartition.h"
#include "plugin.h"
#include "psidhook.h"

#include "pluginmalloc.h"
#include "pluginlog.h"

#include "psresportlog.h"
#include "psresportconfig.h"

#define RESPORT_CONFIG "psresport.conf"

/** psid plugin requirements */
char name[] = "psresport";
int version = 2;
int requiredAPI = 108;
plugin_dep_t dependencies[] = { { NULL, 0 } };

/** the start of the reserved port range */
static int pRangeMin = 0;

/** the end of the reserved port range */
static int pRangeMax = 0;

/** the last used port in a reservation request */
static int pRangeLast = 0;

/** the number of reserved ports */
static int pRangeCount = 0;

/** the number of nodes */
static int nrOfNodes = 0;

/** the main reservation bitfield */
static char **nodeBitField = NULL;

/** list of uniq nodes in the request */
static int *uniqNodeList = NULL;

/** number of uniq nodes */
static uint32_t uniqNodeCount = 0;

/** number of reservations */
static int reservationCount = 0;

/**
 * @brief Initialize the global node reservation bitfield.
 *
 * @return No return value.
 */
static void initNodeBitField(void)
{
    int i;

    /* setup node bitfield */
    if (nodeBitField == NULL) {
	nrOfNodes = PSC_getNrOfNodes();
	nodeBitField = umalloc(nrOfNodes * sizeof(char *));

	for (i=0; i<nrOfNodes; i++) {
	    nodeBitField[i] = NULL;
	}
    }
}

/**
 * @brief Free the uniq nodes list.
 *
 * @return No return value.
 */
static void freeUniqNodes(void)
{
    ufree(uniqNodeList);
    uniqNodeList = NULL;
    uniqNodeCount = 0;
}

/**
 * @brief Extract configured port informations.
 *
 * @param ports The port range to extract the information from.
 *
 * @return Returns 1 if the port range is valid. Otherwise 0 is returned.
 */
static int extractPortInfos(char *ports)
{
    if (!ports) return 0;
    if ((sscanf(ports, "%i-%i", &pRangeMin, &pRangeMax)) != 2) return 0;
    if (pRangeMin >= pRangeMax) return 0;
    if (pRangeMin <= 0 || pRangeMax <= 0) return 0;

    pRangeLast = pRangeMin;
    pRangeCount = pRangeMax - pRangeMin;

    return 1;
}

/**
 * @brief Parse a partition request.
 *
 * This function will extract the number of needed ports and build up a uniq
 * node array. The space for the node list will be dynamically allocated and
 * must be freed using ufree().
 *
 * @param request The request to parse.
 *
 * @return Returns the number of needed ports.
 */
static int parseSlots(uint32_t size, PSpart_slot_t *slots)
{
    int *slotsPerNode;
    int maxPorts = 0;
    unsigned int i, x;

    if (size < 1 || !slots) {
	mlog("%s: invalid slot list\n", __func__);
	return 0;
    }

    slotsPerNode = umalloc(size * sizeof(slotsPerNode));
    uniqNodeList = umalloc(size * sizeof(uniqNodeList));

    for (i=0; i<size; i++) slotsPerNode[i] = 0;
    for (i=0; i<size; i++) uniqNodeList[i] = -1;

    uniqNodeCount = 0;

    /* walk over slots, find max slots per node and build up a uniq node list */
    for (i=0; i<size; i++) {
	for (x=0; x<size; x++) {

	    if (uniqNodeList[x] == slots[i].node) {
		slotsPerNode[x]++;
		break;
	    }

	    if (uniqNodeList[x] == -1) {
		uniqNodeList[x] = slots[i].node;
		slotsPerNode[x]++;
		uniqNodeCount++;
		break;
	    }
	}
    }

    for (x=0; x<size; x++) {
	if (maxPorts < slotsPerNode[x]) {
	    maxPorts = slotsPerNode[x];
	}
    }

    ufree(slotsPerNode);

    return maxPorts + 1;
}

/**
 * @brief Get the next possible port.
 *
 * @param currentPort The last used port.
 *
 * @return Returns the next port candidate.
 */
static int getNextPort(int currentPort)
{
    int nextPort;

    if (currentPort + 1 >= pRangeMax) {
	nextPort = pRangeMin;
    } else {
	nextPort = currentPort + 1;
    }

    return nextPort;
}

/**
 * @brief Allocate and initialize a node bitfield.
 *
 * @param node The nodeID to use.
 *
 * @return No return value.
 */
static void allocNodeField(int node)
{
    int i;

    if (nodeBitField[node]) return;

    nodeBitField[node] = umalloc(pRangeCount);

    /* init slots */
    for (i=0; i<pRangeCount; i++) {
	nodeBitField[node][i] = 0;
    }
}

/**
 * @brief Check if a choosen port is avaiable.
 *
 * @param node The node to reserve to port for.
 *
 * @param index The index in the node bitfield of the port to reserve.
 *
 * @return Returns 1 if the port slot could be reserved. Otherwise 0 is
 * returned.
 */
static int isNodePortFree(int node, int index)
{
    if (!PSC_validNode(node)) {
	mlog("%s: invalid node index %i nrOfNodes %i\n", __func__, node,
	     PSC_getNrOfNodes());
	return 0;
    }

    if (index < 0 || index >= pRangeCount) {
	mlog("%s: invalid bit field index %i nrOfNodes %i\n", __func__,
	     index, PSC_getNrOfNodes());
	return 0;
    }

    if (nodeBitField[node] == NULL) {
	/* node has no reserved ports yet */
	mdbg(RP_LOG_DEBUG, "%s: found empty node %i\n", __func__, node);
	return 1;
    } else {
	/* port is already used, we need to find another one */
	if (nodeBitField[node][index] == 1) {
	    mdbg(RP_LOG_DEBUG, "%s: found used node-port %i\n", __func__, node);
	    return 0;
	} else {
	    return 1;
	}
    }

    return 0;
}

/**
 * @brief Change the state of reserved ports.
 *
 * @param resPorts The reserved port array.
 *
 * @param value The state to set.
 *
 * @return No return value.
 */
static void setPortState(uint16_t *resPorts, int value)
{
    int i = 0, node, port, index;
    uint32_t x;

    if (!resPorts) {
	mlog("%s: got invalid resPorts structure\n", __func__);
	return;
    }

    if (value) {
	reservationCount++;
    } else {
	reservationCount--;
    }

    /* no reservation to reset, we are done here */
    if (!nodeBitField && value == 0) return;

    if (!nodeBitField) initNodeBitField();

    if (uniqNodeCount < 1 || !uniqNodeCount) {
	mlog("%s: internal lists not proper initialized\n", __func__);
	return;
    }

    while (resPorts[i] != 0) {
	port = resPorts[i];
	index = port - pRangeMin;

	for (x=0; x<uniqNodeCount; x++) {
	    node = uniqNodeList[x];
	    if (!PSC_validNode(node)) {
		mlog("%s: skipping invalid node index %i, nrOfNodes %i\n",
		     __func__, node, PSC_getNrOfNodes());
		continue;
	    }

	    if (index < 0 || index >= pRangeCount) {
		mlog("%s: skipping invalid bit field index %i, nrOfNodes %i\n",
		     __func__, index, PSC_getNrOfNodes());
		continue;
	    }

	    if (!nodeBitField[node] && value == 0) continue;

	    if (!nodeBitField[node]) {
		allocNodeField(node);
	    }

	    nodeBitField[node][index] = value;
	    mdbg(RP_LOG_DEBUG, "%s: set node %i : port: %i to %i\n",
		    __func__, node, port, value);
	}
	i++;
    }
}

/**
 * @brief Build a port reservation field.
 *
 * Build a port reservation field which can be passed to @ref setPortState
 * to modify the global reservation bitfield.
 *
 * @param maxSlots The number of port slots to reserve.
 *
 * @param request The partition request to reserve the ports for.
 *
 * @param resPorts Pointer to a buffer which will receive the reserved ports.
 *
 * @return Returns 1 if all requested ports were successfully reserved.
 * Otherwise 0 is returned.
 */
static int reservePorts(int maxSlots, PSpart_request_t *request,
			    uint16_t *resPorts)
{
    int i, node, index = 0, portOK, nextPort = pRangeLast;
    int resCount = 0;
    uint32_t s;

    for (i=0; i<pRangeCount; i++) {
	portOK = 1;

	for (s=0; s<uniqNodeCount; s++) {
	    node = uniqNodeList[s];
	    index = nextPort - pRangeMin;

	    /* reserving port failed, try next one */
	    if (!(isNodePortFree(node, index))) {
		portOK = 0;
		break;
	    }
	}

	/* we can reserve port */
	if (portOK) {
	    resPorts[resCount++] = nextPort;

	    /* are we done? */
	    if (resCount == maxSlots) return 1;
	}

	nextPort = getNextPort(nextPort);

	/* no more ports to try, reservation failed */
	if (nextPort == pRangeLast) return 0;
    }

    if (resCount == maxSlots) return 1;

    return 0;
}

/**
 * @brief Add a new port reservation.
 *
 * @param req The partition request to add the reservation for.
 *
 * @return Always returns 0.
 */
static int addNewReservation(void *req)
{
    PSpart_request_t *request = req;
    int maxPorts, i;
    uint16_t *resPorts;

    if (!nodeBitField) initNodeBitField();

    mdbg(RP_LOG_DEBUG, "%s: min:%i max:%i last:%i count:%i req-size:%i\n",
	    __func__, pRangeMin, pRangeMax, pRangeLast, pRangeCount,
	    request->size);

    if (!(maxPorts = parseSlots(request->size, request->slots))) return 0;

    mdbg(RP_LOG_DEBUG, "%s: %i uniq Nodes, %i ports needed\n", __func__,
	    uniqNodeCount, maxPorts);

    /* init a new reservation array */
    resPorts = umalloc((maxPorts + 1) * sizeof(uint16_t));
    for (i=0; i<=maxPorts; i++) resPorts[i] = 0;

    if ((reservePorts(maxPorts, request, resPorts))) {
	mdbg(RP_LOG_DEBUG, "%s: port reservation was successful\n", __func__);

	/* set the new reservation in the node bitfield */
	setPortState(resPorts, 1);

	/* save reserved ports in request */
	request->resPorts = resPorts;

	/* set new start port to use for next reservation request */
	pRangeLast = resPorts[maxPorts -1];
	pRangeLast = getNextPort(pRangeLast);
    } else {
	mdbg(RP_LOG_DEBUG, "%s: port reservation failed\n", __func__);
	ufree(resPorts);
    }

    /* free uniq node list reserved by parseSlots() */
    freeUniqNodes();

    return 0;
}

/**
 * @brief Recover reserved ports when the psid master switches.
 *
 * @param taskPtr Pointer to the task structure holding the data to recover.
 *
 * @return Always return 0 on success and 1 on error.
 */
static int recoverReservedPorts(void *reqPtr)
{
    PSpart_request_t *req = reqPtr;

    if (!(parseSlots(req->size, req->slots))) return 1;

    setPortState(req->resPorts, 1);

    /* free uniq node list reserved by parseSlots() */
    freeUniqNodes();

    return 0;
}

/**
 * @brief Find empty node bitmasks and free the corresponding memory.
 *
 * @param uNodeList Uniq node list to find empy node bitmasks in.
 *
 * @param uNodeCount The number of elements in the uniq node list.
 *
 * @return No return value.
 */
static void freeEmptyNodeBitmasks(int *uNodeList, uint32_t uNodeCount)
{
    int i, node, isEmpty;
    unsigned int x;

    for (x=0; x<uNodeCount; x++) {
	node = uNodeList[x];

	if (!PSC_validNode(node)) {
	    mlog("%s: invalid node index %i nrOfNodes %i\n", __func__,
		 node, PSC_getNrOfNodes());
	    continue;
	}

	if (!nodeBitField[node]) continue;

	isEmpty = 1;
	for (i=0; i<pRangeCount; i++) {
	    if (nodeBitField[node][i] == 1) {
		isEmpty = 0;
		break;
	    }
	}

	if (isEmpty) {
	    mdbg(RP_LOG_DEBUG, "%s: bitfield for node '%i' is empty, "
		    "freeing it\n", __func__, node);
	    ufree(nodeBitField[node]);
	    nodeBitField[node] = NULL;
	}
    }
}

/**
 * @brief Clear all reservations and free used resources.
 *
 * This functions is called when the local node is freed from the burden
 * of acting as a master. So all reservations are obsolete and should be freed.
 *
 * @return Always returns 0.
 */
static int clearAllReservations(void *info)
{
    ufree(nodeBitField);
    nodeBitField = NULL;

    reservationCount = 0;
    nrOfNodes = 0;

    return 0;
}

/**
 * @brief Release reserved ports.
 *
 * @param The request to release the ports for.
 *
 * @return Always returns 0 on success and 1 on error.
 */
static int releaseReservation(void *req)
{
    PSpart_request_t *request = req;

    if (!request->resPorts) return 0;

    if (!(parseSlots(request->size, request->slots))) return 1;

    setPortState(request->resPorts, 0);

    /* free empty node bitmasks */
    freeEmptyNodeBitmasks(uniqNodeList, uniqNodeCount);

    /* free uniq node list reserved by parseSlots() */
    freeUniqNodes();

    /* free reservation */
    ufree(request->resPorts);
    request->resPorts = NULL;

    return 0;
}


/**
 * @brief Unregister all hooks and message handler.
 *
 * @param verbose If set to true an error message will be displayed
 * when unregistering a hook or a message handle fails.
 *
 * @return No return value.
 */
static void unregisterHooks(int verbose)
{
    if (!(PSIDhook_del(PSIDHOOK_MASTER_GETPART, addNewReservation))) {
	if (verbose) mlog("removing PSIDHOOK_MASTER_GETPART failed\n");
    }

    if (!(PSIDhook_del(PSIDHOOK_MASTER_FINJOB, releaseReservation))) {
	if (verbose) mlog("removing PSIDHOOK_MASTER_FINJOB failed\n");
    }

    if (!(PSIDhook_del(PSIDHOOK_MASTER_RECPART, recoverReservedPorts))) {
	if (verbose) mlog("removing PSIDHOOK_MASTER_RECPART failed\n");
    }

    if (!(PSIDhook_del(PSIDHOOK_MASTER_EXITPART, clearAllReservations))) {
	if (verbose) mlog("removing PSIDHOOK_MASTER_EXITPART failed\n");
    }
}

int initialize(void)
{
    int debugMask;
    char *ports;
    char configfn[200];

    initLogger(NULL);
    initPluginLogger(NULL, NULL);

    /* init the config facility */
    snprintf(configfn, sizeof(configfn), "%s/%s", PLUGINDIR, RESPORT_CONFIG);

    initConfig(configfn);

    /* init logging facility */
    debugMask = getConfValueI(&psresportConfig, "DEBUG_MASK");
    maskLogger(debugMask);

    if (!(PSIDhook_add(PSIDHOOK_MASTER_GETPART, addNewReservation))) {
	mlog("%s: register PSIDHOOK_MASTER_GETPART failed\n", __func__);
	goto INIT_ERROR;
    }

    if (!(PSIDhook_add(PSIDHOOK_MASTER_FINJOB, releaseReservation))) {
	mlog("%s: register PSIDHOOK_MASTER_FINJOB failed\n", __func__);
	goto INIT_ERROR;
    }

    if (!(PSIDhook_add(PSIDHOOK_MASTER_RECPART, recoverReservedPorts))) {
	mlog("%s: register PSIDHOOK_MASTER_RECPART failed\n", __func__);
	goto INIT_ERROR;
    }

    if (!(PSIDhook_add(PSIDHOOK_MASTER_EXITPART, clearAllReservations))) {
	mlog("%s: register PSIDHOOK_MASTER_EXITPART failed\n", __func__);
	goto INIT_ERROR;
    }

    /* get reserved ports from config */
    ports = getConfValueC(&psresportConfig, "RESERVED_PORTS");

    if (!(extractPortInfos(ports))) {
	mlog("%s: invalid port range '%s'\n", __func__, ports);
	goto INIT_ERROR;
    }

    mlog("(%i) successfully started\n", version);

    return 0;


INIT_ERROR:
    unregisterHooks(0);
    return 1;
}

void cleanup(void)
{
    ufree(nodeBitField);
    unregisterHooks(1);
}

/**
 * @brief Show reservation information.

 * @param buf The buffer to write the information to.
 *
 * @param bufSize The size of the buffer.
 *
 * @return Returns the buffer with the requested information.
 */
static char *showReservationCount(char *buf, size_t *bufSize)
{
    char line[100];

    snprintf(line, sizeof(line), "\n%i reservation(s) in port range '%i-%i'\n",
		reservationCount, pRangeMin, pRangeMax);
    str2Buf(line, &buf, bufSize);
    return buf;
}

/**
 * @brief Print the node reservation bitmask.

 * @param buf The buffer to write the information to.
 *
 * @param bufSize The size of the buffer.
 *
 * @return Returns the buffer with the requested bitmask.
 */
static char *dumpNodeBitmask(char *buf, size_t *bufSize)
{
    char line[50], tmp[50];
    int i, node;

    buf = showReservationCount(buf, bufSize);

    for (node=0; node<nrOfNodes; node++) {

	if (!nodeBitField[node]) continue;

	snprintf(tmp, sizeof(tmp), "node[%i]", node);
	snprintf(line, sizeof(line), "%-12s:", tmp);
	str2Buf(line, &buf, bufSize);
	for (i=0; i<pRangeCount; i++) {
	    if (nodeBitField[node][i] == 1) {
		str2Buf("1", &buf, bufSize);
	    } else {
		str2Buf("0", &buf, bufSize);
	    }
	}
	str2Buf("\n", &buf, bufSize);
    }
    return buf;
}

char *show(char *key)
{
    char *buf = NULL;
    size_t bufSize = 0;

    if (!key) {
	str2Buf("\nuse 'reservations' or 'bitmask' as key\n", &buf, &bufSize);
	return buf;
    }

    /* show reservation count */
    if (!(strcmp(key, "reservations"))) {
	return showReservationCount(buf, &bufSize);
    }

    /* dump node reservation bitmask */
    if (!(strcmp(key, "bitmask"))) {
	return dumpNodeBitmask(buf, &bufSize);
    }

    str2Buf("\nInvalid key '", &buf, &bufSize);
    str2Buf(key, &buf, &bufSize);
    str2Buf("' for cmd show : use 'plugin help psresport'.\n", &buf, &bufSize);

    return buf;
}

char *help(void)
{
    char *buf = NULL;
    size_t bufSize = 0;

    str2Buf("\nThe psresport plugin is providing a port reservation "
	    "facility which is currently used\nby the startup mechanism"
	    " of OpenMPI since version 1.5.\n",
	    &buf, &bufSize);

    str2Buf("\nUse 'plugin show psresport [key reservation|bitmask]'.\n",
	    &buf, &bufSize);

    return buf;
}
