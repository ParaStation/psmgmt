/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
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

/** maximal allowed length of a bitstring */
#define MAX_PACK_STR_LEN (16 * 1024 * 1024)

/** maximal number of parallel supported Slurm control daemons */
#define MAX_CTL_HOSTS 16

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
    bool redSize;	    /**< true if the message size was red */
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
    int i;
    for (i=0; i<ctlHostsCount; i++) {
	if (ctlHosts[i].id == id) return i;
    }
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
 * @param info Pointer to additional information passed to @a
 * cb
 *
 * @return Returns a pointer to the added connection
 */
static Connection_t *addConnection(int socket, Connection_CB_t *cb, void *info)
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
	if (!memToDataBuffer(sMsg->reply.buf, sMsg->reply.bufUsed,
			     &fw->body)) {
	    mlog("%s: error saving local result, caller %s at %i\n",
		 __func__, func, line);
	    return true;
	}
    } else {
	/* save message from other node */
	uint16_t i, saved = 0;
	for (i=0; i<fw->head.forward; i++) {
	    Slurm_Forward_Res_t *fwRes = &fw->head.fwRes[i];
	    /* test for double replies */
	    if (fwRes->node == srcNode) {
		mlog("%s: result for node %i already saved, caller %s "
		     "line %i\n", __func__, srcNode, func, line);
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
	    return true;
	}
    }
    return true;
}

