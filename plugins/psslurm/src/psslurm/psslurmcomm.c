/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <string.h>

#include "pscio.h"
#include "psserial.h"
#include "pluginconfig.h"
#include "pluginhelper.h"
#include "pluginmalloc.h"
#include "psidcomm.h"
#include "pslog.h"
#include "selector.h"

#include "slurmcommon.h"
#include "psslurmproto.h"
#include "psslurmconfig.h"
#include "psslurmjob.h"
#include "psslurmlog.h"
#include "psslurmauth.h"
#include "psslurmio.h"
#include "psslurmenv.h"
#include "psslurmpscomm.h"
#include "psslurmpack.h"
#include "psslurmfwcomm.h"

#include "psslurmcomm.h"

/** default slurmd port psslurm listens for new srun/slurmcltd requests */
#define PSSLURM_SLURMD_PORT 6818

/** maximal allowed length of a bit-string */
#define MAX_PACK_STR_LEN (16 * 1024 * 1024)

/** maximal number of parallel supported Slurm control daemons */
#define MAX_CTL_HOSTS 16

/** maximal length of a Slurm I/O message buffer */
#define SLURM_IO_MAX_LEN 1024

/** socket to listen for new Slurm connections */
static int slurmListenSocket = -1;

/** structure holding connection management data */
typedef struct {
    list_t next;	    /**< used to put into connection-list */
    PS_DataBuffer_t data;   /**< buffer for received message parts */
    Connection_CB_t *cb;    /**< function to handle received messages */
    void *info;		    /**< additional info passed to callback */
    int sock;		    /**< socket of the connection */
    time_t recvTime;	    /**< time first complete message was received */
    bool readSize;	    /**< true if the message size was read */
    Msg_Forward_t fw;	    /**< message forwarding structure */
} Connection_t;

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

/** list which holds all connections */
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
 * Free used memory and reset management data of a connection.
 *
 * @param socket The socket of the connection to reset
 *
 * @return Returns false if the connection could not be found
 * and true on success
 */
static bool resetConnection(int socket)
{
    Connection_t *con = findConnection(socket);

    if (!con) return false;

    ufree(con->data.buf);
    con->data.buf = NULL;
    con->data.size = 0;
    con->data.used = 0;
    con->readSize = false;

    return true;
}

/**
 * @brief Add a new connection
 *
 * @param socket The socket of the connection to add
 *
 * @param cb The function to call to handle received messages
 *
 * @param info Pointer to additional information passed to @a
 * cb
 *
 * @return Returns a pointer to the added connection
 */
static Connection_t *addConnection(int socket, Connection_CB_t *cb, void *info)
{
    Connection_t *con = findConnection(socket);

    if (con) {
	flog("socket(%i) already has a connection, resetting it\n", socket);
	resetConnection(socket);
	con->cb = cb;
	con->info = info;
	return con;
    }

    con = ucalloc(sizeof(*con));

    con->sock = socket;
    con->cb = cb;
    con->info = info;
    initSlurmMsgHead(&con->fw.head);

    list_add_tail(&con->next, &connectionList);

    return con;
}

