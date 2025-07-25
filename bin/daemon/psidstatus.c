/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psidstatus.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/sysinfo.h>
#include <sys/time.h>

#include "list.h"
#include "pscio.h"
#include "pscommon.h"
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
#include "psidpartition.h"
#include "psidaccount.h"
#include "psidstate.h"
#include "psidspawn.h"
#include "psidscripts.h"
#include "psidhook.h"

/** Number of tasks on the local node */
static PSID_TaskCount_t localTasks = { .normal = 0, .total = 0 };

/** Total number of nodes connected. Needed for keep-alive pings */
static int32_t totNodes = 0;

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
    struct sysinfo s_info;

    sysinfo(&s_info);
    load.load[0] = (double) s_info.loads[0] / (1<<SI_LOAD_SHIFT);
    load.load[1] = (double) s_info.loads[1] / (1<<SI_LOAD_SHIFT);
    load.load[2] = (double) s_info.loads[2] / (1<<SI_LOAD_SHIFT);

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
static PSID_Mem_t getMemInfo(void)
{
    PSID_Mem_t mem = {-1, -1};
    struct sysinfo s_info;

    sysinfo(&s_info);
    mem.total = s_info.totalram * s_info.mem_unit;
    mem.free = s_info.freeram * s_info.mem_unit;

    return mem;
}

/** Structure used to hold status information of all nodes on the master */
typedef struct {
    struct timeval lastPing;  /**< Time-stamp of last received ping */
    PSID_TaskCount_t tasks;   /**< Number of tasks on the node */
    PSID_Load_t load;         /**< Load parameters of node */
    PSID_Mem_t mem;           /**< Memory parameters of node */
    short missCounter;        /**< # of consecutively missing status pings */
    short wrongClients;       /**< # of consecutively wrong client numbers */
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
    clientStat = realloc(clientStat, PSC_getNrOfNodes() * sizeof(*clientStat));
    if (!clientStat) PSID_exit(errno, "%s", __func__);

    for (PSnodes_ID_t n = 0; n < PSC_getNrOfNodes(); n++) {
	gettimeofday(&clientStat[n].lastPing, NULL);
	clientStat[n].tasks = (PSID_TaskCount_t) { .normal = 0, .total = 0 };
	clientStat[n].mem = (PSID_Mem_t) { -1, -1 };
	clientStat[n].missCounter = 0;
	clientStat[n].wrongClients = 0;
    }
}

/**
 * @brief Free master space.
 *
 * Free space needed by master when master burden is passed to next
 * node. The space has to be allocated using @ref allocMasterSpace().
 *
 * @return No return value.
 *
 * @see allocMasterSpace()
 */
static void freeMasterSpace(void)
{
    free(clientStat);
    clientStat = NULL;

    exitPartHandler();
}

/**
 * Major timeout after which status ping are sent and @ref
 * handleMasterTask() is called.
 */
static struct timeval statusTimeout = { .tv_sec = 2, .tv_usec = 0 };

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
 * Counter for status broadcasts per round. This is used to limit the
 * number of status broadcasts per status iteration. Too many
 * broadcast might lead to running out of message-buffers within RDP
 * on huge clusters.
 */
static int statusBcasts = 0;

/**
 * Maximum number of status broadcasts per round. This is used to limit the
 * number of status broadcasts per status iteration. Too many
 * broadcast might lead to running out of message-buffers within RDP
 * on huge clusters.
 */
static int maxStatusBcasts = 4;

int getMaxStatBCast(void)
{
    return maxStatusBcasts;
}

void setMaxStatBCast(int limit)
{
    if (limit >= 0) maxStatusBcasts = limit;
}

/**
 * @brief Master handling routine.
 *
 * All the stuff the master has to handle when @ref statusTimeout is
 * elapsed. This includes test if all expected status pings were
 * received and ringing down nodes in order to prevent decomposition
 * of the cluster into independent sub-clusters.
 *
 * @return No return value.
 */
