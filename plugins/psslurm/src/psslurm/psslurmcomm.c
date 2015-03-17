/*
 * ParaStation
 *
 * Copyright (C) 2014 - 2015 ParTec Cluster Competence Center GmbH, Munich
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
#include "psslurmenv.h"
#include "psslurmpscomm.h"

#include "plugincomm.h"
#include "pluginconfig.h"
#include "pluginhelper.h"
#include "pluginmalloc.h"
#include "selector.h"

#include "psslurmcomm.h"

#define MAX_PACK_STR_LEN        (16 * 1024 * 1024)

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

void initConnectionList()
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
	ufree(con->fw.nodes);
	ufree(con->fw.body.buf);
	ufree(con->data.buf);
	ufree(con);
    }
}

void clearConnections()
{
    list_t *pos, *tmp;
    Connection_t *con;

    list_for_each_safe(pos, tmp, &ConnectionList.list) {
	if (!(con = list_entry(pos, Connection_t, list))) return;
	closeConnection(con->sock);
    }
}

void saveForwardedMsgRes(Slurm_Msg_t *sMsg, PS_DataBuffer_t *data,
			    uint32_t error)
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

    mdbg(PSSLURM_LOG_FWD, "%s: type '%s' forward '%u' resCount '%u' "
	    "source '%s'\n", __func__,
	    msgType2String(sMsg->head.type), fw->head.forward, fw->res,
	    PSC_printTID(sMsg->source));

    if (fw->res == fw->nodesCount + 1) {
	mdbg(PSSLURM_LOG_FWD, "%s: forward '%s' complete, sending answer\n",
		__func__, msgType2String(sMsg->head.type));

	fw->head.forward = 0;
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

int registerSlurmMessage(int sock, Connection_CB_t *cb)
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

    ptr = &sMsg->ptr;
    getUint32(ptr, &rc);

    mdbg(PSSLURM_LOG_PROTO, "%s: type '%s' rc '%u'\n", __func__,
	    msgType2String(sMsg->head.type), rc);

    /* TODO handle the message */
    if (sMsg->source == -1) {
	closeConnection(sMsg->sock);
    }
    return 0;
}