void __handleFrwrdMsgReply(Slurm_Msg_t *sMsg, uint32_t error, const char *func,
			   const int line)
{
    if (!sMsg) {
	mlog("%s: invalid sMsg from %s at %i\n", __func__, func, line);
	return;
    }

    /* find open connection for forwarded message */
    Connection_t *con = findConnectionEx(sMsg->sock, sMsg->recvTime);
    if (!con) {
	mlog("%s: no connection to %s socket %i recvTime %zu type %s caller %s"
	     " at %i\n", __func__, PSC_printTID(sMsg->source), sMsg->sock,
	     sMsg->recvTime, msgType2String(sMsg->head.type), func, line);
	return;
    }

    Msg_Forward_t *fw = &con->fw;

    /* save the new result for a single node */
    if (!saveFrwrdMsgReply(sMsg, fw, error, func, line)) return;

    mdbg(PSSLURM_LOG_FWD, "%s(%s:%i): type %s forward %u resCount %u "
	    "source %s sock %i recvTime %zu\n", __func__, func, line,
	    msgType2String(sMsg->head.type), fw->head.forward, fw->numRes,
	    PSC_printTID(sMsg->source), sMsg->sock, sMsg->recvTime);

    /* test if all nodes replied and therefore the forward is complete */
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

	/* Answer the original RPC request from Slurm. This reply will hold
	 * results from all the nodes the RPC was forwarded to. */
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
	int eno = errno;

	if (ret < 0) {
	    if (size > 0 || eno == EAGAIN || eno == EINTR) {
		/* not all data arrived yet, lets try again later */
		dBuf->bufUsed += size;
		mdbg(PSSLURM_LOG_COMM, "%s: we try later for sock %u "
		     "read %zu\n", __func__, sock, size);
		return 0;
	    }
	    /* read error */
	    mwarn(eno, "%s: doReadExtP(%d, toRead %zd, size %zd)", __func__,
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
    int eno = errno;

    if (ret < 0) {
	if (size > 0 || eno == EAGAIN || eno == EINTR) {
	    /* not all data arrived yet, lets try again later */
	    dBuf->bufUsed += size;
	    mdbg(PSSLURM_LOG_COMM, "%s: we try later for sock %u read %zu\n",
		 __func__, sock, size);
	    return 0;
	}
	/* read error */
	mwarn(eno, "%s: doReadExtP(%d, toRead %zd, size %zd)", __func__, sock,
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

	processSlurmMsg(&sMsg, &con->fw, con->cb, con->info);
    }
    resetConnection(sock);

    if (error) {
	mdbg(ret ? -1 : PSSLURM_LOG_COMM, "%s: closeSlurmCon(%d)\n",
	     __func__, sock);
	closeSlurmCon(sock);
    }

    return 0;
}

bool registerSlurmSocket(int sock, Connection_CB_t *cb, void *info)
{
    Connection_t *con = addConnection(sock, cb, info);

    if (Selector_register(sock, readSlurmMsg, con) == -1) {
	mlog("%s: register socket %i failed\n", __func__, sock);
	return false;
    }
    return true;
}

/**
 * @brief Handle a reply from slurmctld
 *
 * Report errors for failed requests send to slurmctld and close
 * the corresponding connection.
 *
 * @param sMsg The reply message to handle
 *
 * @param info Additional info currently unused
 *
 * @return Always returns 0.
 */
static int handleSlurmctldReply(Slurm_Msg_t *sMsg, void *info)
{
    char **ptr = &sMsg->ptr;
    uint32_t rc;

    switch (sMsg->head.type) {
	case RESPONSE_SLURM_RC:
	    break;
	case RESPONSE_NODE_REGISTRATION:
	    handleSlurmdMsg(sMsg, info);
	    return 0;
	default:
	    flog("unexpected slurmctld reply %s(%i)\n",
		 msgType2String(sMsg->head.type), sMsg->head.type);
	    return 0;
    }

    /* return code */
    getUint32(ptr, &rc);
    if (rc != SLURM_SUCCESS) {
	flog("error: msg %s rc %u sock %i\n",
	     msgType2String(sMsg->head.type), rc, sMsg->sock);
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

int openSlurmctldCon(void)
{
    return openSlurmctldConEx(handleSlurmctldReply, NULL);
}

int openSlurmctldConEx(Connection_CB_t *cb, void *info)
{
    char *port = getConfValueC(&SlurmConfig, "SlurmctldPort");

    int i, sock = -1;
    for (i=0; i<ctlHostsCount; i++) {
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
    int ret;
    char *ptr;
    size_t towrite;

    ptr = data->buf + offset;
    towrite = data->bufUsed - offset;

    ret = PSCio_sendPProg(sock, ptr, towrite, written);
    int eno = errno;
    if (ret == -1) {
	mlog("%s: writing message of length %i failed, ret %i written %zu "
	     "caller %s " "line %i\n", __func__, data->bufUsed, ret, *written,
	     caller, line);
	errno = eno;
	return -1;
    }
    mdbg(PSSLURM_LOG_COMM, "%s: wrote data: %zu offset: %zu sock %i for "
	 "caller %s at %i\n", __func__, *written, offset, sock, caller, line);

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
    PS_DataBuffer_t payload = { .buf = NULL };
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

    fdbg(PSSLURM_LOG_PROTO, "msg(%i): %s, version %u\n",
	 head->type, msgType2String(head->type), head->version);

    /* connect to slurmctld */
    if (sock < 0) {
	sock = openSlurmctldCon();
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
    PSCio_setFDblock(sock, false);

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

int __sendSlurmReq(slurm_msg_type_t type, PS_SendDB_t *body,
		   Connection_CB_t *cb, void *info, const char *caller,
		   const int line)
{
    int sock = openSlurmctldConEx(cb, info);

    if (sock < 0) {
	flog("connection to slurmctld failed\n");
	return -1;
    }

    return __sendSlurmMsg(sock, type, body, caller, line);
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

bool hexBitstr2Array(char *bitstr, int **array, size_t *arraySize)
{
    size_t len;
    int32_t next, count = 0;

    if (!bitstr) {
	mlog("%s: invalid bitstring\n", __func__);
	return false;
    }

    if (!strncmp(bitstr, "0x", 2)) bitstr += 2;
    len = strlen(bitstr);

    *array = umalloc(len * 4 * sizeof(**array));
    *arraySize = 0;

    while (len--) {
	next = (int32_t) bitstr[len];

	if (!isxdigit(next)) {
	    ufree(*array);
	    return false;
	}

	if (isdigit(next)) {
	    next -= '0';
	} else {
	    next = toupper(next);
	    next -= 'A' - 10;
	}

	for (int32_t i = 1; i <= 8; i *= 2) {
	    if (next & i) (*array)[(*arraySize)++] = count;
	    count++;
	}
    }

    *array = urealloc(*array, *arraySize * sizeof(**array));

    return true;
}

bool hexBitstr2List(char *bitstr, char **list, size_t *listSize)
{
    int *array;
    size_t arraySize;

    if (!hexBitstr2Array(bitstr, &array, &arraySize)) return false;

    char tmp[1024];
    for (size_t i = 0; i < arraySize; i++) {
        if (*listSize) str2Buf(",", list, listSize);
        snprintf(tmp, sizeof(tmp), "%i", array[i]);
        str2Buf(tmp, list, listSize);
    }
    str2Buf("", list, listSize);

    ufree(array);

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

    mdbg(PSSLURM_LOG_COMM, "%s: got pty message for step %u:%u\n", __func__,
	 step->jobid, step->stepid);

    /* Shall be safe to do first a blocking read() inside a selector */
    uint16_t buffer[2];
    ssize_t ret = doRead(sock, buffer, sizeof(buffer));
    if (ret <= 0) {
	if (ret < 0) mwarn(errno, "%s: doRead()", __func__);
	mlog("%s: close pty connection\n", __func__);
	closeSlurmCon(sock);
	return 0;
    }

    if (ret != 4) {
	mlog("%s: update window size error, len %zu\n", __func__, ret);
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
	mlog("%s: task for rank %u of step %u:%u local ID %i not found\n",
	     __func__, rank, step->jobid, step->stepid, step->localNodeId);
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
    IO_Slurm_Header_t *ioh = NULL;
    uint16_t i;
    uint32_t myTaskIdsLen, *myTaskIds;
    bool pty;

    if (!verifyStepPtr(step)) {
	/* late answer from srun, associated step is already gone */
	fdbg(PSSLURM_LOG_IO, "no step for socket %i found\n", sock);
	goto ERROR;
    }

    /* Shall be safe to do first a blocking read() inside a selector */
    ret = doRead(sock, buffer, SLURM_IO_HEAD_SIZE);
    if (ret <= 0) {
	if (ret < 0) {
	    mwarn(errno, "%s: doRead()", __func__);
	}
	flog("close srun connection %i for step %u:%u\n", sock,
	     step->jobid, step->stepid);
	goto ERROR;
    }
    ptr = buffer;

    if (!unpackSlurmIOHeader(&ptr, &ioh)) {
	flog("unpack Slurm I/O header for step %u:%u failed\n",
	     step->jobid, step->stepid);
	goto ERROR;
    }

    if (!step->fwdata) {
	if (ioh->len) {
	    fdbg(PSSLURM_LOG_IO, "no forwarder for step %u:%u I/O"
		 " type %s len %u\n", step->jobid, step->stepid,
		 IO_strType(ioh->type), ioh->len);
	}
	goto ERROR;
    }
    pty = step->taskFlags & LAUNCH_PTY;
    fd = pty ? step->fwdata->stdOut[1] : step->fwdata->stdIn[1];
    myTaskIdsLen = step->globalTaskIdsLen[step->localNodeId];
    myTaskIds = step->globalTaskIds[step->localNodeId];

    mdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB,
	 "%s: step %u:%u stdin %d type %s length %u grank %u lrank %u pty %u"
	 " myTIDsLen %u\n", __func__, step->jobid, step->stepid, fd,
	 IO_strType(ioh->type), ioh->len, ioh->grank, ioh->lrank, pty,
	 myTaskIdsLen);

    if (ioh->type == SLURM_IO_CONNECTION_TEST) {
	if (ioh->len != 0) {
	    flog("invalid connection test for step %u:%u len %u\n",
		 step->jobid, step->stepid, ioh->len);
	}
	srunSendIO(SLURM_IO_CONNECTION_TEST, 0, step, NULL, 0);
    } else if (!ioh->len) {
	/* forward eof to all forwarders */
	flog("got eof of stdin %d for step %u:%u\n", fd,
	     step->jobid, step->stepid);

	if (ioh->type == SLURM_IO_STDIN) {
	    forwardInputMsg(step, ioh->grank, NULL, 0);
	} else if (ioh->type == SLURM_IO_ALLSTDIN) {
	    for (i=0; i<myTaskIdsLen; i++) {
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
	    /* @todo This shall be non-blocking! */
	    ret = doRead(sock, buffer, readNow);
	    if (ret <= 0) {
		mlog("%s: reading body failed\n", __func__);
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
		    for (i=0; i<myTaskIdsLen; i++) {
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
    char port[32];
    int sock;

    if (!step->numSrunPorts) {
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

    PSCio_setFDblock(sock, false);

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
    char *port = envGet(&step->env, "SLURM_PTY_PORT");
    if (!port) {
	mlog("%s: missing SLURM_PTY_PORT variable\n", __func__);
	return -1;
    }

    int sock = tcpConnect(inet_ntoa(step->srun.sin_addr), port);
    if (sock < 0) {
	mlog("%s: connection to srun %s:%s failed\n", __func__,
	     inet_ntoa(step->srun.sin_addr), port);
	return -1;
    }
    mlog("%s: pty connection (%i) to %s:%s\n", __func__, sock,
	 inet_ntoa(step->srun.sin_addr), port);
    step->srunPTYMsg.sock = sock;

    PSCio_setFDblock(sock, false);

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

    PSCio_setFDblock(sock, false);

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

    PSCio_sendP(sock, data.buf, data.bufUsed);

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
    int ret = 0, once = 1;
    int32_t towrite, written = 0;
    IO_Slurm_Header_t ioh;

    if (sock < 0) return -1;

    if (iohead->len > 0) {
	mdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB,
	     "%s: msg '%.*s'\n", __func__, iohead->len, buf);
    }

    towrite = iohead->len;
    errno = *error = 0;
    memcpy(&ioh, iohead, sizeof(IO_Slurm_Header_t));

    while (once || towrite > 0) {
	ioh.len = towrite > 1000 ? 1000 : towrite;

	packSlurmIOMsg(&data, &ioh, buf + written);
	ret = PSCio_sendF(sock, data.buf, data.bufUsed);

	data.bufUsed = once = 0;

	if (ret < 0) {
	    *error = errno;
	    return -1;
	}
	if (!ret) continue;

	ret -= SLURM_IO_HEAD_SIZE;
	written += ret;
	towrite -= ret;
	fdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB, "fd %i ret %i written %i"
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

		handleFrwrdMsgReply(&sMsg, SLURM_COMMUNICATIONS_CONNECTION_ERROR);
		mlog("%s: message error for node %i saved\n", __func__, nodeID);

		/* assuming nodes[] contains each node id only once */
		break;
	    }
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

    ctlHosts[ctlHostsCount].host = host;
    ctlHosts[ctlHostsCount].addr = addr;
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

    ctlHosts[ctlHostsCount].host = host;
    ctlHosts[ctlHostsCount].addr = addr;
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
    int i, numEntry = getConfValueI(&Config, "SLURM_CTLHOST_ENTRY_COUNT");

    for (i=0; i<numEntry; i++) {
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
	ctlHosts[ctlHostsCount].host = host;
	ctlHosts[ctlHostsCount].addr = addr;
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
    if ((openSlurmdSocket(ctlPort)) < 0) return false;

    /* register to slurmctld */
    ctlPort = getConfValueI(&SlurmConfig, "SlurmctldPort");
    if (ctlPort < 0) {
	addConfigEntry(&SlurmConfig, "SlurmctldPort", PSSLURM_SLURMCTLD_PORT);
    }
    sendNodeRegStatus(true);

    return true;
}
