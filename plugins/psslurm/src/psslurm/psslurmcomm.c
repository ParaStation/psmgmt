/*
 * ParaStation
 *
 * Copyright (C) 2014-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

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

#include "plugincomm.h"
#include "pluginconfig.h"
#include "pluginhelper.h"
#include "pluginmalloc.h"
#include "selector.h"
#include "pslog.h"
#include "psidcomm.h"

#include "psslurmcomm.h"

#define MAX_PACK_STR_LEN (16 * 1024 * 1024)

static int slurmListenSocket = -1;

const char *msgType2String(int type)
{
    switch(type) {
	case REQUEST_BATCH_JOB_LAUNCH:
	    return "REQUEST_BATCH_JOB_LAUNCH";
	case REQUEST_LAUNCH_TASKS:
	    return "REQUEST_LAUNCH_TASKS";
	case REQUEST_SIGNAL_TASKS:
	    return "REQUEST_SIGNAL_TASKS";
	case REQUEST_CHECKPOINT_TASKS:
	    return "REQUEST_CHECKPOINT_TASKS";
	case REQUEST_TERMINATE_TASKS:
	    return "REQUEST_TERMINATE_TASKS";
	case REQUEST_KILL_PREEMPTED:
	    return "REQUEST_KILL_PREEMPTED";
	case REQUEST_KILL_TIMELIMIT:
	    return "REQUEST_KILL_TIMELIMIT";
	case REQUEST_REATTACH_TASKS:
	    return "REQUEST_REATTACH_TASKS";
	case REQUEST_SIGNAL_JOB:
	    return "REQUEST_SIGNAL_JOB";
	case REQUEST_SUSPEND:
	    return "REQUEST_SUSPEND";
	case REQUEST_SUSPEND_INT:
	    return "REQUEST_SUSPEND_INT";
	case REQUEST_ABORT_JOB:
	    return "REQUEST_ABORT_JOB";
	case REQUEST_TERMINATE_JOB:
	    return "REQUEST_TERMINATE_JOB";
	case REQUEST_COMPLETE_BATCH_SCRIPT:
	    return "REQUEST_COMPLETE_BATCH_SCRIPT";
	case REQUEST_UPDATE_JOB_TIME:
	    return "REQUEST_UPDATE_JOB_TIME";
	case REQUEST_SHUTDOWN:
	    return "REQUEST_SHUTDOWN";
	case REQUEST_RECONFIGURE:
	    return "REQUEST_RECONFIGURE";
	case REQUEST_REBOOT_NODES:
	    return "REQUEST_REBOOT_NODES";
	case REQUEST_NODE_REGISTRATION_STATUS:
	    return "REQUEST_NODE_REGISTRATION_STATUS";
	case REQUEST_PING:
	    return "REQUEST_PING";
	case REQUEST_HEALTH_CHECK:
	    return "REQUEST_HEALTH_CHECK";
	case REQUEST_ACCT_GATHER_UPDATE:
	    return "REQUEST_ACCT_GATHER_UPDATE";
	case REQUEST_ACCT_GATHER_ENERGY:
	    return "REQUEST_ACCT_GATHER_ENERGY";
	case REQUEST_JOB_ID:
	    return "REQUEST_JOB_ID";
	case REQUEST_FILE_BCAST:
	    return "REQUEST_FILE_BCAST";
	case REQUEST_STEP_COMPLETE:
	    return "REQUEST_STEP_COMPLETE";
	case REQUEST_JOB_STEP_STAT:
	    return "REQUEST_JOB_STEP_STAT";
	case REQUEST_JOB_STEP_PIDS:
	    return "REQUEST_JOB_STEP_PIDS";
	case REQUEST_DAEMON_STATUS:
	    return "REQUEST_DAEMON_STATUS";
	case REQUEST_JOB_NOTIFY:
	    return "REQUEST_JOB_NOTIFY";
	case REQUEST_FORWARD_DATA:
	    return "REQUEST_FORWARD_DATA";
	case REQUEST_LAUNCH_PROLOG:
	    return "REQUEST_LAUNCH_PROLOG";
	case REQUEST_COMPLETE_PROLOG:
	    return "REQUEST_COMPLETE_PROLOG";
	case RESPONSE_PING_SLURMD:
	    return "RESPONSE_PING_SLURMD";
	case RESPONSE_SLURM_RC:
	    return "RESPONSE_SLURM_RC";
	case RESPONSE_ACCT_GATHER_UPDATE:
	    return "RESPONSE_ACCT_GATHER_UPDATE";
	case RESPONSE_ACCT_GATHER_ENERGY:
	    return "RESPONSE_ACCT_GATHER_ENERGY";
	case RESPONSE_JOB_ID:
	    return "RESPONSE_JOB_ID";
	case RESPONSE_JOB_STEP_STAT:
	    return "RESPONSE_JOB_STEP_STAT";
	case RESPONSE_JOB_STEP_PIDS:
	    return "RESPONSE_JOB_STEP_PIDS";
	case RESPONSE_LAUNCH_TASKS:
	    return "RESPONSE_LAUNCH_TASKS";
	case RESPONSE_FORWARD_FAILED:
	    return "RESPONSE_FORWARD_FAILED";
    }
    return "unknown";
}

typedef struct {
    list_t next;
    PS_DataBuffer_t data;
    Connection_CB_t *cb;
    int error;
    int sock;
    time_t recvTime;
    Connection_Forward_t fw;
} Connection_t;

/* list which holds all connections */
static LIST_HEAD(connectionList);

