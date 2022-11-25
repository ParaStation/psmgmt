/*
 * ParaStation
 *
 * Copyright (C) 2022 ParTec AG, Munich
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
#include <sys/un.h>
#include <unistd.h>

#include "pscio.h"
#include "pscommon.h"
#include "pslog.h"
#include "psprotocol.h"
#include "psserial.h"
#include "pspluginprotocol.h"
#include "selector.h"

#include "psidcomm.h"
#include "psidforwarder.h"
#include "psidhook.h"

#include "rrcomm_common.h"
#include "rrcommlog.h"
#include "rrcommproto.h"

/** Socket listening for connecting clients */
static int listenSock = -1;

/** All information on the client to spawn used within the fragment header */
static RRComm_hdr_t clntHdr;

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
 * @param data Pointer to the task structure to spawn (ignored)
 *
 * @return On success 0 is returned or -1 in case of failure
 */
static int hookExecForwarder(void *data)
{
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

    fdbg(RRCOMM_LOG_VERBOSE, "client socket (%d) created at '%s'\n",
	 listenSock, sa.sun_path + 1);

    /* setup common header from proto-task information */
    PStask_t *client = (PStask_t *)data;
    clntHdr.sender = client->rank;
    clntHdr.loggerTID = client->loggertid;
    clntHdr.spawnerTID = client->spawnertid;

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

/** Cache of destination task IDs */
static PStask_ID_t *addrCache = NULL;

/** Current size of the address cache */
static int32_t addrCacheSize = 0;

static void updateAddrCache(int32_t rank, PStask_ID_t taskID)
{
    if (rank >= addrCacheSize) {
	size_t newSize = (rank / 256 + 1) * 256;
	PStask_ID_t *tmp = realloc(addrCache, sizeof(*tmp) * newSize);
	if (!tmp) return;
	for (size_t i = addrCacheSize; i < newSize; i++) tmp[i] = -1;
	addrCache = tmp;
	addrCacheSize = newSize;
    }
    addrCache[rank] = taskID;
}

static PStask_ID_t getAddrFromCache(int32_t rank)
{
    if (rank >= 0 && rank < addrCacheSize) return addrCache[rank];
    return -1;
}

/** Socket connected to current client */
static int clntSock = -1;

static int closeClientSock(void)
{
    if (clntSock == -1) return 0;
    Selector_remove(clntSock);
    close(clntSock);
    clntSock = -1;
    return 0;
}

/** Maximum protocol version the lib is capable to handle */
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
    ssize_t ret = PSCio_recvBufP(fd, &clntHdr.dest, sizeof(clntHdr.dest));
    if (ret != sizeof(clntHdr.dest)) return closeClientSock();

    size_t msgSize;
    ret = PSCio_recvBufP(fd, &msgSize, sizeof(msgSize));
    if (ret != sizeof(msgSize)) return closeClientSock();

    /* setup frag message */
    PS_SendDB_t fBuf;
    initFragBufferExtra(&fBuf, PSP_PLUG_RRCOMM, RRCOMM_DATA,
			&clntHdr, sizeof(clntHdr));

    PStask_ID_t dest = getAddrFromCache(clntHdr.dest);
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
	if ((getRRCommLoggerMask() & RRCOMM_LOG_COMM) && first) {
	    fdbg(RRCOMM_LOG_COMM, "Data is");
	    for (ssize_t i = 0; i < MIN(chunk, 20); i++)
		mdbg(RRCOMM_LOG_COMM, " %d", buf[i]);
	    mdbg(RRCOMM_LOG_COMM, "... total %zd\n", msgSize);
	    first = false;
	}

	msgSize -= chunk;
    }
    sendFragMsg(&fBuf);

    return 0;
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

    if (clntVer > RRCOMM_PROTO_VERSION) clntVer = RRCOMM_PROTO_VERSION;
    if (PSCio_sendF(clntSock, &clntVer, sizeof(clntVer)) < 0) {
	return closeClientSock();
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
static void handleRRCommData(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *rData)
{
    RRComm_hdr_t *hdr;
    size_t used = 0, hdrSize;
    fetchFragHeader(msg, &used, NULL, NULL, (void **)&hdr, &hdrSize);
    updateAddrCache(hdr->sender, msg->header.sender);

    if (clntSock == -1) {
	PSIDfwd_printMsgf(STDERR, "drop RRComm msg from %d\n", hdr->sender);
	flog("drop RRComm msg from %d\n", hdr->sender);
	return;
    }

    // @todo On the long run move to non-blocking sends!
    if (PSCio_sendF(clntSock, &rData->used, sizeof(rData->used)) < 0) {
	flog("failed to send size\n");
	closeClientSock();
    }
    if (PSCio_sendF(clntSock, &hdr->sender, sizeof(hdr->sender)) < 0) {
	flog("failed to send sender rank\n");
	closeClientSock();
    }
    if (PSCio_sendF(clntSock, rData->buf, rData->used) < 0) {
	flog("failed to send data\n");
	closeClientSock();
    } else if (getRRCommLoggerMask() & RRCOMM_LOG_COMM) {
	fdbg(RRCOMM_LOG_COMM, "Data is");
	for (size_t i = 0; i < MIN(rData->used, 20); i++)
	    mdbg(RRCOMM_LOG_COMM, " %d", rData->buf[i]);
	mdbg(RRCOMM_LOG_COMM, "... total %zd\n", rData->used);
    }
}

static void handleRRCommError(DDTypedBufferMsg_t *msg)
{
    size_t used = 0;
    RRComm_hdr_t hdr;
    PSP_getTypedMsgBuf(msg, &used, "hdr", &hdr, sizeof(hdr));

    /* invalidate cache entry if any */
    updateAddrCache(hdr.dest, -1);

    /* pack data into single blob */
    PS_SendDB_t data = { .bufUsed = 0, .useFrag = false };
    bool byteOrder = setByteOrder(false); // libRRC does not use byteorder
    addUint8ToMsg(RRCOMM_ERROR, &data);
    addInt32ToMsg(hdr.dest, &data);
    setByteOrder(byteOrder);

    if (sendToClient(data.buf, data.bufUsed) < 0) {
	flog("failed to send error data\n");
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
	recvFragMsg(msg, handleRRCommData);
	break;
    case RRCOMM_ERROR:
	handleRRCommError(msg);
	break;
    default:
	flog("unknown messsage type %d/%d\n", msg->header.type, msg->type);
    }

    return true;
}

/**
 * @brief Function hooked to PSIDHOOK_FRWRD_INIT
 *
 * This function establishs the setup within the psidforwarder
 * process. In detail it will:
 *  - Cleanup the environment prepared for the client process
 *  - Initialize the serialization layer
 *  - Register the PSP_PLUG_RRCOMM message types to the message handler
 *  - Register the abstract socket listening for clients to the Selector
 *
 * @param data Pointer to the task structure to spawn (ignored)
 *
 * @return On success 0 is returned or -1 in case of failure
 */
static int hookFrwrdInit(void *data)
{
    /* cleanup environment */
    unsetenv(RRCOMM_SOCKET_ENV);

    /* initialize fragmentation layer */
    if (!initSerial(0, sendDaemonMsg)) {
	PSIDfwd_printMsgf(STDERR, "initSerial failed\n");
	flog("initSerial failed\n");
	return -1;
    }

    /* message type to handle */
    if (!PSID_registerMsg(PSP_PLUG_RRCOMM, (handlerFunc_t)handleDaemonMsg)) {
	PSIDfwd_printMsgf(STDERR, "cannot register PSP_PLUG_RRCOMM handler\n");
	flog("cannot register 'PSP_PLUG_RRCOMM' handler\n");
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
    /* register the listening socket */
    Selector_register(listenSock, acceptNewClient, NULL);

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
    free(addrCache);
    addrCacheSize = 0;

    /* cleanup the listening socket */
    closeListenSock();

    /* cleanup message handler */
    PSID_clearMsg(PSP_PLUG_RRCOMM, (handlerFunc_t)handleDaemonMsg);

    /* finalize fragmentation layer */
    finalizeSerial();

    return 0;
}

bool attachRRCommForwarderHooks(void)
{
    if (!PSIDhook_add(PSIDHOOK_EXEC_FORWARDER, hookExecForwarder)) {
	flog("attaching 'PSIDHOOK_EXEC_FORWARDER' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_EXEC_CLIENT, hookExecClient)) {
	flog("attaching 'PSIDHOOK_EXEC_CLIENT' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_FRWRD_INIT, hookFrwrdInit)) {
	flog("attaching 'PSIDHOOK_FRWRD_INIT' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_FRWRD_EXIT, hookFrwrdExit)) {
	flog("attaching 'PSIDHOOK_FRWRD_EXIT' failed\n");
	return false;
    }

    return true;
}

void detachRRCommForwarderHooks(bool verbose)
{
    if (!PSIDhook_del(PSIDHOOK_FRWRD_EXIT, hookFrwrdExit)) {
	if (verbose) flog("unregister 'PSIDHOOK_FRWRD_EXIT' failed\n");
    }
    if (!PSIDhook_del(PSIDHOOK_FRWRD_INIT, hookFrwrdInit)) {
	if (verbose) flog("unregister 'PSIDHOOK_FRWRD_INIT' failed\n");
    }
    if (!PSIDhook_del(PSIDHOOK_EXEC_CLIENT, hookExecClient)) {
	if (verbose) flog("unregister 'PSIDHOOK_EXEC_CLIENT' failed\n");
    }
    if (!PSIDhook_del(PSIDHOOK_EXEC_FORWARDER, hookExecForwarder)) {
	if (verbose) flog("unregister 'PSIDHOOK_EXEC_FORWARDER' failed\n");
    }
}
