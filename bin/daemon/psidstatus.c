/*
 *               ParaStation
 * psidstatus.c
 *
 * Helper functions for master-node detection and status actions.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidstatus.c,v 1.2 2004/01/09 16:13:12 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidstatus.c,v 1.2 2004/01/09 16:13:12 eicker Exp $";
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
#include "psidtimer.h"

#include "psidstatus.h"

static char errtxt[256]; /**< General string to create error messages */

/** The jobs of the local node node */
static PSID_Jobs_t myJobs = { .normal = 0, .total = 0 };

/**
 * @brief Get load information from kernel.
 *
 * Get load information from the kernel. The implementation is platform
 * specific, since POSIX has no mechanism to retrieve this info.
 *
 * @return A @ref PSID_Load_t structure containing the load info.
 */
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

/** Structure used to hold status information of all nodes on the master */
typedef struct {
    struct timeval lastPing;  /**< Timestamp of last received ping */
    PSID_Jobs_t jobs;   /**< Number of jobs on the node */  
    PSID_Load_t load;   /**< Load parameters of node */
    short missCounter;  /**< Number of consecutively missing status pings */
    short placedJobs;   /**< @todo Move to psidpartition.c */
} ClientStatus_t;

/**
 * Master's array holding status information of all nodes. Will be
 * allocated on demand within @ref allocMasterSpace()
 */
static ClientStatus_t *clientStat = NULL;

/**
 * @brief Allocate master space.
 *
 * Allocate space needed in order to fulfill all the tasks a master
 * node is expected to handle. @ref clientStat will point to this space.
 *
 * Use @ref freeMasterSpace() to release the no longer needed space.
 *
 * @return No return value.
 *
 * @see freeMasterSpace()
 */
static void allocMasterSpace(void)
{
    PSnodes_ID_t node;

    clientStat = realloc(clientStat, PSC_getNrOfNodes() * sizeof(*clientStat));
    for (node=0; node<PSC_getNrOfNodes(); node++) {
	gettimeofday(&clientStat[node].lastPing, NULL);
	clientStat[node].missCounter = 0;
	clientStat[node].placedJobs = 0;
    }
}

/**
 * @brief Free master space.
 *
 * Free space needed by master when master burdon is passed to next
 * node. The space has to be allocated using @ref allocMasterSpace().
 *
 * @return No return value.
 *
 * @see allocMasterSpace()
 */
static void freeMasterSpace(void)
{
    if (clientStat) {
	free(clientStat);
	clientStat = NULL;
    }
}

/**
 * Major timeout after which status ping are sent and @ref
 * handleMasterTask() is called.
 */
static struct timeval StatusTimeout = { .tv_sec = 2, .tv_usec = 0 };

/**
 * Number of consecutive status pings allowed to miss before a node is
 * declared to be dead.
*/
static const int DeadLimit = 5;

/**
 * @brief Master handling routine.
 *
 * All the stuff the master has to handle when @ref StatusTimeout is
 * elapsed. This includes test if all expected status pings were
 * received and ringing down nodes in order to prevent decomposition
 * of the cluster into independent sub-clusters.
 *
 * @return No return value.
 */
static void handleMasterTasks(void)
{
    static PSnodes_ID_t next = 0;
    PSnodes_ID_t node;
    static int round = 0;
    int nrDownNodes = 0;
    struct timeval tv1, tv2;

    gettimeofday(&tv2, NULL);
    mytimersub(&tv2, StatusTimeout.tv_sec, StatusTimeout.tv_usec);
    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (PSnodes_isUp(node)) {
	    if (timercmp(&clientStat[node].lastPing, &tv2, <)) {
		/* no ping in the last 'round' */
		snprintf(errtxt, sizeof(errtxt),
			 "%s: Ping from node %d missing [%d]",
			 __func__, node, clientStat[node].missCounter);
		clientStat[node].missCounter++;
		PSID_errlog(errtxt, 5);
	    }
	    if (clientStat[node].missCounter > DeadLimit) {
		snprintf(errtxt, sizeof(errtxt),
			 "%s: misscount exceeded to node %d", __func__, node);
		PSID_errlog(errtxt, 1);
		send_DAEMONCONNECT(node);
	    }
	} else {
	    nrDownNodes++;
	}
    }

    round %= 10;
    if (!round) {
	int count = (nrDownNodes > 10) ? 10 : nrDownNodes;
	while (count) {
	    next %= PSC_getNrOfNodes();
	    if (!PSnodes_isUp(next)) {
		send_DAEMONCONNECT(next);
		count--;
	    }
	    next++;
	}
    }
    round++;

    return;
}