static Connection_t *findConnection(int socket)
{
    list_t *c;

    list_for_each(c, &connectionList) {
	Connection_t *con = list_entry(c, Connection_t, next);
	if (con->sock == socket) return con;
    }

    return NULL;
}

static Connection_t *findConnectionEx(int socket, time_t recvTime)
{
    list_t *c;

    list_for_each(c, &connectionList) {
	Connection_t *con = list_entry(c, Connection_t, next);
	if (con->sock == socket && con->recvTime == recvTime) return con;
    }

    return NULL;
}

static int resetConnection(int socket)
{
    Connection_t *con = findConnection(socket);

    if (!con) return 0;

    ufree(con->data.buf);
    con->data.buf = NULL;
    con->data.bufSize = 0;
    con->data.bufUsed = 0;
    con->error = 0;

    return 1;
}

static Connection_t *addConnection(int socket, Connection_CB_t *cb)
{
    Connection_t *con = findConnection(socket);

    if (con) {
	mlog("%s: socket(%i) already has a connection, resetting it\n",
		__func__, socket);
	resetConnection(socket);
	con->cb = cb;
	return con;
    }

    con = ucalloc(sizeof(*con));

    con->sock = socket;
    con->cb = cb;
    initSlurmMsgHead(&con->fw.head);

    list_add_tail(&con->next, &connectionList);

    return con;
}

static void closeConnection(int socket)
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

void clearConnections(void)
{
    list_t *c, *tmp;

    list_for_each_safe(c, tmp, &connectionList) {
	Connection_t *con = list_entry(c, Connection_t, next);
	closeConnection(con->sock);
    }
}

void initSlurmMsg(Slurm_Msg_t *sMsg)
{
    memset(sMsg, 0, sizeof(Slurm_Msg_t));
    sMsg->sock = -1;
    sMsg->source = -1;
    initSlurmMsgHead(&sMsg->head);
}

void freeSlurmMsg(Slurm_Msg_t *sMsg)
{
    if (sMsg->source == -1) {
	/* local connection */
	if (sMsg->sock != -1) {
	    closeConnection(sMsg->sock);
	    sMsg->sock = -1;
	}
    }

    initSlurmMsg(sMsg);
}

void __saveFrwrdMsgRes(Slurm_Msg_t *sMsg, uint32_t error, const char *func,
		       const int line)
{
    Connection_t *con;
    Connection_Forward_t *fw;
    Slurm_Forward_Data_t *fwdata;
    uint16_t i, saved = 0;
    PSnodes_ID_t srcNode;

    if (!sMsg || !sMsg->data) {
	mlog("%s: invalid %s from %s at %i\n", __func__,
		(!sMsg ? "sMsg" : "data"), func, line);
	return;
    }

    con = findConnectionEx(sMsg->sock, sMsg->recvTime);
    if (!con) {
	mlog("%s: no connection to %s socket %i recvTime %zu type %s caller %s"
	     " at %i\n", __func__, PSC_printTID(sMsg->source), sMsg->sock,
	     sMsg->recvTime, msgType2String(sMsg->head.type), func, line);
	return;
    }

    fw = &con->fw;
    fw->res++;
    srcNode = PSC_getID(sMsg->source);

    if (srcNode == PSC_getMyID()) {
	/* save local processed message */
	fw->head.type = sMsg->head.type;
	if (!addMemToMsg(sMsg->data->buf, sMsg->data->bufUsed, &fw->body)) {
	    mlog("%s: error saving local result, caller %s at %i\n",
		 __func__, func, line);
	}
    } else {
	/* save message from other node */
	for (i=0; i<fw->head.forward; i++) {
	    fwdata = &fw->head.fwdata[i];
	    if (fwdata->node == srcNode) {
		mlog("%s: result for node %i already saved, caller %s "
		     "line %i\n", __func__, srcNode, func, line);
		fw->res--;
		return;
	    }
	    if (fwdata->node == -1) {
		fwdata->error = error;
		fwdata->type = sMsg->head.type;
		fwdata->node = srcNode;
		if (sMsg->data->bufUsed) {
		    if (!addMemToMsg(sMsg->data->buf, sMsg->data->bufUsed,
				     &fwdata->body)) {
			mlog("%s: saving error failed, caller %s at %i\n",
			     __func__, func, line);
		    }
		}
		fw->head.returnList++;
		saved = 1;
		break;
	    }
	}
	if (!saved) {
	    mlog("%s: error saving result for src %s, caller %s at %i\n",
		 __func__, PSC_printTID(sMsg->source), func, line);
	}
    }

    mdbg(PSSLURM_LOG_FWD, "%s(%s:%i): type %s forward %u resCount %u "
	    "source %s sock %i recvTime %zu\n", __func__, func, line,
	    msgType2String(sMsg->head.type), fw->head.forward, fw->res,
	    PSC_printTID(sMsg->source), sMsg->sock, sMsg->recvTime);

    if (fw->res == fw->nodesCount + 1) {
	/* all answers collected */
	mdbg(PSSLURM_LOG_FWD, "%s: forward %s complete, sending answer\n",
		__func__, msgType2String(sMsg->head.type));

	fw->head.forward = 0;

	if (!fw->body.buf || !fw->body.bufUsed) {
	    mlog("%s: invalid local data, dropping msg from caller %s at %i\n",
		 __func__, func, line);
	    closeConnection(con->sock);
	    return;
	}
	sendSlurmMsgEx(con->sock, &fw->head, &fw->body);
	closeConnection(con->sock);
    }
}

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

