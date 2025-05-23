/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#define _GNU_SOURCE
#include "psslurmcomm.h"

#include <stdio.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>

#include "list.h"
#include "pscio.h"
#include "pscommon.h"
#include "psenv.h"
#include "pslog.h"
#include "psprotocol.h"
#include "psstrbuf.h"
#include "selector.h"

#include "pluginconfig.h"
#include "pluginforwarder.h"
#include "pluginhelper.h"
#include "pluginmalloc.h"

#include "slurmcommon.h"
#include "slurmerrno.h"
#include "psslurmauth.h"
#include "psslurmconfig.h"
#include "psslurmfwcomm.h"
#include "psslurmlog.h"
#include "psslurmpack.h"
#include "psslurmproto.h"
#include "psslurmtasks.h"
#include "psslurm.h"

/** default slurmd port psslurm listens for new srun/slurmcltd requests */
#define PSSLURM_SLURMD_PORT 6818

/** maximal number of parallel supported Slurm control daemons */
#define MAX_CTL_HOSTS 16

/** maximal length of a Slurm I/O message buffer */
#define SLURM_IO_MAX_LEN 1024

/** socket to listen for new Slurm connections */
static int slurmListenSocket = -1;

/** structure holding slurmctld host definitions */
typedef struct {
    char *host;		    /**< hostname */
    char *addr;		    /**< optional host address */
    PSnodes_ID_t id;	    /**< PS node ID */
} Ctl_Hosts_t;

/** array holding all configured slurmctld hosts */
static Ctl_Hosts_t ctlHosts[MAX_CTL_HOSTS];

/** number of control hosts */
static int ctlHostsCount = 0;

/** list holding all connections */
static LIST_HEAD(connectionList);

PSnodes_ID_t getCtlHostID(int index)
{
    if (index >= ctlHostsCount) return -1;
    return ctlHosts[index].id;
}

int getCtlHostIndex(PSnodes_ID_t id)
{
    for (int i = 0; i < ctlHostsCount; i++) if (ctlHosts[i].id == id) return i;
    return -1;
}

/**
 * @brief Find a connection
 *
 * @param socket The socket of the connection to find
 *
 * @return Returns a pointer to the connection or NULL on error
 */
static Connection_t *findConnection(int socket)
{
    list_t *c;
    list_for_each(c, &connectionList) {
	Connection_t *con = list_entry(c, Connection_t, next);
	if (con->sock == socket) return con;
    }

    return NULL;
}

Connection_t *findConnectionByStep(Step_t *step)
{
    if (!step) return NULL;

    list_t *c;
    list_for_each(c, &connectionList) {
	Connection_t *con = list_entry(c, Connection_t, next);
	if (con->step == step) return con;
    }
    return NULL;
}

/**
 * @brief Find a connection
 *
 * Find a connection by its socket and receive time.
 *
 * @param socket The socket of the connection to find
 *
 * @param recvTime The time the connection received its first message
 *
 * @return Returns a pointer to the connection or NULL on error
 */
static Connection_t *findConnectionEx(int socket, time_t recvTime)
{
    list_t *c;
    list_for_each(c, &connectionList) {
	Connection_t *con = list_entry(c, Connection_t, next);
	if (con->sock == socket && con->recvTime == recvTime) return con;
    }

    return NULL;
}

/**
 * @brief Reset a connection structure
 *
 * Free used memory and reset management data of the connection
 * associated to the socket @a socket.
 *
 * The connection might already hold data to be forwarded as an answer
 * in fw.body or fw.head. Thus, the connection's message forwarding
 * structure must not be touched here.
 *
 * Since connection validity is tracked via @ref connectionList, the
 * socket is used as an identifier to lookup the connection instead of
 * the connection itself.
 *
 * @param socket Socket idenfitying the connection to reset
 *
 * @return Return true on success or false on error
 */
static bool resetConnection(int socket)
{
    Connection_t *con = findConnection(socket);
    if (!con) return false;

    fdbg(PSSLURM_LOG_COMM, "for socket %i\n", socket);

    PSdbClearBuf(con->data);
    con->readSize = 0;

    return true;
}

/**
 * @brief Add a new connection
 *
 * @param socket The socket of the connection to add
 *
 * @param cb Function to call to handle received messages
 *
 * @param info Pointer to additional information passed to @a
 * cb. Responsibility on @a info is passed to the connection here only
 * upon success. Otherwise the caller remains responsible.
 *
 * @return Returns a pointer to the new connection or NULL on error
 */
static Connection_t *addConnection(int socket, Connection_CB_t *cb, void *info)
{
    if (socket < 0) {
	flog("got invalid socket %i\n", socket);
	return NULL;
    }

    fdbg(PSSLURM_LOG_COMM, "add connection for socket %i\n", socket);
    Connection_t *con = findConnection(socket);
    if (con) {
	flog("socket(%i) already has a connection, resetting it\n", socket);
	resetConnection(socket);
	con->cb = cb;
	ufree(con->info);
	con->info = info;
	gettimeofday(&con->openTime, NULL);
	con->authByInMsg = false;
	return con;
    }

    con = ucalloc(sizeof(*con));
    if (con) {
	con->sock = socket;
	con->cb = cb;
	con->info = info;
	con->data = PSdbNew(NULL, 0);
	con->fw.body = PSdbNew(NULL, 0);
	gettimeofday(&con->openTime, NULL);
	initSlurmMsgHead(&con->fw.head);

	list_add_tail(&con->next, &connectionList);
    }

    return con;
}

/**
 * @brief Close connection
 *
 * Close the connection and free all used memory of the connection
 * associated to the socket @a socket.
 *
 * Since connection validity is tracked via @ref connectionList, the
 * socket is used as an identifier to lookup the connection instead of
 * the connection itself.
 *
 * @param socket Socket idenfitying the connection to close
 *
 * @return Return true on success or false on error
 */
void closeSlurmCon(int socket)
{
    Connection_t *con = findConnection(socket);
    if (!con) fdbg(PSSLURM_LOG_COMM, "(%d)\n", socket);
    if (socket < 0) return;

    /* close the connection */
    if (Selector_isRegistered(socket)) Selector_remove(socket);
    close(socket);

    /* free memory */
    if (con) {
	if (mset(PSSLURM_LOG_COMM)) {
	    struct timeval time_now, time_diff;

	    gettimeofday(&time_now, NULL);
	    timersub(&time_now, &con->openTime, &time_diff);
	    flog("(%i) was open %.4f seconds\n", socket,
		 time_diff.tv_sec + 1e-6 * time_diff.tv_usec);
	}
	list_del(&con->next);
	freeSlurmMsgHead(&con->fw.head);
	if (con->fw.nodesCount) ufree(con->fw.nodes);
	PSdbDestroy(con->fw.body);
	PSdbDestroy(con->data);
	ufree(con->info);
	ufree(con);
    }
}

void clearSlurmCon(void)
{
    list_t *c, *tmp;

    list_for_each_safe(c, tmp, &connectionList) {
	Connection_t *con = list_entry(c, Connection_t, next);
	closeSlurmCon(con->sock);
    }

    for (int i=0; i<ctlHostsCount; i++) {
	ufree(ctlHosts[i].host);
	ufree(ctlHosts[i].addr);
    }
}

/**
 * @brief Save a single result of a forwarded RPC messages
 *
 * @param sMsg The message holding the result to save
 *
 * @param fw The forward structure to save the result in
 *
 * @param error Error code to save
 *
 * @param func Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returns false if the processing should be stopped;
 * otherwise true is returned even if errors occurred
 */
static bool saveFrwrdMsgReply(Slurm_Msg_t *sMsg, Msg_Forward_t *fw,
			      uint32_t error, const char *func, const int line)
{
    fw->numRes++;
    PSnodes_ID_t srcNode = PSC_getID(sMsg->source);
    if (srcNode == PSC_getMyID()) {
	/* save local processed message */
	fw->head.type = sMsg->head.type;
	fw->head.uid = sMsg->head.uid;
	if (!memToDataBuffer(sMsg->reply.buf, sMsg->reply.bufUsed, fw->body)) {
	    flog("error saving local result, caller %s@%i\n", func, line);
	    return true;
	}
    } else {
	/* save message from other node */
	bool saved = false;
	for (uint16_t i = 0; i < fw->head.forward; i++) {
	    Slurm_Forward_Res_t *fwRes = &fw->head.fwRes[i];
	    /* test for double replies */
	    if (fwRes->node == srcNode) {
		flog("result for node %i already saved, caller %s@%i\n",
		     srcNode, func, line);
		fw->numRes--;
		return false;
	    }
	    /* find the next free slot */
	    if (fwRes->node == -1) {
		/* save the result */
		fwRes->error = error;
		fwRes->type = sMsg->head.type;
		fwRes->node = srcNode;
		if (sMsg->reply.bufUsed) {
		    if (!memToDataBuffer(sMsg->reply.buf, sMsg->reply.bufUsed,
					 fwRes->body)) {
			flog("saving error failed, caller %s@%i\n", func, line);
		    }
		}
		fw->head.returnList++;
		saved = true;
		break;
	    }
	}
	if (!saved) {
	    flog("error saving result for src %s, caller %s@%i\n",
		 PSC_printTID(sMsg->source), func, line);
	    return true;
	}
    }
    return true;
}

