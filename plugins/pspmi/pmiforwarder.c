/*
 * ParaStation
 *
 * Copyright (C) 2013-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pmiforwarder.h"

#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "pscommon.h"
#include "kvscommon.h"
#include "selector.h"
#include "pluginmalloc.h"

#include "psidforwarder.h"  // IWYU pragma: keep
#include "psidhook.h"

#include "pmiclient.h"
#include "pmilog.h"

/** The socket listening for connection from the MPI client (tcp/ip only) */
static int pmiTCPSocket = -1;

/** The type of the connection between forwarder and client */
static PMItype_t pmiType = -1;

/** The socket connected to the MPI client */
static int pmiClientSock = -1;

/** The task structure of the MPI client */
static PStask_t *childTask = NULL;

/** Buffer pointer to concatenate multiple PMI messages */
static char *mmBuffer = NULL;

/** Size of the buffer to concatenate multiple PMI messages */
static size_t mmBufferSize = 0;

/** Used part of the buffer to concatenate multiple PMI messages */
static size_t mmBufferUsed = 0;

/** Connection status of the client to serve */
PSIDhook_ClntRls_t clientStatus = IDLE;

/**
 * @brief Close the socket connecting to PMI client
 *
 * @return No return value
 */
static void closePMIclientSocket(void)
{
    /* close PMI client socket */
    if (pmiClientSock < 0) return;
    /* reg. check needed since selector might have been removed in critErr() */
    if (Selector_isRegistered(pmiClientSock)) Selector_remove(pmiClientSock);
    Selector_startOver();
    close(pmiClientSock);
    pmiClientSock = -1;
}

/**
 * @brief Close the socket which listens for new PMI TCP/IP connections
 *
 * @return No return value
 */
static void closePMIlistenSocket(void)
{
    /* close PMI accept socket */
    if (pmiTCPSocket < 0) return;
    Selector_remove(pmiTCPSocket);
    close(pmiTCPSocket);
    pmiTCPSocket = -1;
}

/**
 * @brief Read a PMI message from the client.
 *
 * @param fd Unused parameter.
 *
 * @param data Unused parameter.
 *
 * @return Always returns 0.
 */