static int readSlurmMsg(int sock, void *param)
{
    Connection_t *con = param;
    PS_DataBuffer_t *dBuf = &con->data;
    uint32_t msglen = 0;
    int ret;
    bool error = false;
    size_t size = 0, toRead;
    char *ptr;

    if (!param) {
	mlog("%s: invalid connection data buffer\n", __func__);
	closeConnection(sock);
	return 0;
    }

    if (dBuf->bufSize && dBuf->bufSize == dBuf->bufUsed) {
	mlog("%s: data buffer for sock %i already in use, resetting\n",
	     __func__, sock);
	resetConnection(sock);
    }

    if (!dBuf->bufSize) {
	/* new request, we need to read the size first */
	/* Shall be safe to do first a blocking read() inside a selector */
	ret = doRead(sock, &msglen, sizeof(msglen));
	if (ret != sizeof(msglen)) {
	    if (ret < 0) {
		mwarn(errno, "%s: doRead(%d)", __func__, sock);
	    } else if (!ret) {
		mdbg(PSSLURM_LOG_COMM, "%s: closing connection, empty message "
		     "len on sock %i\n", __func__, sock);
	    } else {
		mlog("%s: invalid message len %u, expect %zu\n", __func__, ret,
		     sizeof(msglen));
	    }
	    error = true;
	    goto CALLBACK;
	}

	msglen = ntohl(msglen);
	if (msglen > MAX_MSG_SIZE) {
	    mlog("%s: msg too big %u (max %u)\n", __func__, msglen,
		 MAX_MSG_SIZE);
	    error = true;
	    goto CALLBACK;
	}

	dBuf->buf = umalloc(msglen);
	dBuf->bufSize = msglen;
	dBuf->bufUsed = 0;
    }

    /* try to read the missing data */
    ptr = dBuf->buf + dBuf->bufUsed;
    toRead = dBuf->bufSize - dBuf->bufUsed;
    ret = doReadExtP(sock, ptr, toRead, &size);

    if (ret < 0) {
	if (size > 0 || errno == EAGAIN || errno == EINTR) {
	    /* not all data arrived yet, lets try again later */
	    dBuf->bufUsed += size;
	    mdbg(PSSLURM_LOG_COMM, "%s: we try later for sock %u read %zu\n",
		 __func__, sock, size);
	    return 0;
	}
	/* read error */
	mwarn(errno, "%s: doReadExtP(%d, toRead %zd, size %zd)", __func__, sock,
	      toRead, size);
	error = true;
	goto CALLBACK;

    } else if (!ret) {
	/* connection reset */
	mlog("%s: connection reset on sock %i\n", __func__, sock);
	error = true;
	goto CALLBACK;
    } else {
	/* all data red successful */
	dBuf->bufUsed += size;
	mdbg(PSSLURM_LOG_COMM,
	     "%s: all data read for %u ret %u toread %zu msglen %u size %zu\n",
	     __func__, sock, ret, toRead, msglen, size);
	goto CALLBACK;
    }

    mdbg(PSSLURM_LOG_COMM, "%s: Never be here!\n", __func__);
    return 0;

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

	/* extract slurm message header */
	getSlurmMsgHeader(&sMsg, &con->fw);

	/* forward the message */
	if (sMsg.head.forward > 0) {
	    forwardSlurmMsg(&sMsg, &con->fw);
	}

	/* let the callback handle the message */
	con->cb(&sMsg);
    }
    resetConnection(sock);

    if (error) {
	mdbg(ret ? -1 : PSSLURM_LOG_COMM, "%s: closeConnection(%d)\n",
	     __func__, sock);
	closeConnection(sock);
    }

    return 0;
}

