/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
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

#include "slurmcommon.h"
#include "psslurmproto.h"
#include "psslurmconfig.h"
#include "psslurmjob.h"
#include "psslurmlog.h"
#include "psslurmauth.h"
#include "psslurmio.h"
#include "psslurmenv.h"
#include "psslurmpscomm.h"

#include "plugincomm.h"
#include "pluginconfig.h"
#include "pluginhelper.h"
#include "pluginmalloc.h"
#include "selector.h"
#include "pslog.h"
#include "psidcomm.h"

#include "psslurmcomm.h"

#define MAX_PACK_STR_LEN        (16 * 1024 * 1024)
#define DEBUG_IO 0

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
    return "";
}

void initConnectionList(void)
{
    INIT_LIST_HEAD(&ConnectionList.list);
}

void initSlurmMsg(Slurm_Msg_t *sMsg)
{
    sMsg->sock = -1;
    sMsg->data = NULL;
    sMsg->ptr = NULL;
    sMsg->source = -1;
    sMsg->recvTime = 0;
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
    } else {
	/* do we need to send result ??? */
    }

    initSlurmMsg(sMsg);
}

static int resetConnection(int socket)
{
    Connection_t *con;

    if (!(con = findConnection(socket))) return 0;

    ufree(con->data.buf);
    con->data.buf = NULL;
    con->data.bufSize = 0;
    con->data.bufUsed = 0;
    con->error = 0;

    return 1;
}

Connection_t *addConnection(int socket, Connection_CB_t *cb)
{
    Connection_t *con;

    if ((con = findConnection(socket))) {
	mlog("%s: socket(%i) already has a connection, resetting it\n",
		__func__, socket);
	resetConnection(socket);
	con->cb = cb;
	return con;
    }

    con = (Connection_t *) umalloc(sizeof(Connection_t));
    con->sock = socket;
    con->cb = cb;
    con->data.buf = NULL;
    con->data.bufSize = 0;
    con->data.bufUsed = 0;
    con->error = 0;

    /* init forward struct */
    con->fw.res = 0;
    con->fw.nodes = NULL;
    con->fw.nodesCount = 0;
    con->fw.body.buf = NULL;
    initSlurmMsgHead(&con->fw.head);

    list_add_tail(&(con->list), &ConnectionList.list);

    return con;
}

Connection_t *findConnection(int socket)
{
    list_t *pos, *tmp;
    Connection_t *con;

    list_for_each_safe(pos, tmp, &ConnectionList.list) {
	if (!(con = list_entry(pos, Connection_t, list))) break;
	if (con->sock == socket) return con;
    }

    return NULL;
}

Connection_t *findConnectionEx(int socket, time_t recvTime)
{
    list_t *pos, *tmp;
    Connection_t *con;

    list_for_each_safe(pos, tmp, &ConnectionList.list) {
	if (!(con = list_entry(pos, Connection_t, list))) break;
	if (con->sock == socket && con->recvTime == recvTime) return con;
    }

    return NULL;
}

void closeConnection(int socket)
{
    Connection_t *con;

    /* close the connection */
    if (Selector_isRegistered(socket)) Selector_remove(socket);
    close(socket);

    /* free memory */
    if ((con = findConnection(socket))) {
	list_del(&con->list);
	freeSlurmMsgHead(&con->fw.head);
	if (con->fw.nodesCount) ufree(con->fw.nodes);
	ufree(con->fw.body.buf);
	ufree(con->data.buf);
	ufree(con);
    }
}

void clearConnections(void)
{
    list_t *pos, *tmp;
    Connection_t *con;

    list_for_each_safe(pos, tmp, &ConnectionList.list) {
	if (!(con = list_entry(pos, Connection_t, list))) return;
	closeConnection(con->sock);
    }
}