void __handleFrwrdMsgReply(Slurm_Msg_t *sMsg, uint32_t error, const char *func,
			   const int line)
{
    if (!sMsg) {
	flog("invalid sMsg from %s@%i\n", func, line);
	return;
    }

    /* find open connection for forwarded message */
    Connection_t *con = findConnectionEx(sMsg->sock, sMsg->recvTime);
    if (!con) {
	flog("no connection to %s sock %i recvTime %zu type %s caller %s@%i\n",
	     PSC_printTID(sMsg->source), sMsg->sock, sMsg->recvTime,
	     msgType2String(sMsg->head.type), func, line);
	return;
    }

    Msg_Forward_t *fw = &con->fw;

    /* save the new result for a single node */
    if (!saveFrwrdMsgReply(sMsg, fw, error, func, line)) return;

    fdbg(PSSLURM_LOG_FWD, "caller %s@%i type %s forward %u resCount %u "
	 "source %s sock %i recvTime %zu\n", func, line,
	 msgType2String(sMsg->head.type), fw->head.forward, fw->numRes,
	 PSC_printTID(sMsg->source), sMsg->sock, sMsg->recvTime);

    /* test if all nodes replied and therefore the forward is complete */
    if (fw->numRes == fw->nodesCount + 1) {
	/* all answers collected */
	fdbg(PSSLURM_LOG_FWD, "forward %s complete, sending answer\n",
	     msgType2String(sMsg->head.type));

	/* disable forwarding for the answer message */
	fw->head.forward = 0;

	if (!PSdbGetBuf(fw->body) || !PSdbGetUsed(fw->body)) {
	    flog("invalid data, drop msg from caller %s@%i\n", func, line);
	    closeSlurmCon(con->sock);
	    return;
	}

	/* Answer the original RPC request from Slurm. This reply will hold
	 * results from all the nodes the RPC was forwarded to. */
	PS_SendDB_t msg = {
	    .buf = PSdbGetBuf(fw->body),
	    .bufUsed = PSdbGetUsed(fw->body),
	    .useFrag = false, };
	sendSlurmMsgEx(con->sock, &fw->head, &msg, NULL);
	closeSlurmCon(con->sock);
    }
}

/**
 * @brief Get remote address and port of a socket
 *
 * Retrive the address and port of a given socket. On error
 * @ref slurmAddr will not be updated.
 *
 * @param slurmAddr Holding the result on success
 *
 * Returns true on success and false on error
 */
static bool getSockInfo(int socket, Slurm_Addr_t *slurmAddr)
{
    struct sockaddr_in sock_addr;
    socklen_t len = sizeof(sock_addr);

    if (getpeername(socket, (struct sockaddr*)&sock_addr, &len) == -1) {
	mwarn(errno, "%s: getpeername(%i)", __func__, socket);
	return false;
    }

#ifndef __clang_analyzer__
    slurmAddr->ip = sock_addr.sin_addr.s_addr;
    slurmAddr->port = sock_addr.sin_port;
#endif

    return true;
}

/**
 * @brief Read a Slurm message
 *
 * Read a Slurm message from the provided socket. In the first step
 * the size of the message is read. And secondly the actual message
 * payload is read. All reading is done in a non blocking fashion. If
 * reading of the message was successful it will be saved in a new
 * Slurm message and given to the provided callback of the
 * Connection. After the callback handled the message or an error
 * occurred the Connection will be resetted.
 *
 * @param sock The socket to read the message from
 *
 * @param param The associated connection
 *
 * @return Always returns 0
 */
static int readSlurmMsg(int sock, void *param)
{
    Connection_t *con = param;
    PS_DataBuffer_t dBuf = con->data;
    bool error = false;

    if (!param) {
	flog("invalid connection data buffer\n");
	closeSlurmCon(sock);
	return 0;
    }

    size_t size = PSdbGetSize(dBuf);
    if (size && PSdbGetUsed(dBuf) == size) {
	flog("data buffer for sock %i already in use, resetting\n", sock);
	resetConnection(sock);
    }

    int ret;
    /* try to read the message size */
    if (!con->readSize) {
	PSdbGrow(dBuf, sizeof(con->readSize));
	PSdbClearBuf(dBuf);

	ret = PSdbRecvPProg(sock, sizeof(con->readSize), dBuf);
	if (ret < 0) {
	    int eno = errno;
	    if (eno == ENODATA) {
		/* not all data arrived yet, lets try again later */
		fdbg(PSSLURM_LOG_COMM, "we try later for sock %u read %zu\n",
		     sock, PSdbGetUsed(dBuf));
		return 0;
	    }
	    /* read error */
	    fwarn(eno, "PSdbRecvPProg(%d, toRead %zd) got %zd", sock,
		  sizeof(con->readSize), PSdbGetUsed(dBuf));
	    error = true;
	    goto CALLBACK;
	} else if (!ret) {
	    /* connection reset */
	    fdbg(PSSLURM_LOG_COMM, "closing connection, empty message len on"
		 " sock %i\n", sock);
	    error = true;
	    goto CALLBACK;
	}

	/* all data read successful */
	PSdbRewind(dBuf);
	getUint32(dBuf, &con->readSize);
	fdbg(PSSLURM_LOG_COMM, "msg size read for %u ret %u msglen %u\n",
	     sock, ret, con->readSize);

	if (con->readSize > MAX_MSG_SIZE) {
	    flog("msg too big %u (max %u)\n", con->readSize, MAX_MSG_SIZE);
	    error = true;
	    goto CALLBACK;
	} else if (!con->readSize) {
	    flog("zero-length message not supported\n");
	    error = true;
	    goto CALLBACK;
	}

	PSdbGrow(dBuf, con->readSize);
	PSdbClearBuf(dBuf);
    }

    /* try to read the actual payload (missing data) */
    ret = PSdbRecvPProg(sock, con->readSize, dBuf);
    if (ret < 0) {
	int eno = errno;
	if (eno == ENODATA) {
	    /* not all data arrived yet, lets try again later */
	    fdbg(PSSLURM_LOG_COMM, "we try later for sock %u read %zu\n",
		 sock, PSdbGetUsed(dBuf));
	    return 0;
	}
	/* read error */
	fwarn(eno, "PSdbRecvPProg(%d, toRead %u) got %zd)", sock,
	      con->readSize, PSdbGetUsed(dBuf));
	error = true;
	goto CALLBACK;

    } else if (!ret) {
	/* connection reset */
	flog("connection reset on sock %i\n", sock);
	error = true;
	goto CALLBACK;
    }

    /* all data read successfully */
    PSdbRewind(dBuf);
    fdbg(PSSLURM_LOG_COMM, "all data read for %u ret %u toread %zu\n",
	 sock, ret, PSdbGetSize(dBuf));

CALLBACK:

    if (!error) {
	Slurm_Msg_t sMsg;

	initSlurmMsg(&sMsg);
	sMsg.sock = sock;
	sMsg.data = dBuf;
	sMsg.recvTime = con->recvTime = time(NULL);
	sMsg.authRequired = con->authByInMsg;

	/* overwrite empty addr informations */
	getSockInfo(sock, &sMsg.head.addr);

	processSlurmMsg(&sMsg, &con->fw, con->cb, con->info);
    }
    resetConnection(sock);

    if (error) {
	fdbg(ret ? -1 : PSSLURM_LOG_COMM, "closeSlurmCon(%d)\n", sock);
	closeSlurmCon(sock);
    }

    return 0;
}

Connection_t *registerSlurmSocket(int sock, Connection_CB_t *cb, void *info)
{
    if (sock < 0) {
	flog("invalid socket %i\n", sock);
	return NULL;
    }

    Connection_t *con = addConnection(sock, cb, info);
    if (!con) {
	flog("failed to add connection for sock %i\n", sock);
	return NULL;
    }

    PSCio_setFDblock(sock, false);
    if (Selector_register(sock, readSlurmMsg, con) == -1) {
	flog("register socket %i failed\n", sock);
	con->info = NULL; // prevent free() since info is handled outside
	closeSlurmCon(sock);
	return NULL;
    }
    return con;
}

