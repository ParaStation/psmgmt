/*
 * ParaStation
 *
 * Copyright (C) 2022-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file RRComm functionality running either in the psidforwarder
 * process itself or being executed while setting it up
 */
#include "rrcommforwarder.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "list.h"
#include "pscio.h"
#include "pscommon.h"
#include "pslog.h"
#include "psprotocol.h"
#include "psserial.h"
#include "pspluginprotocol.h"
#include "selector.h"
#include "timer.h"

#include "psidcomm.h"
#include "psidforwarder.h"
#include "psidhook.h"
#include "psidmsgbuf.h"

#include "rrcomm_common.h"
#include "rrcommaddrcache.h"
#include "rrcommlog.h"
#include "rrcommproto.h"

/** Socket listening for connecting clients */
static int listenSock = -1;

/** All information on the client to spawn used within the fragment header */
static RRComm_hdr_t clntHdr;

/** Timer ensuring premature data does not pile up indefinitely */
static int startupTimer = -1;

static int closeListenSock(void)
{
    if (listenSock == -1) return 0;
    if (Selector_isRegistered(listenSock)) Selector_remove(listenSock);
    close(listenSock);
    listenSock = -1;
    return -1;
}

/**
 * @brief Function hooked to PSIDHOOK_EXEC_FORWARDER
 *
 * This function prepares for the execution of the psidforwarder (and
 * client) processes. It is used to prepare a coherent setup between
 * the two process. In detail it will:
 *  - Create an abstract UNIX socket for the psidforwarder to listen to
 *  - Store the name of the socket to the environment
 *  - Store proto-task information on the client to spawn into clntHdr
 *
 * @param data Pointer to the task structure to spawn
 *
 * @return On success 0 is returned or -1 in case of failure
 */
static int hookExecForwarder(void *data)
{
    PStask_t *client = data;
    /* no RRComm in service processes */
    if (!client || client->rank < 0 || client->group != TG_ANY) return 0;

    listenSock = socket(PF_UNIX, SOCK_STREAM, 0);
    if (listenSock < 0) {
	mwarn(errno, "%s: socket()", __func__);
	return -1;
    }

    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path + 1, sizeof(sa.sun_path) - 1, "rrcomm_%d", getpid());

    if (bind(listenSock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
	mwarn(errno, "%s: bind()", __func__);
	return closeListenSock();
    }

    if (listen(listenSock, 20) < 0) {
	mwarn(errno, "%s: listen()", __func__);
	return closeListenSock();
    }

    /* put abstract socket's name to environment for the client to find it */
    setenv(RRCOMM_SOCKET_ENV, sa.sun_path + 1, 1);

    fdbg(RRCOMM_LOG_FRWRD, "client socket (%d) created at '%s'\n",
	 listenSock, sa.sun_path + 1);

    /* setup common header from proto-task information */
    clntHdr.sender = client->jobRank;
    clntHdr.loggerTID = client->loggertid;
    clntHdr.spawnerTID = client->spawnertid;

    /* initialize the address cache */
    initAddrCache(client->spawnertid);

    return 0;
}

/**
 * @brief Function hooked to PSIDHOOK_EXEC_CLIENT
 *
 * This function prepares for the execution of the client process. In
 * detail it will:
 *  - Cleanup the socket descriptor the psidforwarder process will listen on
 *
 * @param data Pointer to the task structure to spawn (ignored)
 *
 * @return Always return 0
 */
static int hookExecClient(void *data)
{
    closeListenSock();
    return 0;
}

/** Socket connected to current client */
static int clntSock = -1;

/** Track if ever a client has connected */
static bool hasConnected = false;

/** forward declaration */
static int closeClientSock(void);

/**
 * Protocol version the client's lib is capable to handle; negotiated
 * while accepting the client in @ref acceptNewClient()
 */
static uint32_t clntVer = 0;

/**
 * @brief Handle client's message
 *
 * Handle a message received from the connected client. The actual
 * message is shoveled in chunks into the fragmentation
 * layer. Fragments are sent to the local daemon to determine the
 * routing or preferably to the actual destination if corresponding
 * information is available.
 *
 * @param fd Socket descriptor connecting to the client process
 *
 * @param data Additional data passed to @ref Selector_register() (ignored)
 *
 * @return Always return 0
 */
