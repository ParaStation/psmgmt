/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
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
	case RESPONSE_SLURM_RC:
	    return "RESPONSE_SLURM_RC";
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
    }
    return "";
}

void initConnectionList()
{
    INIT_LIST_HEAD(&ConnectionList.list);
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

void closeConnection(int socket)
{
    Connection_t *con;

    /* close the connection */
    if (Selector_isRegistered(socket)) Selector_remove(socket);
    close(socket);

    /* free memory */
    if ((con = findConnection(socket))) {
	list_del(&con->list);
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

    con->cb(sock, dBuf->buf, dBuf->bufUsed, error);
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

static void addSlurmHeader(slurm_msg_type_t type, PS_DataBuffer_t *data,
			    uint32_t bodyLen)
{
    uint16_t flags = 0;

    addUint16ToMsg(SLURM_14_03_PROTOCOL_VERSION, data);
    flags |= SLURM_GLOBAL_AUTH_KEY;
    addUint16ToMsg(flags, data);
    addUint16ToMsg(type, data);
    /* body len */
    addUint32ToMsg(bodyLen, data);
    /* forward */
    addUint16ToMsg(0, data);
    /* return list */
    addUint16ToMsg(0, data);
    /* addr/port */
    addUint32ToMsg(0, data);
    addUint16ToMsg(0, data);
}

static int handleSlurmctldReply(int sock, void *data, size_t len, int error)
{
    Slurm_msg_header_t msgHead;
    char *ptr;
    uint32_t rc;

    if (error) return 0;

    ptr = data;
    getSlurmMsgHeader(sock, &ptr, &msgHead);
    getUint32(&ptr, &rc);

    mdbg(PSSLURM_LOG_PROTO, "%s: type '%s' rc '%u'\n", __func__,
	    msgType2String(msgHead.type), rc);

    /* TODO handle the message */

    closeConnection(sock);
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

static int connect2Slurmctld(void *data)
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

int sendSlurmMsg(int sock, slurm_msg_type_t type, PS_DataBuffer_t *body,
		    void *sockData)
{
    PS_DataBuffer_t data = { .buf = NULL };
    uint32_t msgLen;
    int ret;

    /* connect to slurmctld */
    if (sock < 0) {
	if ((sock = connect2Slurmctld(sockData))<0) {
	    /* TODO */
	    mlog("%s: fixme: need to retry later!!!\n", __func__);
	    return 0;
	}
    }

    /* add message header */
    addSlurmHeader(type, &data, body->bufUsed);
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

int srunOpenControlConnection(Step_t *step)
{
    char port[256];
    int sock;

    if (step->numSrunPorts <= 0) {
	mlog("%s: sending failed, no srun ports available\n", __func__);
	return -1;
    }

    snprintf(port, sizeof(port), "%u", step->srunPorts[0]);
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
    return sendSlurmMsg(sock, type, body, step);
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
    step->srunPTYSock = sock;

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
    int sock; //, ret;
    PSnodes_ID_t nodeID = 0;
    uint32_t i;

    /* find my job local nodeid */
    for (i=0; i<step->nrOfNodes; i++) {
	if (step->nodes[i] == PSC_getMyID()) {
	    break;
	}
	nodeID++;
    }

    /* open connection to waiting srun */
    snprintf(port, sizeof(port), "%u", step->IOPort[0]);

    if ((sock = tcpConnect(inet_ntoa(step->srun.sin_addr), port)) <0) {
	mlog("%s: connection to srun '%s:%s' failed\n", __func__,
		inet_ntoa(step->srun.sin_addr), port);
	return 0;
    }
    mlog("%s: addr '%s:%s' sock '%u'\n", __func__,
	    inet_ntoa(step->srun.sin_addr), port, sock);

    step->srunIOSock = sock;
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
			ret += doWriteP(step->srunIOSock, data.buf, data.bufUsed);
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
    ret = doWriteP(step->srunIOSock, data.buf, data.bufUsed);
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
    if (step->srunIOSock > -1) {
	closeConnection(step->srunIOSock);
    }

    if (step->srunControlSock > -1) {
	closeConnection(step->srunControlSock);
    }

    if (step->srunPTYSock > -1) {
	closeConnection(step->srunPTYSock);
    }
}
