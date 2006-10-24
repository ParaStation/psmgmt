/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2006 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

/* Extra includes for load-determination */
#ifdef __linux__
#include <sys/sysinfo.h>
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
#include "psidpartition.h"
#include "psidaccount.h"

#include "psidstatus.h"

/** The jobs of the local node node */
static PSID_Jobs_t myJobs = { .normal = 0, .total = 0 };

/** Total number of nodes connected. Needed for keep-alive pings */
static int totalNodes = 0;

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
#ifdef __linux__
    struct sysinfo s_info;

    sysinfo(&s_info);
    load.load[0] = (double) s_info.loads[0] / (1<<SI_LOAD_SHIFT);
    load.load[1] = (double) s_info.loads[1] / (1<<SI_LOAD_SHIFT);
    load.load[2] = (double) s_info.loads[2] / (1<<SI_LOAD_SHIFT);
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
    short wrongClients; /**< Number of consecutively wrong client numbers */
} ClientStatus_t;

/**
 * Maximum number of consecutive wrong client numbers within load
 * messages. If this number of wrong counts is exceeded, a list
 * containing the actually active clients is send to the corresponding
 * node via @ref send_ACTIVENODES().
 */
static const short MAX_WRONG_CLIENTS = 10;

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
	clientStat[node].jobs = (PSID_Jobs_t) { .normal = 0, .total = 0 };
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
    exitPartHandler();
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
    struct timeval tv;

    gettimeofday(&tv, NULL);
    mytimersub(&tv, StatusTimeout.tv_sec, StatusTimeout.tv_usec);
    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (PSnodes_isUp(node)) {
	    if (timercmp(&clientStat[node].lastPing, &tv, <)) {
		/* no ping in the last 'round' */
		PSID_log(PSID_LOG_STATUS,
			 "%s: Ping from node %d missing [%d]\n",
			 __func__, node, clientStat[node].missCounter);
		clientStat[node].missCounter++;
	    }
	    if (clientStat[node].missCounter > DeadLimit) {
		PSID_log(PSID_LOG_STATUS,
			 "%s: misscount exceeded to node %d\n",
			 __func__, node);
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

    *(int *)ptr = totalNodes;
    ptr += sizeof(int);
    msg.header.len += sizeof(int);

    sendMsg(&msg);
    if (getMasterID() == PSC_getMyID()) handleMasterTasks();
}

void incJobs(int total, int normal)
{
    PSID_log(PSID_LOG_STATUS, "%s(%d,%d)\n", __func__, total, normal);

    if (total) myJobs.total++;
    if (normal) myJobs.normal++;

    if (config->useMCast) incJobsMCast(PSC_getMyID(), total, normal);
}

void decJobs(int total, int normal)
{
    PSID_log(PSID_LOG_STATUS, "%s(%d,%d)\n", __func__, total, normal);

    if (total) myJobs.total--;
    if (normal) myJobs.normal--;

    if (config->useMCast) decJobsMCast(PSC_getMyID(), total, normal);
}

void decJobsHint(PSnodes_ID_t node)
{
    PSID_log(PSID_LOG_STATUS, "%s(%d)\n", __func__, node);

    if (clientStat && clientStat[node].jobs.normal)
	clientStat[node].jobs.normal--;
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
    PSID_log(PSID_LOG_STATUS, "%s(%d)\n", __func__, newMaster);

    if (knowMaster() && newMaster == getMasterID()) return;

    if (knowMaster() && getMasterID() == PSC_getMyID()) freeMasterSpace();

    if (newMaster == PSC_getMyID()) allocMasterSpace();

    masterNode = newMaster;

    if (config->useMCast) {
	timerID = 0;
    } else if (!knowMaster()) {
	if (!Timer_isInitialized()) {
	    Timer_init(config->logfile);
	}
	timerID = Timer_register(&StatusTimeout, sendRDPPing); 
	sendRDPPing();
    }

    if (newMaster == PSC_getMyID()) initPartHandler();
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

/* Prototype forward declaration. */
static int send_DEADNODE(PSnodes_ID_t deadnode);

void declareNodeDead(PSnodes_ID_t id, int sendDeadnode)
{
    PStask_t *task;

    if (!PSnodes_isUp(id)) return;

    PSID_log(PSID_LOG_STATUS,
	     "%s: node %d goes down. Will %ssend PSP_DD_DEAD_NODE messages\n",
	     __func__, id, sendDeadnode ? "" : "not ");

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
	PStask_t *next;
	/* loop over all controlled tasks */
	while (sig) {
	    if (PSC_getID(sig->tid)==id) {
		/* controlled task was on dead node */
		PStask_ID_t senderTid = sig->tid;
		int signal = sig->signal;

		/* Send the signal */
		PSID_sendSignal(task->tid, task->uid, senderTid, signal, 0, 0);

		sig = sig->next;
		/* Remove signal from list */
		PSID_removeSignal(&task->assignedSigs, senderTid, signal);
		if (signal == -1 && senderTid != task->ptid) {
		    /* This might have been a child */
		    PSID_removeSignal(&task->childs, senderTid, -1);
		}
	    } else {
		sig = sig->next;
	    }
	}
	next = task->next;
	if (task->removeIt && ! task->childs) {
	    PSID_log(PSID_LOG_TASK, "%s: PStask_cleanup()\n", __func__);
	    PStask_cleanup(task->tid);
	}
	task=next;
    }

    /* Disable accounters located on dead node */
    PSID_cleanAcctFromNode(id);

    PSID_log(-1, "%s: connection lost to node %d\n", __func__, id);

    if (id == getMasterID()) {
	/* Dead node was master, find new one */
	PSnodes_ID_t node = id;

	while (node < PSC_getMyID()) {
	    if (PSnodes_isUp(node)) break;
	    node++;
	}

	PSID_log(PSID_LOG_STATUS, "%s: new master %d\n", __func__, node);

	declareMaster(node);
    } else if (PSC_getMyID() == getMasterID()) {
	cleanupRequests(id);
    }

    if (config->useMCast) return;

    if (getMasterID() == PSC_getMyID() && sendDeadnode) {
	send_DEADNODE(id);
    }
}

/* Prototype forward declaration */
static int send_ACTIVENODES(PSnodes_ID_t dest);

void declareNodeAlive(PSnodes_ID_t id, int physCPUs, int virtCPUs)
{
    int wasUp = PSnodes_isUp(id);

    PSID_log(PSID_LOG_STATUS, "%s: node %d\n", __func__, id);

    if (id<0 || id>=PSC_getNrOfNodes()) {
	PSID_log(-1, "%s: id %d out of range\n", __func__, id);
	return;
    }

    if (!wasUp) {
	totalNodes++;
    }
    PSnodes_bringUp(id);
    PSnodes_setPhysCPUs(id, physCPUs);
    PSnodes_setVirtCPUs(id, virtCPUs);

    if (!knowMaster()) {
	if (id < PSC_getMyID()) {
	    PSID_log(PSID_LOG_STATUS, "%s: master %d\n", __func__, id);
	    declareMaster(id);
	} else if (id > PSC_getMyID()) {
	    PSnodes_ID_t mID = PSC_getMyID();
	    PSID_log(PSID_LOG_STATUS, "%s: master %d\n", __func__, mID);
	    declareMaster(mID);
	}
    } else if (id < getMasterID()) {
	/* New node will be master */
	PSnodes_ID_t oldMaster = getMasterID();

	PSID_log(PSID_LOG_STATUS, "%s: new master %d\n", __func__, id);

	declareMaster(id);

	if (!config->useMCast) send_MASTERIS(oldMaster);
    }

    if (!config->useMCast && getMasterID() == PSC_getMyID() && !wasUp) {
	send_ACTIVENODES(id);
    }

    if (getMasterID() == PSC_getMyID() && !wasUp) {
	send_GETTASKS(id);
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

    PSID_log(PSID_LOG_STATUS, "%s(%d)\n", __func__, id);
    
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

    PSID_log(PSID_LOG_STATUS, "%s(%d)\n", __func__, id);

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
	PSID_warn(PSID_LOG_STATUS, errno, "%s: sendMsg()", __func__);
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

    PSID_log(PSID_LOG_STATUS, "%s(%d)\n", __func__, id);

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
    PSID_log(PSID_LOG_STATUS, "%s(%d)\n", __func__, PSC_getID(msg->sender));
    declareNodeDead(PSC_getID(msg->sender), 0);
}

int send_MASTERIS(PSnodes_ID_t dest)
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

    PSID_log(PSID_LOG_STATUS, "%s: num is %d\n", __func__, num);

    for (idx=0; idx<num; idx++) {
	PSID_log(PSID_LOG_STATUS,
		 "%s: %d. is %d\n", __func__, idx, nodeBuf[idx]);
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

    PSID_log(PSID_LOG_STATUS, "%s(%d)\n", __func__, deadnode);

    return broadcastMsg(&msg);
}

void msg_DEADNODE(DDBufferMsg_t *msg)
{
    PSnodes_ID_t deadNode = *(PSnodes_ID_t *)msg->buf;

    PSID_log(PSID_LOG_STATUS, "%s(%d)\n", __func__, deadNode);

    send_DAEMONCONNECT(deadNode);
}

void msg_LOAD(DDBufferMsg_t *msg)
{
    char *ptr = msg->buf;
    PSnodes_ID_t client = PSC_getID(msg->header.sender);

    if (PSC_getMyID() != getMasterID()) {
	send_MASTERIS(PSC_getID(msg->header.sender));
    } else {
	int clientNodes;

	clientStat[client].jobs = *(PSID_Jobs_t *)ptr;
	ptr += sizeof(PSID_Jobs_t);

	clientStat[client].load = *(PSID_Load_t *)ptr;
	ptr += sizeof(PSID_Load_t);

	gettimeofday(&clientStat[client].lastPing, NULL);
	clientStat[client].missCounter = 0;

	clientNodes = *(int *)ptr;
	ptr += sizeof(int);

	if (clientNodes != totalNodes) {
	    clientStat[client].wrongClients++;

	    if (clientStat[client].wrongClients > MAX_WRONG_CLIENTS) {
		/* Too many wrong client counts. Try to fix this */
		send_ACTIVENODES(client);
		clientStat[client].wrongClients = 0;
	    }
	} else if (clientStat[client].wrongClients) {
	    clientStat[client].wrongClients = 0;
	}
    }
}