static int readFromPMIClient(int fd, void *data)
{
    char stackBuf[PMIU_MAXLINE+1];
    int ret = 0;

    if (pmiClientSock < 0) {
	elog("%s: invalid PMI client socket %i\n", __func__, pmiClientSock);
	return 0;
    }

    /* if there is a static buffer, append, else use stack buffer */
    char *recvBuf, *msgBuf;
    size_t msgBufUsed;
    if (mmBuffer) {
	mmBufferSize = mmBufferUsed + PMIU_MAXLINE + 1;
	mmBuffer = urealloc(mmBuffer, mmBufferSize);
	if (!mmBuffer) {
	    elog("%s: failed to allocate message buffer\n", __func__);
	    goto readFromPMIClient_error;
	}
	recvBuf = mmBuffer + mmBufferUsed;
	msgBuf = mmBuffer;
	msgBufUsed = mmBufferUsed;
    } else {
	recvBuf = stackBuf;
	msgBuf = stackBuf;
	msgBufUsed = 0;
    }

    ssize_t len = recv(pmiClientSock, recvBuf, PMIU_MAXLINE, 0);
    if (len < 0) {
	/* socket error occurred */
	elog( "%s: error on PMI socket occurred\n", __func__);
	return 0;
    } else if (!len) {
	/* no data received from client */
	if (clientStatus == CONNECTED)
	    elog("%s: lost connection to the PMI client\n", __func__);

	/* close connection */
	closePMIclientSocket();
	return 0;
    }


    /* truncate msg to received bytes */
    recvBuf[len] = '\0';

    /* update buffer usage counters */
    msgBufUsed += len;
    if (mmBuffer) mmBufferUsed += len;

    clientStatus = CONNECTED;

    int rLen = strlen(recvBuf);
    if (rLen > 0) rLen--;
    while (rLen > 0 && recvBuf[rLen] == '\n') rLen--;
    mdbg(PSPMI_LOG_RECV, "%s: PMI message received: {%.*s}\n", __func__, rLen,
	 recvBuf);

    while (true) {
	int mLen = strlen(msgBuf);
	if (mLen > 0) mLen--;
	while (mLen > 0 && msgBuf[mLen] == '\n') mLen--;
	mdbg(PSPMI_LOG_VERBOSE, "%s: Current message buffer: {%.*s}\n",
	     __func__, mLen, msgBuf);

	char *strptr;
	if (strncmp("cmd=", msgBuf, 4) == 0) {
	    strptr = strchr(msgBuf, '\n');
	} else if (strncmp("mcmd=", msgBuf, 5) == 0) {
	    strptr = strstr(msgBuf, "\nendcmd\n");
	    if (strptr) strptr += 7;
	} else {
	    elog("%s: Invalid PMI message received: '%.*s\n", __func__, mLen,
		 msgBuf);
	    goto readFromPMIClient_error;
	}

	if (!strptr) {
	    /* we need another receive to have a complete message */

	    if (mmBuffer) break;

	    mmBuffer = umalloc(msgBufUsed);
	    memcpy(mmBuffer, msgBuf, msgBufUsed);
	    mmBufferUsed = msgBufUsed;

	    break;
	}

	/* we have a complete message to handle */
	strptr[0] = '\0';

	/* parse and handle the PMI msg */
	mdbg(PSPMI_LOG_RECV, "%s: PMI message complete: {%.*s}\n", __func__,
	     mLen, msgBuf);
	ret = handlePMIclientMsg(msgBuf);

	if (ret != 0) break;

	strptr++;

	if (strptr >= (msgBuf + msgBufUsed)) {
	    /* complete message handled */
	    ufree(mmBuffer);
	    mmBuffer = NULL;
	    mmBufferSize = 0;
	    mmBufferUsed = 0;

	    break;
	}

	msgBufUsed -= strlen(msgBuf) + 1;
	msgBuf = strptr;
    }

    if (mmBuffer && (mmBuffer != msgBuf)) {
	memmove(mmBuffer, msgBuf, msgBufUsed);
	mmBufferUsed = msgBufUsed;
    }

    /* PMI communication finished */
    if (ret == PMI_FINALIZED) {
	clientStatus = RELEASED;
	ackFinalize();
	/* prepare for re-connecting client */
	closePMIclientSocket();
    }

    return 0;

readFromPMIClient_error:
    ufree(mmBuffer);
    mmBuffer = NULL;
    mmBufferSize = 0;
    mmBufferUsed = 0;

    return 0;
}

/**
 * @brief Accept new PMI connection
 *
 * @param fd Unused parameter
 *
 * @param data Unused parameter
 *
 * @return Always returns 0
 */
static int acceptPMIClient(int fd, void *data)
{
    /* accept a new PMI connection */
    struct sockaddr_in SAddr;
    socklen_t clientlen = sizeof(SAddr);
    int sock = accept(pmiTCPSocket, (void *)&SAddr, &clientlen);
    if (sock == -1) {
	elog( "%s: error on accepting new pmi connection\n", __func__);
	return 0;
    }

    /* check if a client is already connected */
    if (pmiClientSock != -1) {
	elog( "%s: error only one PMI connection is allowed\n", __func__);
	close(sock);
	return 0;
    }
    pmiClientSock = sock;

    /* init the PMI interface */
    if (pmi_init(pmiClientSock, childTask) != 0) {
	pmiType = PMI_DISABLED;
	return 0;
    }

    /* register the new PMI socket */
    Selector_register(pmiClientSock, readFromPMIClient, NULL);

    return 0;
}

/**
 * @brief Get the pmi status.
 *
 * @param data Unsed parameter.
 *
 * @return Returns the status of the pmi connection.
 */
static int getClientStatus(void *data)
{
    return clientStatus;
}

void setConnectionInfo(PMItype_t type, int sock)
{
    pmiType = type;

    switch (type) {
    case PMI_OVER_TCP:
	pmiTCPSocket = sock;
	break;
    case PMI_OVER_UNIX:
	pmiClientSock = sock;
	break;
    default:
	break;
    }
}

