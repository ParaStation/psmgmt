/*
 *               ParaStation3
 * psnodes.c
 *
 * ParaStation node handling functions.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psnodes.c,v 1.6 2003/10/29 17:25:09 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psnodes.c,v 1.6 2003/10/29 17:25:09 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdlib.h>
#include <netinet/in.h>
#include <sys/types.h>

#include "pscommon.h"

#include "psnodes.h"


/** Number of nodes that can be currently handled. Set within PSnodes_init() */
static PSnodes_ID_t numNodes = -1;

/** Hashed host table for reverse lookup (ip-addr given, determine id) */
struct host_t {
    unsigned int addr;
    int id;
    struct host_t *next;
};

static struct host_t *hosts[256];  /* host table */

/* List of all nodes, info about hardware included */
struct node_t {
    unsigned int addr;     /**< IP address of that node */
    int version;           /**< Version of the config info from that node */
    short numCPU;          /**< Number of CPUs in that node */
    char isUp;             /**< Actual status of that node */
    unsigned int hwType;   /**< Communication hardware on that node */
    unsigned int hwStatus; /**< Corresponding stati of the hardware */
    unsigned int extraIP;  /**< Additional IP address of that node */
    int jobs;              /**< Flag to mark that node to run jobs */
    int starter;           /**< Flag to allow to start jobs from that node */
    uid_t uid;             /**< User this nodes is reserved to */
    gid_t gid;             /**< Group this nodes is reserved to */
    int procs;             /**< Number of processes this node will handle */
};

static struct node_t *nodes = NULL;

int PSnodes_init(PSnodes_ID_t num)
{
    int i;

    if (nodes) free(nodes);

    numNodes = num;

    nodes = (struct node_t *)malloc(sizeof(*nodes) * numNodes);

    if (!nodes) {
	return -1;
    }

    /* Clear nodes */
    for (i=0; i<numNodes; i++) {
        nodes[i].addr = INADDR_ANY;
	nodes[i].numCPU = 0;
	nodes[i].isUp = 0;
        nodes[i].hwType = 0;
        nodes[i].hwStatus = 0;
	nodes[i].extraIP = INADDR_ANY;
	nodes[i].jobs = 0;
	nodes[i].starter = 0;
	nodes[i].uid = -1;
	nodes[i].gid = -1;
	nodes[i].procs = -1;
    }

    return 0;
}

PSnodes_ID_t PSnodes_getNum(void)
{
    return numNodes;
}

static int ID_ok(PSnodes_ID_t id)
{
    if (PSnodes_getNum() == -1 || id < 0 || id >= PSnodes_getNum()) {
	/* id out of Range */
	return 0;
    }

    return 1;
}

int PSnodes_register(PSnodes_ID_t id, unsigned int IPaddr)
{
    unsigned int hostno;
    struct host_t *host;

    if (! ID_ok(id)) {
	return -1;
    }

    if (PSnodes_lookupHost(IPaddr)!=-1) {
	/* duplicated host */
	return -1;
    }

    if (PSnodes_getAddr(id) != INADDR_ANY) { /* duplicated PS-ID */
	return -1;
    }

    /* install hostname */
    nodes[id].addr = IPaddr;

    hostno = ntohl(IPaddr) & 0xff;

    host = (struct host_t*) malloc(sizeof(struct host_t));
    if (!host) {
	return -1;
    }

    host->addr = IPaddr;
    host->id = id;
    host->next = hosts[hostno];
    hosts[hostno] = host;

    return 0;
}

PSnodes_ID_t PSnodes_lookupHost(unsigned int IPaddr)
{
    unsigned int hostno;
    struct host_t *host;

    /* loopback address */
    if ((ntohl(IPaddr) >> 24 ) == 0x7f)
	return PSC_getMyID();

    /* other addresses */
    hostno = ntohl(IPaddr) & 0xff;
    for (host = hosts[hostno]; host; host = host->next) {
	if (host->addr == IPaddr) {
	    return host->id;
	}
    }

    return -1;
}