static int handleClientMsg(int fd, void *data)
{
    /* we rely on blocking receives from the client */
    bool blocked = PSCio_setFDblock(clntSock, true);

    ssize_t ret = PSCio_recvBufP(fd, &clntHdr.dest, sizeof(clntHdr.dest));
    if (ret != sizeof(clntHdr.dest)) return closeClientSock();

    clntHdr.destJob = 0;
    if (clntVer > 1) {
	ret = PSCio_recvBufP(fd, &clntHdr.destJob, sizeof(clntHdr.destJob));
	if (ret != sizeof(clntHdr.destJob)) return closeClientSock();
    }
    if (!clntHdr.destJob) clntHdr.destJob = clntHdr.spawnerTID;

    size_t msgSize;
    ret = PSCio_recvBufP(fd, &msgSize, sizeof(msgSize));
    if (ret != sizeof(msgSize)) return closeClientSock();

    /* setup frag message */
    PS_SendDB_t fBuf;
    initFragBufferExtra(&fBuf, PSP_PLUG_RRCOMM, RRCOMM_DATA,
			&clntHdr, sizeof(clntHdr));

    PStask_ID_t dest = getAddrFromCache(clntHdr.destJob, clntHdr.dest);
    setFragDest(&fBuf, dest != -1 ? dest : PSC_getTID(PSC_getMyID(), 0));

    /* shovel data in 32 kB chunks */
    char buf[32*1024];
    bool first = true;
    while (msgSize) {
	ssize_t chunk = msgSize > sizeof(buf) ? sizeof(buf) : msgSize;
	ret = PSCio_recvBufP(fd, buf, chunk);
	if (ret != chunk) {
	    closeClientSock();
	    break;
	}

	addMemToMsg(buf, chunk, &fBuf);
	if ((getRRCommLoggerMask() & RRCOMM_LOG_VERBOSE) && first) {
	    fdbg(RRCOMM_LOG_VERBOSE, "Data is");
	    for (ssize_t i = 0; i < MIN(chunk, 20); i++)
		mdbg(RRCOMM_LOG_VERBOSE, " %d", buf[i]);
	    mdbg(RRCOMM_LOG_VERBOSE, "... total %zd\n", msgSize);
	    first = false;
	}

	msgSize -= chunk;
    }
    sendFragMsg(&fBuf);

    /* reestablish old blocking behavior */
    PSCio_setFDblock(clntSock, blocked);

    return 0;
}

/** List of data waiting to be sent */
static LIST_HEAD(oldData);

/**
 * @brief Send data to client
 *
 * Send data of size @a len in @a buf to the client socket starting at
 * an offset of @a offset bytes. It is expected that the previous
 * parts of the message were sent in earlier calls to this function.
 *
 * @param buf Data to transmit
 *
 * @param len Size of @a buf
 *
 * @param offset Number of bytes sent in earlier calls
 *
 * @return On success, the total number of bytes sent is returned,
 * i.e. usually this is @a len. If the socket blocks, it might be
 * smaller. In this case the total number of bytes sent in this and
 * all previous calls is returned. If an error occurs, e.g. the client
 * socket was closed, -1 is returned and @ref errno is set
 * appropriately.
 */
static ssize_t doClientSend(char *buf, size_t len, size_t offset)
{
    if (clntSock == -1) return 0; // pile up data while waiting for client

    size_t sent;
    ssize_t ret = PSCio_sendProg(clntSock, buf + offset, len - offset, &sent);
    if (ret < 0) {
	int eno = errno;
	if (eno == EAGAIN) return offset + sent;

	char *errstr = strerror(eno);
	PSIDfwd_printMsgf(STDERR, "%s: errno %d on clientSock: %s\n",
			  __func__, eno, errstr ? errstr : "UNKNOWN");
	if (Selector_isRegistered(clntSock)) Selector_vacateWrite(clntSock);
	closeClientSock();
	errno = eno;
	return ret;
    }
    return offset + ret;
}

