/*
 *               ParaStation
 * psidstatus.c
 *
 * Helper functions for master-node detection and status actions.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidstatus.c,v 1.1 2003/12/19 15:12:08 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidstatus.c,v 1.1 2003/12/19 15:12:08 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <errno.h>

/* Extra includes for load-determination */
#if defined(__linux__)
#include <sys/sysinfo.h>
#elif defined(__osf__)
#include <sys/table.h>
#else
#error WRONG OS Type
#endif

#include "pscommon.h"
#include "psprotocol.h"
#include "psdaemonprotocol.h"
#include "psnodes.h"

#include "mcast.h"
#include "timer.h"

#include "psidutil.h"
#include "psidtask.h"
#include "psidsignal.h"
#include "psidoption.h"
#include "psidcomm.h"
#include "psidrdp.h"

#include "psidstatus.h"

static char errtxt[256];

static PSnodes_ID_t masterNode = 0;

PSnodes_ID_t getMasterID(void)
{
    return masterNode;
}

void setMasterID(PSnodes_ID_t id)
{
    masterNode = id;
}

int amMasterNode(void)
{
    return (PSC_getMyID() == masterNode);
}


static PSID_Jobs_t myJobs = { .normal = 0, .total = 0 };

void incJobs(int total, int normal)
{
    if (total) myJobs.total++;
    if (normal) myJobs.normal++;

    if (config->useMCast) {
	incJobsMCast(PSC_getMyID(), total, normal);
    } else {
	// doRDPPing(); @todo
    }
}

void decJobs(int total, int normal)
{
    if (total) myJobs.total--;
    if (normal) myJobs.normal--;

    if (config->useMCast) {
	decJobsMCast(PSC_getMyID(), total, normal);
    } else {
	// doRDPPing(); @todo
    }
}

static PSID_Load_t getLoad(void)
{
    PSID_Load_t load = {{0.0, 0.0, 0.0}};
#if defined __linux__
    struct sysinfo s_info;

    sysinfo(&s_info);
    load.load[0] = (double) s_info.loads[0] / (1<<SI_LOAD_SHIFT);
    load.load[1] = (double) s_info.loads[1] / (1<<SI_LOAD_SHIFT);
    load.load[2] = (double) s_info.loads[2] / (1<<SI_LOAD_SHIFT);
#elif defined __osf__
    struct tbl_loadavg load_struct;

    /* Use table call to extract the load for the node. */
    table(TBL_LOADAVG, 0, &load_struct, 1, sizeof(struct tbl_loadavg));

    /* Get the double value of the load. */
    load.load[0] = (load_struct.tl_lscale == 0)?load_struct.tl_avenrun.d[0] :
	((double)load_struct.tl_avenrun.l[0]/(double)load_struct.tl_lscale);
    load.load[1] = (load_struct.tl_lscale == 0)?load_struct.tl_avenrun.d[1] :
	((double)load_struct.tl_avenrun.l[1]/(double)load_struct.tl_lscale);
    load.load[2] = (load_struct.tl_lscale == 0)?load_struct.tl_avenrun.d[2] :
	((double)load_struct.tl_avenrun.l[2]/(double)load_struct.tl_lscale);
#else
#error BAD OS !!!!
#endif

    return load;
}

typedef struct {
    struct timeval lastping;  /**< Timestamp of last received ping */
    PSID_Jobs_t jobs;   /**< Number of jobs on the node */  
    PSID_Load_t load;   /**< Load parameters of node */
    short placedJobs;
} ClientStatus_t;

static ClientStatus_t *clientStat = NULL;

static void becomeMaster(void)
{
    clientStat = realloc(clientStat, PSC_getNrOfNodes() * sizeof(*clientStat));
    setMasterID(PSC_getMyID());
}

static void abandonMaster(void)
{
    if (clientStat) {
	free(clientStat);
	clientStat = NULL;
    }
}

void sendRDPPing(void)
{
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_DD_LOAD,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getTID(getMasterID(), 0),
	    .len = sizeof(msg.header) },
	.buf = { 0 } };
    char *ptr = msg.buf;

    *(PSID_Jobs_t *)ptr = myJobs;
    ptr += sizeof(PSID_Jobs_t);
    msg.header.len += sizeof(PSID_Jobs_t);

    *(PSID_Load_t *)ptr = getLoad();
    ptr += sizeof(PSID_Load_t);
    msg.header.len += sizeof(PSID_Load_t);

    if (getMasterID() == PSC_getMyID()) {
	msg_LOAD(&msg);
    } else {
	sendMsg(&msg);
    }
}