static void handleMasterTasks(void)
{
    static int round = 0;

    PSID_fdbg(PSID_LOG_STATUS, "\n");

    if (PSID_getDaemonState() & PSID_STATE_SHUTDOWN) return;

    struct timeval timeNow, lastCheck;
    gettimeofday(&timeNow, NULL);
    timersub(&timeNow, &statusTimeout, &lastCheck);
    int nrDownNodes = 0;
    for (PSnodes_ID_t n = 0; n < PSC_getNrOfNodes(); n++) {
	if (PSIDnodes_isUp(n)) {
	    if (timercmp(&clientStat[n].lastPing, &lastCheck, <)) {
		/* no ping in the last 'round' */
		PSID_fdbg(PSID_LOG_STATUS, "Ping from %d missing [%d]\n",
			  n, clientStat[n].missCounter);
		clientStat[n].missCounter++;
	    }
	    if (clientStat[n].missCounter > DeadLimit) {
		PSID_fdbg(PSID_LOG_STATUS, "miss-count exceeded to %d\n", n);
		send_DAEMONCONNECT(n);
	    }
	} else {
	    nrDownNodes++;
	}
    }

    /* Re-enable PSP_DD_DEAD_NODE broadcasts */
    statusBcasts = 0;

    if (!round) {
	static PSnodes_ID_t next = -1;
	int count = (nrDownNodes > 10) ? 10 : nrDownNodes;

	if (next < 0) next = PSC_getMyID(); // first call

	PSnodes_ID_t last = next;
	do {
	    if (!PSIDnodes_isUp(next)) {
		errno = 0;
		if (send_DAEMONCONNECT(next) != -1 || errno != EHOSTUNREACH) {
		    count--;
		}
	    }
	    next = (next + 1) % PSC_getNrOfNodes();
	} while (count && next != last);
    }
    round = (round + 1) % 10;

    return;
}

/**
 * @brief Send status ping
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
	    .len = 0 },
	.buf = {'\0'} };
    PSID_Load_t load = getLoad();
    PSID_Mem_t mem = getMemInfo();

    PSID_fdbg(PSID_LOG_STATUS, "to %d\n", getMasterID());

    PSP_putMsgBuf(&msg, "localTasks", &localTasks, sizeof(localTasks));
    PSP_putMsgBuf(&msg, "load", &load, sizeof(load));
    PSP_putMsgBuf(&msg, "mem", &mem, sizeof(mem));
    PSP_putMsgBuf(&msg, "totNodes", &totNodes, sizeof(totNodes));

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
    return statusTimeout.tv_sec * 1000 + statusTimeout.tv_usec / 1000;
}

void setStatusTimeout(int timeout)
{
    if (timeout < MIN_TIMEOUT_MSEC) return;

    statusTimeout.tv_sec = timeout / 1000;
    statusTimeout.tv_usec = (timeout%1000) * 1000;

    if (timerID > 0) {
	Timer_block(timerID, true);
	releaseStatusTimer();
	timerID = Timer_register(&statusTimeout, sendRDPPing);
	if (timerID < 0) {
	    PSID_flog("failed to re-register status timer\n");
	}
    }
}

void incTaskCount(bool normal)
{
    PSID_fdbg(PSID_LOG_STATUS, "%s task\n", normal ? "normal" : "service");

    localTasks.total++;
    if (normal) localTasks.normal++;

    if (PSID_config->useMCast) incJobsMCast(PSC_getMyID(), 1, normal);
}

void decTaskCount(bool normal)
{
    PSID_fdbg(PSID_LOG_STATUS, "%s task\n", normal ? "normal" : "service");

    localTasks.total--;
    if (normal) localTasks.normal--;

    if (PSID_config->useMCast) decJobsMCast(PSC_getMyID(), 1, normal);
}

void decTaskCountHint(PSnodes_ID_t node)
{
    PSID_fdbg(PSID_LOG_STATUS, "node %d\n", node);

    if (clientStat && clientStat[node].tasks.normal)
	clientStat[node].tasks.normal--;
}

PSID_NodeStatus_t getStatusInfo(PSnodes_ID_t node)
{
    PSID_NodeStatus_t status;

    if (PSID_config->useMCast) {
	MCastConInfo_t info;

	getInfoMCast(node, &info);

	status.load.load[0] = info.load.load[0];
	status.load.load[1] = info.load.load[1];
	status.load.load[2] = info.load.load[2];
	status.tasks.total = info.jobs.total;
	status.tasks.normal = info.jobs.normal;
    } else {
	if (node == PSC_getMyID()) {
	    status.tasks = localTasks;
	    status.load = getLoad();
	} else if (PSC_getMyID() != getMasterID()
		   || !PSC_validNode(node) || !clientStat) {
	    status.tasks = (PSID_TaskCount_t) { .normal = -1, .total = -1 };
	    status.load = (PSID_Load_t) {{ 0.0, 0.0, 0.0}};
	} else {
	    status.tasks = clientStat[node].tasks;
	    status.load = clientStat[node].load;
	}
    }

    return status;
}

PSID_Mem_t getMemoryInfo(PSnodes_ID_t node)
{
    PSID_Mem_t memory;

    if (PSID_config->useMCast) {
	memory = (PSID_Mem_t) { -1, -1 };
    } else {
	if (node == PSC_getMyID()) {
	    memory = getMemInfo();
	} else if (PSC_getMyID() != getMasterID()
		   || !PSC_validNode(node) || !clientStat) {
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
    PSID_fdbg(PSID_LOG_STATUS, "%d\n", newMaster);

    if (knowMaster() && newMaster == getMasterID()) return;

    if (knowMaster() && getMasterID() == PSC_getMyID()) freeMasterSpace();

    masterNode = newMaster;

    if (PSID_getDaemonState() & PSID_STATE_SHUTDOWN) return;

    if (newMaster == PSC_getMyID()) allocMasterSpace();

    if (PSID_config->useMCast) {
	timerID = 0;
    } else if (!knowMaster()) {
	if (!Timer_isInitialized()) {
	    Timer_init(PSID_config->logfile);
	}
	timerID = Timer_register(&statusTimeout, sendRDPPing);
	sendRDPPing();
    }

    if (newMaster == PSC_getMyID()) initPartHandler();
}

bool knowMaster(void)
{
    return timerID != -1;
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

typedef struct {
    int id;
    char *script;
} stateChangeInfo_t;

static void stateChangeEnv(void *info)
{
    int nID = -1;
    in_addr_t nAddr;
    char *nName, buf[NI_MAXHOST];

    if (info) {
	stateChangeInfo_t *i = info;
	nID = i->id;
    }
    if (!PSC_validNode(nID)) nID = -1;

    snprintf(buf, sizeof(buf), "%d", nID);
    setenv("NODE_ID", buf, 1);

    if (nID < 0) return;

    /* identify and set hostname */
    nAddr = PSIDnodes_getAddr(nID);
    if (nAddr == INADDR_ANY) {
	nName = "<unknown>";
    } else {
	struct sockaddr_in addr = {
	    .sin_family = AF_INET,
	    .sin_port = 0,
	    .sin_addr = { .s_addr = nAddr } };
	if (getnameinfo((struct sockaddr *)&addr, sizeof(addr),
			buf, sizeof(buf), NULL, 0, NI_NAMEREQD)) {
	    nName = "<unknown>";
	} else {
	    nName = buf;
	}
    }
    setenv("NODE_NAME", nName, 1);
}