/**
 * @brief Init the pmi interface.
 *
 * Init the pmi interface and start listening for new connection from
 * the mpi client.
 *
 * @param data Pointer to the task structure of the child.
 *
 * @return Returns 0 on success and -1 on error.
 */
static int setupPMIsockets(void *data)
{
    childTask = data;
    if (!childTask) return -1;

    if (childTask->group != TG_ANY) return 0;

    if (pmiType == PMI_OVER_TCP || pmiType == PMI_OVER_UNIX) {
	/* save infos on KVS provider */
	char *env = getenv("__KVS_PROVIDER_TID");
	PStask_ID_t providertid;
	if (!env || sscanf(env, "%ld", &providertid) != 1) {
	    elog("%s: no KVS provider TID from '%s'\n", __func__, env);
	    pmiType = PMI_DISABLED;
	    return -1;
	}
	setKVSProviderTID(providertid);
    }

    switch (pmiType) {
    case PMI_OVER_UNIX:
	/* init the PMI interface */
	if (pmi_init(pmiClientSock, childTask)) {
	    pmiType = PMI_DISABLED;
	    return -1;
	}

	/* close remote side of PMI client socket */
	/* this ensures an exiting client to close pmiClientSock */
	char *env = getenv("PMI_FD");
	if (env) {
	    int fd = atoi(env);
	    if (fd >= 0) close(fd);
	}

	/* register PMI client socket */
	Selector_register(pmiClientSock, readFromPMIClient, NULL);
	break;
    case PMI_OVER_TCP:
	/* register the PMI TCP socket for accepting new clients */
	Selector_register(pmiTCPSocket, acceptPMIClient, NULL);
	break;
    case PMI_DISABLED:
	/* nothing for me to do */
	break;
    default:
	elog("%s: invalid PMI type: %i\n", __func__, pmiType);
	pmiType = PMI_DISABLED;
	return -1;
    }

    return 0;
}

/**
 * @brief Hook function for PSIDHOOK_FRWRD_CLNT_RES
 *
 * Close listening sockets upon receiving client's SIGCHLD since the
 * client will not connect in the future. This is required since
 * psidforwarder will only exit on all selectors removed.
 *
 * @param data Pointer to exited client's status (ignored)
 *
 * @return Always returns 0
 */
static int hookForwarderClntRes(void *data)
{
    closePMIlistenSocket();
    Selector_startOver();
    return 0;
}

/**
 * @brief Hook function for PSIDHOOK_FRWRD_EXIT
 *
 * Close all sockets and leave the KVS explicitly if necessary. This is
 * required especially in the case of PMI_Spawn() to prevent left over
 * kvssproviders waiting for the whole job to finish.
 *
 * @param data Unused parameter
 *
 * @return Always returns 0
 */
static int hookForwarderExit(void *data)
{
    /* Since the client has exited it will for sure not reconnect */
    leaveKVS();

    /*close connections */
    closePMIclientSocket();
    closePMIlistenSocket();

    return 0;
}

void initForwarder(void)
{
    PSIDhook_add(PSIDHOOK_FRWRD_INIT, setupPMIsockets);
    PSIDhook_add(PSIDHOOK_FRWRD_CLNT_RLS, getClientStatus);
    PSIDhook_add(PSIDHOOK_FRWRD_CLNT_RES, hookForwarderClntRes);
    PSIDhook_add(PSIDHOOK_FRWRD_EXIT, hookForwarderExit);
}

void finalizeForwarder(void)
{
    PSIDhook_del(PSIDHOOK_FRWRD_INIT, setupPMIsockets);
    PSIDhook_del(PSIDHOOK_FRWRD_CLNT_RLS, getClientStatus);
    PSIDhook_del(PSIDHOOK_FRWRD_CLNT_RES, hookForwarderClntRes);
    PSIDhook_del(PSIDHOOK_FRWRD_EXIT, hookForwarderExit);
}
