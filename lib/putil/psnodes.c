/*
 *               ParaStation3
 * psnodes.c
 *
 * ParaStation node handling functions.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psnodes.c,v 1.4 2003/06/25 16:33:24 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psnodes.c,v 1.4 2003/06/25 16:33:24 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdlib.h>
#include <netinet/in.h>
#include <sys/types.h>

#include "pscommon.h"

#include "psnodes.h"


/** Number of nodes that can be currently handled. Set within PSnodes_init() */
static int numNodes = -1;

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

int PSnodes_init(int num)
{
    int i;

    if (nodes) free(--nodes);

    numNodes = num;

    nodes = (struct node_t *)malloc(sizeof(*nodes) * (numNodes + 1));

    if (!nodes) {
	return -1;
    }

    nodes++;

    /* Clear nodes */
    for (i=-1; i<numNodes; i++) {
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

int PSnodes_getNum(void)
{
    return numNodes;
}

static int ID_ok(int id)
{
    if (PSnodes_getNum() == -1 || id < -1 || id >= PSnodes_getNum()) {
	/* id out of Range */
	return 0;
    }

    return 1;
}

int PSnodes_register(int id, unsigned int IPaddr)
{
    unsigned int hostno;
    struct host_t *host;

    if (! ID_ok(id)) {
	return -1;
    }

    if (id != PSNODES_LIC && PSnodes_lookupHost(IPaddr)!=-1) {
	/* duplicated host */
	return -1;
    }

    if (PSnodes_getAddr(id) != INADDR_ANY) { /* duplicated PS-ID */
	return -1;
    }

    /* install hostname */
    nodes[id].addr = IPaddr;

    if (id == PSNODES_LIC) return 0;

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

int PSnodes_lookupHost(unsigned int IPaddr)
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

unsigned int PSnodes_getAddr(int id)
{
    if (ID_ok(id)) {
	return nodes[id].addr;
    } else {
	return -1;
    }
}


int PSnodes_bringUp(int id)
{
    if (ID_ok(id)) {
	nodes[id].isUp = 1;
	return 0;
    } else {
	return -1;
    }
}

int PSnodes_bringDown(int id)
{
    if (ID_ok(id)) {
	nodes[id].isUp = 0;
	return 0;
    } else {
	return -1;
    }
}

int PSnodes_isUp(int id)
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
int PSnodes_setInfoVersion(int id, unsigned int version)
{
    if (ID_ok(id)) {
	nodes[id].version = version;
	return 0;
    } else {
	return -1;
    }
}

int PSnodes_getInfoVersion(int id)
{
    if (ID_ok(id)) {
	return nodes[id].version;
    } else {
	return -1;
    }
}
/**********************************************************************/

int PSnodes_setHWType(int id, int hwType)
{
    if (ID_ok(id)) {
	nodes[id].hwType = hwType;
	return 0;
    } else {
	return -1;
    }
}

int PSnodes_getHWType(int id)
{
    if (ID_ok(id)) {
	return nodes[id].hwType;
    } else {
	return -1;
    }
}

int PSnodes_setRunJobs(int id, int runjobs)
{
    if (ID_ok(id)) {
	nodes[id].jobs = runjobs;
	return 0;
    } else {
	return -1;
    }
}


int PSnodes_runJobs(int id)
{
    if (ID_ok(id)) {
	return nodes[id].jobs;
    } else {
	return -1;
    }
}

int PSnodes_setIsStarter(int id, int starter)
{
    if (ID_ok(id)) {
	nodes[id].starter = starter;
	return 0;
    } else {
	return -1;
    }
}

int PSnodes_isStarter(int id)
{
    if (ID_ok(id)) {
	return nodes[id].starter;
    } else {
	return -1;
    }
}

int PSnodes_setExtraIP(int id, unsigned int addr)
{
    if (ID_ok(id)) {
	nodes[id].extraIP = addr;
	return 0;
    } else {
	return -1;
    }
}

unsigned int PSnodes_getExtraIP(int id)
{
    if (ID_ok(id)) {
	return nodes[id].extraIP;
    } else {
	return -1;
    }
}

int PSnodes_setCPUs(int id, short numCPU)
{
    if (ID_ok(id)) {
	nodes[id].numCPU = numCPU;
	return 0;
    } else {
	return -1;
    }
}

short PSnodes_getCPUs(int id)
{
    if (ID_ok(id)) {
	return nodes[id].numCPU;
    } else {
	return -1;
    }
}

int PSnodes_setHWStatus(int id, int hwStatus)
{
    if (ID_ok(id)) {
	nodes[id].hwStatus = hwStatus;
	return 0;
    } else {
	return -1;
    }
}

int PSnodes_getHWStatus(int id)
{
    if (ID_ok(id)) {
	return nodes[id].hwStatus;
    } else {
	return -1;
    }
}

int PSnodes_setUser(int id, uid_t uid)
{
    if (ID_ok(id)) {
	nodes[id].uid = uid;
	return 0;
    } else {
	return -1;
    }
}

uid_t PSnodes_getUser(int id)
{
    if (ID_ok(id)) {
	return nodes[id].uid;
    } else {
	return -1;
    }
}

int PSnodes_setGroup(int id, gid_t gid)
{
    if (ID_ok(id)) {
	nodes[id].gid = gid;
	return 0;
    } else {
	return -1;
    }
}

gid_t PSnodes_getGroup(int id)
{
    if (ID_ok(id)) {
	return nodes[id].gid;
    } else {
	return -1;
    }
}

int PSnodes_setProcs(int id, int procs)
{
    if (ID_ok(id)) {
	nodes[id].procs = procs;
	return 0;
    } else {
	return -1;
    }
}

int PSnodes_getProcs(int id)
{
    if (ID_ok(id)) {
	return nodes[id].procs;
    } else {
	return -1;
    }
}