static void stateChangeCB(int result, bool tmdOut, int iofd, void *info)
{
    char *sName = "<unknown>";
    if (info) {
	stateChangeInfo_t *i = info;
	sName = i->script;
	free(i);
    }

    if (result) {
	char line[128] = { '\0' };
	if (iofd > -1) {
	    int num = PSCio_recvBuf(iofd, line, sizeof(line));
	    int eno = errno;
	    if (num < 0) {
		PSID_fwarn(eno, "PSCio_recvBuf(iofd)");
		line[0] = '\0';
	    } else if (num == sizeof(line)) {
		strcpy(&line[sizeof(line)-4], "...");
	    } else {
		line[num]='\0';
	    }
	}
	PSID_flog("script '%s' returned %d: '%s'\n", sName, result, line);
    }
    if (iofd > -1) close(iofd); /* Discard further output */
}

bool declareNodeDead(PSnodes_ID_t id, bool sendDeadnode, bool silent)
{
    if (!PSC_validNode(id)) {
	PSID_flog("id %d out of range\n", id);
	return false;
    }
    if (!PSIDnodes_isUp(id)) {
	/* Drop messages not yet in the sending window */
	clearRDPMsgs(id);
	return true;
    }

    PSID_fdbg(PSID_LOG_STATUS,
	      "node %d goes down. Will %ssend PSP_DD_DEAD_NODE messages\n",
	      id, sendDeadnode ? "" : "not ");

    totNodes--;
    PSIDnodes_bringDown(id);
    PSIDnodes_setNumCores(id, 0);
    PSIDnodes_setNumThrds(id, 0);
    if (PSID_mixedProto() && PSIDnodes_getProtoV(id) != PSProtocolVersion) {
	/* Check if we are still heterogeneous */
	PSnodes_ID_t n;
	for (n = 0; n < PSC_getNrOfNodes(); n++) {
	    if (PSIDnodes_isUp(n)
		&& PSIDnodes_getProtoV(n) != PSProtocolVersion) break;
	}
	if (n == PSC_getNrOfNodes()) PSID_setMixedProto(false);
    }

    if (PSID_config->useMCast) declareNodeDeadMCast(id);

    /* Send signals to all processes that controlled task on the dead node */
    list_t *t;
    list_for_each(t, &managedTasks) {
	PStask_t *task = list_entry(t, PStask_t, next);
	if (task->deleted) continue;
	/* deliver all assigned signals */
	int sig;
	PStask_ID_t sndr;
	while ((sndr = PSID_getSignalByID(&task->assignedSigs, id, &sig))) {
	    /* controlled task was on dead node => send signal now */
	    PSID_sendSignal(task->tid, task->uid, sndr, sig,
			    false /* pervasive */, false /* answer */);
	}
	if (task->ptid && PSC_getID(task->ptid) == id) {
	    /* parent resided on that node => send signal now */
	    PSID_sendSignal(task->tid, task->uid, task->ptid, -1,
			    false /* pervasive */, false /* answer */);
	}
	/* one or more children resided on that node */
	while ((sndr = PSID_getSignalByID(&task->childList, id, &sig))) {
	    /* child task was on dead node => send signal now */
	    PSID_sendSignal(task->tid, task->uid, sndr, -1,
			    false /* pervasive */, false /* answer */);
	}
	if (task->removeIt && PSID_emptySigList(&task->childList)) {
	    PSID_fdbg(PSID_LOG_TASK, "PSIDtask_cleanup()\n");
	    task->pendingReleaseRes = 0; // ignore pending inheritance
	    PSIDtask_cleanup(task);
	    continue;
	}

	/* take kept children into account, too */
	while ((sndr = PSID_getSignalByID(&task->keptChildren, id, &sig))) {
	    /* kept child was on dead node */
	    /* Send the signal */
	    PSID_sendSignal(task->tid, task->uid, sndr, -1,
			    false /* pervasive */, false /* answer */);
	}
    }

    /* We might have to cleanup obsolete tasks, too (but no signals required) */
    list_for_each(t, &obsoleteTasks) {
	PStask_t *task = list_entry(t, PStask_t, next);
	if (task->deleted) continue;
	/* delete remote children */
	int sig = -1;
	while (PSID_getSignalByID(&task->childList, id, &sig)) sig = -1;

	if (task->removeIt && PSID_emptySigList(&task->childList)) {
	    PSID_fdbg(PSID_LOG_TASK, "PSIDtask_cleanup()\n");
	    task->pendingReleaseRes = 0; // ignore pending inheritance
	    PSIDtask_cleanup(task);
	}
    }

    PSIDspawn_cleanupByNode(id);

    /* Disable accounters located on dead node */
    PSID_cleanAcctFromNode(id);

    PSID_fdbg(silent ? PSID_LOG_STATUS : -1, "connection lost to %d\n", id);

    PSIDhook_call(PSIDHOOK_NODE_DOWN, &id);

    if (id == getMasterID()) {
	/* Dead node was master, find new one */
	PSnodes_ID_t node = id;

	while (node < PSC_getMyID()) {
	    if (PSIDnodes_isUp(node)) break;
	    node++;
	}

	PSID_fdbg(PSID_LOG_STATUS, "new master %d\n", node);

	declareMaster(node);
    } else if (PSC_getMyID() == getMasterID()) {
	cleanupRequests(id);
	if (PSID_config->nodeDownScript && *PSID_config->nodeDownScript) {
	    stateChangeInfo_t *info = malloc(sizeof(*info));
	    if (!info) {
		PSID_fwarn(errno, "malloc()");
	    } else {
		info->id = id;
		info->script = PSID_config->nodeDownScript;
	    }
	    PSID_execScript(PSID_config->nodeDownScript, stateChangeEnv,
			    stateChangeCB, NULL, info);
	}
    }

    /* Drop messages not before new master is found */
    clearRDPMsgs(id);

    if (!PSID_config->useMCast
	&& getMasterID() == PSC_getMyID() && sendDeadnode) {
	send_DEADNODE(id);
    }
    return true;
}