Slurm_Msg_t * dupSlurmMsg(Slurm_Msg_t *sMsg)
{
    Slurm_Msg_t *dupMsg = umalloc(sizeof(*dupMsg));

    if (!dupMsg) return NULL;

    initSlurmMsg(dupMsg);
    dupMsg->sock = sMsg->sock;
    dupMsg->data = dupDataBuffer(sMsg->data);
    dupMsg->ptr = dupMsg->data->buf + (sMsg->ptr - sMsg->data->buf);
    dupMsg->recvTime = sMsg->recvTime;

    dupMsg->head.version = sMsg->head.version;
    dupMsg->head.flags = sMsg->head.flags;
    dupMsg->head.type = sMsg->head.type;
    dupMsg->head.bodyLen = sMsg->head.bodyLen;
    dupMsg->head.forward = sMsg->head.forward;
    dupMsg->head.returnList = sMsg->head.returnList;
    dupMsg->head.addr = sMsg->head.addr;
    dupMsg->head.port = sMsg->head.port;
    dupMsg->head.timeout = sMsg->head.timeout;
#ifdef MIN_SLURM_PROTO_1605
    dupMsg->head.index = sMsg->head.index;
    dupMsg->head.treeWidth = sMsg->head.treeWidth;
#endif

    if (sMsg->head.nodeList) {
	dupMsg->head.nodeList = strdup(sMsg->head.nodeList);
    }

    return dupMsg;
}

void releaseSlurmMsg(Slurm_Msg_t *sMsg)
{
    if (!sMsg) return;

    if (sMsg->data) {
	if (sMsg->data->buf) ufree(sMsg->data->buf);
	ufree(sMsg->data);
    }
    if (sMsg->head.nodeList) ufree(sMsg->head.nodeList);

    ufree(sMsg);
}

static int registerSlurmMessage(int sock, Connection_CB_t *cb)
{
    Connection_t *con;

    con = addConnection(sock, cb);

    if (Selector_register(sock, readSlurmMsg, con) == -1) {
	mlog("%s: register socket %i failed\n", __func__, sock);
    }
    return 1;
}

