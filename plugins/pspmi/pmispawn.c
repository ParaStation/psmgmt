/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <strings.h>
#include <errno.h>
#include <netinet/in.h>

#include "pmitypes.h"
#include "pmiforwarder.h"
#include "pmilog.h"
#include "pmiprovider.h"

#include "pmispawn.h"

/** the socket connecting the PMI client with the forwarder */
static int forwarderSock = -1;

/** the type of the PMI connection */
static PMItype_t pmiType;

/**
 * @brief Set up a new PMI TCP socket and start listening for new connections.
 *
 * Setup and start to listen to a TCP socket that clients can connect
 * to for PMI requests.
 *
 * @return Upon success the fd of the initialized socket is
 * returned. In case of an error, -1 is returned and errno is set
 * appropriately.
 */
static int init_PMISocket(void)
{
    struct sockaddr_in saClient;
    int res, sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (sock < 0) {
	int eno = errno;
	mwarn(eno, "%s: create PMI socket failed", __func__);
	errno = eno;
	return -1;
    }

    /* set up the sockaddr structure */
    saClient.sin_family = AF_INET;
    saClient.sin_addr.s_addr = INADDR_ANY;
    saClient.sin_port = htons(0);
    bzero(&(saClient.sin_zero), 8);

    /* bind the socket */
    res = bind(sock, (struct sockaddr *)&saClient, sizeof(saClient));
    if (res == -1) {
	int eno = errno;
	mwarn(errno, "%s: binding PMIsock failed", __func__);
	errno = eno;
	return -1;
    }

    /* set socket to listen state */
    res = listen(sock, 5);
    if (res == -1) {
	int eno = errno;
	mwarn(eno, "%s: listen on PMIsock failed", __func__);
	errno = eno;
	return -1;
    }

    return sock;
}

/**
 * @brief Set up the PMI_PORT variable.
 *
 * @param PMISock The PMI socket to get the information from.
 *
 * @param cPMI_PORT The buffer which receives the result.
 *
 * @param size The size of the cPMI_PORT buffer.
 *
 * @return No return value
 */
static void get_PMI_PORT(int PMISock, char *cPMI_PORT, int size )
{
    struct sockaddr_in addr;
    socklen_t len;

    /* get the PMI port */
    len = sizeof(addr);
    bzero(&(addr), 8);
    if (getsockname(PMISock,(struct sockaddr*)&addr,&len) == -1) {
	mwarn(errno, "%s: getsockname(pmisock)", __func__);
	exit(1);
    }

    snprintf(cPMI_PORT,size,"127.0.0.1:%i", ntohs(addr.sin_port));
}

/**
 * @brief Prepare PMI
 *
 * Prepare PMI environment presented to the client process that will
 * be handled by the forwarder. For further use within the calling
 * function, @a forwarderSock will hold the file-descriptor of this
 * socket upon return.
 *
 * This function will evaluate various environment variables in order
 * to determine which type of socket to open:
 *
 * - PMI_ENABLE_TCP will trigger an TCP socket.
 *
 * - PMI_ENABLE_SOCKP will trigger an AF_UNIX socketpair.
 *
 * @return Depending on the socket-type created PMI_OVER_TCP or
 * PMI_OVER_UNIX will be returned. If both environment variables
 * discussed above are set or an error occurred PMI_FAILED will be
 * returned. If no socket is created due to missing environment
 * variables PMI_DISABLED is returned.
 */
static PMItype_t preparePMI(int *forwarderSock)
{
    PMItype_t pmiType = PMI_DISABLED;
    int pmiEnableTcp = 0;
    int pmiEnableSockp = 0;
    char *envstr = getenv("PMI_ENABLE_TCP");

    /* check if PMI should be started */
    if (envstr) pmiEnableTcp = atoi(envstr);

    envstr = getenv("PMI_ENABLE_SOCKP");
    if (envstr) pmiEnableSockp = atoi(envstr);

    /* only one option is allowed */
    if (pmiEnableSockp && pmiEnableTcp) {
	mwarn(EINVAL,
		  "%s: only one type of PMI connection allowed", __func__);
	return PMI_FAILED;
    }

    /* unset bogus PMI settings */
    unsetenv("PMI_PORT");
    unsetenv("PMI_FD");

    /* open PMI socket for comm. between the PMI client and forwarder */
    if (pmiEnableTcp) {
	char cPMI_PORT[50];

	*forwarderSock = init_PMISocket();
	if (*forwarderSock < 0) {
	    mwarn(errno, "%s: create PMI/TCP socket failed", __func__);
	    return PMI_FAILED;
	}
	pmiType = PMI_OVER_TCP;

	get_PMI_PORT(*forwarderSock, cPMI_PORT, sizeof(cPMI_PORT));
	setenv("PMI_PORT", cPMI_PORT, 1);
    }

    /* create a socketpair for comm. between the PMI client and forwarder */
    if (pmiEnableSockp) {
	int socketfds[2];
	char cPMI_FD[50];

	if (socketpair(PF_UNIX, SOCK_STREAM, 0, socketfds)<0) {
	    mwarn(errno, "%s: socketpair()", __func__);
	    return PMI_FAILED;
	}
	*forwarderSock = socketfds[1];
	pmiType = PMI_OVER_UNIX;

	snprintf(cPMI_FD, sizeof(cPMI_FD), "%d", socketfds[0]);
	setenv("PMI_FD", cPMI_FD, 1);
    }

    return pmiType;
}

int handleClientSpawn(void *data)
{
    PStask_t *task = data;

    /* cleanup child environment */
    if (task->group == TG_ANY) {
	char *env = getenv("__PMI_preput_num");

	/* close the forwarder socket in the client process */
	if (pmiType == PMI_OVER_UNIX) close(forwarderSock);

	unsetenv("__KVS_PROVIDER_TID");
	unsetenv("__PMI_PROCESS_MAPPING");
	unsetenv("PMI_KVS_TMP");
	unsetenv("__PMI_SPAWN_PARENT");

	if (env) {
	    int i, num = atoi(env);
	    char buf[100];

	    for (i=0; i<num; i++) {
		snprintf(buf, sizeof(buf), "__PMI_preput_key_%i", i);
		unsetenv(buf);
		snprintf(buf, sizeof(buf), "__PMI_preput_val_%i", i);
		unsetenv(buf);
	    }
	    unsetenv("__PMI_preput_num");
	}
    } else if (task->group == TG_KVS) {
	closeKVSForwarderSock();
    }

    return 0;
}

int handleForwarderSpawn(void *data)
{
    PStask_t *task = data;

    if (task->group == TG_ANY) {
	pmiType = preparePMI(&forwarderSock);
	if (pmiType == PMI_FAILED) return -1;

	setConnectionInfo(pmiType, forwarderSock);
    } else if (task->group == TG_KVS) {
	setupKVSProviderComm();
    }

    return 0;
}