/**
 * @brief Send status ping.
 *
 * Send a status ping 
 *
 * @return No return value.
 */
static void sendRDPPing(void)
{
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_DD_LOAD,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getTID(getMasterID(), 0),
	    .len = sizeof(msg.header) },
	.buf = {'\0'} };
    char *ptr = msg.buf;

    *(PSID_Jobs_t *)ptr = myJobs;
    ptr += sizeof(PSID_Jobs_t);
    msg.header.len += sizeof(PSID_Jobs_t);

    *(PSID_Load_t *)ptr = getLoad();
    ptr += sizeof(PSID_Load_t);
    msg.header.len += sizeof(PSID_Load_t);

    if (getMasterID() == PSC_getMyID()) {
	msg_LOAD(&msg);
	handleMasterTasks();
    } else {
	sendMsg(&msg);
    }
}

void incJobs(int total, int normal)
{
    snprintf(errtxt, sizeof(errtxt), "%s(%d,%d)", __func__, total, normal);
    PSID_errlog(errtxt, 10);

    if (total) myJobs.total++;
    if (normal) myJobs.normal++;

    if (config->useMCast) {
	incJobsMCast(PSC_getMyID(), total, normal);
    } else {
	if (knowMaster()) sendRDPPing();
    }
}

void decJobs(int total, int normal)
{
    snprintf(errtxt, sizeof(errtxt), "%s(%d,%d)", __func__, total, normal);
    PSID_errlog(errtxt, 10);

    if (total) myJobs.total--;
    if (normal) myJobs.normal--;

    if (config->useMCast) {
	decJobsMCast(PSC_getMyID(), total, normal);
    } else {
	if (knowMaster()) sendRDPPing();
    }
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

/**
 * ID of the timer used for status control. We need to store this in
 * order to be able to release the timer again.
 *
 * Furthermore this is used for identifying, if the master is already
 * known.
 */
static int timerID = -1;

/** The actual master node. */
static PSnodes_ID_t masterNode = 0;

void declareMaster(PSnodes_ID_t newMaster)
{
    snprintf(errtxt, sizeof(errtxt), "%s(%d)", __func__, newMaster);
    PSID_errlog(errtxt, 2);

    if (knowMaster() && newMaster == getMasterID()) return;

    if (knowMaster() && getMasterID() == PSC_getMyID()) freeMasterSpace();

    if (newMaster == PSC_getMyID()) allocMasterSpace();

    masterNode = newMaster;

    if (config->useMCast) {
	timerID = 0;
	return;
    }

    if (!knowMaster()) {
	if (!Timer_isInitialized()) {
	    Timer_init(config->useSyslog);
	}
	timerID = Timer_register(&StatusTimeout, sendRDPPing); 
	sendRDPPing();
    }
}

int knowMaster(void)
{
    return (timerID != -1);
}

PSnodes_ID_t getMasterID(void)
{
    return masterNode;
}

void releaseStatusTimer(void)
{
    if (knowMaster()) Timer_remove(timerID);
}

/** Total number of nodes connected. Needed for license testing */
static int totalNodes = 0;

/** Total number of physical CPUs connected. Needed for license testing */
static int totalCPUs = 0;

/* Prototype forward declaration. */
static int send_DEADNODE(PSnodes_ID_t deadnode);

void declareNodeDead(PSnodes_ID_t id, int sendDeadnode)
{
    PStask_t *task;

    if (!PSnodes_isUp(id)) return;

    snprintf(errtxt, sizeof(errtxt), "%s: node %d send DEADNODE %d",
	     __func__, id, sendDeadnode);
    PSID_errlog(errtxt, 2);

    totalCPUs -= PSnodes_getPhysCPUs(id);
    totalNodes--;
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

    snprintf(errtxt, sizeof(errtxt), "%s: connection lost to node %d",
	     __func__, id);
    PSID_errlog(errtxt, 0);

    if (id == getMasterID()) {
	/* Dead node was master, find new one */
	PSnodes_ID_t node = id;

	while (node < PSC_getMyID()) {
	    if (PSnodes_isUp(node)) break;
	    node++;
	}

	snprintf(errtxt, sizeof(errtxt), "%s: new master %d", __func__, node);
	PSID_errlog(errtxt, 2);

	declareMaster(node);
    }

    if (config->useMCast) return;

    if (knowMaster() && getMasterID() == PSC_getMyID() && sendDeadnode) {
	send_DEADNODE(id);
    }
}

/* External function. @todo */
int shutdownNode(int phase);

/* Prototype forward declaration */
static int send_MASTERIS(PSnodes_ID_t dest);
static int send_ACTIVENODES(PSnodes_ID_t dest);

void declareNodeAlive(PSnodes_ID_t id, int physCPUs, int virtCPUs)
{
    int wasUp = PSnodes_isUp(id);

    snprintf(errtxt, sizeof(errtxt), "%s: node %d", __func__, id);
    PSID_errlog(errtxt, 2);

    if (id<0 || id>=PSC_getNrOfNodes()) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: id %d out of range", __func__, id);
	PSID_errlog(errtxt, 0);
	return;
    }

    if (wasUp) {
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
	    declareNodeDead(id, 0);
	}
    }
    if (totalCPUs > lic_numval(&config->licEnv, LIC_CPUs, 0)) {
	if (id <= PSC_getMyID()) {
	    snprintf(errtxt, sizeof(errtxt), "%s: too many CPUs.", __func__);
	    PSID_errlog(errtxt, 0);
	    shutdownNode(1);
	    return;
	} else {
	    declareNodeDead(id, 0);
	}
    }

    if ((!knowMaster() && id != PSC_getMyID()) || id < getMasterID()) {
	/* New node will be master */
	PSnodes_ID_t oldMaster = knowMaster() ? getMasterID() : PSC_getMyID();

	snprintf(errtxt, sizeof(errtxt), "%s: new master %d", __func__, id);
	PSID_errlog(errtxt, 2);

	declareMaster(id);

	if (config->useMCast) return;

	if (oldMaster != PSC_getMyID()) send_MASTERIS(oldMaster);
    }

    if (config->useMCast) return;

    if (getMasterID() == PSC_getMyID() && !wasUp) {
	send_ACTIVENODES(id);
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

    snprintf(errtxt, sizeof(errtxt), "%s(%d)", __func__, id);
    PSID_errlog(errtxt, 10);
    
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

int send_DAEMONSHUTDOWN(void)
{
    DDMsg_t msg = {
	.type = PSP_DD_DAEMONSHUTDOWN,
	.sender = PSC_getMyTID(),
	.dest = 0,
	.len = sizeof(msg) };

    return broadcastMsg(&msg);
}

void msg_DAEMONSHUTDOWN(DDMsg_t *msg)
{
    snprintf(errtxt, sizeof(errtxt), "%s(%d)",
	     __func__, PSC_getID(msg->sender));
    PSID_errlog(errtxt, 10);
    declareNodeDead(PSC_getID(msg->sender), 0);
}

void msg_LOAD(DDBufferMsg_t *msg)
{
    char *ptr = msg->buf;
    PSnodes_ID_t client = PSC_getID(msg->header.sender);

    if (PSC_getMyID() != getMasterID()) {
	send_MASTERIS(PSC_getID(msg->header.sender));
    } else {
	clientStat[client].jobs = *(PSID_Jobs_t *)ptr;
	ptr += sizeof(PSID_Jobs_t);

	clientStat[client].load = *(PSID_Load_t *)ptr;
	ptr += sizeof(PSID_Load_t);

	gettimeofday(&clientStat[client].lastPing, NULL);
	clientStat[client].missCounter = 0;
    }
}

/**
 * @brief Send a PSP_DD_MASTER_IS message.
 *
 * Send a PSP_DD_MASTER_IS message to node @a dest.
 *
 * @param dest ParaStation ID of the node to send the message to.
 *
 * @return On success, the number of bytes sent within the
 * PSP_DD_MASTER_IS message is returned. If an error occured, -1 is
 * returned and errno is set appropriately.
 */
static int send_MASTERIS(PSnodes_ID_t dest)
{
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_DD_MASTER_IS,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getTID(dest, 0),
	    .len = sizeof(msg.header) },
	.buf = {'\0'} };

    *(PSnodes_ID_t *)msg.buf = getMasterID();
    return sendMsg(&msg);
}