static int handleSlurmctldReply(Slurm_Msg_t *sMsg)
{
    char **ptr;
    uint32_t rc;

    if (!extractSlurmAuth(&sMsg->ptr, &sMsg->head)) return 0;

    ptr = &sMsg->ptr;
    getUint32(ptr, &rc);

    if (rc != SLURM_SUCCESS) {
	mdbg(PSSLURM_LOG_PROTO, "%s: error: msg %s rc %u sock %i\n",
		__func__, msgType2String(sMsg->head.type), rc, sMsg->sock);
    }

    if (sMsg->source == -1) {
	closeConnection(sMsg->sock);
    }
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
		mwarn(errno, "%s: connect(addr %s port %s)", __func__,
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

static int connect2Slurmctld(void)
{
    int sock, len;
    char *addr, *port;

    port = getConfValueC(&SlurmConfig, "SlurmctldPort");
    addr = getConfValueC(&SlurmConfig, "ControlMachine");

    if (addr[0] == '"') addr++;
    len = strlen(addr);
    if (addr[len-1] == '"') addr[len-1] = '\0';

    /* need polling or slurmctld can run into problems ??
     * but polling is not good inside a psid plugin ....
     */
    sock = tcpConnect(addr, port);
    if (sock < 0) {
	/* try backup controller */
	addr = getConfValueC(&SlurmConfig, "BackupController");
	if (!addr) return sock;

	if (addr[0] == '"') addr++;
	len = strlen(addr);
	if (addr[len-1] == '"') addr[len-1] = '\0';
	mlog("%s: connect to %s\n", __func__, addr);

	sock = tcpConnect(addr, port);

	if (sock < 0) return sock;
    }
    mdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB,
	 "%s: connect to %s socket %i\n", __func__, addr, sock);

    registerSlurmMessage(sock, handleSlurmctldReply);

    return sock;
}

void initSlurmMsgHead(Slurm_Msg_Header_t *head)
{
    memset(head, 0, sizeof(Slurm_Msg_Header_t));
    head->version = SLURM_CUR_PROTOCOL_VERSION;
    head->flags |= SLURM_GLOBAL_AUTH_KEY;
#ifdef MIN_SLURM_PROTO_1605
    head->treeWidth = 1;
#endif
}

void freeSlurmMsgHead(Slurm_Msg_Header_t *head)
{
    uint32_t i;

    if (head->fwdata) {
	for (i=0; i<head->returnList; i++) {
	    ufree(head->fwdata[i].body.buf);
	}
    }

    ufree(head->nodeList);
    ufree(head->fwdata);
}

int __sendSlurmMsg(int sock, slurm_msg_type_t type, PS_DataBuffer_t *body,
		    const char *caller, const int line)
{
    Slurm_Msg_Header_t head;

    initSlurmMsgHead(&head);
    head.type = type;

    return __sendSlurmMsgEx(sock, &head, body, caller, line);
}

int __sendSlurmMsgEx(int sock, Slurm_Msg_Header_t *head, PS_DataBuffer_t *body,
		    const char *caller, const int line)
{
    PS_DataBuffer_t data = { .buf = NULL };
    uint32_t msgLen, lastBufLen = 0;
    int ret = 0, written;

    if (!head) {
	mlog("%s: invalid header from %s at %i\n", __func__, caller, line);
	return -1;
    }

    if (!body) {
	mlog("%s: invalid body from %s at %i\n", __func__, caller, line);
	return -1;
    }

    /* connect to slurmctld */
    if (sock < 0) {
	sock = connect2Slurmctld();
	if (sock < 0) {
	    /* TODO */
	    mlog("%s: fixme: need to retry later! caller %s at %i\n",
		    __func__, caller, line);
	    return -1;
	}
    }

    /* add message header */
    head->bodyLen = body->bufUsed;
    packSlurmHeader(&data, head);

    mdbg(PSSLURM_LOG_COMM, "%s: added slurm header (%i) : body len :%i\n",
	    __func__, data.bufUsed, body->bufUsed);

    if (logger_getMask(psslurmlogger) & PSSLURM_LOG_IO_VERB) {
	printBinaryData(data.buf + lastBufLen, data.bufUsed - lastBufLen,
			"msg header");
	lastBufLen = data.bufUsed;
    }

    /* add munge auth string, will *not* be counted to msg header body len */
    addSlurmAuth(&data);
    mdbg(PSSLURM_LOG_COMM, "%s: added slurm auth (%i)\n",
	    __func__, data.bufUsed);


    if (logger_getMask(psslurmlogger) & PSSLURM_LOG_IO_VERB) {
	printBinaryData(data.buf + lastBufLen, data.bufUsed - lastBufLen,
			"slurm auth");
	lastBufLen = data.bufUsed;
    }

    /* add the message body */
    addMemToMsg(body->buf, body->bufUsed, &data);
    mdbg(PSSLURM_LOG_COMM, "%s: added slurm msg body (%i)\n",
	    __func__, data.bufUsed);

    if (logger_getMask(psslurmlogger) & PSSLURM_LOG_IO_VERB) {
	printBinaryData(data.buf + lastBufLen, data.bufUsed - lastBufLen,
			"msg body");
	lastBufLen = data.bufUsed;
    }

    /* send the message */
    msgLen = htonl(data.bufUsed);
    written = doWriteP(sock, &msgLen, sizeof(msgLen));
    if (written != sizeof(msgLen)) {
	mlog("%s: writing message len %i failed, written %i caller %s at %i\n",
	     __func__, msgLen, written, caller, line);
	ufree(data.buf);
	return -1;
    }
    ret += written;
    mdbg(PSSLURM_LOG_COMM, "%s: wrote len: %u\n", __func__, ret);

    written = doWriteP(sock, data.buf, data.bufUsed);
    if (written == -1 || written != (int) data.bufUsed) {
	mlog("%s: writing message of length %i failed, written %i caller %s "
	     "line %i\n", __func__, data.bufUsed, written, caller, line);
	ufree(data.buf);
	return -1;
    }
    ret += written;
    mdbg(PSSLURM_LOG_COMM, "%s: wrote data: %u for caller %s at %i\n",
	    __func__, ret, caller, line);

    ufree(data.buf);
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

bool hexBitstr2List(char *bitstr, char **list, size_t *listSize)
{
    size_t len;
    int32_t next, count = 0;

    if (!bitstr) {
	mlog("%s: invalid bitstring\n", __func__);
	return false;
    }

    if (!strncmp(bitstr, "0x", 2)) bitstr += 2;
    len = strlen(bitstr);

    while (len--) {
	char tmp[1024];
	next = (int32_t) bitstr[len];

	if (!isxdigit(next)) return false;

	if (isdigit(next)) {
	    next -= '0';
	} else {
	    next = toupper(next);
	    next -= 'A' - 10;
	}

	if (next & 1) {
	    if (*listSize) str2Buf(",", list, listSize);
	    snprintf(tmp, sizeof(tmp), "%u", count);
	    str2Buf(tmp, list, listSize);
	}
	count++;

	if (next & 2) {
	    if (*listSize) str2Buf(",", list, listSize);
	    snprintf(tmp, sizeof(tmp), "%u", count);
	    str2Buf(tmp, list, listSize);
	}
	count++;

	if (next & 4) {
	    if (*listSize) str2Buf(",", list, listSize);
	    snprintf(tmp, sizeof(tmp), "%u", count);
	    str2Buf(tmp, list, listSize);
	}
	count++;

	if (next & 8) {
	    if (*listSize) str2Buf(",", list, listSize);
	    snprintf(tmp, sizeof(tmp), "%u", count);
	    str2Buf(tmp, list, listSize);
	}
	count++;
    }

    return true;
}

static int acceptSlurmClient(int socket, void *data)
{
    struct sockaddr_in SAddr;
    socklen_t clientlen = sizeof(SAddr);
    int newSock = accept(socket, (struct sockaddr *)&SAddr, &clientlen);

    if (newSock == -1) {
	mwarn(errno, "%s: error accepting new tcp connection", __func__);
	return 0;
    }

    mdbg(PSSLURM_LOG_COMM, "%s: from %s:%u socket:%i\n", __func__,
	 inet_ntoa(SAddr.sin_addr), ntohs(SAddr.sin_port), newSock);

    registerSlurmMessage(newSock, handleSlurmdMsg);

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
	mlog("%s: Selector_register(%i, port %i) failed\n", __func__, sock,
	     port);
	return -1;
    }
    mdbg(PSSLURM_LOG_COMM, "%s: sock %i port %i\n", __func__, sock, port);

    slurmListenSocket = sock;
    return sock;
}

static int handleSrunPTYMsg(int sock, void *data)
{
    Step_t *step = data;
    char buffer[4];
    struct winsize ws;
    uint16_t cols, rows;
    int ret;

    mlog("%s: got pty message for step %u:%u\n", __func__,
	 step->jobid, step->stepid);

    /* Shall be safe to do first a blocking read() inside a selector */
    ret = doRead(sock, buffer, sizeof(buffer));
    if (ret <= 0) {
	if (ret < 0) mwarn(errno, "%s: doRead()", __func__);
	mlog("%s: close pty connection\n", __func__);
	closeConnection(sock);
	return 0;
    }

    if (ret != 4) {
	mlog("%s: update window size error, len %u\n", __func__, ret);
    }
    memcpy(&cols, buffer, 2);
    memcpy(&rows, buffer+2, 2);
    ws.ws_col = ntohs(cols);
    ws.ws_row = ntohs(rows);

    if (ioctl(step->stdOut[1], TIOCSWINSZ, &ws)) {
	mwarn(errno, "%s: ioctl(TIOCSWINSZ)", __func__);
    }
    if (step->fwdata && killChild(step->fwdata->cPid, SIGWINCH)) {
	if (errno == ESRCH) return 0;
	mwarn(errno, "%s: send SIGWINCH to %i", __func__, step->fwdata->cPid);
    }
    return 0;
}

static int forwardInputMsg(Step_t *step, uint16_t rank, char *buf, int bufLen)
{
    char *ptr = buf;
    size_t c = bufLen;
    PS_Tasks_t *task = findTaskByRank(&step->tasks.list, rank);
    PSLog_Msg_t msg = (PSLog_Msg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CC_MSG,
	    .dest = task ? task->forwarderTID : -1,
	    .sender = PSC_getMyTID(),
	    .len = PSLog_headerSize },
       .version = 2,
       .type = STDIN,
       .sender = -1};

    if (!task) {
	mlog("%s: task for rank %u of step %u:%u not found\n", __func__, rank,
	     step->jobid, step->stepid);
	return -1;
    }

    do {
	int n = (c > sizeof(msg.buf)) ? sizeof(msg.buf) : c;
	if (n) memcpy(msg.buf, ptr, n);
	mdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB,
	     "%s: rank %u len %u msg.header.len %u to %s\n", __func__,
	     rank, n, PSLog_headerSize + n, PSC_printTID(task->forwarderTID));
	mdbg(PSSLURM_LOG_IO_VERB, "%s: '%.*s'\n", __func__, n, msg.buf);
	msg.header.len = PSLog_headerSize + n;
	sendMsgToMother(&msg);
	c -= n;
	ptr += n;
    } while (c > 0);

    return bufLen;
}