static struct timeval StatusTimeout = {2, 0}; /* sec, usec */

static int timerID;

static void becomeClient(void)
{
    abandonMaster();
    if (!Timer_isInitialized()) {
	Timer_init(config->useSyslog);
    }
    timerID = Timer_register(&StatusTimeout, sendRDPPing); 
}

PSID_NodeStatus_t getStatus(PSnodes_ID_t node)
{
    PSID_NodeStatus_t status;

    if (config->useMCast) {
	MCastConInfo_t info;

	getInfoMCast(node, &info);

	status.load.load[0] = info.load.load[0];
	status.load.load[1] = info.load.load[1];
	status.load.load[2] = info.load.load[2];
	status.jobs.total = info.jobs.total;
	status.jobs.normal = info.jobs.normal;
    } else {
	if (node == PSC_getMyID()) {
	    status.jobs = myJobs;
	    status.load = getLoad();
	} else if ((PSC_getMyID() != getMasterID())
	    || (node<0) || (node>PSC_getNrOfNodes()) || !clientStat) {
	    status.jobs = (PSID_Jobs_t) { .normal = -1, .total = -1 };
	    status.load = (PSID_Load_t) {{ 0.0, 0.0, 0.0}};
	} else {
	    status.jobs = clientStat[node].jobs;
	    status.load = clientStat[node].load;
	}
    }

    return status;
}

/** Total number of nodes connected. Needed for license testing */
static int totalNodes = 0;

/** Total number of physical CPUs connected. Needed for license testing */
static int totalCPUs = 0;


void declareNodeDead(PSnodes_ID_t id)
{
    PStask_t *task;

    if (PSnodes_isUp(id)) {
	totalCPUs -= PSnodes_getPhysCPUs(id);
	totalNodes--;
    }
    PSnodes_bringDown(id);
    PSnodes_setPhysCPUs(id, 0);
    PSnodes_setVirtCPUs(id, 0);

    clearRDPMsgs(id);

    if (config->useMCast) declareNodeDeadMCast(id);

    /* Send signals to all processes that controlled task on the dead node */
    task=managedTasks;
    /* loop over all tasks */
    while (task) {
	PStask_sig_t *sig = task->assignedSigs;
	/* loop over all controlled tasks */
	while (sig) {
	    if (PSC_getID(sig->tid)==id) {
		/* controlled task was on dead node */
		PStask_ID_t senderTid = sig->tid;
		int signal = sig->signal;

		/* Send the signal */
		PSID_sendSignal(task->tid, task->uid, senderTid, signal, 0);

		sig = sig->next;
		/* Remove signal from list */
		PSID_removeSignal(&task->assignedSigs, senderTid, signal);
	    } else {
		sig = sig->next;
	    }
	}
	task = task->next;
    }

    snprintf(errtxt, sizeof(errtxt),
	     "Lost connection to daemon of node %d", id);
    PSID_errlog(errtxt, 2);
}

/* External function. @todo */
int shutdownNode(int phase);

void declareNodeAlive(PSnodes_ID_t id, int physCPUs, int virtCPUs)
{
    snprintf(errtxt, sizeof(errtxt), "%s: node %d", __func__, id);
    PSID_errlog(errtxt, 2);

    if (id<0 || id>=PSC_getNrOfNodes()) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: id %d out of range", __func__, id);
	PSID_errlog(errtxt, 0);
	return;
    }

    if (PSnodes_isUp(id)) {
	totalCPUs += physCPUs - PSnodes_getPhysCPUs(id);
    } else {
	totalNodes++;
	totalCPUs += physCPUs;
    }
    PSnodes_bringUp(id);
    PSnodes_setPhysCPUs(id, physCPUs);
    PSnodes_setVirtCPUs(id, virtCPUs);

    /* Test the license */
    if (totalNodes > lic_numval(&config->licEnv, LIC_NODES, 0)) {
	if (id <= PSC_getMyID()) {
	    snprintf(errtxt, sizeof(errtxt), "%s: too many nodes.", __func__);
	    PSID_errlog(errtxt, 0);
	    shutdownNode(1);
	    return;
	} else {
	    declareNodeDead(id);
	}
    }
    if (totalCPUs > lic_numval(&config->licEnv, LIC_CPUs, 0)) {
	if (id <= PSC_getMyID()) {
	    snprintf(errtxt, sizeof(errtxt), "%s: too many CPUs.", __func__);
	    PSID_errlog(errtxt, 0);
	    shutdownNode(1);
	    return;
	} else {
	    declareNodeDead(id);
	}
    }
}