void msg_MASTERIS(DDBufferMsg_t *msg)
{
    PSnodes_ID_t newMaster = *(PSnodes_ID_t *)msg->buf;

    if (newMaster != getMasterID()) {
	if (newMaster < getMasterID()) {
	    send_DAEMONCONNECT(newMaster);
	} else {
	    send_MASTERIS(PSC_getID(msg->header.sender));
	}
    }
}

/**
 * @brief Send a PSP_DD_ACTIVE_NODES message.
 *
 * Send a PSP_DD_ACTIVE_NODES message to node @a dest.
 *
 * @param dest ParaStation ID of the node to send the message to.
 *
 * @return On success, the number of bytes sent within the
 * PSP_DD_ACTIVE_NODES message is returned. If an error occured, -1 is
 * returned and errno is set appropriately.
 */
static int send_ACTIVENODES(PSnodes_ID_t dest)
{
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_DD_ACTIVE_NODES,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getTID(dest, 0),
	    .len = sizeof(msg.header) },
	.buf = {'\0'} };

    PSnodes_ID_t node, *nodeBuf = (PSnodes_ID_t *)msg.buf;
    unsigned int idx = 0;
    int total = 0;

    if (dest == PSC_getMyID()) return 0;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (PSnodes_isUp(node)) {
	    nodeBuf[idx] = node;
	    idx++;
	    if (idx == NODES_CHUNK) {
		int ret;
		msg.header.len += idx * sizeof(PSnodes_ID_t);
		ret = sendMsg(&msg);
		if (ret<0) {
		    return ret;
		} else {
		    total += ret;
		}
		msg.header.len -= idx * sizeof(PSnodes_ID_t);
		idx=0;
	    }
	}
    }
    if (idx) {
	int ret;
	msg.header.len += idx * sizeof(PSnodes_ID_t);
	ret = sendMsg(&msg);
	if (ret<0) {
	    return ret;
	} else {
	    total += ret;
	}
    }

    return total;
}

