/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2009 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
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

#include "mcast.h"
#include "rdp.h"
#include "timer.h"

#include "psidutil.h"
#include "psidnodes.h"
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

/**
 * @brief Get memory information from kernel.
 *
 * Get memory information from the kernel. The implementation is
 * platform specific, since POSIX has no mechanism to retrieve this
 * info.
 *
 * @return A @ref PSID_Mem_t structure containing the load info.
 */
static PSID_Mem_t getMem(void)
{
    PSID_Mem_t mem = {-1, -1};
#ifdef __linux__
    struct sysinfo s_info;

    sysinfo(&s_info);
    mem.total = s_info.totalram * s_info.mem_unit;
    mem.free = s_info.freeram * s_info.mem_unit;
#else
#error BAD OS !!!!
#endif

    return mem;
}

/** Structure used to hold status information of all nodes on the master */
typedef struct {
    struct timeval lastPing;  /**< Timestamp of last received ping */
    PSID_Jobs_t jobs;   /**< Number of jobs on the node */
    PSID_Load_t load;   /**< Load parameters of node */
    PSID_Mem_t mem;     /**< Memory parameters of node */
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
	clientStat[node].mem = (PSID_Mem_t) { -1, -1 };
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
static int DeadLimit = 5;

int getDeadLimit(void)
{
    return DeadLimit;
}

void setDeadLimit(int limit)
{
    if (limit > 0) DeadLimit = limit;
}

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

    PSID_log(PSID_LOG_STATUS, "%s\n", __func__);

    gettimeofday(&tv, NULL);
    mytimersub(&tv, StatusTimeout.tv_sec, StatusTimeout.tv_usec);
    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (PSIDnodes_isUp(node)) {
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
	    if (!PSIDnodes_isUp(next)) {
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
 * Send a status ping.
 *
 * For masters with protocol versions before 334 some entries might be
 * not correctly aligned. This is fixed in later versions.
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

    PSID_log(PSID_LOG_STATUS, "%s to %d\n", __func__, getMasterID());

    if (PSIDnodes_getProtoVersion(getMasterID()) < 334) {
	*(PSID_Jobs_t *)ptr = myJobs;
	ptr += sizeof(PSID_Jobs_t);
	msg.header.len += sizeof(PSID_Jobs_t);

	*(PSID_Load_t *)ptr = getLoad();
	ptr += sizeof(PSID_Load_t);
	msg.header.len += sizeof(PSID_Load_t);

	*(int *)ptr = totalNodes;
	ptr += sizeof(int);
	msg.header.len += sizeof(int);

	*(PSID_Mem_t *)ptr = getMem();
	ptr += sizeof(PSID_Mem_t);
	msg.header.len += sizeof(PSID_Mem_t);
    } else {
	*(PSID_Jobs_t *)ptr = myJobs;
	ptr += sizeof(PSID_Jobs_t);
	msg.header.len += sizeof(PSID_Jobs_t);
	/* Now we are on a 64-bit boundary */

	*(PSID_Load_t *)ptr = getLoad();
	ptr += sizeof(PSID_Load_t);
	msg.header.len += sizeof(PSID_Load_t);
	/* Load_t contains doubles -> still on 64-bit */

	*(PSID_Mem_t *)ptr = getMem();
	ptr += sizeof(PSID_Mem_t);
	msg.header.len += sizeof(PSID_Mem_t);
	/* Load_t contains uint64_t -> still on 64-bit */

	*(int *)ptr = totalNodes;
	ptr += sizeof(int);
	msg.header.len += sizeof(int);
    }

    sendMsg(&msg);
    if (getMasterID() == PSC_getMyID()) handleMasterTasks();
}

/**
 * ID of the timer used for status control. We need to store this in
 * order to be able to release the timer again.
 *
 * Furthermore this is used for identifying, if the master is already
 * known.
 */
static int timerID = -1;

int getStatusTimeout(void)
{
    return StatusTimeout.tv_sec * 1000 + StatusTimeout.tv_usec / 1000;
}

void setStatusTimeout(int timeout)
{
    if (timeout < MIN_TIMEOUT_MSEC) return;

    StatusTimeout.tv_sec = timeout / 1000;
    StatusTimeout.tv_usec = (timeout%1000) * 1000;

    if (timerID > 0) {
	Timer_block(timerID, 1);
	releaseStatusTimer();
	timerID = Timer_register(&StatusTimeout, sendRDPPing);
	if (timerID < 0) {
	    PSID_log(-1, "%s: Failed to re-register status timer\n", __func__);
	}
    }
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

PSID_NodeStatus_t getStatusInfo(PSnodes_ID_t node)
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

PSID_Mem_t getMemoryInfo(PSnodes_ID_t node)
{
    PSID_Mem_t memory;

    if (config->useMCast) {
	memory = (PSID_Mem_t) { -1, -1 };
    } else {
	if (node == PSC_getMyID()) {
	    memory = getMem();
	} else if ((PSC_getMyID() != getMasterID())
	    || (node<0) || (node>PSC_getNrOfNodes()) || !clientStat) {
	    memory = (PSID_Mem_t) { -1, -1 };
	} else {
	    memory = clientStat[node].mem;
	}
    }

    return memory;
}

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
    if (timerID > 0) Timer_remove(timerID);
    timerID = -1;
}

/* Prototype forward declaration. */
static int send_DEADNODE(PSnodes_ID_t deadnode);

void declareNodeDead(PSnodes_ID_t id, int sendDeadnode, int silent)
{
    PStask_t *task;

    if (!PSIDnodes_isUp(id)) return;

    PSID_log(PSID_LOG_STATUS,
	     "%s: node %d goes down. Will %ssend PSP_DD_DEAD_NODE messages\n",
	     __func__, id, sendDeadnode ? "" : "not ");

    totalNodes--;
    PSIDnodes_bringDown(id);
    PSIDnodes_setPhysCPUs(id, 0);
    PSIDnodes_setVirtCPUs(id, 0);

    clearRDPMsgs(id);

    if (config->useMCast) declareNodeDeadMCast(id);

    /* Send signals to all processes that controlled task on the dead node */
    for (task=managedTasks; task; task=task->next) {
	PStask_ID_t sender;
	int sig;
	if (task->deleted) continue;
	/* loop over all controlled tasks */
	while ((sender = PSID_getSignalByID(& task->assignedSigs, id, &sig))) {
	    /* controlled task was on dead node */

	    /* This might have been a child */
	    if (sig == -1) PSID_removeSignal(&task->childs, sender, sig);
	    if (task->removeIt && !task->childs) break;

	    /* Send the signal */
	    PSID_sendSignal(task->tid, task->uid, sender, sig, 0, 0);
	}
	if (task->removeIt && ! task->childs) {
	    PSID_log(PSID_LOG_TASK, "%s: PStask_cleanup()\n", __func__);
	    PStask_cleanup(task->tid);
	}
    }

    /* Disable accounters located on dead node */
    PSID_cleanAcctFromNode(id);

    PSID_log(silent ? PSID_LOG_STATUS : -1, "%s: connection lost to node %d\n",
	     __func__, id);

    if (id == getMasterID()) {
	/* Dead node was master, find new one */
	PSnodes_ID_t node = id;

	while (node < PSC_getMyID()) {
	    if (PSIDnodes_isUp(node)) break;
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
    int wasUp = PSIDnodes_isUp(id);

    PSID_log(PSID_LOG_STATUS, "%s: node %d\n", __func__, id);

    if (id<0 || id>=PSC_getNrOfNodes()) {
	PSID_log(-1, "%s: id %d out of range\n", __func__, id);
	return;
    }

    if (!wasUp) {
	totalNodes++;
    }
    PSIDnodes_bringUp(id);
    PSIDnodes_setPhysCPUs(id, physCPUs);
    PSIDnodes_setVirtCPUs(id, virtCPUs);

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

    if (PSIDnodes_getAddr(id) == INADDR_ANY) {
	errno = EHOSTUNREACH;
	return -1;
    }

    CPUs[0] = PSIDnodes_getPhysCPUs(PSC_getMyID());
    CPUs[1] = PSIDnodes_getVirtCPUs(PSC_getMyID());
    msg.header.len += 2 * sizeof(*CPUs);

    return sendMsg(&msg);
}

/**
 * @brief Handle a PSP_DD_DAEMONCONNECT message.
 *
 * Handle the message @a msg of type PSP_DD_DAEMONCONNECT.
 *
 * A PSP_DD_DAEMONCONNECT message is sent whenever a daemon detects a
 * node it is not connected to. Receiving this message provides
 * information on the setup and status of the sending node.
 *
 * This message is answered by a PSP_DD_DAEMONESTABLISHED message
 * providing the corresponding information to the connecting node.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_DAEMONCONNECT(DDBufferMsg_t *msg)
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

    CPUs[0] = PSIDnodes_getPhysCPUs(PSC_getMyID());
    CPUs[1] = PSIDnodes_getVirtCPUs(PSC_getMyID());
    msg->header.len += 2 * sizeof(*CPUs);

    if (sendMsg(msg) == -1 && errno != EWOULDBLOCK) {
	PSID_warn(PSID_LOG_STATUS, errno, "%s: sendMsg()", __func__);
    } else {
	send_OPTIONS(id);
    }
}

/**
 * @brief Handle a PSP_DD_DAEMONESTABLISHED message.
 *
 * Handle the message @a msg of type PSP_DD_DAEMONESTABLISHED.
 *
 * Receiving this answer on a PSP_DD_DAEMONCONNECT message sent to the
 * sending node provides the local daemon with the information on the
 * setup and status of this other node.
 *
 * With the receive of this message the setup of the daemon-daemon
 * connection is finished and the other node is marked to be up now.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_DAEMONESTABLISHED(DDBufferMsg_t *msg)
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

/**
 * @brief Handle a PSP_DD_DAEMONSHUTDOWN message.
 *
 * Handle the message @a msg of type PSP_DD_DAEMONSHUTDOWN.
 *
 * This kind of messages tells the receiver that the sending node will
 * go down soon and no longer accepts messages for receive. Thus this
 * node should be marked to be down now.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_DAEMONSHUTDOWN(DDMsg_t *msg)
{
    PSnodes_ID_t id = PSC_getID(msg->sender);

    PSID_log(PSID_LOG_STATUS, "%s(%d)\n", __func__, id);
    declareNodeDead(id, 0, 1);
    closeConnRDP(id);
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
    msg.header.len += sizeof(PSnodes_ID_t);

    PSID_log(PSID_LOG_STATUS, "%s: tell %s master is %d\n",
	     __func__, PSC_printTID(msg.header.dest), getMasterID());

    return sendMsg(&msg);
}

/**
 * @brief Handle a PSP_DD_MASTER_IS message.
 *
 * Handle the message @a msg of type PSP_DD_MASTER_IS.
 *
 * The sending node give a hint on the correct master. If the local
 * information differs from the information provided, one of two
 * measures will be taken:
 *
 * - If the master provided has a node number smaller than the current
 * master, it will be tried to contact this new master via @ref
 * send_DAEMONCONNECT().
 *
 * - Otherwise a PSP_DD_MASTER_IS message is sent to the sender in
 * order to inform on the actual master node.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_MASTERIS(DDBufferMsg_t *msg)
{
    PSnodes_ID_t newMaster = *(PSnodes_ID_t *)msg->buf;

    PSID_log(PSID_LOG_STATUS, "%s: %s says master is %d\n", __func__,
	     PSC_printTID(msg->header.sender), newMaster);

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
	if (PSIDnodes_isUp(node)) {
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

/**
 * @brief Handle a PSP_DD_ACTIVE_NODES message.
 *
 * Handle the message @a msg of type PSP_DD_ACTIVE_NODES.
 *
 * Whenever a daemon node connects to a new node, one or more
 * PSP_DD_ACTIVE_NODES messages are sent the this node in order to
 * inform about the active nodes currently known to the master. The
 * receiving node will try to contact each of this nodes provided in
 * order to setup a working connection.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_ACTIVENODES(DDBufferMsg_t *msg)
{
    int num = (msg->header.len - sizeof(msg->header)) / sizeof(PSnodes_ID_t);
    PSnodes_ID_t *nodeBuf = (PSnodes_ID_t *)msg->buf;
    PSnodes_ID_t senderNode = PSC_getID(msg->header.sender);
    static PSnodes_ID_t firstUntested = 0;
    int idx;

    PSID_log(PSID_LOG_STATUS, "%s: num is %d\n", __func__, num);

    for (idx=0; idx<num; idx++) {
	PSnodes_ID_t currentNode = nodeBuf[idx], n;
	PSID_log(PSID_LOG_STATUS,
		 "%s: %d. is %d\n", __func__, idx, nodeBuf[idx]);
	if (currentNode == senderNode) {
	    /* Sender is first active node, all previous nodes are down */
	    for (n=0; n<currentNode; n++) {
		if (PSIDnodes_isUp(n)) send_DAEMONCONNECT(n);
	    }
	    firstUntested = currentNode+1;
	}
	for (n=firstUntested; n<currentNode; n++) {
	    if (PSIDnodes_isUp(n)) send_MASTERIS(n);
	}
	if (!PSIDnodes_isUp(currentNode)) send_DAEMONCONNECT(currentNode);
	firstUntested = currentNode+1;
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

/**
 * @brief Handle a PSP_DD_DEAD_NODE message.
 *
 * Handle the message @a msg of type PSP_DD_DEAD_NODE.
 *
 * Whenever a daemon node detects a node to be down all other nodes
 * will be informed about this fact via a PSP_DD_DEAD_NODE
 * message. Each node receiving this kind of message will try contact
 * the according node and usually mark this node as dead via a
 * callback from daemon's the RDP facility.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_DEADNODE(DDBufferMsg_t *msg)
{
    PSnodes_ID_t deadNode = *(PSnodes_ID_t *)msg->buf;

    PSID_log(PSID_LOG_STATUS, "%s(%d)\n", __func__, deadNode);

    send_DAEMONCONNECT(deadNode);
}

/**
 * @brief Handle a PSP_DD_LOAD message.
 *
 * Handle the message @a msg of type PSP_DD_LOAD.
 *
 * PSP_DD_LOAD messages are send by each node to the current master
 * process. Thus upon receive of this kind of message by a node not
 * acting as the master, a PSP_DD_MASTER_IS message will be initiated
 * in order to inform the sending node about the actual master
 * process.
 *
 * The master process will handle this message by storing the
 * information contained to the local status arrays.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_LOAD(DDBufferMsg_t *msg)
{
    char *ptr = msg->buf;
    PSnodes_ID_t client = PSC_getID(msg->header.sender);

    if (PSC_getMyID() != getMasterID()) {
	send_MASTERIS(PSC_getID(msg->header.sender));
    } else {
	int clientNodes;

	if (PSIDnodes_getProtoVersion(client) < 334) {
	    clientStat[client].jobs = *(PSID_Jobs_t *)ptr;
	    ptr += sizeof(PSID_Jobs_t);

	    clientStat[client].load = *(PSID_Load_t *)ptr;
	    ptr += sizeof(PSID_Load_t);

	    clientNodes = *(int *)ptr;
	    ptr += sizeof(int);

	    if (((void *)ptr - (void *)msg) < msg->header.len) {
		clientStat[client].mem = *(PSID_Mem_t *)ptr;
		ptr += sizeof(PSID_Mem_t);
	    }
	} else {
	    clientStat[client].jobs = *(PSID_Jobs_t *)ptr;
	    ptr += sizeof(PSID_Jobs_t);

	    clientStat[client].load = *(PSID_Load_t *)ptr;
	    ptr += sizeof(PSID_Load_t);

	    clientStat[client].mem = *(PSID_Mem_t *)ptr;
	    ptr += sizeof(PSID_Mem_t);

	    clientNodes = *(int *)ptr;
	    ptr += sizeof(int);
	}

	gettimeofday(&clientStat[client].lastPing, NULL);
	clientStat[client].missCounter = 0;

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

void initStatus(void)
{
    PSID_log(PSID_LOG_VERB, "%s()\n", __func__);

    PSID_registerMsg(PSP_DD_DAEMONCONNECT, msg_DAEMONCONNECT);
    PSID_registerMsg(PSP_DD_DAEMONESTABLISHED, msg_DAEMONESTABLISHED);
    PSID_registerMsg(PSP_DD_DAEMONSHUTDOWN, (handlerFunc_t) msg_DAEMONSHUTDOWN);
    PSID_registerMsg(PSP_DD_MASTER_IS, msg_MASTERIS);
    PSID_registerMsg(PSP_DD_ACTIVE_NODES, msg_ACTIVENODES);
    PSID_registerMsg(PSP_DD_DEAD_NODE, msg_DEADNODE);
    PSID_registerMsg(PSP_DD_LOAD, msg_LOAD);
}