int tcpConnect(char *addr, char *port)
{
/* number of reconnect tries until we give up */
#define TCP_CONNECTION_RETRYS 10

    struct addrinfo *result, *rp;
    int sock = -1, ret, reConnect = 0;

TCP_RECONNECT:

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
	    mwarn(errno, "%s: socket failed, port '%s' addr '%s' ", __func__,
		    port, addr);
	    continue;
	}

	if ((connect(sock, rp->ai_addr, rp->ai_addrlen)) == -1) {
	    close(sock);
	    if (errno != EINTR) {
		mwarn(errno, "%s: connect failed, port '%s' addr '%s' ",
			__func__, port, addr);
	    }
	    continue;
	}
	break;
    }
    freeaddrinfo(result);

    if (rp == NULL) {
	if (errno == EISCONN && reConnect < TCP_CONNECTION_RETRYS) {
	    close(sock);
	    reConnect++;
	    goto TCP_RECONNECT;
	}
	mwarn(errno, "%s: failed(%i) addr '%s' port '%s' ", __func__,
	    errno, addr, port);
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
    //mlog("%s: connect to %s socket %i\n", __func__, addr, sock);

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
    //char *tmp;

    getUint32(ptr, &len);
    //mlog("%s: len1 '%u'\n", __func__, len);

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
    //mlog("%s: len2 '%u'\n", __func__, len);

    if (len > 0) {
	*bitStr = umalloc(len);
	memcpy(*bitStr, *ptr, len);
	*ptr += len;

	/* TODO convert to bitstr?? */
	//*bitStr = (bitstr_t *) tmp;
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
    if (step->fwdata && kill(step->fwdata->childPid, SIGWINCH)) {
	if (errno == ESRCH) return 0;
	mlog("%s: error sending SIGWINCH to '%i'\n", __func__,
	step->fwdata->childPid);
    }
    return 0;
}

static int handleSrunMsg(int sock, void *data)
{
    Step_t *step = data;
    char *ptr, buffer[1024];
    int ret, headSize, readnow, fd = -1;
    size_t toread;
    uint16_t type, gtid, ltid;
    uint32_t lenght;

    headSize = sizeof(uint32_t) + 3 * sizeof(uint16_t);
    if ((ret = doReadP(sock, buffer, headSize)) <= 0) {
	//mlog("%s: closing srun connection '%u'\n", __func__, sock);
	closeConnection(sock);
	return 0;
    }
    ptr = buffer;
    if (step->nodes[0] == PSC_getMyID()) {
	if (!step->fwdata) {
	    mlog("%s: no forwarder running\n", __func__);
	    closeConnection(sock);
	    return 0;
	}

	fd = (step->pty) ? step->fwdata->stdOut[1] : step->fwdata->stdIn[1];
    }

    /* type */
    getUint16(&ptr, &type);
    /* global taskid */
    getUint16(&ptr, &gtid);
    /* local taskid */
    getUint16(&ptr, &ltid);
    /* lenght */
    getUint32(&ptr, &lenght);

    /*
    mlog("%s: step '%u:%u' stdin '%u' type '%u' lenght '%u' gtid '%u' "
	    "ltid '%u' pty:%u\n", __func__, step->jobid, step->stepid, fd,
	    type, lenght, gtid, ltid, step->pty);
    */

    if (type == SLURM_IO_CONNECTION_TEST) {
	if (lenght != 0) {
	    mlog("%s: invalid connection test, lenght '%u'\n", __func__,
		    lenght);
	}
	mlog("%s: got connection test\n", __func__);
	srunSendIO(SLURM_IO_CONNECTION_TEST, step, NULL, 0);
    } else if (!lenght) {
	mlog("%s: got eof of stdin '%u'\n", __func__, fd);
	if (!step->pty && (step->nodes[0] == PSC_getMyID())) closeConnection(fd);
    } else {
	/* read stdin message from srun and write to child pty */
	toread = lenght;
	while (toread > 0) {
	    readnow = (toread > (int) sizeof(buffer)) ? sizeof(buffer) : toread;
	    if ((ret = doRead(sock, buffer, readnow)) <= 0) {
		mlog("%s: reading body failed\n", __func__);
		break;
	    }
	    if (step->nodes[0] == PSC_getMyID()) doWriteP(fd, buffer, ret);
	    toread -= ret;
	}
    }

    return 0;
}

static PSnodes_ID_t getJobLocalNodeID(Step_t *step)
{
    PSnodes_ID_t nodeID = 0;
    uint32_t i;

    /* find my job local nodeid */
    for (i=0; i<step->nrOfNodes; i++) {
	if (step->nodes[i] == PSC_getMyID()) {
	    break;
	}
	nodeID++;
    }

    return nodeID;
}

int srunOpenControlConnection(Step_t *step)
{
    char port[256];
    int sock;
    PSnodes_ID_t nodeID;

    if (step->numSrunPorts <= 0) {
	mlog("%s: sending failed, no srun ports available\n", __func__);
	return -1;
    }

    nodeID = getJobLocalNodeID(step);
    snprintf(port, sizeof(port), "%u",
		step->srunPorts[nodeID % step->numSrunPorts]);
    if ((sock = tcpConnect(inet_ntoa(step->srun.sin_addr), port)) <0) {
	mlog("%s: connection to srun '%s:%s' failed\n", __func__,
		inet_ntoa(step->srun.sin_addr), port);
	return -1;
    }
    //mlog("%s: new srun connection %i\n", __func__, sock);

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

    /*
    mlog("%s: sock %u, len: body.bufUsed %u body.bufSize %u\n", __func__,
	    sock, body->bufUsed, body->bufSize);
    */
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

    if ((Selector_register(sock, handleSrunPTYMsg, step)) == -1) {
	mlog("%s: Selector_register(%i) failed\n", __func__, sock);
	return -1;
    }
    return sock;
}

int srunOpenIOConnection(Step_t *step)
{
    PS_DataBuffer_t data = { .buf = NULL };
    char port[100];
    int sock;
    PSnodes_ID_t nodeID = 0;

    /* open connection to waiting srun */
    nodeID = getJobLocalNodeID(step);
    snprintf(port, sizeof(port), "%u", step->IOPort[nodeID % step->numIOPort]);

    if ((sock = tcpConnect(inet_ntoa(step->srun.sin_addr), port)) <0) {
	mlog("%s: connection to srun '%s:%s' failed\n", __func__,
		inet_ntoa(step->srun.sin_addr), port);
	return 0;
    }
    mlog("%s: addr '%s:%s' sock '%u'\n", __func__,
	    inet_ntoa(step->srun.sin_addr), port, sock);

    step->srunIOMsg.sock = sock;
    if ((Selector_register(sock, handleSrunMsg, step)) == -1) {
	mlog("%s: Selector_register(%i) failed\n", __func__, sock);
	return 0;
    }

    addUint16ToMsg(IO_PROTOCOL_VERSION, &data);
    /* nodeid */
    addUint32ToMsg(nodeID, &data);

    /* stdout obj count */
    if ((step->stdOut && strlen(step->stdOut) >0) ||
	(step->nodes[0] != PSC_getMyID())) {
	addUint32ToMsg(0, &data);
    } else {
	addUint32ToMsg(1, &data);
    }

    /* stderr obj count */
    if (step->pty || (step->stdOut && strlen(step->stdOut) >0) ||
	(step->stdErr && strlen(step->stdErr) > 0) ||
	(step->nodes[0] != PSC_getMyID())) {
	/* stderr uses stdout in pty mode */
	addUint32ToMsg(0, &data);
    } else {
	addUint32ToMsg(1, &data);
    }

    /* io key */
    addUint32ToMsg((uint32_t) SLURM_IO_KEY_SIZE, &data);
    addMemToMsg(step->cred->sig, (uint32_t) SLURM_IO_KEY_SIZE, &data);

    doWriteP(sock, data.buf, data.bufUsed);
    ufree(data.buf);

    //mlog("%s: ret:%i used:%i\n", __func__, ret, data.bufUsed);
    return 1;
}

int srunSendIO(uint16_t type, Step_t *step, char *buf, uint32_t bufLen)
{
    PS_DataBuffer_t data = { .buf = NULL };
    uint16_t taskid = 0;
    int id, ret = 0;
    char *sendBuf = buf, *start;
    const char delimiters[] ="\n";
    char *next, *saveptr;

    if ((bufLen > 3) && step->labelIO && (buf[0] == '[')) {

	next = strtok_r(buf, delimiters, &saveptr);

	while (next) {
	    if ((sscanf(next, "[%u]", &id)) == 1) {
		taskid = id;
		if ((start = strchr(next, ' ')) && start++) {
		    if (start) {
			sendBuf = start;
			bufLen = strlen(sendBuf);

			addUint16ToMsg(type, &data);
			/* gtaskid */
			addUint16ToMsg(taskid, &data);
			/* ltaskid */
			addUint16ToMsg(0, &data);
			addUint32ToMsg(bufLen, &data);

			addMemToMsg(sendBuf, bufLen, &data);
			ret += doWriteP(step->srunIOMsg.sock, data.buf, data.bufUsed);
			ufree(data.buf);
			data.buf = NULL;
		    } else {
			mlog("%s: empty i/o\n", __func__);
		    }
		} else {
		    mlog("%s: label error, missing space '%s'\n", __func__, buf);
		    goto SRUN_IO_DEFAULT;
		}
	    } else {
		mlog("%s: label error, scanf failed '%s'\n", __func__, buf);
		goto SRUN_IO_DEFAULT;
	    }
	    next = strtok_r(NULL, delimiters, &saveptr);
	}
	ufree(data.buf);
	return ret;

    }

SRUN_IO_DEFAULT:

    addUint16ToMsg(type, &data);
    /* gtaskid */
    addUint16ToMsg(0, &data);
    /* ltaskid */
    addUint16ToMsg(0, &data);
    addUint32ToMsg(bufLen, &data);

    if (bufLen >0) addMemToMsg(sendBuf, bufLen, &data);
    ret = doWriteP(step->srunIOMsg.sock, data.buf, data.bufUsed);
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
    freeSlurmMsg(&step->srunIOMsg);
    freeSlurmMsg(&step->srunControlMsg);
    freeSlurmMsg(&step->srunPTYMsg);
}