const char *slurmRC2String(int rc)
{
    if (rc < 1000) return strerror(rc);

    switch (rc) {
	case ESLURM_INVALID_PARTITION_NAME:
	    return "ESLURM_INVALID_PARTITION_NAME";
	case ESLURM_DEFAULT_PARTITION_NOT_SET:
	    return "ESLURM_DEFAULT_PARTITION_NOT_SET";
	case ESLURM_ACCESS_DENIED:
	    return "ESLURM_ACCESS_DENIED";
	case ESLURM_JOB_MISSING_REQUIRED_PARTITION_GROUP:
	    return "ESLURM_JOB_MISSING_REQUIRED_PARTITION_GROUP";
	case ESLURM_REQUESTED_NODES_NOT_IN_PARTITION:
	    return "ESLURM_REQUESTED_NODES_NOT_IN_PARTITION";
	case ESLURM_TOO_MANY_REQUESTED_CPUS:
	    return "ESLURM_TOO_MANY_REQUESTED_CPUS";
	case ESLURM_INVALID_NODE_COUNT:
	    return "ESLURM_INVALID_NODE_COUNT";
	case ESLURM_ERROR_ON_DESC_TO_RECORD_COPY:
	    return "ESLURM_ERROR_ON_DESC_TO_RECORD_COPY";
	case ESLURM_JOB_MISSING_SIZE_SPECIFICATION:
	    return "ESLURM_JOB_MISSING_SIZE_SPECIFICATION";
	case ESLURM_JOB_SCRIPT_MISSING:
	    return "ESLURM_JOB_SCRIPT_MISSING";
	case ESLURM_USER_ID_MISSING:
	    return "ESLURM_USER_ID_MISSING";
	case ESLURM_DUPLICATE_JOB_ID:
	    return "ESLURM_DUPLICATE_JOB_ID";
	case ESLURM_PATHNAME_TOO_LONG:
	    return "ESLURM_PATHNAME_TOO_LONG";
	case ESLURM_NOT_TOP_PRIORITY:
	    return "ESLURM_NOT_TOP_PRIORITY";
	case ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE:
	    return "ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE";
	case ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE:
	    return "ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE";
	case ESLURM_NODES_BUSY:
	    return "ESLURM_NODES_BUSY";
	case ESLURM_INVALID_JOB_ID:
	    return "ESLURM_INVALID_JOB_ID";
	case ESLURM_INVALID_NODE_NAME:
	    return "ESLURM_INVALID_NODE_NAME";
	case ESLURM_WRITING_TO_FILE:
	    return "ESLURM_WRITING_TO_FILE";
	case ESLURM_TRANSITION_STATE_NO_UPDATE:
	    return "ESLURM_TRANSITION_STATE_NO_UPDATE";
	case ESLURM_ALREADY_DONE:
	    return "ESLURM_ALREADY_DONE";
	case ESLURM_INTERCONNECT_FAILURE:
	    return "ESLURM_INTERCONNECT_FAILURE";
	case ESLURM_BAD_DIST:
	    return "ESLURM_BAD_DIST";
	case ESLURM_JOB_PENDING:
	    return "ESLURM_JOB_PENDING";
	case ESLURM_BAD_TASK_COUNT:
	    return "ESLURM_BAD_TASK_COUNT";
	case ESLURM_INVALID_JOB_CREDENTIAL:
	    return "ESLURM_INVALID_JOB_CREDENTIAL";
	case ESLURM_IN_STANDBY_MODE:
	    return "ESLURM_IN_STANDBY_MODE";
	case ESLURM_INVALID_NODE_STATE:
	    return "ESLURM_INVALID_NODE_STATE";
	case ESLURM_INVALID_FEATURE:
	    return "ESLURM_INVALID_FEATURE";
	case ESLURM_INVALID_AUTHTYPE_CHANGE:
	    return "ESLURM_INVALID_AUTHTYPE_CHANGE";
	case ESLURM_INVALID_CHECKPOINT_TYPE_CHANGE:
	    return "ESLURM_INVALID_CHECKPOINT_TYPE_CHANGE";
	case ESLURM_INVALID_SCHEDTYPE_CHANGE:
	    return "ESLURM_INVALID_SCHEDTYPE_CHANGE";
	case ESLURM_INVALID_SELECTTYPE_CHANGE:
	    return "ESLURM_INVALID_SELECTTYPE_CHANGE";
	case ESLURM_INVALID_SWITCHTYPE_CHANGE:
	    return "ESLURM_INVALID_SWITCHTYPE_CHANGE";
	case ESLURM_FRAGMENTATION:
	    return "ESLURM_FRAGMENTATION";
	case ESLURM_NOT_SUPPORTED:
	    return "ESLURM_NOT_SUPPORTED";
	case ESLURM_DISABLED:
	    return "ESLURM_DISABLED";
	case ESLURM_DEPENDENCY:
	    return "ESLURM_DEPENDENCY";
	case ESLURM_BATCH_ONLY:
	    return "ESLURM_BATCH_ONLY";
	case ESLURM_TASKDIST_ARBITRARY_UNSUPPORTED:
	    return "ESLURM_TASKDIST_ARBITRARY_UNSUPPORTED";
	case ESLURM_TASKDIST_REQUIRES_OVERCOMMIT:
	    return "ESLURM_TASKDIST_REQUIRES_OVERCOMMIT";
	case ESLURM_JOB_HELD:
	    return "ESLURM_JOB_HELD";
	case ESLURM_INVALID_CRYPTO_TYPE_CHANGE:
	    return "ESLURM_INVALID_CRYPTO_TYPE_CHANGE";
	case ESLURM_INVALID_TASK_MEMORY:
	    return "ESLURM_INVALID_TASK_MEMORY";
	case ESLURM_INVALID_ACCOUNT:
	    return "ESLURM_INVALID_ACCOUNT";
	case ESLURM_INVALID_PARENT_ACCOUNT:
	    return "ESLURM_INVALID_PARENT_ACCOUNT";
	case ESLURM_SAME_PARENT_ACCOUNT:
	    return "ESLURM_SAME_PARENT_ACCOUNT";
	case ESLURM_INVALID_LICENSES:
	    return "ESLURM_INVALID_LICENSES";
	case ESLURM_NEED_RESTART:
	    return "ESLURM_NEED_RESTART";
	case ESLURM_ACCOUNTING_POLICY:
	    return "ESLURM_ACCOUNTING_POLICY";
	case ESLURM_INVALID_TIME_LIMIT:
	    return "ESLURM_INVALID_TIME_LIMIT";
	case ESLURM_RESERVATION_ACCESS:
	    return "ESLURM_RESERVATION_ACCESS";
	case ESLURM_RESERVATION_INVALID:
	    return "ESLURM_RESERVATION_INVALID";
	case ESLURM_INVALID_TIME_VALUE:
	    return "ESLURM_INVALID_TIME_VALUE";
	case ESLURM_RESERVATION_BUSY:
	    return "ESLURM_RESERVATION_BUSY";
	case ESLURM_RESERVATION_NOT_USABLE:
	    return "ESLURM_RESERVATION_NOT_USABLE";
	case ESLURM_INVALID_WCKEY:
	    return "ESLURM_INVALID_WCKEY";
	case ESLURM_RESERVATION_OVERLAP:
	    return "ESLURM_RESERVATION_OVERLAP";
	case ESLURM_PORTS_BUSY:
	    return "ESLURM_PORTS_BUSY";
	case ESLURM_PORTS_INVALID:
	    return "ESLURM_PORTS_INVALID";
	case ESLURM_PROLOG_RUNNING:
	    return "ESLURM_PROLOG_RUNNING";
	case ESLURM_NO_STEPS:
	    return "ESLURM_NO_STEPS";
	case ESLURM_INVALID_BLOCK_STATE:
	    return "ESLURM_INVALID_BLOCK_STATE";
	case ESLURM_INVALID_BLOCK_LAYOUT:
	    return "ESLURM_INVALID_BLOCK_LAYOUT";
	case ESLURM_INVALID_BLOCK_NAME:
	    return "ESLURM_INVALID_BLOCK_NAME";
	case ESLURM_INVALID_QOS:
	    return "ESLURM_INVALID_QOS";
	case ESLURM_QOS_PREEMPTION_LOOP:
	    return "ESLURM_QOS_PREEMPTION_LOOP";
	case ESLURM_NODE_NOT_AVAIL:
	    return "ESLURM_NODE_NOT_AVAIL";
	case ESLURM_INVALID_CPU_COUNT:
	    return "ESLURM_INVALID_CPU_COUNT";
	case ESLURM_PARTITION_NOT_AVAIL:
	    return "ESLURM_PARTITION_NOT_AVAIL";
	case ESLURM_CIRCULAR_DEPENDENCY:
	    return "ESLURM_CIRCULAR_DEPENDENCY";
	case ESLURM_INVALID_GRES:
	    return "ESLURM_INVALID_GRES";
	case ESLURM_JOB_NOT_PENDING:
	    return "ESLURM_JOB_NOT_PENDING";
	case ESLURM_QOS_THRES:
	    return "ESLURM_QOS_THRES";
	case ESLURM_PARTITION_IN_USE:
	    return "ESLURM_PARTITION_IN_USE";
	case ESLURM_STEP_LIMIT:
	    return "ESLURM_STEP_LIMIT";
	case ESLURM_JOB_SUSPENDED:
	    return "ESLURM_JOB_SUSPENDED";
	case ESLURM_CAN_NOT_START_IMMEDIATELY:
	    return "ESLURM_CAN_NOT_START_IMMEDIATELY";
	case ESLURM_INTERCONNECT_BUSY:
	    return "ESLURM_INTERCONNECT_BUSY";
	case ESLURM_RESERVATION_EMPTY:
	    return "ESLURM_RESERVATION_EMPTY";
	case ESLURM_INVALID_ARRAY:
	    return "ESLURM_INVALID_ARRAY";
	case SLURM_UNEXPECTED_MSG_ERROR:
	    return "SLURM_UNEXPECTED_MSG_ERROR";
	case SLURM_COMMUNICATIONS_CONNECTION_ERROR:
	    return "SLURM_COMMUNICATIONS_CONNECTION_ERROR";
	case SLURM_COMMUNICATIONS_SEND_ERROR:
	    return "SLURM_COMMUNICATIONS_SEND_ERROR";
	case SLURM_COMMUNICATIONS_RECEIVE_ERROR:
	    return "SLURM_COMMUNICATIONS_RECEIVE_ERROR";
	case SLURM_COMMUNICATIONS_SHUTDOWN_ERROR:
	    return "SLURM_COMMUNICATIONS_SHUTDOWN_ERROR";
	case SLURM_PROTOCOL_VERSION_ERROR:
	    return "SLURM_PROTOCOL_VERSION_ERROR";
	case SLURM_PROTOCOL_IO_STREAM_VERSION_ERROR:
	    return "SLURM_PROTOCOL_IO_STREAM_VERSION_ERROR";
	case SLURM_PROTOCOL_AUTHENTICATION_ERROR:
	    return "SLURM_PROTOCOL_AUTHENTICATION_ERROR";
	case SLURM_PROTOCOL_INSANE_MSG_LENGTH:
	    return "SLURM_PROTOCOL_INSANE_MSG_LENGTH";
	case SLURM_MPI_PLUGIN_NAME_INVALID:
	    return "SLURM_MPI_PLUGIN_NAME_INVALID";
	case SLURM_MPI_PLUGIN_PRELAUNCH_SETUP_FAILED:
	    return "SLURM_MPI_PLUGIN_PRELAUNCH_SETUP_FAILED";
	case SLURM_PLUGIN_NAME_INVALID:
	    return "SLURM_PLUGIN_NAME_INVALID";
	case SLURM_UNKNOWN_FORWARD_ADDR:
	    return "SLURM_UNKNOWN_FORWARD_ADDR";
	case SLURMCTLD_COMMUNICATIONS_CONNECTION_ERROR:
	    return "SLURMCTLD_COMMUNICATIONS_CONNECTION_ERROR";
	case SLURMCTLD_COMMUNICATIONS_SEND_ERROR:
	    return "SLURMCTLD_COMMUNICATIONS_SEND_ERROR";
	case SLURMCTLD_COMMUNICATIONS_RECEIVE_ERROR:
	    return "SLURMCTLD_COMMUNICATIONS_RECEIVE_ERROR";
	case SLURMCTLD_COMMUNICATIONS_SHUTDOWN_ERROR:
	    return "SLURMCTLD_COMMUNICATIONS_SHUTDOWN_ERROR";
	case ESLURM_CONFIGLESS_DISABLED:
	    return "ESLURM_CONFIGLESS_DISABLED";
    }

    static char buf[64];
    snprintf(buf, sizeof(buf), "unkown %d", rc);

    return buf;
}

