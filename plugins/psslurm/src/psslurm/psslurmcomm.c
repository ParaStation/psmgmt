/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
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

#include "psserial.h"
#include "pluginconfig.h"
#include "pluginhelper.h"
#include "pluginmalloc.h"
#include "selector.h"
#include "pslog.h"
#include "psidcomm.h"

#include "psslurmcomm.h"

#define MAX_PACK_STR_LEN (16 * 1024 * 1024)

/** socket to listen for new Slurm connections */
static int slurmListenSocket = -1;

/** structure holding connection management data */
typedef struct {
    list_t next;	    /**< used to put into connection-list */
    PS_DataBuffer_t data;   /**< buffer for received message parts */
    Connection_CB_t *cb;    /**< function to handle received messages */
    int sock;		    /**< socket of the connection */
    time_t recvTime;	    /**< time first complete message was received */
    bool redSize;	    /**< true if the message size was red */
    Msg_Forward_t fw;	    /**< message forwarding structure */
} Connection_t;

/* list which holds all connections */
static LIST_HEAD(connectionList);

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
    con->data.bufSize = 0;
    con->data.bufUsed = 0;
    con->redSize = false;

    return true;
}

/**
 * @brief Add a new connection
 *
 * @param socket The socket of the connection to add
 *
 * @param cb The function to call to handle received messages
 *
 * @return Returns a pointer to the added connection
 */
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
}