int send_DAEMONCONNECT(PSnodes_ID_t id)
{
    DDBufferMsg_t msg = (DDBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_DD_DAEMONCONNECT,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getTID(id, 0),
	    .len = sizeof(msg.header) },
	.buf = {'\0'} };
    int32_t *CPUs = (int32_t *)msg.buf;

    CPUs[0] = PSnodes_getPhysCPUs(PSC_getMyID());
    CPUs[1] = PSnodes_getVirtCPUs(PSC_getMyID());
    msg.header.len += 2 * sizeof(*CPUs);

    return sendMsg(&msg);
}

void msg_DAEMONCONNECT(DDBufferMsg_t *msg)
{
    int id = PSC_getID(msg->header.sender);
    int32_t *CPUs = (int32_t *) msg->buf;
    int physCPUs = CPUs[0];
    int virtCPUs = CPUs[1];

    snprintf(errtxt, sizeof(errtxt), "%s(%d)", __func__, id);
    PSID_errlog(errtxt, 1);

    /*
     * accept this request and send an ESTABLISH msg back to the requester
     */
    declareNodeAlive(id, physCPUs, virtCPUs);

    msg->header = (DDMsg_t) {
	.type = PSP_DD_DAEMONESTABLISHED,
	.sender = PSC_getMyTID(),
	.dest = PSC_getTID(id, 0),
	.len = sizeof(msg->header) };

    CPUs[0] = PSnodes_getPhysCPUs(PSC_getMyID());
    CPUs[1] = PSnodes_getVirtCPUs(PSC_getMyID());
    msg->header.len += 2 * sizeof(*CPUs);

    if (sendMsg(msg) == -1 && errno != EWOULDBLOCK) {
	snprintf(errtxt, sizeof(errtxt), "%s: sendMsg() errno %d: %s",
		 __func__, errno, strerror(errno));
	PSID_errlog(errtxt, 2);
    } else {
	send_OPTIONS(id);
    }
}

void msg_DAEMONESTABLISHED(DDBufferMsg_t *msg)
{
    int id = PSC_getID(msg->header.sender);
    int32_t *CPUs = (int32_t *) msg->buf;
    int physCPUs = CPUs[0];
    int virtCPUs = CPUs[1];

    snprintf(errtxt, sizeof(errtxt), "%s(%d)", __func__, id);
    PSID_errlog(errtxt, 1);

    declareNodeAlive(id, physCPUs, virtCPUs);

    /* Send some info about me to the other node */
    send_OPTIONS(id);
}


int send_MASTER_IS(PSnodes_ID_t dest)
{
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_DD_MASTER_IS,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getTID(dest, 0),
	    .len = sizeof(msg.header) },
	.buf = {0} };

    *(PSnodes_ID_t *)msg.buf = getMasterID();
    return sendMsg(&msg);
}

void msg_MASTER_IS(DDBufferMsg_t *msg)
{
    PSnodes_ID_t newMaster = *(PSnodes_ID_t *)msg->buf;

    if (newMaster != getMasterID()) {
	if (newMaster < getMasterID()) {
	    /* @todo */
	    /* Try to connect new master */
	} else {
	    send_MASTER_IS(PSC_getID(msg->header.sender));
	}
    }
}

void msg_LOAD(DDBufferMsg_t *msg)
{
    char *ptr = msg->buf;
    PSnodes_ID_t client = PSC_getID(msg->header.sender);

    if (PSC_getMyID() != getMasterID()) {
	send_MASTER_IS(PSC_getID(msg->header.sender));
    } else {
	clientStat[client].jobs = *(PSID_Jobs_t *)ptr;
	ptr += sizeof(PSID_Jobs_t);

	clientStat[client].load = *(PSID_Load_t *)ptr;
	ptr += sizeof(PSID_Load_t);
    }
}

void msg_DEAD_NODE(DDBufferMsg_t *msg)
{
    PSnodes_ID_t deadNode = *(PSnodes_ID_t *)msg->buf;

    send_MASTER_IS(deadNode);
}

void msg_ACTIVE_NODES(DDBufferMsg_t *msg)
{}