/**
 * @brief Handle a reply from slurmctld
 *
 * Report errors for failed requests send to slurmctld and close
 * the corresponding connection. If a callback and an expected
 * response message type is specified, the response is passed
 * to that callback for further handling.
 *
 * @param sMsg The reply message to handle
 *
 * @param info Holding optional information about the original request
 *
 * @return Always returns 0.
 */
static int handleSlurmctldReply(Slurm_Msg_t *sMsg, void *info)
{
    Req_Info_t *req = info;

    /* let the callback handle expected responses */
    if (req && req->cb && sMsg->head.type == req->expRespType) {
	fdbg(PSSLURM_LOG_COMM, "req %s -> resp %s jobid %u handled by cb\n",
	     msgType2String(req->type), msgType2String(sMsg->head.type),
	     req->jobid);
	req->cb(sMsg, info);
	goto CLEANUP;
    }

    /* we expect to handle RESPONSE_SLURM_RC only, every other response
     * has to be handled by the callback of the request */
    if (sMsg->head.type != RESPONSE_SLURM_RC) {
	flog("unexpected slurmctld reply %s(%i)",
	     msgType2String(sMsg->head.type), sMsg->head.type);
	if (req) {
	    mlog(" for request %s jobid %u",
		 msgType2String(req->type), req->jobid);
	}
	mlog("\n");
	goto CLEANUP;
    }

    /* inspect return code */
    uint32_t rc;
    getUint32(sMsg->data, &rc);
    if (rc != SLURM_SUCCESS) {
	flog("error: response %s rc %s sock %i",
	     msgType2String(sMsg->head.type), slurmRC2String(rc), sMsg->sock);
	if (req) {
	    mlog(" for request %s jobid %u",
		 msgType2String(req->type), req->jobid);
	}
	mlog("\n");
    } else {
	if (req) {
	    fdbg(PSSLURM_LOG_COMM, "got SLURM_SUCCESS for req %s jobid %u\n",
		 msgType2String(req->type), req->jobid);
	}
    }

CLEANUP:
    if (sMsg->source == -1) closeSlurmCon(sMsg->sock);
    return 0;
}

int tcpConnectU(uint32_t addr, uint16_t port)
{
    char sPort[128];
    struct in_addr sin_addr;

    snprintf(sPort, sizeof(sPort), "%u", port);
    sin_addr.s_addr = addr;

    return tcpConnect(inet_ntoa(sin_addr), sPort);
}

int tcpConnect(char *addr, char *port)
{
/* number of reconnect tries until we give up */
#define TCP_CONNECTION_RETRYS 10

    struct addrinfo *result, *rp;
    int sock = -1, ret, reConnect = 0, err;

TCP_RECONNECT:

    err = errno = 0;

    /* set up the sockaddr structure */
    ret = getaddrinfo(addr, port, NULL, &result);
    if (ret) {
	mlog("%s: getaddrinfo(%s:%s) failed : %s\n", __func__,
	     addr, port, gai_strerror(ret));
	return -1;
    }

    if (!reConnect) {
	mdbg(PSSLURM_LOG_COMM, "%s: to %s port:%s\n", __func__, addr, port);
    }

    for (rp = result; rp; rp = rp->ai_next) {
	bool connectFailed = false;
	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
	    err = errno;
	    mwarn(errno, "%s: socket(addr %s port %s)", __func__, addr, port);
	    continue;
	}

	/* NOTE: This is only suitable for systems implementing connect() in
	 *       Linux style and will fail on systems implementing it the
	 *       Solaris way (meaning a subsequent connect after EINTR does
	 *       not block but immediately return with EALREADY) */
	while (connect(sock, rp->ai_addr, rp->ai_addrlen) == -1
	       && errno != EISCONN) {
	    err = errno;
	    if (errno != EINTR) {
		connectFailed = true;
		mwarn(err, "%s: connect(addr %s port %s)", __func__,
		      addr, port);
		break;
	    }
	}

	if (!connectFailed) break;

	close(sock);
    }
    freeaddrinfo(result);

    if (!rp) {
	if (err == EISCONN && reConnect < TCP_CONNECTION_RETRYS) {
	    close(sock);
	    reConnect++;
	    goto TCP_RECONNECT;
	}
	mwarn(err, "%s: addr %s port %s err %i", __func__, addr, port, err);
	close(sock);
	return -1;
    }

    if (mset(PSSLURM_LOG_COMM)) {
	struct sockaddr_in sockLocal, sockRemote;
	socklen_t lenLoc = sizeof(sockLocal), lenRem = sizeof(sockRemote);

	if (getsockname(sock, (struct sockaddr*)&sockLocal, &lenLoc) == -1) {
	    mwarn(errno, "%s: getsockname(%i)", __func__, sock);
	} else if (getpeername(sock, (struct sockaddr*)&sockRemote,
			       &lenRem) == -1) {
	    mwarn(errno, "%s: getpeername(%i)", __func__, sock);
	} else {
#ifndef __clang_analyzer__
	    flog("socket %i connected local %s:%u remote %s:%u\n", sock,
		 inet_ntoa(sockRemote.sin_addr), ntohs(sockRemote.sin_port),
		 inet_ntoa(sockLocal.sin_addr), ntohs(sockLocal.sin_port));
#endif
	}
    }

    return sock;
}

int openSlurmctldCon(void *info)
{
    return openSlurmctldConEx(handleSlurmctldReply, info);
}