void saveForwardedMsgRes(Slurm_Msg_t *sMsg, PS_DataBuffer_t *data,
			    uint32_t error, const char *func, const int line)
{
    Connection_t *con;
    Connection_Forward_t *fw;
    Slurm_Forward_Data_t *fwdata;
    uint16_t i, saved = 0;

    if (!(con = findConnectionEx(sMsg->sock, sMsg->recvTime))) {
	mlog("%s: connection source '%s' socket '%i' recvTime '%zu' type '%s' "
		"not found\n", __func__, PSC_printTID(sMsg->source), sMsg->sock,
		sMsg->recvTime, msgType2String(sMsg->head.type));
	return;
    }
    fw = &con->fw;

    /* check fw time */
    fw->res++;

    if (PSC_getID(sMsg->source) == PSC_getMyID()) {
	fw->head.type = sMsg->head.type;
	addMemToMsg(data->buf, data->bufUsed, &fw->body);
    } else {
	for (i=0; i<fw->head.forward; i++) {
	    fwdata = &fw->head.fwdata[i];
	    if (fwdata->node == -1) {
		fwdata->error = error;
		fwdata->type = sMsg->head.type;
		fwdata->node = PSC_getID(sMsg->source);
		addMemToMsg(data->buf, data->bufUsed, &fwdata->body);
		fw->head.returnList++;
		saved = 1;
		break;
	    }
	}
	if (!saved) {
	    mlog("%s: could not save messages result for '%s'\n", __func__,
		    PSC_printTID(sMsg->source));
	}
    }

    mdbg(PSSLURM_LOG_FWD, "%s (%s:%i): type '%s' forward '%u' resCount '%u' "
	    "source '%s' sock '%i' recvTime '%zu'\n", __func__, func, line,
	    msgType2String(sMsg->head.type), fw->head.forward, fw->res,
	    PSC_printTID(sMsg->source), sMsg->sock, sMsg->recvTime);

    if (fw->res == fw->nodesCount + 1) {
	mdbg(PSSLURM_LOG_FWD, "%s: forward '%s' complete, sending answer\n",
		__func__, msgType2String(sMsg->head.type));

	fw->head.forward = 0;

	if (!fw->body.buf || !fw->body.bufUsed) {
	    mlog("%s: invalid local data, dropping msg\n", __func__);
	    closeConnection(con->sock);
	    return;
	}
	sendSlurmMsgEx(con->sock, &fw->head, &fw->body);

	closeConnection(con->sock);
    }
}