/* Prototype forward declaration */
static int send_ACTIVENODES(PSnodes_ID_t dest);

bool declareNodeAlive(PSnodes_ID_t id, int numCores, int numThrds,
		      int proto, int dmnProto)
{
    bool wasUp = PSIDnodes_isUp(id);

    PSID_fdbg(PSID_LOG_STATUS, "node %d\n", id);

    if (!PSC_validNode(id)) {
	PSID_flog("id %d out of range\n", id);
	return false;
    }

    if (!wasUp) totNodes++;
    PSIDnodes_bringUp(id);
    PSIDnodes_setNumCores(id, numCores);
    PSIDnodes_setNumThrds(id, numThrds);
    PSIDnodes_setProtoV(id, proto);
    PSIDnodes_setDmnProtoV(id, dmnProto);
    if (proto != PSProtocolVersion) PSID_setMixedProto(true);

    if (!wasUp) PSIDhook_call(PSIDHOOK_NODE_UP, &id);

    if (!knowMaster()) {
	if (id < PSC_getMyID()) {
	    PSID_fdbg(PSID_LOG_STATUS, "master %d\n", id);
	    declareMaster(id);
	} else if (id > PSC_getMyID()) {
	    PSnodes_ID_t mID = PSC_getMyID();
	    PSID_fdbg(PSID_LOG_STATUS, "master %d\n", mID);
	    declareMaster(mID);
	}
    } else if (id < getMasterID()) {
	/* New node will be master */
	PSnodes_ID_t oldMaster = getMasterID();

	PSID_fdbg(PSID_LOG_STATUS, "new master %d\n", id);

	declareMaster(id);

	if (!PSID_config->useMCast) send_MASTERIS(oldMaster);
    }

    if (!PSID_config->useMCast && getMasterID() == PSC_getMyID() && !wasUp) {
	send_ACTIVENODES(id);
    }

    if (getMasterID() == PSC_getMyID() && !wasUp) {
	if (PSID_config->nodeUpScript && *PSID_config->nodeUpScript) {
	    stateChangeInfo_t *info = malloc(sizeof(*info));
	    if (!info) {
		PSID_fwarn(errno, "malloc()");
	    } else {
		info->id = id;
		info->script = PSID_config->nodeUpScript;
	    }
	    PSID_execScript(PSID_config->nodeUpScript, stateChangeEnv,
			    stateChangeCB, NULL, info);
	}

	send_GETTASKS(id);
    }
    return true;
}