/**
 * @brief Store data to send in oldData list
 *
 * Store the data in @a buf of size @a len to be transmitted to the
 * client to the @ref oldData list. @a offset contains the number of
 * bytes already transmitted to the client's socket
 *
 * @param buf Data to store
 *
 * @param len Size of @a buf
 *
 * @param offset Number of bytes already transmitted
 *
 * @return Upon successful storing @a buf, true is returned; or false
 * otherwise
 */
static bool storeData(char *buf, size_t len, int offset)
{
    PSIDmsgbuf_t *msgbuf = PSIDMsgbuf_get(len);
    if (!msgbuf) return false;

    memcpy(msgbuf->msg, buf, len);
    msgbuf->offset = offset;

    list_add_tail(&msgbuf->next, &oldData);
    return true;
}

/**
 * @brief Send RRCOMM_ERROR message for each meta-data blob
 *
 * Filter used by @ref dropAllData() in order to deliver a
 * RRCOMM_ERROR message to the sender of a RRCOMM_DATA message which
 * is just dropped. For this @a blob is analyzed and if meta-data is
 * detected, all information for error-message creation is
 * extraced. In order to skip the following blob in @ref oldData that
 * will contain the actual payload of the dropped message, true is
 * returned.
 *
 * @param blob Data blob to be analyzed
 *
 * @return Return true if meta-data was detected and a data blob is expected
 */
static bool sendErrorMsg(PSIDmsgbuf_t *blob)
{
    if (blob->size < 1) return false;

    PS_DataBuffer_t data = PSdbNew(blob->msg, blob->size);
    uint8_t type;
    getUint8(data, &type);
    if (type != RRCOMM_DATA) {
	int32_t destRank;
	getInt32(data, &destRank);
	fdbg(RRCOMM_LOG_ERR, "drop type %d from %d (size %d, off %d)\n", type,
	     destRank, blob->size, blob->offset);
	PSdbDelete(data);
	return false;
    }

    bool byteOrder = setByteOrder(false); // libRRC does not use byteorder
    uint32_t len;
    getUint32(data, &len);

    /* reconstruct original fragment's extra header */
    RRComm_hdr_t msgHdr = clntHdr;
    msgHdr.dest = clntHdr.sender;
    getInt32(data, &msgHdr.sender);
    msgHdr.destJob = clntHdr.spawnerTID;
    msgHdr.spawnerTID = clntHdr.spawnerTID; // assume the message was job local
    // unless we know better
    if (clntVer > 1 || !clntVer) getTaskId(data, &msgHdr.spawnerTID);

    setByteOrder(byteOrder);
    PSdbDelete(data);

    /* send RRCOMM_ERROR to sender */
    DDTypedBufferMsg_t answer = {
	.header = {
	    .type = PSP_PLUG_RRCOMM,
	    .sender = PSC_getMyTID(),
	    .dest = getAddrFromCache(msgHdr.spawnerTID, msgHdr.sender),
	    .len = 0, /* to be set by PSP_putTypedMsgBuf */ },
	.type = RRCOMM_ERROR,
	.buf = { '\0' } };
    /* Add all information we have concerning the original message */
    PSP_putTypedMsgBuf(&answer, "hdr", &msgHdr, sizeof(msgHdr));

    fdbg(RRCOMM_LOG_FRWRD, "(%s:%d",
	 PSC_printTID(msgHdr.spawnerTID), msgHdr.sender);
    mdbg(RRCOMM_LOG_FRWRD, " -> %s:%d / size %d)",
	 PSC_printTID(msgHdr.destJob), msgHdr.dest, len);
    mdbg(RRCOMM_LOG_FRWRD, " to %s\n", PSC_printTID(answer.header.dest));

    sendDaemonMsg(&answer);

    return len; // if len == 0, no data blob was stored!
}

/**
 * @brief Drop all data stored to oldData list
 *
 * Drop all data stored to the @ref oldData list waiting to be sent to
 * the client socket. This function shall be called once the
 * corresponding socket is closed, especially if this happens
 * unexpectedly.
 *
 * For each blob of data in the @ref oldData list @ref sendErrorMsg()
 * is called with the blob as a parameter.
 *
 * @return No return value
 */