int openSlurmctldConEx(Connection_CB_t *cb, void *info)
{
    char *port = getConfValueC(SlurmConfig, "SlurmctldPort");

    int first, last;

    if (strstr(port, "-")) {
	/* this is a port range */
	if (sscanf(port, "%d-%d", &first, &last) != 2) {
	    flog("failed to parse port range '%s'\n", port);
	    return -1;
	}
    } else {
	/* this cannot fail since at least default is set */
	first = last = atoi(port);
    }

    int sock = -1;
    for (int i = 0; i < ctlHostsCount; i++) {
	char *addr = (ctlHosts[i].addr) ? ctlHosts[i].addr : ctlHosts[i].host;

	if (first == last) {
	    sock = tcpConnect(addr, port);
	    if (sock > -1) {
		fdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB,
		     "connected to %s socket %i\n", addr, sock);
		break;
	    }
	} else {
	    int offset = PSC_getMyID() % (last - first + 1);
	    for (int p = 0; p < last - first + 1; p++) {
		char cur[10];
		snprintf(cur, 10, "%d",
			 first + (p + offset) % (last - first + 1));
		sock = tcpConnect(addr, cur);
		if (sock > -1) {
		    fdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB,
			 "connected to %s socket %i\n", addr, sock);
		    break;
		}
	    }
	    if (sock > -1) break;
	}
    }

    if (sock < 0) {
	flog("connecting to %i configured slurmctld failed\n", ctlHostsCount);
	return sock;
    }

    if (!registerSlurmSocket(sock, cb, info)) {
	flog("register Slurm socket %i failed\n", sock);
	close(sock);
	return -1;
    }

    return sock;
}

int __sendDataBuffer(int sock, PS_SendDB_t *data, size_t offset,
		     size_t *written, const char *caller, const int line)
{
    char *ptr = data->buf + offset;
    size_t towrite = data->bufUsed - offset;
    ssize_t ret = PSCio_sendPProg(sock, ptr, towrite, written);
    int eno = errno;
    if (ret == -1) {
	flog("writing message of length %i failed, ret %zi written %zu"
	     " caller %s@%i\n", data->bufUsed, ret, *written, caller, line);
	errno = eno;
	return -1;
    }
    fdbg(PSSLURM_LOG_COMM, "wrote data: %zu offset %zu sock %i for "
	 "caller %s@%i\n", *written, offset, sock, caller, line);

    return ret;
}

int __sendSlurmMsg(int sock, slurm_msg_type_t type, PS_SendDB_t *body,
		   uid_t uid, const char *caller, const int line)
{
    Slurm_Msg_Header_t head;
    initSlurmMsgHead(&head);
    head.type = type;
    head.uid = uid;

    return __sendSlurmMsgEx(sock, &head, body, NULL, caller, line);
}

int __sendSlurmMsgEx(int sock, Slurm_Msg_Header_t *head, PS_SendDB_t *body,
		     Req_Info_t *req, const char *caller, const int line)
{
    if (sock >= 0 && req) {
	flog("new request for existing connection from %s@%i\n", caller, line);
	ufree(req);
	return -1;
    }

    if (!head) {
	flog("invalid header from %s@%i\n", caller, line);
	ufree(req);
	return -1;
    }

    if (!body) {
	flog("invalid body from %s@%i\n", caller, line);
	ufree(req);
	return -1;
    }

    fdbg(PSSLURM_LOG_PROTO, "msg(%i): %s, version %u munge uid %i "
	 "caller %s:%i\n", head->type, msgType2String(head->type),
	 head->version, head->uid, caller, line);

    Connection_t *con = findConnection(sock);
    Slurm_Auth_t *auth = NULL;
    if (slurmProto > SLURM_22_05_PROTO_VERSION &&
	sock >= 0 && con && con->authByInMsg) {
	/* if the connection was verified before, the authentication can
	 * be skipped for the response */
	head->flags |= SLURM_NO_AUTH_CRED;
	fdbg(PSSLURM_LOG_AUTH, "skipping Slurm auth for msg %s\n",
	     msgType2String(head->type));
    } else {
	auth = getSlurmAuth(head, body->buf, body->bufUsed);
	if (!auth) {
	    flog("getting a slurm authentication token failed\n");
	    ufree(req);
	    return -1;
	}
	fdbg(PSSLURM_LOG_AUTH, "added Slurm auth for msg %s\n",
	     msgType2String(head->type));
    }

    /* connect to slurmctld */
    if (sock < 0) {
	sock = openSlurmctldCon(req);
	if (sock < 0) {
	    freeSlurmAuth(auth);
	    if (needMsgResend(head->type)) {
		Slurm_Msg_Buf_t *savedMsg;

		savedMsg = saveSlurmMsg(head, body, req, NULL, -1, 0);
		if (setReconTimer(savedMsg) == -1) {
		    flog("setting resend timer failed\n");
		    /* without a connection the request has to be freed */
		    ufree(req);
		    deleteMsgBuf(savedMsg);
		    return -1;
		}
		flog("sending msg %s failed, saved for later resend\n",
		     msgType2String(head->type));
		return -2;
	    }
	    /* without re-sending the request has to be freed */
	    ufree(req);
	    return -1;
	}
    }

    /* non blocking write */
    PSCio_setFDblock(sock, false);

    PS_DataBuffer_t payload = PSdbNew(NULL, 0);
    memToDataBuffer(body->buf, body->bufUsed, payload);

    PS_SendDB_t data = { .bufUsed = 0, .useFrag = false };
    packSlurmMsg(&data, head, payload, auth);

    size_t written = 0;
    int ret = __sendDataBuffer(sock, &data, 0, &written, caller, line);
    if (ret == -1) {
	if (errno == EAGAIN || errno == EINTR) {
	    /* msg was fractionally written, retry later */
	    Slurm_Msg_Buf_t *savedMsg = saveSlurmMsg(head, body, req, auth,
						     sock, written);
	    Selector_awaitWrite(sock, resendSlurmMsg, savedMsg);
	    ret = -2;
	} else {
	    flog("sending msg type %s failed\n", msgType2String(head->type));
	}
    }

    freeSlurmAuth(auth);
    PSdbDestroy(payload);

    return ret;
}

int __sendSlurmctldReq(Req_Info_t *req, void *data,
		       const char *caller, const int line)
{
    PS_SendDB_t msg = { .bufUsed = 0, .useFrag = false };
    if (!packSlurmReq(req, &msg, data, caller, line)) {
	flog("packing request %s for %s:%i failed\n", msgType2String(req->type),
	     caller, line);
	ufree(req);
	return -1;
    }

    req->time = time(NULL);

    Slurm_Msg_Header_t head;
    initSlurmMsgHead(&head);
    head.type = req->type;
    head.uid = slurmUserID;

    return __sendSlurmMsgEx(SLURMCTLD_SOCK, &head, &msg, req, caller, line);
}

static void addVal2List(strbuf_t buf, int32_t val, bool range, bool fin,
			hexBitStrConv_func_t *conv)
{
    char tmp[128];
    static int32_t lastVal, rangeVal;

    if (fin) {
	/* add end range of compacted list */
	if (range && rangeVal != -1) {
	    snprintf(tmp, sizeof(tmp), "-%i", rangeVal);
	    strbufAdd(buf, tmp);
	}
	lastVal = rangeVal = -1;
	return;
    }

    /* call convert func */
    if (conv) {
	val = conv(val);
	if (val == -1) return;
    }

    if (range) {
	/* compact list with range syntax */
	if (!strbufLen(buf)) lastVal = rangeVal = -1;

	if (lastVal != -1 && val == lastVal+1) {
	    rangeVal = val;
	} else {
	    if (rangeVal != -1) {
		snprintf(tmp, sizeof(tmp), "-%i", rangeVal);
		strbufAdd(buf, tmp);
		rangeVal = -1;
	    }
	    if (strbufLen(buf)) strbufAdd(buf, ",");
	    snprintf(tmp, sizeof(tmp), "%i", val);
	    strbufAdd(buf, tmp);
	}
	lastVal = val;
    } else {
	/* comma separated list only */
	if (strbufLen(buf)) strbufAdd(buf, ",");
	snprintf(tmp, sizeof(tmp), "%i", val);
	strbufAdd(buf, tmp);
    }
}

bool hexBitstr2List(char *bitstr, strbuf_t buf, bool range)
{
    return hexBitstr2ListEx(bitstr, buf, range, NULL);
}

bool hexBitstr2ListEx(char *bitstr, strbuf_t buf, bool range,
		      hexBitStrConv_func_t *conv)
{
    if (!bitstr) {
	flog("invalid bitstring\n");
	return false;
    }

    strbufClear(buf);

    if (!strncmp(bitstr, "0x", 2)) bitstr += 2;
    size_t len = strlen(bitstr);
    int32_t count = 0;

    while (len--) {
	int32_t next = (int32_t) bitstr[len];

	if (!isxdigit(next)) return false;

	if (isdigit(next)) {
	    next -= '0';
	} else {
	    next = toupper(next);
	    next -= 'A' - 10;
	}

	for (int32_t i = 1; i <= 8; i *= 2) {
	    if (next & i) addVal2List(buf, count, range, false, conv);
	    count++;
	}
    }

    if (range) addVal2List(buf, 0, range, true, conv);
    strbufAdd(buf, "");

    return true;
}