int send_DAEMONCONNECT(PSnodes_ID_t id)
{
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_DD_DAEMONCONNECT,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getTID(id, 0),
	    .len = 0 },
	.buf = {'\0'} };

    PSID_fdbg(PSID_LOG_STATUS, "to %d\n", id);

    if (PSIDnodes_getAddr(id) == INADDR_ANY) {
	errno = EHOSTUNREACH;
	return -1;
    }

    /* reset IP for dynamic nodes not in RDP state ACTIVE */
    if (RDP_getState(id) != ACTIVE && PSIDnodes_isDynamic(id)
	&& PSIDnodes_getAddr(id) != INADDR_NONE) {
	PSIDnodes_setAddr(id, INADDR_NONE);
	RDP_updateNode(id, INADDR_NONE);
    }

    if (PSIDnodes_getAddr(id) == INADDR_NONE) {
	int ret = PSIDhook_call(PSIDHOOK_NODE_UNKNOWN, &id);
	if (ret == PSIDHOOK_NOFUNC || PSIDnodes_getAddr(id) == INADDR_NONE) {
	    errno = EHOSTUNREACH;
	    return -1;
	}
	RDP_updateNode(id, PSIDnodes_getAddr(id));
    }

    int32_t tmp = PSIDnodes_getNumCores(PSC_getMyID());
    PSP_putMsgBuf(&msg, "numCores", &tmp, sizeof(tmp));

    tmp = PSIDnodes_getNumThrds(PSC_getMyID());
    PSP_putMsgBuf(&msg, "numThrds", &tmp, sizeof(tmp));

    tmp = PSProtocolVersion;
    PSP_putMsgBuf(&msg, "proto", &tmp, sizeof(tmp));

    tmp = PSDaemonProtocolVersion;
    PSP_putMsgBuf(&msg, "dmnProto", &tmp, sizeof(tmp));

    return sendMsg(&msg);
}