void msg_ACTIVENODES(DDBufferMsg_t *msg)
{
    int num = (msg->header.len - sizeof(msg->header)) / sizeof(PSnodes_ID_t);
    PSnodes_ID_t *nodeBuf = (PSnodes_ID_t *)msg->buf;
    int idx;

    snprintf(errtxt, sizeof(errtxt), "%s: num is %d", __func__, num);
    PSID_errlog(errtxt, 2);

    for (idx=0; idx<num; idx++) {
	snprintf(errtxt, sizeof(errtxt), "%s: %d. is %d",
		 __func__, idx, nodeBuf[idx]);
	PSID_errlog(errtxt, 10);
	if (!PSnodes_isUp(nodeBuf[idx])) send_DAEMONCONNECT(nodeBuf[idx]);
    }
}

/**
 * @brief Broadcast a PSP_DD_DEAD_NODE message.
 *
 * Broadcast a PSP_DD_DEAD_NODE message to all active nodes.
 *
 * @return On success, the number of nodes the PSP_DD_DEAD_NODE
 * message is sent to is returned, i.e. the value returned by the @ref
 * broadcastMsg() call. If an error occured, -1 is returned and errno
 * is set appropriately.
 */
static int send_DEADNODE(PSnodes_ID_t deadnode)
{
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_DD_DEAD_NODE,
	    .sender = PSC_getMyTID(),
	    .dest = 0,
	    .len = sizeof(msg.header) },
	.buf = {'\0'} };
    *(PSnodes_ID_t *)msg.buf = deadnode;

    msg.header.len += sizeof(PSnodes_ID_t);

    snprintf(errtxt, sizeof(errtxt), "%s: node %d", __func__, deadnode);
    PSID_errlog(errtxt, 2);

    return broadcastMsg(&msg);
}

void msg_DEADNODE(DDBufferMsg_t *msg)
{
    PSnodes_ID_t deadNode = *(PSnodes_ID_t *)msg->buf;

    snprintf(errtxt, sizeof(errtxt), "%s(%d)", __func__, deadNode);
    PSID_errlog(errtxt, 10);

    send_DAEMONCONNECT(deadNode);
}