bool hexBitstr2Set(char *bitstr, PSCPU_set_t set)
{
    if (!set) {
	flog("invalid set\n");
	return false;
    }
    PSCPU_clrAll(set);

    if (!bitstr) {
	flog("invalid bitstring\n");
	return false;
    }
    if (!strncmp(bitstr, "0x", 2)) bitstr += 2;
    size_t len = strlen(bitstr);

    uint16_t count = 0;
    while (len--) {
	uint8_t next = bitstr[len];

	if (!isxdigit(next)) return false;

	if (isdigit(next)) {
	    next -= '0';
	} else {
	    next = toupper(next);
	    next -= 'A' - 10;
	}

	for (uint8_t i = 1; i <= 8; i *= 2) {
	    if (next & i) PSCPU_setCPU(set, count);
	    count++;
	}
    }

    return true;
}

/**
 * @brief Accept a new Slurm client
 *
 * @param socket The socket of the new client
 *
 * @param data Unused
 *
 * @return Always returns 0
 */
static int acceptSlurmClient(int socket, void *data)
{
#ifndef __clang_analyzer__
    struct sockaddr_in SAddr;
    socklen_t clientlen = sizeof(SAddr);

    int newSock = accept(socket, (struct sockaddr *)&SAddr, &clientlen);
    if (newSock == -1) {
	fwarn(errno, "error accepting new tcp connection");
	return 0;
    }

    if (mset(PSSLURM_LOG_COMM)) {
	struct sockaddr_in sockLocal;
	socklen_t len = sizeof(sockLocal);

	flog("from %s:%u socket:%i", inet_ntoa(SAddr.sin_addr),
	     ntohs(SAddr.sin_port), newSock);
	if (getsockname(newSock, (struct sockaddr*)&sockLocal, &len) == -1) {
	    mwarn(errno, " getsockname(%i)", newSock);
	} else {
	    mlog(" local %s:%u\n", inet_ntoa(sockLocal.sin_addr),
		 ntohs(sockLocal.sin_port));
	}
    }

    Connection_t *con = registerSlurmSocket(newSock, handleSlurmdMsg, NULL);
    if (!con) {
	flog("failed to register socket %i\n", newSock);
    } else {
	con->authByInMsg = true;
    }
#endif
    return 0;
}

void closeSlurmdSocket(void)
{
    if (slurmListenSocket == -1) return;

    mdbg(PSSLURM_LOG_COMM, "%s\n", __func__);

    if (Selector_isRegistered(slurmListenSocket)) {
	Selector_remove(slurmListenSocket);
    }
    close(slurmListenSocket);
    slurmListenSocket = -1;
}

int openSlurmdSocket(int port)
{
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == -1) {
	fwarn(errno, "socket() failed for port %i", port);
	return -1;
    }

    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0 ) {
	mwarn(errno, "%s: setsockopt(%i, port %i)", __func__, sock, port);
    }

    /* set up the sockaddr structure */
    struct sockaddr_in saClient;
    saClient.sin_family = AF_INET;
    saClient.sin_addr.s_addr = INADDR_ANY;
    saClient.sin_port = htons(port);
    bzero(&(saClient.sin_zero), 8);

    /* bind the socket */
    int res = bind(sock, (struct sockaddr *)&saClient, sizeof(saClient));
    if (res == -1) {
	mwarn(errno, "%s: bind(%i, port %i)", __func__, sock, port);
	return -1;
    }

    /* set socket to listen state */
    res = listen(sock, SOMAXCONN);
    if (res == -1) {
	mwarn(errno, "%s: listen(%i, port %i)", __func__, sock, port);
	return -1;
    }

    /* add socket to psid selector */
    if (Selector_register(sock, acceptSlurmClient, NULL) == -1) {
	flog("Selector_register(%i, port %i) failed\n", sock, port);
	return -1;
    }
    fdbg(PSSLURM_LOG_COMM, "sock %i port %i\n", sock, port);

    slurmListenSocket = sock;
    return sock;
}

/**
 * @brief Handle a PTY control message
 *
 * Handle a srun PTY message to set the window size.
 *
 * @param sock The socket to read the message from
 *
 * @param data The step of the message
 *
 * @return Always returns 0
 */
static int handleSrunPTYMsg(int sock, void *data)
{
    Step_t *step = data;

    fdbg(PSSLURM_LOG_COMM, "got pty message for %s\n", Step_strID(step));

    uint16_t buffer[2];
    ssize_t ret = PSCio_recvBuf(sock, buffer, sizeof(buffer));
    if (ret <= 0) {
	if (ret < 0) mwarn(errno, "%s: PSCio_recvBuf()", __func__);
	flog("close pty connection\n");
	closeSlurmCon(sock);
	return 0;
    }

    if (ret != sizeof(buffer)) {
	flog("update window size error, len %zu\n", ret);
	return 0;
    }

    if (!step->fwdata) return 0;

    struct winsize ws = {
	.ws_row = ntohs(buffer[1]),
	.ws_col = ntohs(buffer[0]),
	.ws_xpixel = 0, // unused
	.ws_ypixel = 0, // unused
    };
    if (ioctl(step->fwdata->stdOut[1], TIOCSWINSZ, &ws)) {
	mwarn(errno, "%s: ioctl(TIOCSWINSZ)", __func__);
    }
    return 0;
}

/**
 * @brief Forward an input message to a rank
 *
 * @param step The step of the rank
 *
 * @param rank The rank to forward the message to
 *
 * @param buf The message to forward
 *
 * @param bufLen The size of the message
 *
 * @return Returns the number of bytes written or -1 on error
 */
static int forwardInputMsg(Step_t *step, uint16_t rank, char *buf, int bufLen)
{
    PS_Tasks_t *task = findTaskByJobRank(&step->tasks, rank);
    if (!task) {
	flog("task for rank %u of %s local ID %i not found\n", rank,
	     Step_strID(step), step->localNodeId);
	return -1;
    }

    PSLog_Msg_t msg = {
	.header = {
	    .type = PSP_CC_MSG,
	    .dest = task->forwarderTID,
	    .sender = PSC_getMyTID(),
	    .len = PSLog_headerSize },
       .version = 2,
       .type = STDIN,
       .sender = -1};

    char *ptr = buf;
    size_t c = bufLen;
    do {
	int n = (c > sizeof(msg.buf)) ? sizeof(msg.buf) : c;
	if (n) memcpy(msg.buf, ptr, n);
	fdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB,
	     "rank %u len %u msg.header.len %zu to %s\n",
	     rank, n, PSLog_headerSize + n, PSC_printTID(task->forwarderTID));
	fdbg(PSSLURM_LOG_IO_VERB, "'%.*s'\n", n, msg.buf);
	msg.header.len = PSLog_headerSize + n;
	// @todo most probably we have to put msg into an envelope
	sendMsgToMother((DDTypedBufferMsg_t *)&msg);
	c -= n;
	ptr += n;
    } while (c > 0);

    return bufLen;
}

int handleSrunIOMsg(int sock, void *stepPtr)
{
    IO_Slurm_Header_t *ioh = NULL;

    Step_t *step = stepPtr;
    if (!Step_verifyPtr(step)) {
	/* late answer from srun, associated step is already gone */
	fdbg(PSSLURM_LOG_IO, "no step for socket %i found\n", sock);
	goto ERROR;
    }

    /* Shall be safe to do first a pedantic receive inside a selector */
    char buffer[SLURM_IO_MAX_LEN];
    size_t rcvd = 0;
    ssize_t ret = PSCio_recvBufPProg(sock, buffer, SLURM_IO_HEAD_SIZE, &rcvd);
    if (ret <= 0) {
	if (ret < 0) mwarn(errno, "%s: PSCio_recvBufPProg()", __func__);
	flog("close srun connection %i for %s (rcvd %zd)\n", sock,
	     Step_strID(step), rcvd);
	if (sock == step->srunIOMsg.sock) {
	    step->srunIOMsg.sock = -1;
	}
	goto ERROR;
    }

    PS_DataBuffer_t data = PSdbNew(buffer, rcvd);
    bool success = unpackSlurmIOHeader(data, &ioh);
    PSdbDelete(data);
    if (!success) {
	flog("unpack Slurm I/O header for %s failed\n", Step_strID(step));
	goto ERROR;
    }

    if (!step->fwdata) {
	if (ioh->len) {
	    fdbg(PSSLURM_LOG_IO, "no forwarder for %s I/O type %s len %u\n",
		 Step_strID(step), IO_strType(ioh->type), ioh->len);
	}
	goto ERROR;
    }
    bool pty = step->taskFlags & LAUNCH_PTY;
    int fd = pty ? step->fwdata->stdOut[1] : step->fwdata->stdIn[1];
    uint32_t myTaskIdsLen = step->globalTaskIdsLen[step->localNodeId];
    uint32_t *myTaskIds = step->globalTaskIds[step->localNodeId];

    fdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB,
	 "%s stdin %d type %s length %u grank %u lrank %u pty %u"
	 " myTIDsLen %u\n", Step_strID(step), fd, IO_strType(ioh->type),
	 ioh->len, ioh->grank, ioh->lrank, pty, myTaskIdsLen);

    if (ioh->type == SLURM_IO_CONNECTION_TEST) {
	if (ioh->len != 0) {
	    flog("invalid connection test for %s len %u\n",
		 Step_strID(step), ioh->len);
	}
	srunSendIO(SLURM_IO_CONNECTION_TEST, 0, step, NULL, 0);
    } else if (!ioh->len) {
	/* forward eof to all forwarders */
	flog("got eof of stdin %d for %s\n", fd, Step_strID(step));

	if (ioh->type == SLURM_IO_STDIN) {
	    forwardInputMsg(step, ioh->grank, NULL, 0);
	} else if (ioh->type == SLURM_IO_ALLSTDIN) {
	    for (uint16_t i = 0; i < myTaskIdsLen; i++) {
		forwardInputMsg(step, myTaskIds[i], NULL, 0);
	    }
	} else {
	    flog("unsupported I/O type %s\n", IO_strType(ioh->type));
	}

	/* close loggers stdin */
	if (!pty) closeSlurmCon(fd);
    } else {
	/* forward stdin message to forwarders */
	size_t left = ioh->len;
	while (left > 0) {
	    size_t readNow = (left > sizeof(buffer)) ? sizeof(buffer) : left;
	    /* Assume body follows header swiftly */
	    ret = PSCio_recvBufP(sock, buffer, readNow);
	    if (ret <= 0) {
		mwarn(ret < 0 ? errno : 0, "%s: read body (size %zd/%zd)",
		      __func__, readNow, left);
		break;
	    }

	    if (pty) {
		if (step->leader) PSCio_sendP(fd, buffer, ret);
	    } else {
		switch (ioh->type) {
		case SLURM_IO_STDIN:
		    forwardInputMsg(step, ioh->grank, buffer, ret);
		    break;
		case SLURM_IO_ALLSTDIN:
		    for (uint16_t i = 0; i < myTaskIdsLen; i++) {
			forwardInputMsg(step, myTaskIds[i], buffer, ret);
		    }
		    break;
		default:
		    flog("unsupported I/O type %s\n", IO_strType(ioh->type));
		}
	    }
	    left -= ret;
	}
    }

    ufree(ioh);
    return 0;