/**
 * @brief Handle a PSP_DD_DAEMONCONNECT message
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
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_DAEMONCONNECT(DDBufferMsg_t *msg)
{
    PSnodes_ID_t id = PSC_getID(msg->header.sender);
    size_t used = 0;

    PSID_fdbg(PSID_LOG_STATUS, "from %d\n", id);

    int32_t pCPUs, vCPUs, proto, dmnProto;
    PSP_getMsgBuf(msg, &used, "numCores", &pCPUs, sizeof(pCPUs));
    PSP_getMsgBuf(msg, &used, "numThrds", &vCPUs, sizeof(vCPUs));
    PSP_getMsgBuf(msg, &used, "proto", &proto, sizeof(proto));
    PSP_getMsgBuf(msg, &used, "dmnProto", &dmnProto, sizeof(dmnProto));

    /* id is out of range -> nothing left to do */
    if (!declareNodeAlive(id, pCPUs, vCPUs, proto, dmnProto)) return true;

    /* accept this request and send an ESTABLISH msg back to the requester */
    msg->header = (DDMsg_t) {
	.type = PSP_DD_DAEMONESTABLISHED,
	.sender = PSC_getMyTID(),
	.dest = PSC_getTID(id, 0),
	.len = 0 };

    int32_t tmp = PSIDnodes_getNumCores(PSC_getMyID());
    PSP_putMsgBuf(msg, "numCores", &tmp, sizeof(tmp));

    tmp = PSIDnodes_getNumThrds(PSC_getMyID());
    PSP_putMsgBuf(msg, "numThrds", &tmp, sizeof(tmp));

    tmp = PSProtocolVersion;
    PSP_putMsgBuf(msg, "proto", &tmp, sizeof(tmp));

    tmp = PSDaemonProtocolVersion;
    PSP_putMsgBuf(msg, "dmnProto", &tmp, sizeof(tmp));

    if (sendMsg(msg) == -1 && errno != EWOULDBLOCK) {
	PSID_fdwarn(PSID_LOG_STATUS, errno, "sendMsg()");
    } else {
	send_OPTIONS(id);
    }
    return true;
}

/**
 * @brief Drop a PSP_DD_DAEMONCONNECT message
 *
 * Drop the message @a msg of type PSP_DD_DAEMONCONNECT.
 *
 * Since the connecting daemon waits for a reaction to its request a
 * corresponding answer is created.
 *
 * @param msg Pointer to message to drop
 *
 * @return Always return true
 */
static bool drop_DAEMONCONNECT(DDBufferMsg_t *msg)
{
    static bool block = false;

    if (!block && !PSID_config->useMCast && !knowMaster()
	&& ! (PSID_getDaemonState() & PSID_STATE_SHUTDOWN)) {
	PSnodes_ID_t next = PSC_getID(msg->header.dest) + 1;

	block = true;
	while (next < PSC_getMyID() && send_DAEMONCONNECT(next) < 0
	       && (errno == EHOSTUNREACH || errno == ECONNREFUSED)) {
	    next++;
	}
	block = false;
	if (next == PSC_getMyID()) declareMaster(next);
    }
    return true;
}

/**
 * @brief Handle a PSP_DD_DAEMONESTABLISHED message
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
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_DAEMONESTABLISHED(DDBufferMsg_t *msg)
{
    PSnodes_ID_t id = PSC_getID(msg->header.sender);
    size_t used = 0;

    PSID_fdbg(PSID_LOG_STATUS, "(%d)\n", id);

    int32_t pCPUs, vCPUs, proto, dmnProto;
    if (!PSP_getMsgBuf(msg, &used, "numCores", &pCPUs, sizeof(pCPUs))
	|| !PSP_getMsgBuf(msg, &used, "numThrds", &vCPUs, sizeof(vCPUs))
	|| !PSP_getMsgBuf(msg, &used, "proto", &proto, sizeof(proto))
	|| !PSP_getMsgBuf(msg, &used, "dmnProto", &dmnProto, sizeof(dmnProto))) {
	PSID_flog("truncated message from node %d\n", id);
	return true;
    }

    /* id is out of range -> nothing left to do */
    if (!declareNodeAlive(id, pCPUs, vCPUs, proto, dmnProto)) return true;

    /* Send some info about me to the other node */
    send_OPTIONS(id);
    return true;
}

int send_DAEMONSHUTDOWN(void)
{
    DDMsg_t msg = {
	.type = PSP_DD_DAEMONSHUTDOWN,
	.sender = PSC_getMyTID(),
	.dest = 0,
	.len = sizeof(msg) };
    int count = 1;

    /* broadcast to every daemon except the sender */
    for (PSnodes_ID_t n = 0; n < PSC_getNrOfNodes(); n++) {
	if (PSIDnodes_isUp(n) && n != PSC_getMyID()) {
	    msg.dest = PSC_getTID(n, 0);
	    if (sendMsg(&msg) >= 0) count++;
	    /* Close RDP connection immediately after send */
	    closeConnRDP(n);
	}
    }

    return count;
}