unsigned int PSnodes_getAddr(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	return nodes[id].addr;
    } else {
	return -1;
    }
}


int PSnodes_bringUp(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	nodes[id].isUp = 1;
	return 0;
    } else {
	return -1;
    }
}

int PSnodes_bringDown(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	nodes[id].isUp = 0;
	return 0;
    } else {
	return -1;
    }
}

int PSnodes_isUp(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	return nodes[id].isUp;
    } else {
	return 0;
    }
}

/**********************************************************************/
/* @todo This does not really make sense, but is a good start.
   Actually each piece of information needs its own version number */
int PSnodes_setInfoVersion(PSnodes_ID_t id, unsigned int version)
{
    if (ID_ok(id)) {
	nodes[id].version = version;
	return 0;
    } else {
	return -1;
    }
}

int PSnodes_getInfoVersion(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	return nodes[id].version;
    } else {
	return -1;
    }
}
/**********************************************************************/

int PSnodes_setHWType(PSnodes_ID_t id, int hwType)
{
    if (ID_ok(id)) {
	nodes[id].hwType = hwType;
	return 0;
    } else {
	return -1;
    }
}

int PSnodes_getHWType(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	return nodes[id].hwType;
    } else {
	return -1;
    }
}

int PSnodes_setRunJobs(PSnodes_ID_t id, int runjobs)
{
    if (ID_ok(id)) {
	nodes[id].jobs = runjobs;
	return 0;
    } else {
	return -1;
    }
}


int PSnodes_runJobs(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	return nodes[id].jobs;
    } else {
	return -1;
    }
}

int PSnodes_setIsStarter(PSnodes_ID_t id, int starter)
{
    if (ID_ok(id)) {
	nodes[id].starter = starter;
	return 0;
    } else {
	return -1;
    }
}

int PSnodes_isStarter(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	return nodes[id].starter;
    } else {
	return -1;
    }
}

int PSnodes_setExtraIP(PSnodes_ID_t id, unsigned int addr)
{
    if (ID_ok(id)) {
	nodes[id].extraIP = addr;
	return 0;
    } else {
	return -1;
    }
}

unsigned int PSnodes_getExtraIP(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	return nodes[id].extraIP;
    } else {
	return -1;
    }
}

int PSnodes_setCPUs(PSnodes_ID_t id, short numCPU)
{
    if (ID_ok(id)) {
	nodes[id].numCPU = numCPU;
	return 0;
    } else {
	return -1;
    }
}

short PSnodes_getCPUs(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	return nodes[id].numCPU;
    } else {
	return -1;
    }
}

int PSnodes_setHWStatus(PSnodes_ID_t id, int hwStatus)
{
    if (ID_ok(id)) {
	nodes[id].hwStatus = hwStatus;
	return 0;
    } else {
	return -1;
    }
}

int PSnodes_getHWStatus(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	return nodes[id].hwStatus;
    } else {
	return -1;
    }
}

int PSnodes_setUser(PSnodes_ID_t id, uid_t uid)
{
    if (ID_ok(id)) {
	nodes[id].uid = uid;
	return 0;
    } else {
	return -1;
    }
}

uid_t PSnodes_getUser(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	return nodes[id].uid;
    } else {
	return -1;
    }
}

int PSnodes_setGroup(PSnodes_ID_t id, gid_t gid)
{
    if (ID_ok(id)) {
	nodes[id].gid = gid;
	return 0;
    } else {
	return -1;
    }
}

gid_t PSnodes_getGroup(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	return nodes[id].gid;
    } else {
	return -1;
    }
}

int PSnodes_setProcs(PSnodes_ID_t id, int procs)
{
    if (ID_ok(id)) {
	nodes[id].procs = procs;
	return 0;
    } else {
	return -1;
    }
}

int PSnodes_getProcs(PSnodes_ID_t id)
{
    if (ID_ok(id)) {
	return nodes[id].procs;
    } else {
	return -1;
    }
}