static void dropAllData(void)
{
    bool ignrNext = false;

    list_t *m, *tmp;
    list_for_each_safe(m, tmp, &oldData) {
	PSIDmsgbuf_t *msgbuf = list_entry(m, PSIDmsgbuf_t, next);
	list_del(&msgbuf->next);
	if (!ignrNext && sendErrorMsg(msgbuf)) {
	    ignrNext = true;
	} else {
	    ignrNext = false;
	}
	PSIDMsgbuf_put(msgbuf);
    }

    if (startupTimer != -1) {
	Timer_remove(startupTimer);
	startupTimer = -1;
    }
}

/**
 * @brief Flush data stored in oldData list
 *
 * Flush data stored in the @ref oldData list to be sent to the client
 * socket. All arguments of this function are ignored and only present
 * to act as a writeHandler for the Selector facility.
 *
 * @param fd Ignored
 *
 * @param info Ignored
 *
 * @return According to the Selector facility's expectations -1, 0, or
 * 1 is returned
 */
static int flushData(int fd /* dummy */, void *info /* dummy */)
{
    list_t *m, *tmp;
    list_for_each_safe(m, tmp, &oldData) {
	PSIDmsgbuf_t *msgbuf = list_entry(m, PSIDmsgbuf_t, next);

	ssize_t sent = doClientSend(msgbuf->msg, msgbuf->size, msgbuf->offset);
	if (sent < 0) return sent;

	if (sent != msgbuf->size) {
	    msgbuf->offset = sent;
	    break;
	}
	list_del(&msgbuf->next);
	PSIDMsgbuf_put(msgbuf);
    }

    if (!list_empty(&oldData)) return 1;

    if (Selector_isRegistered(clntSock)) Selector_vacateWrite(clntSock);

    return 0;
}

/**
 * @brief Transmit data to client via its socket connection
 *
 * Transmit the data in @a buf of size @a len to the client process
 * via the socket connecting to it. For this, first data queued in the
 * @ref oldData list are flushed before the data in @a buf is
 * transmitted. If the data can not or only partially be transmitted,
 * @a buf will be queued via @ref storeData() to @ref oldData, too.
 *
 * @param buf Data to be sent
 *
 * @param len Size of @a buf
 *
 * @return On success, the total number of bytes sent is returned,
 * i.e. usually this is @a len. If the socket blocks, it might be
 * smaller. If an error occurs, e.g. the client socket was closed, -1
 * is returned and @ref errno is set appropriately.
 */
static ssize_t sendToClient(char *buf, size_t len)
{
    if (!list_empty(&oldData)) flushData(0, NULL);

    bool emptyList = list_empty(&oldData);
    ssize_t sent = 0;
    if (emptyList) sent = doClientSend(buf, len, 0);

    if (sent >= 0 && sent != (ssize_t)len
	&& storeData(buf, len, sent) && emptyList && clntSock != -1) {
	Selector_awaitWrite(clntSock, flushData, NULL);
    }
    if (clntSock == -1 && hasConnected) {
	errno = EPIPE;
	return -1;
    }
    return sent;
}

/**
 * @brief Handle new client
 *
 * @ref accept() a newly connecting client after a possible old client
 * has detached. After negotiating the protocol version to utilize the
 * new socket descriptor is registered at the Selector to be handled
 * by @ref handleClientMsg().
 *
 * @param fd Socket descriptor listening for new clients
 *
 * @param data Additional data passed to @ref Selector_register() (ignored)
 *
 * @return Return 0 on success or -1 in case of fatal error
 */