/**
 * @brief Handle a PSP_DD_DAEMONSHUTDOWN message
 *
 * Handle the message @a msg of type PSP_DD_DAEMONSHUTDOWN.
 *
 * This kind of messages tells the receiver that the sending node will
 * go down soon and no longer accepts messages for receive. Thus this
 * node should be marked to be down now.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_DAEMONSHUTDOWN(DDMsg_t *msg)
{
    PSnodes_ID_t id = PSC_getID(msg->sender);

    PSID_fdbg(id != PSC_getMyID() ? PSID_LOG_STATUS : -1, "from %d\n", id);
    declareNodeDead(id, false /* sendDeadNode */, true /* silent */);
    closeConnRDP(id);
    return true;
}

int send_MASTERIS(PSnodes_ID_t dest)
{
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_DD_MASTER_IS,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getTID(dest, 0),
	    .len = 0 },
	.buf = {'\0'} };
    PSnodes_ID_t master = getMasterID();
    PSP_putMsgBuf(&msg, "master", &master, sizeof(master));

    PSID_fdbg(PSID_LOG_STATUS, "tell %s master is %d\n",
	      PSC_printTID(msg.header.dest), getMasterID());

    return sendMsg(&msg);
}

/**
 * @brief Handle a PSP_DD_MASTER_IS message
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
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_MASTERIS(DDBufferMsg_t *msg)
{
    size_t used = 0;
    PSnodes_ID_t newM = -1;
    PSP_getMsgBuf(msg, &used, "master", &newM, sizeof(newM));

    PSID_fdbg(PSID_LOG_STATUS, "%s says master is %d\n",
	      PSC_printTID(msg->header.sender), newM);

    if (newM != -1 && newM != getMasterID()) {
	if (newM < getMasterID()) {
	    send_DAEMONCONNECT(newM);
	} else {
	    send_MASTERIS(PSC_getID(msg->header.sender));
	}
    }
    return true;
}

/**
 * @brief Send a PSP_DD_ACTIVE_NODES message.
 *
 * Send a PSP_DD_ACTIVE_NODES message to node @a dest.
 *
 * @param dest ParaStation ID of the node to send the message to.
 *
 * @return On success, the number of bytes sent within the
 * PSP_DD_ACTIVE_NODES message is returned. If an error occurred, -1 is
 * returned and errno is set appropriately.
 */
static int send_ACTIVENODES(PSnodes_ID_t dest)
{
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_DD_ACTIVE_NODES,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getTID(dest, 0),
	    .len = 0 },
	.buf = {'\0'} };
    int total = 0;

    if (dest == PSC_getMyID()) return 0;

    for (PSnodes_ID_t n = 0; n < PSC_getNrOfNodes(); n++) {
	if (!PSIDnodes_isUp(n)) continue;
	if (!PSP_tryPutMsgBuf(&msg, "node", &n, sizeof(n))) {
	    int ret = sendMsg(&msg);
	    if (ret < 0) {
		return ret;
	    } else {
		total += ret;
	    }
	    msg.header.len = 0;
	    PSP_putMsgBuf(&msg, "node", &n, sizeof(n));
	}
    }

    if (msg.header.len) {
	int ret = sendMsg(&msg);
	if (ret < 0) {
	    return ret;
	} else {
	    total += ret;
	}
    }

    return total;
}

/**
 * @brief Handle a PSP_DD_ACTIVE_NODES message
 *
 * Handle the message @a msg of type PSP_DD_ACTIVE_NODES.
 *
 * Whenever a daemon node connects to a new node, one or more
 * PSP_DD_ACTIVE_NODES messages are sent the this node in order to
 * inform about the active nodes currently known to the master. The
 * receiving node will try to contact each of this nodes provided in
 * order to setup a working connection.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_ACTIVENODES(DDBufferMsg_t *msg)
{
    PSnodes_ID_t sender = PSC_getID(msg->header.sender), node;
    static PSnodes_ID_t firstUntested = 0;
    size_t used = 0;

    while (PSP_tryGetMsgBuf(msg, &used, "node", &node, sizeof(node))) {
	PSID_fdbg(PSID_LOG_STATUS, "check %d\n", node);
	if (node == sender) {
	    /* Sender is first active node, all previous nodes are down */
	    for (PSnodes_ID_t n = 0; n < node; n++) {
		if (PSIDnodes_isUp(n)) send_DAEMONCONNECT(n);
	    }
	    firstUntested = node + 1;
	}
	for (PSnodes_ID_t n = firstUntested; n < node; n++) {
	    if (PSIDnodes_isUp(n)) send_MASTERIS(n);
	}
	if (!PSIDnodes_isUp(node)) send_DAEMONCONNECT(node);
	firstUntested = node + 1;
    }
    return true;
}