int handleSrunMsg(int sock, void *data)
{
    Step_t *step = data;
    char *ptr, buffer[1024];
    int ret, fd = -1;
    Slurm_IO_Header_t *ioh = NULL;
    uint16_t i;
    uint32_t myTaskIdsLen, *myTaskIds;
    bool pty;

    /* Shall be safe to do first a blocking read() inside a selector */
    ret = doRead(sock, buffer, SLURM_IO_HEAD_SIZE);
    if (ret <= 0) {
	if (ret < 0) {
	    mwarn(errno, "%s: doRead()", __func__);
	}
	mlog("%s: close srun connection\n", __func__);
	goto ERROR;
    }
    ptr = buffer;

    if (!step->fwdata) {
	mlog("%s: no forwarder running for step %u:%u\n", __func__,
	     step->jobid, step->stepid);
	goto ERROR;
    }
    pty = step->taskFlags & LAUNCH_PTY;
    fd = pty ? step->fwdata->stdOut[1] : step->fwdata->stdIn[1];
    myTaskIdsLen = step->globalTaskIdsLen[step->myNodeIndex];
    myTaskIds = step->globalTaskIds[step->myNodeIndex];

    if (!unpackSlurmIOHeader(&ptr, &ioh)) {
	mlog("%s: unpack slurm I/O header failed\n", __func__);
	goto ERROR;
    }

    mdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB,
	 "%s: step %u:%u stdin %d type %u length %u gtid %u ltid %u pty %u"
	 " myTIDsLen %u\n", __func__, step->jobid, step->stepid, fd, ioh->type,
	 ioh->len, ioh->gtid, ioh->ltid, pty, myTaskIdsLen);

    if (ioh->type == SLURM_IO_CONNECTION_TEST) {
	if (ioh->len != 0) {
	    mlog("%s: invalid connection test, len %u\n", __func__, ioh->len);
	}
	srunSendIO(SLURM_IO_CONNECTION_TEST, 0, step, NULL, 0);
    } else if (!ioh->len) {
	/* forward eof to all forwarders */
	mlog("%s: got eof of stdin %d\n", __func__, fd);

	if (ioh->type == SLURM_IO_STDIN) {
	    forwardInputMsg(step, ioh->gtid, NULL, 0);
	} else if (ioh->type == SLURM_IO_ALLSTDIN) {
	    for (i=0; i<myTaskIdsLen; i++) {
		forwardInputMsg(step, myTaskIds[i], NULL, 0);
	    }
	} else {
	    mlog("%s: unsupported I/O type %u\n", __func__, ioh->type);
	}

	/* close loggers stdin */
	if (!pty) closeConnection(fd);
    } else {
	/* foward stdin message to forwarders */
	size_t left = ioh->len;
	while (left > 0) {
	    size_t readNow = (left > sizeof(buffer)) ? sizeof(buffer) : left;
	    /* @todo This shall be non-blocking! */
	    ret = doRead(sock, buffer, readNow);
	    if (ret <= 0) {
		mlog("%s: reading body failed\n", __func__);
		break;
	    }

	    if (pty) {
		if (step->nodes[0] == PSC_getMyID()) doWriteP(fd, buffer, ret);
	    } else {
		switch (ioh->type) {
		case SLURM_IO_STDIN:
		    forwardInputMsg(step, ioh->gtid, buffer, ret);
		    break;
		case SLURM_IO_ALLSTDIN:
		    for (i=0; i<myTaskIdsLen; i++) {
			forwardInputMsg(step, myTaskIds[i], buffer, ret);
		    }
		    break;
		default:
		    mlog("%s: unsupported I/O type %u\n", __func__, ioh->type);
		}
	    }
	    left -= ret;
	}
    }

    ufree(ioh);
    return 0;