static int readSlurmMsg(int sock, void *param)
{
    Connection_t *con = param;
    PS_DataBuffer_t *dBuf = &con->data;
    uint32_t msglen = 0;
    int ret, error = 0;
    size_t size = 0, toRead;
    char *ptr;

    if (!param) {
	mlog("%s: invalid connection data buffer\n", __func__);
	closeConnection(sock);
	return 0;
    }

    if (dBuf->bufSize && (dBuf->bufSize == dBuf->bufUsed)) {
	mlog("%s: data buffer for sock '%i' already used, doing a reset\n",
		__func__, sock);
	resetConnection(sock);
    }

    if (!dBuf->bufSize) {
	/* new request, we need to read the size first */
	if ((ret = doReadP(sock, &msglen, sizeof(msglen))) != sizeof(msglen)) {
	    if (!ret) {
		mdbg(PSSLURM_LOG_COMM, "%s: closing connection, empty message "
			"len on sock '%i'\n", __func__, sock);
		error = 1;
		goto CALLBACK;
	    }
	    mlog("%s: invalid message len '%u', expect '%zu'\n", __func__, ret,
		    sizeof(msglen));
	    error = 1;
	    goto CALLBACK;
	}

	if ((msglen = ntohl(msglen)) > MAX_MSG_SIZE) {
	    mlog("%s: msg too big '%u' : max %u\n", __func__, msglen,
		    MAX_MSG_SIZE);
	    error = 1;
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
	if (size > 0) {
	    /* not all data arrived yet, lets try again later */
	    dBuf->bufUsed += size;
	    //mlog("%s: we try later for sock '%u' red '%zu'\n",
	    //	    __func__, sock, size);
	    return 0;
	}
	/* read error */
	error = 1;
	goto CALLBACK;

    } else if (!ret) {
	/* connection reset */
	mdbg(PSSLURM_LOG_COMM, "%s: connection reset on sock '%i'\n",
		__func__, sock);
	error = 1;
	goto CALLBACK;
    } else {
	/* all data red successful */
	dBuf->bufUsed += size;
	/*
	mlog("%s: all data red for '%u' ret '%u' toread '%zu' msglen '%u' size"
		" '%zu'\n", __func__, sock, ret, toRead, msglen, size);
	*/
	goto CALLBACK;
    }

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

    if (error) closeConnection(sock);

    return 0;
}

static int registerSlurmMessage(int sock, Connection_CB_t *cb)
{
    Connection_t *con;

    con = addConnection(sock, cb);

    Selector_register(sock, readSlurmMsg, con);
    return 1;
}

static int handleSlurmctldReply(Slurm_Msg_t *sMsg)
{
    char **ptr;
    uint32_t rc;

    if (!(testMungeAuth(&sMsg->ptr, &sMsg->head))) return 0;

    ptr = &sMsg->ptr;
    getUint32(ptr, &rc);

    if (rc != SLURM_SUCCESS) {
	mdbg(PSSLURM_LOG_PROTO, "%s: error: msg '%s' rc '%u' sock '%i'\n",
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

    /* workaround for psid SIGCHLD malloc deadlock */

    /* set up the sockaddr structure */
    if ((ret = getaddrinfo(addr, port, NULL, &result)) != 0) {
	mlog("%s: getaddrinfo(%s:%s) failed : %s\n", __func__,
		addr, port, gai_strerror(ret));
	return -1;
    }

    if (!reConnect) {
	mdbg(PSSLURM_LOG_COMM, "%s: to %s port:%s\n",
		__func__, addr, port);
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
	if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
	    err = errno;
	    mwarn(errno, "%s: socket failed, port '%s' addr '%s' ", __func__,
		    port, addr);
	    continue;
	}

	if ((connect(sock, rp->ai_addr, rp->ai_addrlen)) == -1) {
	    err = errno;
	    if (errno != EINTR) {
		mwarn(errno, "%s: connect failed, port '%s' addr '%s' ",
			__func__, port, addr);
	    }
	    close(sock);
	    continue;
	}
	break;
    }
    freeaddrinfo(result);

    if (rp == NULL) {
	if (err == EISCONN && reConnect < TCP_CONNECTION_RETRYS) {
	    close(sock);
	    reConnect++;
	    goto TCP_RECONNECT;
	}
	mwarn(err, "%s: failed(%i) addr '%s' port '%s' ", __func__,
	    err, addr, port);
	close(sock);
	return -1;
    }

    return sock;
}

static int connect2Slurmctld()
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
    if ((sock = tcpConnect(addr, port)) < 0) {
	/* try backup controller */
	if ((addr = getConfValueC(&SlurmConfig, "BackupController"))) {

	    if (addr[0] == '"') addr++;
	    len = strlen(addr);
	    if (addr[len-1] == '"') addr[len-1] = '\0';
	    mlog("%s: connect to %s\n", __func__, addr);

	    if ((sock = tcpConnect(addr, port)) < 0) return sock;
	} else {
	    return sock;
	}
    }
    mdbg(PSSLURM_LOG_IO, "%s: connect to %s socket %i\n", __func__, addr, sock);

    registerSlurmMessage(sock, handleSlurmctldReply);

    return sock;
}

void initSlurmMsgHead(Slurm_Msg_Header_t *head)
{
    head->version = SLURM_CUR_PROTOCOL_VERSION;
    head->flags = 0;
    head->flags |= SLURM_GLOBAL_AUTH_KEY;
    head->type = 0;
    head->bodyLen = 0;
    head->forward = 0;
    head->nodeList = NULL;
    head->timeout = 0;
    head->returnList = 0;
    head->addr = 0;
    head->port = 0;
    head->fwdata = NULL;
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

int sendSlurmMsg(int sock, slurm_msg_type_t type, PS_DataBuffer_t *body)
{
    Slurm_Msg_Header_t head;

    initSlurmMsgHead(&head);
    head.type = type;

    return sendSlurmMsgEx(sock, &head, body);
}

static void addSlurmHeader(Slurm_Msg_Header_t *head, uint32_t bodyLen,
			    PS_DataBuffer_t *data)
{
    uint32_t i;
    const char *hn;

    /* protocol version */
    addUint16ToMsg(head->version, data);
    /* flags */
    addUint16ToMsg(head->flags, data);
    /* type */
    addUint16ToMsg(head->type, data);
    /* body len */
    addUint32ToMsg(bodyLen, data);

    /* forward */
    addUint16ToMsg(head->forward, data);
    if (head->forward > 0) {
	addStringToMsg(head->nodeList, data);
	addUint32ToMsg(head->timeout, data);
    }

    /* return list */
    addUint16ToMsg(head->returnList, data);
    for (i=0; i<head->returnList; i++) {
	/* error */
	addUint32ToMsg(head->fwdata[i].error, data);

	/* msg type */
	addUint16ToMsg(head->fwdata[i].type, data);

	/* nodename */
	hn = getHostnameByNodeId(head->fwdata[i].node);
	addStringToMsg(hn, data);

	/* msg body */
	addMemToMsg(head->fwdata[i].body.buf,
		head->fwdata[i].body.bufUsed, data);
    }

    /* addr/port */
    addUint32ToMsg(head->addr, data);
    addUint16ToMsg(head->port, data);
}

int sendSlurmMsgEx(int sock, Slurm_Msg_Header_t *head, PS_DataBuffer_t *body)
{
    PS_DataBuffer_t data = { .buf = NULL };
    uint32_t msgLen;
    int ret;

    /* connect to slurmctld */
    if (sock < 0) {
	if ((sock = connect2Slurmctld())<0) {
	    /* TODO */
	    mlog("%s: fixme: need to retry later!!!\n", __func__);
	    return 0;
	}
    }

    /* add message header */
    addSlurmHeader(head, body->bufUsed, &data);
    mdbg(PSSLURM_LOG_COMM, "%s: added slurm header (%i) : body len :%i\n",
	    __func__, data.bufUsed, body->bufUsed);

    /* add munge auth string, will *not* be counted to msg header body len */
    addSlurmAuth(&data);
    mdbg(PSSLURM_LOG_COMM, "%s: added slurm auth (%i)\n",
	    __func__, data.bufUsed);

    /* add the message body */
    addMemToMsg(body->buf, body->bufUsed, &data);
    mdbg(PSSLURM_LOG_COMM, "%s: added slurm msg body (%i)\n",
	    __func__, data.bufUsed);

    /* send the message */
    msgLen = htonl(data.bufUsed);
    if ((ret = doWriteP(sock, &msgLen, sizeof(msgLen))) < 1) {
	ufree(data.buf);
	return ret;
    }
    mdbg(PSSLURM_LOG_COMM, "%s: wrote len: %u\n", __func__, ret);
    if ((ret = doWriteP(sock, data.buf, data.bufUsed)) < 1) {
	ufree(data.buf);
	return ret;
    }
    mdbg(PSSLURM_LOG_COMM, "%s: wrote data: %u\n", __func__, ret);

    ufree(data.buf);
    return 1;
}

void __getBitString(char **ptr, char **bitStr, const char *func,
				const int line)
{
    uint32_t len;

    getUint32(ptr, &len);

    if (len == NO_VAL) {
	*bitStr = NULL;
	return;
    }

    getUint32(ptr, &len);

    if (len > MAX_PACK_STR_LEN) {
	*bitStr = NULL;
	mlog("%s(%s:%i): invalid str len '%i'\n", __func__, func, line, len);
	return;
    }

    if (len > 0) {
	*bitStr = umalloc(len);
	memcpy(*bitStr, *ptr, len);
	*ptr += len;
    } else {
	*bitStr = NULL;
    }
}

static int acceptSlurmClient(int asocket, void *data)
{

#define MAX_ADDR_SIZE 20
    unsigned int clientlen;
    struct sockaddr_in SAddr;
    int socket = -1;

    /* accept new TCP connection */
    clientlen = sizeof(SAddr);

    if ((socket = accept(asocket, (void *)&SAddr, &clientlen)) == -1) {
	mwarn(errno, "%s error accepting new tcp connection ", __func__);
	return 0;
    }

    mdbg(PSSLURM_LOG_COMM, "%s: from %s:%u socket:%i\n", __func__,
	inet_ntoa(SAddr.sin_addr), ntohs(SAddr.sin_port), socket);

    registerSlurmMessage(socket, handleSlurmdMsg);

    return 0;
}

void closeSlurmdSocket(void)
{
    Selector_remove(slurmListenSocket);
    close(slurmListenSocket);
    slurmListenSocket = -1;
}

int openSlurmdSocket(int port)
{
    int res, sock;
    int opt = 1;
    struct sockaddr_in saClient;

    if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == 0) {
	mwarn(errno, "%s: socket failed, socket:%i port:%i ", __func__,
		sock, port);
	return -1;
    }

    if ((setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) < 0 ) {
	mwarn(errno, "%s: setsockopt failed, socket:%i port:%i ", __func__,
		sock, port);
    }

    /* set up the sockaddr structure */
    saClient.sin_family = AF_INET;
    saClient.sin_addr.s_addr = INADDR_ANY;
    saClient.sin_port = htons(port);
    bzero(&(saClient.sin_zero), 8);

    /* bind the socket */
    res = bind(sock, (struct sockaddr *)&saClient, sizeof(saClient));

    if (res == -1) {
        mwarn(errno, "%s: bind failed, socket:%i port:%i ", __func__,
	    sock, port);
	return -1;
    }

    /* set socket to listen state */
    if ((res = listen(sock, 5)) == -1) {
        mwarn(errno, "%s: listen failed, socket:%i port:%i ", __func__,
	    sock, port);
        return -1;
    }

    /* add socket to psid selector */
    //mlog("%s: register fd:%i port:%i\n", __func__, sock, port);
    if ((Selector_register(sock, acceptSlurmClient, NULL)) == -1) {
	mlog("%s: Selector_register(%i) failed\n", __func__, sock);
	return -1;
    }

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

    mlog("%s: got pty message for step '%u:%u'\n", __func__,
	    step->jobid, step->stepid);

    if ((ret = doReadP(sock, buffer, sizeof(buffer))) <= 0) {
	mlog("%s: closing pty connection\n", __func__);
	closeConnection(sock);
	return 0;
    }

    if (ret != 4) {
	mlog("%s: update window size error, len '%u'\n", __func__, ret);
    }
    memcpy(&cols, buffer, 2);
    memcpy(&rows, buffer+2, 2);
    ws.ws_col = ntohs(cols);
    ws.ws_row = ntohs(rows);

    if (ioctl(step->stdOut[1], TIOCSWINSZ, &ws)) {
	mwarn(errno, "%s: ioctl(TIOCSWINSZ): ", __func__);
    }
    if (step->fwdata && killChild(step->fwdata->childPid, SIGWINCH)) {
	if (errno == ESRCH) return 0;
	mlog("%s: error sending SIGWINCH to '%i'\n", __func__,
	step->fwdata->childPid);
    }
    return 0;
}

static int forwardInputMsg(Step_t *step, uint16_t rank, char *buf, int bufLen)
{
    char *ptr = buf, format[128];
    int n = 0;
    size_t c = bufLen;
    PSLog_Msg_t msg;
    PS_Tasks_t *task;

    if (!(task = findTaskByRank(&step->tasks.list, rank))) {
	mlog("%s: task for rank '%u' of step '%u:%u' not found\n", __func__,
		rank, step->jobid, step->stepid);
	return -1;
    }

    msg.header.type = PSP_CC_MSG;
    msg.header.sender = PSC_getTID(-1, getpid());
    msg.version = 2;
    msg.type = STDIN;
    msg.sender = -1;
    msg.header.dest = task->forwarderTID;

    do {
	n = (c > sizeof(msg.buf)) ? sizeof(msg.buf) : c;
	if (n) memcpy(msg.buf, ptr, n);
	if (DEBUG_IO) {
	    mlog("%s: rank '%u' len '%u' msg.header.len %u' to '%s'\n",
		    __func__, rank, n, PSLog_headerSize + n,
		    PSC_printTID(task->forwarderTID));
	    snprintf(format, sizeof(format), "%s: %%.%is\n", __func__, n);
	    mlog(format, msg.buf);
	}
	n = msg.header.len = PSLog_headerSize + n;
	forwardMsgtoMother((DDMsg_t *) &msg);
	c -= n - PSLog_headerSize;
	ptr += n - PSLog_headerSize;
    } while (c > 0);

    return n;
}

int handleSrunMsg(int sock, void *data)
{
    Step_t *step = data;
    char *ptr, buffer[1024];
    int ret, headSize, readnow, fd = -1;
    size_t toread;
    uint16_t type, gtid, ltid, i;
    uint32_t lenght, myTaskIdsLen;
    uint32_t *myTaskIds;

    headSize = sizeof(uint32_t) + 3 * sizeof(uint16_t);
    if ((ret = doReadP(sock, buffer, headSize)) <= 0) {
	closeConnection(sock);
	return 0;
    }
    ptr = buffer;

    if (!step->fwdata) {
	mlog("%s: no forwarder running for step '%u:%u'\n", __func__,
		step->jobid, step->stepid);
	closeConnection(sock);
	return 0;
    }
    fd = (step->pty) ? step->fwdata->stdOut[1] : step->fwdata->stdIn[1];

    /* type */
    getUint16(&ptr, &type);
    /* global taskid */
    getUint16(&ptr, &gtid);
    /* local taskid */
    getUint16(&ptr, &ltid);
    /* lenght */
    getUint32(&ptr, &lenght);

    myTaskIdsLen = step->globalTaskIdsLen[step->myNodeIndex];
    myTaskIds = step->globalTaskIds[step->myNodeIndex];

    mdbg(PSSLURM_LOG_IO, "%s: step '%u:%u' stdin '%u' type '%u' lenght '%u' "
	    "gtid '%u' ltid '%u' pty:%u myTIDsLen '%u'\n", __func__,
	    step->jobid, step->stepid, fd, type, lenght, gtid, ltid, step->pty,
	    myTaskIdsLen);

    if (type == SLURM_IO_CONNECTION_TEST) {
	if (lenght != 0) {
	    mlog("%s: invalid connection test, lenght '%u'\n", __func__,
		    lenght);
	}
	srunSendIO(SLURM_IO_CONNECTION_TEST, 0, step, NULL, 0);
    } else if (!lenght) {
	/* forward eof to all forwarders */
	mlog("%s: got eof of stdin '%u'\n", __func__, fd);

	if (type == SLURM_IO_STDIN) {
	    forwardInputMsg(step, gtid, NULL, 0);
	} else if (type == SLURM_IO_ALLSTDIN) {
	    for (i=0; i<myTaskIdsLen; i++) {
		forwardInputMsg(step, myTaskIds[i], NULL, 0);
	    }
	} else {
	    mlog("%s: got unsupported I/O type '%d'\n", __func__, type);
	}

	/* close loggers stdin */
	if (!step->pty) closeConnection(fd);
    } else {
	/* foward stdin message to forwarders */
	toread = lenght;
	while (toread > 0) {
	    readnow = (toread > (int) sizeof(buffer)) ? sizeof(buffer) : toread;
	    if ((ret = doRead(sock, buffer, readnow)) <= 0) {
		mlog("%s: reading body failed\n", __func__);
		break;
	    }

	    if (step->pty) {
		if (step->nodes[0] == PSC_getMyID()) doWriteP(fd, buffer, ret);
	    } else {
		if (type == SLURM_IO_STDIN) {
		    forwardInputMsg(step, gtid, buffer, ret);
		} else if (type == SLURM_IO_ALLSTDIN) {
		    for (i=0; i<myTaskIdsLen; i++) {
			forwardInputMsg(step, myTaskIds[i], buffer, ret);
		    }
		} else {
		    mlog("%s: got unsupported I/O type '%d'\n", __func__, type);
		}
	    }

	    toread -= ret;
	}
    }

    return 0;
}

int srunOpenControlConnection(Step_t *step)
{
    char port[256];
    int sock;

    if (step->numSrunPorts <= 0) {
	mlog("%s: sending failed, no srun ports available\n", __func__);
	return -1;
    }

    snprintf(port, sizeof(port), "%u",
		step->srunPorts[step->myNodeIndex % step->numSrunPorts]);
    if ((sock = tcpConnect(inet_ntoa(step->srun.sin_addr), port)) <0) {
	mlog("%s: connection to srun '%s:%s' failed\n", __func__,
		inet_ntoa(step->srun.sin_addr), port);
	return -1;
    }

    setFDblock(sock, 0);

    mdbg(PSSLURM_LOG_IO, "%s: new srun connection %i\n", __func__, sock);

    return sock;
}

int srunSendMsg(int sock, Step_t *step, slurm_msg_type_t type,
		PS_DataBuffer_t *body)
{
    if (sock < 0) {
	if ((sock = srunOpenControlConnection(step)) < 0) return -1;
    }

    if ((Selector_register(sock, handleSrunMsg, step)) == -1) {
	mlog("%s: Selector_register(%i) failed\n", __func__, sock);
	return -1;
    }

    if (DEBUG_IO) {
	mlog("%s: sock %u, len: body.bufUsed %u body.bufSize %u\n", __func__,
		sock, body->bufUsed, body->bufSize);
    }
    return sendSlurmMsg(sock, type, body);
}

int srunOpenPTY(Step_t *step)
{
    int sock;
    char *port;

    if (!(port = envGet(&step->env, "SLURM_PTY_PORT"))) {
	mlog("%s: missing SLURM_PTY_PORT variable\n", __func__);
	return -1;
    }
    if ((sock = tcpConnect(inet_ntoa(step->srun.sin_addr), port)) <0) {
	mlog("%s: connection to srun '%s:%s' failed\n", __func__,
		inet_ntoa(step->srun.sin_addr), port);
	return -1;
    }
    mlog("%s: pty connection (%i) to '%s:%s'\n", __func__, sock,
	    inet_ntoa(step->srun.sin_addr), port);
    step->srunPTYMsg.sock = sock;

    setFDblock(sock, 0);

    if ((Selector_register(sock, handleSrunPTYMsg, step)) == -1) {
	mlog("%s: Selector_register(%i) failed\n", __func__, sock);
	return -1;
    }
    return sock;
}

int srunOpenIOConnection(Step_t *step, int sock, char *sig)
{
    PS_DataBuffer_t data = { .buf = NULL };
    char port[100];
    PSnodes_ID_t nodeID = step->myNodeIndex;
    uint32_t i;

    if (sock <= 0) {
	/* open connection to waiting srun */
	snprintf(port, sizeof(port), "%u",
		    step->IOPort[nodeID % step->numIOPort]);

	if ((sock = tcpConnect(inet_ntoa(step->srun.sin_addr), port)) <0) {
	    mlog("%s: connection to srun '%s:%s' failed\n", __func__,
		    inet_ntoa(step->srun.sin_addr), port);
	    return 0;
	}

	mlog("%s: addr '%s:%s' sock '%u'\n", __func__,
		inet_ntoa(step->srun.sin_addr), port, sock);


	step->srunIOMsg.sock = sock;
    }

    setFDblock(sock, 0);

    addUint16ToMsg(IO_PROTOCOL_VERSION, &data);
    /* nodeid */
    addUint32ToMsg(nodeID, &data);

    /* stdout obj count */
    if (step->stdOutOpt == IO_SRUN || step->stdOutOpt == IO_SRUN_RANK ||
	step->pty) {
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
    if (step->pty || ((step->stdOut && strlen(step->stdOut) >0)) ||
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

    return 1;
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

    if ((Selector_register(step->srunIOMsg.sock, handleSrunMsg, step)) == -1) {
	mlog("%s: Selector_register(%i) srun I/O socket failed\n", __func__,
		step->srunIOMsg.sock);
    }
    enabled = 1;
}

int srunSendIO(uint16_t type, uint32_t taskid, Step_t *step,
                char *buf, uint32_t bufLen)
{
    int ret, error;

    ret = srunSendIOSock(type, taskid, step->srunIOMsg.sock, buf,
			    bufLen, &error);

    if (ret < 0 ) {
	switch (error) {
	    case ECONNRESET:
	    case EPIPE:
	    case EBADF:
		sendBrokeIOcon();
		freeSlurmMsg(&step->srunIOMsg);
		break;
	}
    }

    return ret;
}

int srunSendIOSock(uint16_t type, uint32_t taskid, int sock,
                char *buf, uint32_t bufLen, int *error)
{
    PS_DataBuffer_t data = { .buf = NULL };
    int ret = 0, once = 1;
    int32_t len, towrite, written;
    uint16_t headLen;
    char format[128];

    if (DEBUG_IO && bufLen>0) {
	snprintf(format, sizeof(format), "%s: msg: '%%.%is'\n", __func__,
		    bufLen);
	mdbg(PSSLURM_LOG_IO, format, buf);
    }

    if (sock == -1) return -1;

    towrite = bufLen;
    written = 0;
    errno = 0;

    while (once || towrite > 0) {
	len = towrite > 1000 ? 1000 : towrite;

	/* type (stdout/stderr) */
	addUint16ToMsg(type, &data);
	/* global taskid */
	addUint16ToMsg(taskid, &data);
	/* local taskid (unused) */
	addUint16ToMsg((uint16_t)NO_VAL, &data);
	/* msg length */
	addUint32ToMsg(len, &data);

	headLen = data.bufUsed;
	if (len>0) addMemToMsg(buf + written, len, &data);
	ret = doWriteF(sock, data.buf, data.bufUsed);
	data.bufUsed = once = 0;

	if (ret < 0) break;
	if (!ret) continue;

	ret -= headLen;
	written += ret;
	towrite -= ret;
	mdbg(PSSLURM_LOG_IO, "%s: fd '%i' ret :%i written :%i towrite: %i\n",
		__func__, sock, ret, written, towrite);
    }

    *error = errno;
    ufree(data.buf);

    return ret;
}

void getSockInfo(int socket, uint32_t *addr, uint16_t *port)
{
    struct sockaddr_in sock_addr;
    socklen_t len = sizeof(sock_addr);

    getpeername(socket, (struct sockaddr*)&sock_addr, &len);
    *addr = sock_addr.sin_addr.s_addr;
    *port = sock_addr.sin_port;
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