void __saveFrwrdMsgRes(Slurm_Msg_t *sMsg, uint32_t error, const char *func,
		       const int line)
{
    Connection_t *con;
    Msg_Forward_t *fw;
    Slurm_Forward_Data_t *fwdata;
    uint16_t i, saved = 0;
    PSnodes_ID_t srcNode;

    if (!sMsg || !sMsg->outdata) {
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
    fw->numRes++;
    srcNode = PSC_getID(sMsg->source);

    if (srcNode == PSC_getMyID()) {
	/* save local processed message */
	fw->head.type = sMsg->head.type;
	if (!memToDataBuffer(sMsg->outdata->buf, sMsg->outdata->bufUsed,
			     &fw->body)) {
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
		fw->numRes--;
		return;
	    }
	    if (fwdata->node == -1) {
		fwdata->error = error;
		fwdata->type = sMsg->head.type;
		fwdata->node = srcNode;
		if (sMsg->outdata->bufUsed) {
		    if (!memToDataBuffer(sMsg->outdata->buf,
					 sMsg->outdata->bufUsed,
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
	    msgType2String(sMsg->head.type), fw->head.forward, fw->numRes,
	    PSC_printTID(sMsg->source), sMsg->sock, sMsg->recvTime);

    if (fw->numRes == fw->nodesCount + 1) {
	PS_SendDB_t msg = { .bufUsed = 0, .useFrag = false };

	/* all answers collected */
	mdbg(PSSLURM_LOG_FWD, "%s: forward %s complete, sending answer\n",
		__func__, msgType2String(sMsg->head.type));

	/* disable forwarding for the answer message */
	fw->head.forward = 0;

	if (!fw->body.buf || !fw->body.bufUsed) {
	    mlog("%s: invalid local data, dropping msg from caller %s at %i\n",
		 __func__, func, line);
	    closeSlurmCon(con->sock);
	    return;
	}

	msg.buf = fw->body.buf;
	msg.bufUsed = fw->body.bufUsed;
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
 * @brief Forward a Slurm message using RDP
 *
 * @param sMsg The Slurm message to forward
 *
 * @param fw The forward header of the connection
 *
 * @return Returns true on success otherwise false
 */
static bool slurmTreeForward(Slurm_Msg_t *sMsg, Msg_Forward_t *fw)
{
    PSnodes_ID_t *nodes = NULL;
    uint32_t i, nrOfNodes, localId;
    bool verbose = logger_getMask(psslurmlogger) & PSSLURM_LOG_FWD;
    struct timeval time_start, time_now, time_diff;

    /* no forwarding active for this message? */
    if (!sMsg->head.forward) return true;

    /* convert nodelist to PS nodes */
    if (!getNodesFromSlurmHL(fw->head.nodeList, &nrOfNodes, &nodes, &localId)) {
	mlog("%s: resolving PS nodeIDs from %s failed\n", __func__,
	     fw->head.nodeList);
	return false;
    }

    /* save forward information in connection, has to be
       done *before* sending any messages  */
    fw->nodes = nodes;
    fw->nodesCount = nrOfNodes;
    fw->head.forward = sMsg->head.forward;
    fw->head.returnList = sMsg->head.returnList;
    fw->head.fwSize = sMsg->head.forward;
    fw->head.fwdata =
	umalloc(sMsg->head.forward * sizeof(Slurm_Forward_Data_t));

    for (i=0; i<sMsg->head.forward; i++) {
	fw->head.fwdata[i].error = SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	fw->head.fwdata[i].type = RESPONSE_FORWARD_FAILED;
	fw->head.fwdata[i].node = -1;
	fw->head.fwdata[i].body.buf = NULL;
	fw->head.fwdata[i].body.bufUsed = 0;
    }

    if (verbose) {
	gettimeofday(&time_start, NULL);

	mlog("%s: forward type %s count %u nodelist %s timeout %u "
	     "at %.4f\n", __func__, msgType2String(sMsg->head.type),
	     sMsg->head.forward, fw->head.nodeList, fw->head.timeout,
	     time_start.tv_sec + 1e-6 * time_start.tv_usec);
    }

    /* use RDP to send the message to other nodes */
    int ret = forwardSlurmMsg(sMsg, nrOfNodes, nodes);

    if (verbose) {
	gettimeofday(&time_now, NULL);
	timersub(&time_now, &time_start, &time_diff);
	mlog("%s: forward type %s of size %u took %.4f seconds\n", __func__,
	     msgType2String(sMsg->head.type), ret,
	     time_diff.tv_sec + 1e-6 * time_diff.tv_usec);

    }

    return true;
}

/**
 * @brief Read a Slurm message
 *
 * Read a Slurm message from the provided socket. In the first step the size of
 * the message is red. And secondly the actual message payload is red. All
 * reading is done in a non blocking fashion. If reading of the message was
 * successful it will be saved in a new Slurm message and given to the provided
 * callback of the Connection. After the callback handled the message or an
 * error occurred the Connection will be resetted.
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
    uint32_t msglen = 0;
    int ret;
    bool error = false;
    size_t size = 0, toRead;
    char *ptr;

    if (!param) {
	mlog("%s: invalid connection data buffer\n", __func__);
	closeSlurmCon(sock);
	return 0;
    }

    if (dBuf->bufSize && dBuf->bufSize == dBuf->bufUsed) {
	mlog("%s: data buffer for sock %i already in use, resetting\n",
	     __func__, sock);
	resetConnection(sock);
    }

    /* try to read the message size */
    if (!con->redSize) {
	if (!dBuf->bufSize) {
	    dBuf->buf = umalloc(sizeof(uint32_t));
	    dBuf->bufSize = sizeof(uint32_t);
	    dBuf->bufUsed = 0;
	}

	ptr = dBuf->buf + dBuf->bufUsed;
	toRead = dBuf->bufSize - dBuf->bufUsed;
	ret = doReadExtP(sock, ptr, toRead, &size);

	if (ret < 0) {
	    if (size > 0 || errno == EAGAIN || errno == EINTR) {
		/* not all data arrived yet, lets try again later */
		dBuf->bufUsed += size;
		mdbg(PSSLURM_LOG_COMM, "%s: we try later for sock %u "
		     "read %zu\n", __func__, sock, size);
		return 0;
	    }
	    /* read error */
	    mwarn(errno, "%s: doReadExtP(%d, toRead %zd, size %zd)", __func__,
		  sock, toRead, size);
	    error = true;
	    goto CALLBACK;
	} else if (!ret) {
	    /* connection reset */
	    mdbg(PSSLURM_LOG_COMM, "%s: closing connection, empty message "
		    "len on sock %i\n", __func__, sock);
	    error = true;
	    goto CALLBACK;
	} else {
	    /* all data red successful */
	    dBuf->bufUsed += size;
	    mdbg(PSSLURM_LOG_COMM,
		 "%s: msg size read for %u ret %u toread %zu msglen %u size "
		 "%zu\n", __func__, sock, ret, toRead, msglen, size);
	}

	msglen = ntohl(*(uint32_t *) dBuf->buf);
	if (msglen > MAX_MSG_SIZE) {
	    mlog("%s: msg too big %u (max %u)\n", __func__, msglen,
		 MAX_MSG_SIZE);
	    error = true;
	    goto CALLBACK;
	}

	dBuf->buf = urealloc(dBuf->buf, msglen);
	dBuf->bufSize = msglen;
	dBuf->bufUsed = 0;
	con->redSize = true;
    }

    /* try to read the actual payload (missing data) */
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

	/* forward the message using RDP */
	if (slurmTreeForward(&sMsg, &con->fw)) {

	    /* let the callback handle the message */
	    con->cb(&sMsg);
	}
    }
    resetConnection(sock);

    if (error) {
	mdbg(ret ? -1 : PSSLURM_LOG_COMM, "%s: closeSlurmCon(%d)\n",
	     __func__, sock);
	closeSlurmCon(sock);
    }

    return 0;
}

/**
 * @brief Register a Slurm socket
 *
 * Add a Slurm connection for a socket and monitor it for incoming
 * data.
 *
 * @param sock The socket to register
 *
 * @param cb The function to call to handle received messages
 */
static void registerSlurmSocket(int sock, Connection_CB_t *cb)
{
    Connection_t *con;

    con = addConnection(sock, cb);

    if (Selector_register(sock, readSlurmMsg, con) == -1) {
	mlog("%s: register socket %i failed\n", __func__, sock);
    }
}

/**
 * @brief Handle a reply from slurmctld
 *
 * Report errors for failed requests send to slurmctld and close
 * the corresponding connection.
 *
 * @param sMsg The reply message to handle
 *
 * @return Always returns 0.
 */
static int handleSlurmctldReply(Slurm_Msg_t *sMsg)
{
    char **ptr;
    uint32_t rc;

    if (!extractSlurmAuth(sMsg)) return 0;

    ptr = &sMsg->ptr;
    getUint32(ptr, &rc);

    if (rc != SLURM_SUCCESS) {
	mdbg(PSSLURM_LOG_PROTO, "%s: error: msg %s rc %u sock %i\n",
		__func__, msgType2String(sMsg->head.type), rc, sMsg->sock);
    }

    if (sMsg->source == -1) {
	closeSlurmCon(sMsg->sock);
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

int connect2Slurmctld(void)
{
    int sock = -1, len;
    char *addr, *port;

    port = getConfValueC(&SlurmConfig, "SlurmctldPort");
    if (!(addr = getConfValueC(&SlurmConfig, "ControlAddr"))) {
	addr = getConfValueC(&SlurmConfig, "ControlMachine");
    }

    if (!addr || !port) {
	mlog("%s: invalid control address %s or port %s\n",
	     __func__, addr, port);
	return sock;
    }

    if (addr[0] == '"') addr++;
    len = strlen(addr);
    if (addr[len-1] == '"') addr[len-1] = '\0';

    /* connect to main controller */
    sock = tcpConnect(addr, port);
    if (sock < 0) {
	/* try to connect to backup controller */
	if (!(addr = getConfValueC(&SlurmConfig, "BackupAddr"))) {
	    addr = getConfValueC(&SlurmConfig, "BackupController");
	}
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

    registerSlurmSocket(sock, handleSlurmctldReply);

    return sock;
}

int __sendDataBuffer(int sock, PS_SendDB_t *data, size_t offset,
		     size_t *written, const char *caller, const int line)
{
    int ret;
    char *ptr;
    size_t towrite;

    ptr = data->buf + offset;
    towrite = data->bufUsed - offset;

    ret = doWriteExP(sock, ptr, towrite, written);
    if (ret == -1) {
	mlog("%s: writing message of length %i failed, ret %i written %zu "
	     "caller %s " "line %i\n", __func__, data->bufUsed, ret, *written,
	     caller, line);
	return -1;
    }
    mdbg(PSSLURM_LOG_COMM, "%s: wrote data: %zu offset: %zu for caller %s "
	 "at %i\n", __func__, *written, offset, caller, line);

    return ret;
}

int __sendSlurmMsg(int sock, slurm_msg_type_t type, PS_SendDB_t *body,
		    const char *caller, const int line)
{
    Slurm_Msg_Header_t head;

    initSlurmMsgHead(&head);
    head.type = type;

    return __sendSlurmMsgEx(sock, &head, body, caller, line);
}

int __sendSlurmMsgEx(int sock, Slurm_Msg_Header_t *head, PS_SendDB_t *body,
		     const char *caller, const int line)
{
    PS_SendDB_t data = { .bufUsed = 0, .useFrag = false };
    PS_DataBuffer_t payload = { .bufUsed = 0 };
    int ret = 0;
    size_t written = 0;

    if (!head) {
	mlog("%s: invalid header from %s at %i\n", __func__, caller, line);
	return -1;
    }

    if (!body) {
	mlog("%s: invalid body from %s at %i\n", __func__, caller, line);
	return -1;
    }

    Slurm_Auth_t *auth = getSlurmAuth();
    if (!auth) {
	flog("getting a slurm authentication token failed\n");
	return -1;
    }

    /* connect to slurmctld */
    if (sock < 0) {
	sock = connect2Slurmctld();
	if (sock < 0) {
	    freeSlurmAuth(auth);
	    if (needMsgResend(head->type)) {
		Slurm_Msg_Buf_t *savedMsg;

		savedMsg = saveSlurmMsg(head, body, NULL, -1, 0);
		if (setReconTimer(savedMsg) == -1) {
		    mlog("%s: setting resend timer failed\n", __func__);
		    deleteMsgBuf(savedMsg);
		    return -1;
		}
		return -2;
	    }
	    return -1;
	}
    }

    /* non blocking write */
    setFDblock(sock, false);

    memToDataBuffer(body->buf, body->bufUsed, &payload);
    packSlurmMsg(&data, head, &payload, auth);

    ret = __sendDataBuffer(sock, &data, 0, &written, caller, line);
    if (ret == -1) {
	if (!written) {
	    mlog("%s: sending msg type %s failed\n", __func__,
		 msgType2String(head->type));
	    return -1;
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

    mdbg(PSSLURM_LOG_COMM, "%s: from %s:%u socket:%i\n", __func__,
	 inet_ntoa(SAddr.sin_addr), ntohs(SAddr.sin_port), newSock);

    registerSlurmSocket(newSock, handleSlurmdMsg);

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
	closeSlurmCon(sock);
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
	     "%s: rank %u len %u msg.header.len %zu to %s\n", __func__,
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
    myTaskIdsLen = step->globalTaskIdsLen[step->localNodeId];
    myTaskIds = step->globalTaskIds[step->localNodeId];

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
	if (!pty) closeSlurmCon(fd);
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
		if (step->leader) doWriteP(fd, buffer, ret);
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
    closeSlurmCon(sock);
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
	     step->srunPorts[step->localNodeId % step->numSrunPorts]);
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
		PS_SendDB_t *body)
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
	 "%s: sock %u, len: body.bufUsed %u\n", __func__, sock, body->bufUsed);

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
    PS_SendDB_t data = { .bufUsed = 0, .useFrag = false };
    PSnodes_ID_t nodeID = step->localNodeId;
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
	myTaskIdsLen = step->globalTaskIdsLen[step->localNodeId];
	myTaskIds = step->globalTaskIds[step->localNodeId];

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
    PS_SendDB_t data = { .bufUsed = 0, .useFrag = false };
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
	    return -1;
	}
	if (!ret) continue;

	ret -= SLURM_IO_HEAD_SIZE;
	written += ret;
	towrite -= ret;
	mdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB, "%s: fd %i ret %i written %i"
	     " towrite %i\n", __func__, sock, ret, written, towrite);
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