void closeSlurmCon(int socket)
{
    Connection_t *con = findConnection(socket);

    mdbg(PSSLURM_LOG_COMM, "%s(%d)\n", __func__, socket);

    /* close the connection */
    if (Selector_isRegistered(socket)) Selector_remove(socket);
    close(socket);

    /* free memory */
    if (con) {
	list_del(&con->next);
	freeSlurmMsgHead(&con->fw.head);
	if (con->fw.nodesCount) ufree(con->fw.nodes);
	ufree(con->fw.body.buf);
	ufree(con->data.buf);
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
 * @return Returns false if the processing should be stopped
 * otherwise true is returned even if errors occurred.
 */
static bool saveFrwrdMsgReply(Slurm_Msg_t *sMsg, Msg_Forward_t *fw,
			      uint32_t error, const char *func, const int line)
{
    fw->numRes++;
    PSnodes_ID_t srcNode = PSC_getID(sMsg->source);
    if (srcNode == PSC_getMyID()) {
	/* save local processed message */
	fw->head.type = sMsg->head.type;
	if (!memToDataBuffer(sMsg->reply.buf, sMsg->reply.bufUsed, &fw->body)) {
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
		    if (!memToDataBuffer(sMsg->reply.buf,
					 sMsg->reply.bufUsed,
					 &fwRes->body)) {
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

    mdbg(PSSLURM_LOG_FWD, "%s(%s@%i): type %s forward %u resCount %u "
	    "source %s sock %i recvTime %zu\n", __func__, func, line,
	    msgType2String(sMsg->head.type), fw->head.forward, fw->numRes,
	    PSC_printTID(sMsg->source), sMsg->sock, sMsg->recvTime);

    /* test if all nodes replied and therefore the forward is complete */
    if (fw->numRes == fw->nodesCount + 1) {
	PS_SendDB_t msg = { .bufUsed = 0, .useFrag = false };

	/* all answers collected */
	fdbg(PSSLURM_LOG_FWD, "forward %s complete, sending answer\n",
	     msgType2String(sMsg->head.type));

	/* disable forwarding for the answer message */
	fw->head.forward = 0;

	if (!fw->body.buf || !fw->body.used) {
	    flog("invalid data, drop msg from caller %s@%i\n", func, line);
	    closeSlurmCon(con->sock);
	    return;
	}

	/* Answer the original RPC request from Slurm. This reply will hold
	 * results from all the nodes the RPC was forwarded to. */
	msg.buf = fw->body.buf;
	msg.bufUsed = fw->body.used;
	sendSlurmMsgEx(con->sock, &fw->head, &msg);
	closeSlurmCon(con->sock);
    }
}

/**
 * @brief Get remote address and port of a socket
 *
 * @param addr Pointer to save the remote address
 *
 * @param port Pointer to save the remote port
 *
 * Returns true on success and false on error
 */
static bool getSockInfo(int socket, uint32_t *addr, uint16_t *port)
{
    struct sockaddr_in sock_addr;
    socklen_t len = sizeof(sock_addr);

    if (getpeername(socket, (struct sockaddr*)&sock_addr, &len) == -1) {
	mwarn(errno, "%s: getpeername(%i)", __func__, socket);
	return false;
    }
    *addr = sock_addr.sin_addr.s_addr;
    *port = sock_addr.sin_port;

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
    PS_DataBuffer_t *dBuf = &con->data;
    int ret;
    bool error = false;

    if (!param) {
	flog("invalid connection data buffer\n");
	closeSlurmCon(sock);
	return 0;
    }

    if (dBuf->size && dBuf->size == dBuf->used) {
	flog("data buffer for sock %i already in use, resetting\n", sock);
	resetConnection(sock);
    }

    /* try to read the message size */
    if (!con->readSize) {
	if (!dBuf->size) {
	    dBuf->size = sizeof(uint32_t);
	    dBuf->buf = umalloc(dBuf->size);
	    dBuf->used = 0;
	}

	ret = PSCio_recvBufPProg(sock, dBuf->buf, dBuf->size, &dBuf->used);
	int eno = errno;

	if (ret < 0) {
	    if (eno == EAGAIN || eno == EINTR) {
		/* not all data arrived yet, lets try again later */
		fdbg(PSSLURM_LOG_COMM, "we try later for sock %u read %zu\n",
		     sock, dBuf->used);
		return 0;
	    }
	    /* read error */
	    mwarn(eno, "%s: PSCio_recvBufPProg(%d, toRead %zd, size %zd)",
		  __func__, sock, dBuf->size, dBuf->used);
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
	uint32_t msglen = ntohl(*(uint32_t *) dBuf->buf);
	fdbg(PSSLURM_LOG_COMM, "msg size read for %u ret %u msglen %u\n",
	     sock, ret, msglen);

	if (msglen > MAX_MSG_SIZE) {
	    flog("msg too big %u (max %u)\n", msglen, MAX_MSG_SIZE);
	    error = true;
	    goto CALLBACK;
	}

	dBuf->size = msglen;
	dBuf->buf = urealloc(dBuf->buf, dBuf->size);
	dBuf->used = 0;   /* drop the length read so far */
	con->readSize = true;
    }

    /* try to read the actual payload (missing data) */
    ret = PSCio_recvBufPProg(sock, dBuf->buf, dBuf->size, &dBuf->used);
    int eno = errno;

    if (ret < 0) {
	if (eno == EAGAIN || eno == EINTR) {
	    /* not all data arrived yet, lets try again later */
	    fdbg(PSSLURM_LOG_COMM, "we try later for sock %u read %zu\n",
		 sock, dBuf->used);
	    return 0;
	}
	/* read error */
	mwarn(eno, "%s: PSCio_recvBufPProg(%d, toRead %zd, size %zd)",
	      __func__, sock, dBuf->size, dBuf->used);
	error = true;
	goto CALLBACK;

    } else if (!ret) {
	/* connection reset */
	flog("connection reset on sock %i\n", sock);
	error = true;
	goto CALLBACK;
    }

    /* all data read successful */
    fdbg(PSSLURM_LOG_COMM, "all data read for %u ret %u toread %zu\n",
	 sock, ret, dBuf->size);

CALLBACK:

    if (!error) {
	Slurm_Msg_t sMsg;

	initSlurmMsg(&sMsg);
	sMsg.sock = sock;
	sMsg.data = dBuf;
	sMsg.ptr = sMsg.data->buf;
	sMsg.recvTime = con->recvTime = time(NULL);

	/* overwrite empty addr informations */
	getSockInfo(sock, &sMsg.head.addr, &sMsg.head.port);

	processSlurmMsg(&sMsg, &con->fw, con->cb, con->info);
    }
    resetConnection(sock);

    if (error) {
	fdbg(ret ? -1 : PSSLURM_LOG_COMM, "closeSlurmCon(%d)\n", sock);
	closeSlurmCon(sock);
    }

    return 0;
}

bool registerSlurmSocket(int sock, Connection_CB_t *cb, void *info)
{
    Connection_t *con = addConnection(sock, cb, info);

    PSCio_setFDblock(sock, false);
    if (Selector_register(sock, readSlurmMsg, con) == -1) {
	flog("register socket %i failed\n", sock);
	return false;
    }
    return true;
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
    }

    static char buf[64];
    snprintf(buf, sizeof(buf), "unkown %u", rc);

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
    char **ptr = &sMsg->ptr;
    uint32_t rc;
    getUint32(ptr, &rc);

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
    ufree(info);
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

    return sock;
}

int openSlurmctldCon(void *info)
{
    return openSlurmctldConEx(handleSlurmctldReply, info);
}

int openSlurmctldConEx(Connection_CB_t *cb, void *info)
{
    char *port = getConfValueC(&SlurmConfig, "SlurmctldPort");

    int sock = -1;
    for (int i = 0; i < ctlHostsCount; i++) {
	char *addr = (ctlHosts[i].addr) ? ctlHosts[i].addr : ctlHosts[i].host;

	sock = tcpConnect(addr, port);
	if (sock > -1) {
	    fdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB,
		 "connected to %s socket %i\n", addr, sock);
	    break;
	}
    }

    if (sock < 0) {
	flog("connecting to %i configured slurmctld failed\n", ctlHostsCount);
	return sock;
    }

    registerSlurmSocket(sock, cb, info);

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
		   void *info, const char *caller, const int line)
{
    Slurm_Msg_Header_t head;

    initSlurmMsgHead(&head);
    head.type = type;

    return __sendSlurmMsgEx(sock, &head, body, NULL, caller, line);
}

int __sendSlurmMsgEx(int sock, Slurm_Msg_Header_t *head, PS_SendDB_t *body,
		     void *info, const char *caller, const int line)
{
    PS_SendDB_t data = { .bufUsed = 0, .useFrag = false };
    PS_DataBuffer_t payload = { .buf = NULL };
    int ret = 0;
    size_t written = 0;

    if (!head) {
	flog("invalid header from %s@%i\n", caller, line);
	return -1;
    }

    if (!body) {
	flog("invalid body from %s@%i\n", caller, line);
	return -1;
    }

    Slurm_Auth_t *auth = getSlurmAuth();
    if (!auth) {
	flog("getting a slurm authentication token failed\n");
	return -1;
    }

    fdbg(PSSLURM_LOG_PROTO, "msg(%i): %s, version %u\n",
	 head->type, msgType2String(head->type), head->version);

    /* connect to slurmctld */
    if (sock < 0) {
	sock = openSlurmctldCon(info);
	if (sock < 0) {
	    freeSlurmAuth(auth);
	    if (needMsgResend(head->type)) {
		Slurm_Msg_Buf_t *savedMsg;

		savedMsg = saveSlurmMsg(head, body, NULL, -1, 0);
		if (setReconTimer(savedMsg) == -1) {
		    flog("setting resend timer failed\n");
		    deleteMsgBuf(savedMsg);
		    return -1;
		}
		return -2;
	    }
	    return -1;
	}
    }

    /* non blocking write */
    PSCio_setFDblock(sock, false);

    memToDataBuffer(body->buf, body->bufUsed, &payload);
    packSlurmMsg(&data, head, &payload, auth);

    ret = __sendDataBuffer(sock, &data, 0, &written, caller, line);
    if (ret == -1) {
	if (!written) {
	    flog("sending msg type %s failed\n", msgType2String(head->type));
	} else {
	    /* msg was fractionally written, retry later */
	    Slurm_Msg_Buf_t *savedMsg;

	    savedMsg = saveSlurmMsg(head, body, auth, sock, written);
	    Selector_awaitWrite(sock, resendSlurmMsg, savedMsg);
	    ret = -2;
	}
    }

    freeSlurmAuth(auth);
    ufree(payload.buf);

    return ret;
}

int __sendSlurmReq(Req_Info_t *req, void *data,
		   const char *caller, const int line)
{
    int ret = -1;

    PS_SendDB_t msg = { .bufUsed = 0, .useFrag = false };
    if (!packSlurmReq(req, &msg, data, caller, line)) {
	flog("packing request %s for %s:%i failed\n", msgType2String(req->type),
	     caller, line);
	goto FINALIZE;
    }

    int sock = openSlurmctldCon(req);
    if (sock < 0) {
	flog("open connection to slurmctld failed\n");
	goto FINALIZE;
    }

    req->time = time(NULL);
    ret = __sendSlurmMsg(sock, req->type, &msg, req, caller, line);

FINALIZE:
    if (ret == -1) ufree(req);
    return ret;
}

char *__getBitString(char **ptr, const char *func, const int line)
{
    uint32_t len;
    char *bitStr = NULL;

    getUint32(ptr, &len);

    if (len == NO_VAL) return NULL;

    getUint32(ptr, &len);

    if (len > MAX_PACK_STR_LEN) {
	mlog("%s(%s:%i): invalid str len %i\n", __func__, func, line, len);
	return NULL;
    }

    if (len > 0) {
	bitStr = umalloc(len);
	memcpy(bitStr, *ptr, len);
	*ptr += len;
    }
    return bitStr;
}

static void addVal2List(StrBuffer_t *strBuf, int32_t val, bool range, bool fin,
			hexBitStrConv_func_t *conv)
{
    char tmp[128];
    static int32_t lastVal, rangeVal;

    if (fin) {
	/* add end range of compacted list */
	if (range && rangeVal != -1) {
	    snprintf(tmp, sizeof(tmp), "-%i", rangeVal);
	    addStrBuf(tmp, strBuf);
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
	if (!strBuf->buf) lastVal = rangeVal = -1;

	if (lastVal != -1 && val == lastVal+1) {
	    rangeVal = val;
	} else {
	    if (rangeVal != -1) {
		snprintf(tmp, sizeof(tmp), "-%i", rangeVal);
		addStrBuf(tmp, strBuf);
		rangeVal = -1;
	    }
	    if (strBuf->buf) addStrBuf(",", strBuf);
	    snprintf(tmp, sizeof(tmp), "%i", val);
	    addStrBuf(tmp, strBuf);
	}
	lastVal = val;
    } else {
	/* comma separated list only */
	if (strBuf->buf) addStrBuf(",", strBuf);
	snprintf(tmp, sizeof(tmp), "%i", val);
	addStrBuf(tmp, strBuf);
    }
}

bool hexBitstr2List(char *bitstr, StrBuffer_t *strBuf, bool range)
{
    return hexBitstr2ListEx(bitstr, strBuf, range, NULL);
}

bool hexBitstr2ListEx(char *bitstr, StrBuffer_t *strBuf, bool range,
		      hexBitStrConv_func_t *conv)
{
    strBuf->buf = NULL;
    if (!bitstr) {
	flog("invalid bitstring\n");
	return false;
    }

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
	    if (next & i) addVal2List(strBuf, count, range, false, conv);
	    count++;
	}
    }

    if (range) addVal2List(strBuf, 0, range, true, conv);
    addStrBuf("", strBuf);

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
    struct sockaddr_in SAddr;
    socklen_t clientlen = sizeof(SAddr);
    int newSock = accept(socket, (struct sockaddr *)&SAddr, &clientlen);

    if (newSock == -1) {
	mwarn(errno, "%s: error accepting new tcp connection", __func__);
	return 0;
    }

    fdbg(PSSLURM_LOG_COMM, "from %s:%u socket:%i\n", inet_ntoa(SAddr.sin_addr),
	 ntohs(SAddr.sin_port), newSock);

    registerSlurmSocket(newSock, handleSlurmdMsg, NULL);

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
    struct sockaddr_in saClient;
    int res, sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int opt = 1;

    if (!sock) {
	mwarn(errno, "%s: socket failed for port %i", __func__, port);
	return -1;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0 ) {
	mwarn(errno, "%s: setsockopt(%i, port %i)", __func__, sock, port);
    }

    /* set up the sockaddr structure */
    saClient.sin_family = AF_INET;
    saClient.sin_addr.s_addr = INADDR_ANY;
    saClient.sin_port = htons(port);
    bzero(&(saClient.sin_zero), 8);

    /* bind the socket */
    res = bind(sock, (struct sockaddr *)&saClient, sizeof(saClient));
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

    fdbg(PSSLURM_LOG_COMM, "got pty message for %s\n", strStepID(step));

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
    char *ptr = buf;
    size_t c = bufLen;
    PS_Tasks_t *task = findTaskByRank(&step->tasks, rank);
    PSLog_Msg_t msg = {
	.header = {
	    .type = PSP_CC_MSG,
	    .dest = task ? task->forwarderTID : -1,
	    .sender = PSC_getMyTID(),
	    .len = PSLog_headerSize },
       .version = 2,
       .type = STDIN,
       .sender = -1};

    if (!task) {
	flog("task for rank %u of %s local ID %i not found\n", rank,
	     strStepID(step), step->localNodeId);
	return -1;
    }

    do {
	int n = (c > sizeof(msg.buf)) ? sizeof(msg.buf) : c;
	if (n) memcpy(msg.buf, ptr, n);
	fdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB,
	     "rank %u len %u msg.header.len %zu to %s\n",
	     rank, n, PSLog_headerSize + n, PSC_printTID(task->forwarderTID));
	fdbg(PSSLURM_LOG_IO_VERB, "'%.*s'\n", n, msg.buf);
	msg.header.len = PSLog_headerSize + n;
	sendMsgToMother(&msg);
	c -= n;
	ptr += n;
    } while (c > 0);

    return bufLen;
}

int handleSrunMsg(int sock, void *data)
{
    IO_Slurm_Header_t *ioh = NULL;

    Step_t *step = data;
    if (!verifyStepPtr(step)) {
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
	     strStepID(step), rcvd);
	goto ERROR;
    }

    char *ptr = buffer;
    if (!unpackSlurmIOHeader(&ptr, &ioh)) {
	flog("unpack Slurm I/O header for %s failed\n", strStepID(step));
	goto ERROR;
    }

    if (!step->fwdata) {
	if (ioh->len) {
	    fdbg(PSSLURM_LOG_IO, "no forwarder for %s I/O type %s len %u\n",
		 strStepID(step), IO_strType(ioh->type), ioh->len);
	}
	goto ERROR;
    }
    bool pty = step->taskFlags & LAUNCH_PTY;
    int fd = pty ? step->fwdata->stdOut[1] : step->fwdata->stdIn[1];
    uint32_t myTaskIdsLen = step->globalTaskIdsLen[step->localNodeId];
    uint32_t *myTaskIds = step->globalTaskIds[step->localNodeId];

    fdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB,
	 "%s stdin %d type %s length %u grank %u lrank %u pty %u"
	 " myTIDsLen %u\n", strStepID(step), fd, IO_strType(ioh->type),
	 ioh->len, ioh->grank, ioh->lrank, pty, myTaskIdsLen);

    if (ioh->type == SLURM_IO_CONNECTION_TEST) {
	if (ioh->len != 0) {
	    flog("invalid connection test for %s len %u\n",
		 strStepID(step), ioh->len);
	}
	srunSendIO(SLURM_IO_CONNECTION_TEST, 0, step, NULL, 0);
    } else if (!ioh->len) {
	/* forward eof to all forwarders */
	flog("got eof of stdin %d for %s\n", fd, strStepID(step));

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

    fdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB, "new srun conn %i\n", sock);

    return sock;
}