ERROR:
    ufree(ioh);
    closeConnection(sock);
    return 0;
}

int srunOpenControlConnection(Step_t *step)
{
    char port[32];
    int sock;

    if (step->numSrunPorts <= 0) {
	mlog("%s: sending failed, no srun ports available\n", __func__);
	return -1;
    }

    snprintf(port, sizeof(port), "%u",
	     step->srunPorts[step->myNodeIndex % step->numSrunPorts]);
    sock = tcpConnect(inet_ntoa(step->srun.sin_addr), port);
    if (sock < 0) {
	mlog("%s: connection to srun %s:%s failed\n", __func__,
	     inet_ntoa(step->srun.sin_addr), port);
	return -1;
    }

    setFDblock(sock, false);

    mdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB,
	 "%s: new srun connection %i\n", __func__, sock);

    return sock;
}

int srunSendMsg(int sock, Step_t *step, slurm_msg_type_t type,
		PS_DataBuffer_t *body)
{
    if (sock < 0) {
	sock = srunOpenControlConnection(step);
	if (sock < 0) return -1;
    }

    if (Selector_register(sock, handleSrunMsg, step) == -1) {
	mlog("%s: Selector_register(%i) failed\n", __func__, sock);
	return -1;
    }

    mdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB,
	 "%s: sock %u, len: body.bufUsed %u body.bufSize %u\n",
	 __func__, sock, body->bufUsed, body->bufSize);

    return sendSlurmMsg(sock, type, body);
}

int srunOpenPTYConnection(Step_t *step)
{
    int sock;
    char *port = envGet(&step->env, "SLURM_PTY_PORT");

    if (!port) {
	mlog("%s: missing SLURM_PTY_PORT variable\n", __func__);
	return -1;
    }
    sock = tcpConnect(inet_ntoa(step->srun.sin_addr), port);
    if (sock < 0) {
	mlog("%s: connection to srun %s:%s failed\n", __func__,
	     inet_ntoa(step->srun.sin_addr), port);
	return -1;
    }
    mlog("%s: pty connection (%i) to %s:%s\n", __func__, sock,
	 inet_ntoa(step->srun.sin_addr), port);
    step->srunPTYMsg.sock = sock;

    setFDblock(sock, false);

    if (Selector_register(sock, handleSrunPTYMsg, step) == -1) {
	mlog("%s: Selector_register(%i) failed\n", __func__, sock);
	return -1;
    }
    return sock;
}