static int acceptNewClient(int fd, void *data)
{
    /* lets hope the old client socket will be closed soon */
    if (clntSock != -1) return 0;

    /* accept new client */
    clntSock = accept(fd, NULL, 0);
    if (clntSock < 0) {
	int eno = errno;
	mwarn(eno, "%s: accept()", __func__);
	errno = eno;
	return -1;
    }

    struct linger linger = { .l_onoff=1, .l_linger=1 };
    setsockopt(clntSock, SOL_SOCKET, SO_LINGER, &linger, sizeof(linger));

    /* negotiate protocol version */
    ssize_t ret = PSCio_recvBufP(clntSock, &clntVer, sizeof(clntVer));
    if (ret != sizeof(clntVer)) return closeClientSock();

    if (clntVer < RRCOMM_PROTO_VERSION) dropAllData();
    if (clntVer > RRCOMM_PROTO_VERSION) clntVer = RRCOMM_PROTO_VERSION;
    if (PSCio_sendF(clntSock, &clntVer, sizeof(clntVer)) < 0) {
	return closeClientSock();
    }
    if (clntVer > 1) {
	PStask_ID_t jobID = clntHdr.spawnerTID;
	if (PSCio_sendF(clntSock, &jobID, sizeof(jobID)) < 0) {
	    return closeClientSock();
	}
    }
    fdbg(RRCOMM_LOG_FRWRD, "version %d @ fd %d\n", clntVer, clntSock);
    hasConnected = true; // track a once connected client

    /* we rely on non-blocking sends to the client */
    PSCio_setFDblock(clntSock, false);

    /* now that the client connected the startupTimer can be canceled */
    if (startupTimer != -1) {
	Timer_remove(startupTimer);
	startupTimer = -1;
    }

    /* setup client handler */
    Selector_register(clntSock, handleClientMsg, NULL);

    /* flush piled up data if any */
    Selector_awaitWrite(clntSock, flushData, NULL);
    flushData(0, NULL);

    return 0;
}

static int closeClientSock(void)
{
    if (clntSock == -1) return 0;
    Selector_remove(clntSock);
    Selector_startOver();
    dropAllData();
    close(clntSock);
    clntSock = -1;
    return 0;
}

/**
 * @brief Callback to handle RRCOMM_DATA messages
 *
 * Handle RRCOMM_DATA message assembled from fragments. This will
 * update the address cache and send a message to the client's
 * library.
 *
 * @param msg Header of the last fragment received
 *
 * @param rData Accumulated payload of all fragments
 *
 * @return No return value
 */
static void handleRRCommData(DDTypedBufferMsg_t *msg, PS_DataBuffer_t rData)
{
    RRComm_hdr_t *hdr;
    size_t used = 0, hdrSize;
    fetchFragHeader(msg, &used, NULL, NULL, (void **)&hdr, &hdrSize);
    updateAddrCache(hdr->spawnerTID, hdr->sender, msg->header.sender);

    uint32_t rDataSize = PSdbGetUsed(rData);
    char * rDataBuf = PSdbGetBuf(rData);
    fdbg(RRCOMM_LOG_FRWRD, "%s:%d", PSC_printTID(hdr->spawnerTID), hdr->sender);
    mdbg(RRCOMM_LOG_FRWRD, " -> %s:%d / size %d\n",
	 PSC_printTID(hdr->destJob), hdr->dest, rDataSize);

    if (clntSock == -1 && startupTimer == -1) {
	/* pile up data blobs for some time during startup before dropping */
	int startupTimeout = 60;
	char *envStr = getenv("RRCOMM_STARTUP_TIMEOUT");
	if (envStr) {
	    char *end;
	    int tmp = strtol(envStr, &end, 0);
	    if (*envStr && !*end && tmp > 0) startupTimeout = tmp;
	}
	struct timeval timeout = { .tv_sec = startupTimeout, .tv_usec = 0 };
	startupTimer = Timer_register(&timeout, dropAllData);
    }

    /* pack meta-data into single blob */
    PS_SendDB_t data = sendDBnoFrag;
    bool byteOrder = setByteOrder(false); // libRRC does not use byteorder
    addUint8ToMsg(RRCOMM_DATA, &data);
    addUint32ToMsg(rDataSize, &data);
    addInt32ToMsg(hdr->sender, &data);
    // Assume client will support protocol version 2 */
    if (clntVer > 1 || !clntVer) addTaskIdToMsg(hdr->spawnerTID, &data);
    setByteOrder(byteOrder);

    if (sendToClient(data.buf, data.bufUsed) < 0) {
	dropHelper(msg, sendDaemonMsg);
    }

    /* send actual data -- no data sent if rData->used == 0 */
    if (sendToClient(rDataBuf, rDataSize) < 0) {
	dropHelper(msg, sendDaemonMsg);
    } else if (getRRCommLoggerMask() & RRCOMM_LOG_VERBOSE) {
	fdbg(RRCOMM_LOG_VERBOSE, "Data is");
	for (size_t i = 0; i < MIN(rDataSize, 20); i++)
	    mdbg(RRCOMM_LOG_VERBOSE, " %d", rDataBuf[i]);
	mdbg(RRCOMM_LOG_VERBOSE, "... total %d\n", rDataSize);
    }
}