int srunSendMsg(int sock, Step_t *step, slurm_msg_type_t type,
		PS_SendDB_t *body)
{
    if (sock < 0) {
	sock = srunOpenControlConnection(step);
	if (sock < 0) return -1;
    }

    if (Selector_register(sock, handleSrunMsg, step) == -1) {
	flog("Selector_register(%i) failed\n", sock);
	return -1;
    }

    fdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB,
	 "sock %u, len: body.bufUsed %u\n", sock, body->bufUsed);

    return sendSlurmMsg(sock, type, body);
}

int srunOpenPTYConnection(Step_t *step)
{
    char *port = envGet(&step->env, "SLURM_PTY_PORT");
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
    PS_SendDB_t data = { .bufUsed = 0, .useFrag = false };
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

    addUint16ToMsg(IO_PROTOCOL_VERSION, &data);
    /* nodeid */
    addUint32ToMsg(nodeID, &data);

    /* stdout obj count */
    if (step->stdOutOpt == IO_SRUN || step->stdOutOpt == IO_SRUN_RANK ||
	step->taskFlags & LAUNCH_PTY) {
	step->outChannels =
		umalloc(sizeof(int32_t) * step->globalTaskIdsLen[nodeID]);
	for (uint32_t i = 0; i < step->globalTaskIdsLen[nodeID]; i++) {
	    step->outChannels[i] = 1;
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
	step->errChannels =
		umalloc(sizeof(int32_t) * step->globalTaskIdsLen[nodeID]);
	for (uint32_t i = 0; i < step->globalTaskIdsLen[nodeID]; i++) {
	    step->errChannels[i] = 1;
	}
	addUint32ToMsg(step->globalTaskIdsLen[nodeID], &data);
    }

    /* io key */
    addUint32ToMsg((uint32_t) SLURM_IO_KEY_SIZE, &data);
    addMemToMsg(sig, (uint32_t) SLURM_IO_KEY_SIZE, &data);

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

    if (Selector_register(step->srunIOMsg.sock, handleSrunMsg, step) == -1) {
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
		freeSlurmMsg(&step->srunIOMsg);
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

    if (iohead->len > 0) {
	fdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB,
	     "msg '%.*s'\n", iohead->len, buf);
    }

    towrite = iohead->len;
    errno = *error = 0;
    memcpy(&ioh, iohead, sizeof(IO_Slurm_Header_t));

    while (once || towrite > 0) {
	ioh.len = towrite;
	if (towrite > SLURM_IO_MAX_LEN) {
	    /* find newline in the maximal accepted message length */
	    char *nl = memrchr(buf + written, '\n', SLURM_IO_MAX_LEN);
	    ioh.len = nl ? (nl +1) - (buf + written) : SLURM_IO_MAX_LEN;
	}
	packSlurmIOMsg(&data, &ioh, buf + written);
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
    if (!step->srunIOMsg.head.forward) {
	freeSlurmMsg(&step->srunIOMsg);
    }
    if (!step->srunControlMsg.head.forward) {
	freeSlurmMsg(&step->srunControlMsg);
    }
    if (!step->srunPTYMsg.head.forward) {
	freeSlurmMsg(&step->srunPTYMsg);
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
	    PS_DataBuffer_t data = { .buf = NULL };

	    initSlurmMsg(&sMsg);
	    sMsg.source = PSC_getTID(nodeID, 0);
	    sMsg.head.type = RESPONSE_FORWARD_FAILED;
	    sMsg.sock = con->sock;
	    sMsg.recvTime = con->recvTime;
	    sMsg.data = &data;

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
    char *addr = getConfValueC(&SlurmConfig, "ControlAddr");
    char *host = getConfValueC(&SlurmConfig, "ControlMachine");

    char *name = (addr) ? addr : host;
    if (!name) mlog("%s: invalid ControlMachine\n", __func__);

    PSnodes_ID_t slurmCtl = getNodeIDbyName(name);
    if (slurmCtl == -1) {
	flog("unable to resolve main controller '%s'\n", name);
	return false;
    }

    ctlHosts[ctlHostsCount].host = ustrdup(host);
    ctlHosts[ctlHostsCount].addr = ustrdup(addr);
    ctlHosts[ctlHostsCount].id = slurmCtl;
    ctlHostsCount++;

    /* resolve backup controller */
    addr = getConfValueC(&SlurmConfig, "BackupAddr");
    host = getConfValueC(&SlurmConfig, "BackupController");

    name = (addr) ? addr : host;
    /* we may not have a backup controller configured */
    if (!name) return true;

    PSnodes_ID_t slurmBackupCtl = getNodeIDbyName(name);
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
    int numEntry = getConfValueI(&Config, "SLURM_CTLHOST_ENTRY_COUNT");
    for (int i = 0; i < numEntry; i++) {
	char key[64];

	snprintf(key, sizeof(key), "SLURM_CTLHOST_ADDR_%i", i);
	char *addr = getConfValueC(&Config, key);

	snprintf(key, sizeof(key), "SLURM_CTLHOST_ENTRY_%i", i);
	char *host = getConfValueC(&Config, key);

	char *name = (addr) ? addr : host;
	PSnodes_ID_t id = getNodeIDbyName(name);
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
    int ctlPort = getConfValueI(&SlurmConfig, "SlurmdPort");
    if (ctlPort < 0) ctlPort = PSSLURM_SLURMD_PORT;
    if (openSlurmdSocket(ctlPort) < 0) {
	flog("open slurmd socket failed");
	return false;
    }

    /* register to slurmctld */
    ctlPort = getConfValueI(&SlurmConfig, "SlurmctldPort");
    if (ctlPort < 0) {
	addConfigEntry(&SlurmConfig, "SlurmctldPort", PSSLURM_SLURMCTLD_PORT);
    }
    sendNodeRegStatus(true);

    return true;
}