int srunOpenIOConnectionEx(Step_t *step, uint32_t addr, uint16_t port,
			   char *sig)
{
    PS_DataBuffer_t data = { .buf = NULL };
    PSnodes_ID_t nodeID = step->myNodeIndex;
    uint32_t i;
    int sock;

    /* open I/O connection to srun */
    if (!addr) {
	char sport[100];
	snprintf(sport, sizeof(sport), "%u",
		    step->IOPort[nodeID % step->numIOPort]);

	sock = tcpConnect(inet_ntoa(step->srun.sin_addr), sport);
	if (sock < 0) {
	    mlog("%s: connection to srun %s:%s failed\n", __func__,
		 inet_ntoa(step->srun.sin_addr), sport);
	    return -1;
	}

	mlog("%s: addr %s:%s sock %u\n", __func__,
	     inet_ntoa(step->srun.sin_addr), sport, sock);

	step->srunIOMsg.sock = sock;
    } else {
	if ((sock = tcpConnectU(addr, port)) <0) {
	    mlog("%s: connection to srun %u:%u failed\n", __func__, addr, port);
	    return -1;
	}
    }

    setFDblock(sock, false);

    addUint16ToMsg(IO_PROTOCOL_VERSION, &data);
    /* nodeid */
    addUint32ToMsg(nodeID, &data);

    /* stdout obj count */
    if (step->stdOutOpt == IO_SRUN || step->stdOutOpt == IO_SRUN_RANK ||
	step->taskFlags & LAUNCH_PTY) {
	step->outChannels =
		umalloc(sizeof(int32_t) * step->globalTaskIdsLen[nodeID]);
	for (i=0; i<step->globalTaskIdsLen[nodeID]; i++) {
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
	for (i=0; i<step->globalTaskIdsLen[nodeID]; i++) {
	    step->errChannels[i] = 1;
	}
	addUint32ToMsg(step->globalTaskIdsLen[nodeID], &data);
    }

    /* io key */
    addUint32ToMsg((uint32_t) SLURM_IO_KEY_SIZE, &data);
    addMemToMsg(sig, (uint32_t) SLURM_IO_KEY_SIZE, &data);

    doWriteP(sock, data.buf, data.bufUsed);
    ufree(data.buf);

    return sock;
}

void srunEnableIO(Step_t *step)
{
    uint32_t i, myTaskIdsLen;
    uint32_t *myTaskIds;
    static int enabled = 0;

    if (enabled) return;

    if (step->stdInRank != -1) {
	/* close stdin for all other ranks */
	myTaskIdsLen = step->globalTaskIdsLen[step->myNodeIndex];
	myTaskIds = step->globalTaskIds[step->myNodeIndex];

	for (i=0; i<myTaskIdsLen; i++) {
	    if ((uint32_t) step->stdInRank == myTaskIds[i]) continue;
	    forwardInputMsg(step, myTaskIds[i], NULL, 0);
	}
    }

    if (Selector_register(step->srunIOMsg.sock, handleSrunMsg, step) == -1) {
	mlog("%s: Selector_register(%i) srun I/O socket failed\n", __func__,
		step->srunIOMsg.sock);
    }
    enabled = 1;
}

int srunSendIO(uint16_t type, uint16_t taskid, Step_t *step, char *buf,
		uint32_t bufLen)
{
    int ret, error;
    Slurm_IO_Header_t ioh = {
	.type = type,
	.gtid = taskid,
	.ltid = (uint16_t)NO_VAL,
	.len = bufLen
    };

    ret = srunSendIOEx(step->srunIOMsg.sock, &ioh, buf, &error);

    if (ret < 0) {
	switch (error) {
	    case ECONNRESET:
	    case EPIPE:
	    case EBADF:
		sendBrokeIOcon(step);
		freeSlurmMsg(&step->srunIOMsg);
		break;
	}
    }

    return ret;
}

int srunSendIOEx(int sock, Slurm_IO_Header_t *iohead, char *buf, int *error)
{
    PS_DataBuffer_t data = { .buf = NULL };
    int ret = 0, once = 1;
    int32_t towrite, written = 0;
    Slurm_IO_Header_t ioh;

    if (sock < 0) return -1;

    if (iohead->len > 0) {
	mdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB,
	     "%s: msg '%.*s'\n", __func__, iohead->len, buf);
    }

    towrite = iohead->len;
    errno = *error = 0;
    memcpy(&ioh, iohead, sizeof(Slurm_IO_Header_t));

    while (once || towrite > 0) {
	ioh.len = towrite > 1000 ? 1000 : towrite;

	packSlurmIOMsg(&data, &ioh, buf + written);
	ret = doWriteF(sock, data.buf, data.bufUsed);

	data.bufUsed = once = 0;

	if (ret < 0) {
	    *error = errno;
	    ufree(data.buf);
	    return -1;
	}
	if (!ret) continue;

	ret -= SLURM_IO_HEAD_SIZE;
	written += ret;
	towrite -= ret;
	mdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB, "%s: fd %i ret %i written %i"
	     " towrite %i\n", __func__, sock, ret, written, towrite);
    }

    ufree(data.buf);
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
	Connection_Forward_t *fw = &con->fw;
	uint32_t i;

	for (i=0; i<fw->nodesCount; i++) {
	    if (fw->nodes[i] == nodeID) {
		Slurm_Msg_t sMsg;
		PS_DataBuffer_t data = { .buf = NULL };

		initSlurmMsg(&sMsg);
		sMsg.source = PSC_getTID(nodeID, 0);
		sMsg.head.type = RESPONSE_FORWARD_FAILED;
		sMsg.sock = con->sock;
		sMsg.recvTime = con->recvTime;
		sMsg.data = &data;

		saveFrwrdMsgRes(&sMsg, SLURM_COMMUNICATIONS_CONNECTION_ERROR);
		mlog("%s: message error for node %i saved\n", __func__, nodeID);

		/* assuming nodes[] contains each node id only once */
		break;
	    }
	}
    }
}