ERROR:
    ufree(ioh);
    closeSlurmCon(sock);
    return 0;
}

int srunOpenControlConnection(Step_t *step)
{
    if (!step->numSrunPorts) {
	flog("sending failed, no srun ports available\n");
	return -1;
    }

    char port[32];
    snprintf(port, sizeof(port), "%u",
	     step->srunPorts[step->localNodeId % step->numSrunPorts]);
    int sock = tcpConnect(inet_ntoa(step->srun.sin_addr), port);
    if (sock < 0) {
	flog("connecting to srun %s:%s failed\n",
	     inet_ntoa(step->srun.sin_addr), port);
	return -1;
    }

    PSCio_setFDblock(sock, false);

    fdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB, "new srun conn %i to %s:%s\n",
	 sock, inet_ntoa(step->srun.sin_addr), port);

    return sock;
}

static int handleSrunMsg(Slurm_Msg_t *sMsg, void *info)
{
    Req_Info_t *req = info;
    Step_t step = { .jobid = req->jobid,
		    .stepid = req->stepid };

    if (sMsg->head.type != RESPONSE_SLURM_RC) {
	flog("unexpected srun response %s for request %s %s sock %i\n",
	     msgType2String(sMsg->head.type), msgType2String(req->type),
	     Step_strID(&step), sMsg->sock);
	goto CLEANUP;
    }

    /* inspect return code */
    uint32_t rc;
    getUint32(sMsg->data, &rc);
    if (rc != SLURM_SUCCESS) {
	flog("error: srun response %s rc %s sock %i for request %s %s\n",
	     msgType2String(sMsg->head.type), slurmRC2String(rc), sMsg->sock,
	     msgType2String(req->type), Step_strID(&step));
    } else {
	fdbg(PSSLURM_LOG_COMM, "got SLURM_SUCCESS for srun req %s %s sock %i\n",
	     msgType2String(req->type), Step_strID(&step), sMsg->sock);
    }

CLEANUP:
    closeSlurmCon(sMsg->sock);
    return 0;
}

int srunSendMsg(int sock, Step_t *step, slurm_msg_type_t type,
		PS_SendDB_t *body)
{
    if (sock < 0) {
	sock = srunOpenControlConnection(step);
	if (sock < 0) return -1;
    }

    Req_Info_t *req = ucalloc(sizeof(*req));
    req->type = type;
    req->jobid = step->jobid;
    req->stepid = step->stepid;
    req->time = time(NULL);

    Connection_t *con = registerSlurmSocket(sock, handleSrunMsg, req);
    if (!con) {
	flog("register Slurm socket %i failed\n", sock);
	ufree(req);
	return -1;
    }
    con->step = step;

    fdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB | PSSLURM_LOG_COMM,
	 "sock %u, len: body.bufUsed %u\n", sock, body->bufUsed);

    return sendSlurmMsg(sock, type, body, step->uid);
}

int srunOpenPTYConnection(Step_t *step)
{
    char *port = envGet(step->env, "SLURM_PTY_PORT");
    if (!port) {
	flog("missing SLURM_PTY_PORT variable\n");
	return -1;
    }

    int sock = tcpConnect(inet_ntoa(step->srun.sin_addr), port);
    if (sock < 0) {
	flog("connection to srun %s:%s failed\n",
	     inet_ntoa(step->srun.sin_addr), port);
	return -1;
    }
    flog("pty connection (%i) to %s:%s\n", sock,
	 inet_ntoa(step->srun.sin_addr), port);
    step->srunPTYMsg.sock = sock;

    if (Selector_register(sock, handleSrunPTYMsg, step) == -1) {
	flog("Selector_register(%i) failed\n", sock);
	return -1;
    }
    return sock;
}

int srunOpenIOConnectionEx(Step_t *step, uint32_t addr, uint16_t port,
			   char *sig)
{
    PSnodes_ID_t nodeID = step->localNodeId;
    int sock;

    /* open I/O connection to srun */
    if (!addr) {
	char sport[100];
	snprintf(sport, sizeof(sport), "%u",
		    step->IOPort[nodeID % step->numIOPort]);

	sock = tcpConnect(inet_ntoa(step->srun.sin_addr), sport);
	if (sock < 0) {
	    flog("connection to srun %s:%s failed\n",
		 inet_ntoa(step->srun.sin_addr), sport);
	    return -1;
	}

	flog("addr %s:%s sock %u\n",
	     inet_ntoa(step->srun.sin_addr), sport, sock);

	step->srunIOMsg.sock = sock;
    } else {
	sock = tcpConnectU(addr, port);
	if (sock < 0) {
	    flog("connection to srun %u:%u failed\n", addr, port);
	    return -1;
	}
    }

    PS_SendDB_t data = { .bufUsed = 0, .useFrag = false };
    /* add placeholder for length */
    addUint32ToMsg(0, &data);
    /* Slurm protocol */
    addUint16ToMsg(slurmProto, &data);

    /* nodeid */
    addUint32ToMsg(nodeID, &data);

    /* stdout obj count */
    if (step->stdOutOpt == IO_SRUN || step->stdOutOpt == IO_SRUN_RANK ||
	step->taskFlags & LAUNCH_PTY) {
	step->outChannels = malloc(sizeof(*step->outChannels)
				   * step->globalTaskIdsLen[nodeID]);
	for (uint32_t i = 0; i < step->globalTaskIdsLen[nodeID]; i++) {
	    step->outChannels[i] = true;
	}
	addUint32ToMsg(step->globalTaskIdsLen[nodeID], &data);
    } else {
	addUint32ToMsg(0, &data);
    }

    /* stderr obj count */
    if (step->taskFlags & LAUNCH_PTY ||
	(step->stdOut && strlen(step->stdOut) > 0) ||
	(step->stdErrRank == -1 && step->stdErr && strlen(step->stdErr) > 0)) {
	/* stderr uses stdout in pty mode */
	addUint32ToMsg(0, &data);
    } else {
	step->errChannels = malloc(sizeof(*step->errChannels)
				   * step->globalTaskIdsLen[nodeID]);
	for (uint32_t i = 0; i < step->globalTaskIdsLen[nodeID]; i++) {
	    step->errChannels[i] = true;
	}
	addUint32ToMsg(step->globalTaskIdsLen[nodeID], &data);
    }

    /* full signature is now the I/O key */
    addStringToMsg(sig, &data);
    /* update length *without* the length itself */
    *(uint32_t *) data.buf = htonl(data.bufUsed - sizeof(uint32_t));

    PSCio_sendP(sock, data.buf, data.bufUsed);

    return sock;
}

void srunEnableIO(Step_t *step)
{
    static bool enabled = false;
    if (enabled) return;

    if (step->stdInRank != -1) {
	/* close stdin for all other ranks */
	uint32_t myTaskIdsLen = step->globalTaskIdsLen[step->localNodeId];
	uint32_t *myTaskIds = step->globalTaskIds[step->localNodeId];

	for (uint32_t i = 0; i < myTaskIdsLen; i++) {
	    if ((uint32_t) step->stdInRank == myTaskIds[i]) continue;
	    forwardInputMsg(step, myTaskIds[i], NULL, 0);
	}
    }

    if (Selector_register(step->srunIOMsg.sock, handleSrunIOMsg, step) == -1) {
	flog("Selector_register(%i) srun I/O socket failed\n",
	     step->srunIOMsg.sock);
    }
    enabled = true;
}