static void handleRRCommError(DDTypedBufferMsg_t *msg)
{
    size_t used = 0;
    RRComm_hdr_t hdr;
    PSP_getTypedMsgBuf(msg, &used, "hdr", &hdr, sizeof(hdr));

    /* invalidate cache entry if any */
    updateAddrCache(hdr.destJob, hdr.dest, -1);

    /* pack data into single blob */
    PS_SendDB_t data = sendDBnoFrag;
    bool byteOrder = setByteOrder(false); // libRRC does not use byteorder
    addUint8ToMsg(RRCOMM_ERROR, &data);
    addInt32ToMsg(hdr.dest, &data);
    // Assume client will support protocol version 2 */
    if (clntVer > 1 || !clntVer) addTaskIdToMsg(hdr.destJob, &data);
    setByteOrder(byteOrder);

    if (sendToClient(data.buf, data.bufUsed) < 0) {
	fdbg(RRCOMM_LOG_ERR, "failed to deliver error on %d\n", hdr.dest);
	return;
    }
}

/**
 * @brief Handle PSP_PLUG_RRCOMM message
 *
 * Handle the message @msg of type PSP_PLUG_RRCOMM.
 *
 * @param msg Message to handle
 *
 * @return Always return true
 */
static bool handleDaemonMsg(DDTypedBufferMsg_t *msg)
{
    switch (msg->type) {
    case RRCOMM_DATA:
	if (!tryRecvFragMsg(msg, handleRRCommData))
	    dropHelper(msg, sendDaemonMsg);
	break;
    case RRCOMM_ERROR:
	handleRRCommError(msg);
	break;
    default:
	flog("unknown message type %d/%d\n", msg->header.type, msg->type);
    }

    return true;
}

/**
 * @brief Function hooked to PSIDHOOK_FRWRD_SETUP
 *
 * This function establishs the setup within the psidforwarder
 * process. In detail it will:
 *  - Cleanup the environment prepared for the client process
 *  - Initialize the serialization layer
 *  - Register the PSP_PLUG_RRCOMM message type to the message handler
 *
 * @param data Pointer to the task structure to spawn (ignored)
 *
 * @return On success 0 is returned or -1 in case of failure
 */
static int hookFrwrdSetup(void *data)
{
    /* cleanup environment */
    unsetenv(RRCOMM_SOCKET_ENV);

    /* initialize fragmentation layer */
    if (!initSerial(0, sendDaemonMsg)) {
	flog("initSerial() failed\n");
	return -1;
    }

    /* message type to handle */
    if (!PSID_registerMsg(PSP_PLUG_RRCOMM, (handlerFunc_t)handleDaemonMsg)) {
	flog("failed to register PSP_PLUG_RRCOMM handler\n");
	return -1;
    }

    return 0;
}

/**
 * @brief Function hooked to PSIDHOOK_FRWRD_INIT
 *
 * This function establishes the initialization within the
 * psidforwarder process. In detail it will:
 *  - Register the abstract socket listening for clients to the Selector
 *
 * @param data Pointer to the task structure to spawn (ignored)
 *
 * @return On success 0 is returned or -1 in case of failure
 */
