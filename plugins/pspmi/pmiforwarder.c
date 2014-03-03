/*
 * ParaStation
 *
 * Copyright (C) 2013 ParTec Cluster Competence Center GmbH, Munich
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
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <strings.h>
#include <errno.h>
#include <netinet/in.h>

#include "selector.h"
#include "pscommon.h"
#include "kvscommon.h"
#include "../../bin/daemon/psidforwarder.h"
#include "pmiclient.h"
#include "pmilog.h"

#include "pmiforwarder.h"

/** The socket listening for connection from the MPI client (tcp/ip only) */
static int pmiTCPSocket = -1;

/** The type of the connection between forwarder and client */
static PMItype_t pmiType = -1;

/** The socket connected to the MPI client */
static int pmiClientSock = -1;

/** The task structure of the MPI client */
static PStask_t *childTask = NULL;

static enum {
    IDLE = 0,
    CONNECTED,
    CLOSED,
} pmiStatus = IDLE;


/**
 * @brief Close the socket which is connected to the MPI client.
 *
 * @return No return value.
 */
static void closePMIclientSocket(void)
{
    /* close MPI client socket */
    if (pmiClientSock > 0) {
	if (Selector_isRegistered(pmiClientSock)) {
	    Selector_remove(pmiClientSock);
	}
	close(pmiClientSock);
	pmiClientSock = -1;
    }
}

PStask_t *getChildTask()
{
    return childTask;
}

/**
 * @brief Close the socket which listens for new PMI TCP/IP connections.
 *
 * @return No return value.
 */
static void closePMIlistenSocket(void)
{
    /* close PMI accept socket */
    if (pmiTCPSocket > 0) {
	Selector_remove(pmiTCPSocket);
	close(pmiTCPSocket);
	pmiTCPSocket = -1;
    }
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
    char msgBuf[PMIU_MAXLINE];
    ssize_t len;
    int ret;

    if (pmiClientSock < 0) {
	elog("%s: invalid PMI client socket %i\n", __func__, pmiClientSock);
	return 0;
    }

    if (!(len = recv(pmiClientSock, msgBuf, sizeof(msgBuf), 0))) {
	/* no data received from client */
	elog("%s: lost connection to the PMI client\n", __func__);

	/* close connection */
	closePMIclientSocket();
	return 0;
    }

    /* socket error occured */
    if (len < 0) {
	elog( "%s: error on PMI socket occured\n", __func__);
	return 0;
    }

    /* truncate msg to received bytes */
    msgBuf[len] = '\0';

    pmiStatus = CONNECTED;

    /* parse and handle the PMI msg */
    ret = handlePMIclientMsg(msgBuf);

    /* PMI communication finished */
    if (ret == PMI_FINALIZED) {

	/* release the child */
	DDSignalMsg_t msg;

	msg.header.type = PSP_CD_RELEASE;
	msg.header.sender = PSC_getMyTID();
	msg.header.dest = childTask->tid;
	msg.header.len = sizeof(msg);
	msg.signal = -1;
	msg.answer = 1;

	sendDaemonMsg((DDMsg_t *)&msg);

	pmiStatus = CLOSED;
    }

    return 0;
}

/**
 * @brief Accept a new PMI connection.
 *
 * @param fd Unused parameter.
 *
 * @param data Unused parameter.
 *
 * @return Always returns 0.
 */
static int acceptPMIClient(int fd, void *data)
{
    unsigned int clientlen;
    struct sockaddr_in SAddr;

    /* check if a client is already connected */
    if (pmiClientSock != -1) {
	elog( "%s: error only one PMI connection is allowed\n", __func__);
	return 0;
    }

    /* accept a new PMI connection */
    clientlen = sizeof(SAddr);
    if ((pmiClientSock = accept(pmiTCPSocket, (void *)&SAddr, &clientlen)) == -1) {
	elog( "%s: error on accepting new pmi connection\n", __func__);
	return 0;
    }

    /* close the socket which waits for new connections */
    closePMIlistenSocket();

    /* init the PMI interface */
    if ((pmi_init(pmiClientSock, childTask->rank, childTask->loggertid))) {
	pmiType = PMI_DISABLED;
	return 0;
    }

    /* register the new PMI socket */
    Selector_register(pmiClientSock, readFromPMIClient, NULL);

    return 0;
}

int getClientStatus(void *data)
{
    return pmiStatus;
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

int setupPMIsockets(void *data)
{
    PStask_ID_t providertid = -1;
    childTask = data;

    if (childTask->group != TG_ANY) return 0;

    if (pmiType == PMI_OVER_TCP || pmiType == PMI_OVER_UNIX) {
	char *env;

	/* save infos from KVS provider */
	if (!(env = getenv("__KVS_PROVIDER_TID"))) {
	    elog("%s: KVS provider TID not available\n", __func__);
	    pmiType = PMI_DISABLED;
	    return -1;
	}
	if ((sscanf(env, "%i", &providertid)) != 1) {
	    elog("%s: invalid KVS provider TID\n", __func__);
	    pmiType = PMI_DISABLED;
	    return -1;
	}
	setKVSProviderTID(providertid);
    }

    switch (pmiType) {
	case PMI_OVER_UNIX:
	    /* init the PMI interface */
	    if ((pmi_init(pmiClientSock, childTask->rank,
				    childTask->loggertid))) {
		pmiType = PMI_DISABLED;
		return -1;
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
	    pmiType = PMI_DISABLED;
	    break;
	default:
	    elog("%s: invalid PMI type: %i\n", __func__, pmiType);
	    pmiType = PMI_DISABLED;
	    return -1;
    }

    return 0;
}

int releasePMIClient(void *data)
{
    int *res;

    /* release the MPI client */
    res = data;
    if (pmiType != PMI_DISABLED) {
	if (*res == 1) {
	    pmi_finalize();
	} else {
	    leaveKVS(1);
	}
    }

    /*close connection */
    closePMIclientSocket();
    return 0;
}