int srunSendIO(uint16_t type, uint16_t grank, Step_t *step, char *buf,
		uint32_t bufLen)
{
    int ret, error = 0;
    IO_Slurm_Header_t ioh = {
	.type = type,
	.grank = grank,
	.lrank = (uint16_t)NO_VAL,
	.len = bufLen
    };

    ret = srunSendIOEx(step->srunIOMsg.sock, &ioh, buf, &error);

    if (ret < 0) {
	switch (error) {
	    case ECONNRESET:
	    case EPIPE:
	    case EBADF:
		fwCMD_brokeIOcon(step);
		clearSlurmMsg(&step->srunIOMsg);
		break;
	}
    }
    errno = error;
    return ret;
}

int srunSendIOEx(int sock, IO_Slurm_Header_t *iohead, char *buf, int *error)
{
    PS_SendDB_t data = { .bufUsed = 0, .useFrag = false };
    bool once = true;
    int32_t towrite, written = 0;
    IO_Slurm_Header_t ioh;

    if (sock < 0) return -1;
    if (iohead->len && !buf) {
	flog("invalid buffer (null)\n");
	return -1;
    }

    if (iohead->len > 0) {
	fdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB,
	     "msg '%.*s'\n", iohead->len, buf);
    }

    towrite = iohead->len;
    errno = *error = 0;
    memcpy(&ioh, iohead, sizeof(IO_Slurm_Header_t));

    while (once || towrite > 0) {
	ioh.len = towrite;
	char *nextPos = buf + written;
	if (nextPos && towrite > SLURM_IO_MAX_LEN) {
	    /* find newline in the maximal accepted message length */
	    char *nl = memrchr(nextPos, '\n', SLURM_IO_MAX_LEN);
	    ioh.len = nl ? (nl +1) - (nextPos) : SLURM_IO_MAX_LEN;
	}
	packSlurmIOMsg(&data, &ioh, nextPos);
	ssize_t ret = PSCio_sendF(sock, data.buf, data.bufUsed);

	data.bufUsed = 0;
	once = false;

	if (ret < 0) {
	    *error = errno;
	    return -1;
	}
	if (!ret) continue;

	ret -= SLURM_IO_HEAD_SIZE;
	written += ret;
	towrite -= ret;
	fdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB, "fd %i ret %zi written %i"
	     " towrite %i type %i grank %i\n", sock, ret, written, towrite,
	     iohead->type, iohead->grank);
    }

    return written;
}

void closeAllStepConnections(Step_t *step)
{
    if (!step->srunIOMsg.head.forward) clearSlurmMsg(&step->srunIOMsg);
    if (!step->srunControlMsg.head.forward) clearSlurmMsg(&step->srunControlMsg);
    if (!step->srunPTYMsg.head.forward) clearSlurmMsg(&step->srunPTYMsg);

    /* close all remaining srun connections */
    list_t *c, *tmp;
    list_for_each_safe(c, tmp, &connectionList) {
	Connection_t *con = list_entry(c, Connection_t, next);
	if (con->step != step) continue;

	struct timeval time_now, time_diff;
	gettimeofday(&time_now, NULL);
	timersub(&time_now, &con->openTime, &time_diff);
	flog("warning: closing lingering %s connection with socket %i"
	     " open for %.4f seconds\n", Step_strID(step), con->sock,
	     time_diff.tv_sec + 1e-6 * time_diff.tv_usec);
	closeSlurmCon(con->sock);
    }
}

void handleBrokenConnection(PSnodes_ID_t nodeID)
{
    list_t *c, *tmp;
    list_for_each_safe(c, tmp, &connectionList) {
	Connection_t *con = list_entry(c, Connection_t, next);
	Msg_Forward_t *fw = &con->fw;

	for (uint32_t i = 0; i < fw->nodesCount; i++) {
	    if (fw->nodes[i] != nodeID) continue;

	    Slurm_Msg_t sMsg;
	    initSlurmMsg(&sMsg);
	    sMsg.source = PSC_getTID(nodeID, 0);
	    sMsg.head.type = RESPONSE_FORWARD_FAILED;
	    sMsg.sock = con->sock;
	    sMsg.recvTime = con->recvTime;
	    // sMsg.data not used in handleFrwrdMsgReply() and descendants

	    handleFrwrdMsgReply(&sMsg, SLURM_COMMUNICATIONS_CONNECTION_ERROR);
	    flog("message error for node %i saved\n", nodeID);

	    /* assuming nodes[] contains each node id only once */
	    break;
	}
    }
}

/**
 * @brief deprecated, tbr
 *
 * Support for deprecated ControlMachine, ControlAddr, BackupController,
 * BackupAddr.
 */
static bool resControllerIDs(void)
{
    /* resolve main controller */
    char *addr = getConfValueC(SlurmConfig, "ControlAddr");
    char *host = getConfValueC(SlurmConfig, "ControlMachine");

    char *name = (addr) ? addr : host;
    if (!name) mlog("%s: invalid ControlMachine\n", __func__);

    PSnodes_ID_t slurmCtl = getNodeIDbyHostname(name);
    if (slurmCtl == -1) {
	flog("unable to resolve main controller '%s'\n", name);
	return false;
    }

    ctlHosts[ctlHostsCount].host = ustrdup(host);
    ctlHosts[ctlHostsCount].addr = ustrdup(addr);
    ctlHosts[ctlHostsCount].id = slurmCtl;
    ctlHostsCount++;

    /* resolve backup controller */
    addr = getConfValueC(SlurmConfig, "BackupAddr");
    host = getConfValueC(SlurmConfig, "BackupController");

    name = (addr) ? addr : host;
    /* we may not have a backup controller configured */
    if (!name) return true;

    PSnodes_ID_t slurmBackupCtl = getNodeIDbyHostname(name);
    if (slurmBackupCtl == -1) {
	flog("unable to resolve backup controller '%s'\n", name);
	return false;
    }

    ctlHosts[ctlHostsCount].host = ustrdup(host);
    ctlHosts[ctlHostsCount].addr = ustrdup(addr);
    ctlHosts[ctlHostsCount].id = slurmBackupCtl;
    ctlHostsCount++;

    return true;
}

/**
 * @param Initialize the slurmctld host array
 *
 * Convert the slurmctld config (SlurmctldHost) to a host array
 * and resolve the corresponding PS node IDs.
 *
 * @return Returns true on success otherwise false is
 * returned.
 */
static bool initControlHosts()
{
    int numEntry = getConfValueI(Config, "SLURM_CTLHOST_ENTRY_COUNT");
    for (int i = 0; i < numEntry; i++) {
	char key[64];

	snprintf(key, sizeof(key), "SLURM_CTLHOST_ADDR_%i", i);
	char *addr = getConfValueC(Config, key);

	snprintf(key, sizeof(key), "SLURM_CTLHOST_ENTRY_%i", i);
	char *host = getConfValueC(Config, key);

	char *name = (addr) ? addr : host;
	PSnodes_ID_t id = getNodeIDbyHostname(name);
	if (id == -1) {
	    flog("unable to resolve controller(%i) '%s'\n", i, name);
	    return false;
	}
	ctlHosts[ctlHostsCount].host = ustrdup(host);
	ctlHosts[ctlHostsCount].addr = ustrdup(addr);
	ctlHosts[ctlHostsCount].id = id;
	ctlHostsCount++;
    }

    return true;
}

bool initSlurmCon(void)
{
    memset(ctlHosts, 0, sizeof(ctlHosts));

    /* resolve control hosts */
    if (!initControlHosts()) {
	flog("initialize of control hosts failed\n");
	return false;
    }

    if (!ctlHostsCount) {
	/* try deprecated method to resolve the control hosts */
	if (!resControllerIDs()) {
	    flog("initialize of control hosts failed\n");
	    return false;
	}
    }

    /* listening on slurmd port */
    int ctlPort = getConfValueI(SlurmConfig, "SlurmdPort");
    if (ctlPort < 0) ctlPort = PSSLURM_SLURMD_PORT;
    if (openSlurmdSocket(ctlPort) < 0) {
	flog("open slurmd socket failed");
	return false;
    }

    /* register to slurmctld */
    ctlPort = getConfValueI(SlurmConfig, "SlurmctldPort");
    if (ctlPort < 0) {
	addConfigEntry(SlurmConfig, "SlurmctldPort", PSSLURM_SLURMCTLD_PORT);
    }
    sendNodeRegStatus(true);

    return true;
}

bool Connection_traverse(ConnectionVisitor_t visitor, const void *info)
{
    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &connectionList) {
	Connection_t *conn = list_entry(s, Connection_t, next);

	if (visitor(conn, info)) return true;
    }

    return false;
}