static int hookFrwrdInit(void *data)
{
    /* there might no listening sock due to no RRComm in service processes */
    if (listenSock == -1) return 0;

    /* register the listening socket */
    if (Selector_register(listenSock, acceptNewClient, NULL) != 0) {
	PSIDfwd_printMsgf(STDERR, "failed to register selector\n");
	flog("failed to register selector\n");
	return -1;
    }

    return 0;
}

/**
 * @brief Function hooked to PSIDHOOK_FRWRD_CLNT_RES
 *
 * This hook is called once the client has sent its SIGCHLD. It will:
 * - Cleanup the abstract socket listening for clients
 *
 * @param data Pointer to exited client's status (ignored)
 *
 * @return Always returns 0
 */
static int hookFrwrdClntRes(void *data)
{
    closeListenSock();
    Selector_startOver();
    return 0;
}

/**
 * @brief Function hooked to PSIDHOOK_FRWRD_EXIT
 *
 * This function cleans up within the psidforwarder process. In detail
 * it will:
 *  - Cleanup the client socket if any
 *  - Stop handling of the listen socket and close() it
 *  - Clear the handler of the PSP_PLUG_RRCOMM message type
 *  - Finalize the serialization layer
 *
 * @param data No data provided by the call to the hook (ignored)
 *
 * @return Always return 0
 */
static int hookFrwrdExit(void *data)
{
    /* cleanup the client socket if any */
    if (clntSock != -1) {
	close(clntSock);
	clntSock = -1;
    }

    /* cleanup the address cache */
    clearAddrCache();

    /* cleanup the listening socket */
    closeListenSock();

    /* cleanup message handler */
    PSID_clearMsg(PSP_PLUG_RRCOMM, (handlerFunc_t)handleDaemonMsg);

    /* finalize fragmentation layer */
    finalizeSerial();

    /* flush all pending logs */
    finalizeRRCommLogger();

    return 0;
}

bool attachRRCommForwarderHooks(void)
{
    if (!PSIDhook_add(PSIDHOOK_EXEC_FORWARDER, hookExecForwarder)) {
	flog("attaching PSIDHOOK_EXEC_FORWARDER failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_EXEC_CLIENT, hookExecClient)) {
	flog("attaching PSIDHOOK_EXEC_CLIENT failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_FRWRD_SETUP, hookFrwrdSetup)) {
	flog("attaching PSIDHOOK_FRWRD_SETUP failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_FRWRD_INIT, hookFrwrdInit)) {
	flog("attaching PSIDHOOK_FRWRD_INIT failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_FRWRD_CLNT_RES, hookFrwrdClntRes)) {
	flog("attaching PSIDHOOK_FRWRD_CLNT_RES failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_FRWRD_EXIT, hookFrwrdExit)) {
	flog("attaching PSIDHOOK_FRWRD_EXIT failed\n");
	return false;
    }

    return true;
}

void detachRRCommForwarderHooks(bool verbose)
{
    if (!PSIDhook_del(PSIDHOOK_FRWRD_EXIT, hookFrwrdExit)) {
	if (verbose) flog("unregister PSIDHOOK_FRWRD_EXIT failed\n");
    }
    if (!PSIDhook_del(PSIDHOOK_FRWRD_CLNT_RES, hookFrwrdClntRes)) {
	if (verbose) flog("unregister PSIDHOOK_FRWRD_CLNT_RES failed\n");
    }
    if (!PSIDhook_del(PSIDHOOK_FRWRD_INIT, hookFrwrdInit)) {
	if (verbose) flog("unregister PSIDHOOK_FRWRD_INIT failed\n");
    }
    if (!PSIDhook_del(PSIDHOOK_FRWRD_SETUP, hookFrwrdSetup)) {
	if (verbose) flog("unregister PSIDHOOK_FRWRD_SETUP failed\n");
    }
    if (!PSIDhook_del(PSIDHOOK_EXEC_CLIENT, hookExecClient)) {
	if (verbose) flog("unregister PSIDHOOK_EXEC_CLIENT failed\n");
    }
    if (!PSIDhook_del(PSIDHOOK_EXEC_FORWARDER, hookExecForwarder)) {
	if (verbose) flog("unregister PSIDHOOK_EXEC_FORWARDER failed\n");
    }
}