/**
 * @brief Broadcast a PSP_DD_DEAD_NODE message
 *
 * Broadcast a PSP_DD_DEAD_NODE message to all active nodes.
 *
 * @return On success, the number of nodes the PSP_DD_DEAD_NODE
 * message is sent to is returned, i.e. the value returned by the @ref
 * broadcastMsg() call. If an error occurred, -1 is returned and errno
 * is set appropriately.
 */
static int send_DEADNODE(PSnodes_ID_t deadnode)
{
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_DD_DEAD_NODE,
	    .sender = PSC_getMyTID(),
	    .dest = 0,
	    .len = 0 },
	.buf = {'\0'} };

    PSP_putMsgBuf(&msg, "deadnode", &deadnode, sizeof(deadnode));

    PSID_fdbg(PSID_LOG_STATUS, "%d is dead\n", deadnode);

    if (statusBcasts++ > maxStatusBcasts) {
	PSID_flog("dropping broadcast\n");
	return 0;
    }

    return broadcastMsg(&msg);
}

/**
 * @brief Handle a PSP_DD_DEAD_NODE message
 *
 * Handle the message @a msg of type PSP_DD_DEAD_NODE.
 *
 * Whenever a daemon node detects a node to be down all other nodes
 * will be informed about this fact via a PSP_DD_DEAD_NODE
 * message. Each node receiving this kind of message will try contact
 * the according node and usually mark this node as dead via a
 * callback from daemon's the RDP facility.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_DEADNODE(DDBufferMsg_t *msg)
{
    size_t used = 0;
    PSnodes_ID_t dead;

    if (!PSP_getMsgBuf(msg, &used, "deadNode", &dead, sizeof(dead))) {
	PSID_flog("truncated\n");
    } else {
	PSID_fdbg(PSID_LOG_STATUS, "%d\n", dead);
	send_DAEMONCONNECT(dead);
    }

    return true;
}

/**
 * @brief Handle a PSP_DD_LOAD message
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
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_LOAD(DDBufferMsg_t *msg)
{
    if (PSC_getMyID() != getMasterID()) {
	send_MASTERIS(PSC_getID(msg->header.sender));
    } else if (clientStat) { /* Ignore msg during on-going shutdown */
	PSnodes_ID_t client = PSC_getID(msg->header.sender);
	size_t used = 0;
	int clientNodes;

	PSP_getMsgBuf(msg, &used, "tasks", &clientStat[client].tasks,
		      sizeof(clientStat[client].tasks));
	PSP_getMsgBuf(msg, &used, "load", &clientStat[client].load,
		      sizeof(clientStat[client].load));
	PSP_getMsgBuf(msg, &used, "mem", &clientStat[client].mem,
		      sizeof(clientStat[client].mem));
	PSP_getMsgBuf(msg, &used, "totNodes", &clientNodes,sizeof(clientNodes));

	gettimeofday(&clientStat[client].lastPing, NULL);
	clientStat[client].missCounter = 0;

	if (clientNodes != totNodes) {
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
    return true;
}

void initStatus(void)
{
    PSID_fdbg(PSID_LOG_VERB, "\n");

    PSID_registerMsg(PSP_DD_DAEMONCONNECT, msg_DAEMONCONNECT);
    PSID_registerMsg(PSP_DD_DAEMONESTABLISHED, msg_DAEMONESTABLISHED);
    PSID_registerMsg(PSP_DD_DAEMONSHUTDOWN, (handlerFunc_t) msg_DAEMONSHUTDOWN);
    PSID_registerMsg(PSP_DD_MASTER_IS, msg_MASTERIS);
    PSID_registerMsg(PSP_DD_ACTIVE_NODES, msg_ACTIVENODES);
    PSID_registerMsg(PSP_DD_DEAD_NODE, msg_DEADNODE);
    PSID_registerMsg(PSP_DD_LOAD, msg_LOAD);

    PSID_registerDropper(PSP_DD_DAEMONCONNECT, drop_DAEMONCONNECT);
}
