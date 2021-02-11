/*
 * ParaStation
 *
 * Copyright (C) 2010-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>
#include <popt.h>
#include <netinet/in.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "pscio.h"
#include "psmomlog.h"
#include "pluginmalloc.h"
#include "psmomcomm.h"
#include "psmomproto.h"
#include "psmomconfig.h"
#include "psmomauth.h"

#include "selector.h"

#include "psmomtcp.h"

#define TCP_RESEND  5		/* number of resend retry until we give up */
#define TCP_RECONNECT 10	/* number of reconnect tries until we give up */
#define TCP_BUFFER_ALLOC 4096   /* the start size for the tcp message buffer */
#define TCP_BUFFER_GROW	 1024	/* add min grow size if we realloc the message buffer */

/**
 * Callback for new tcp data.
 *
 * @param sock The socket were new data arrived.
 *
 * @return Returns 1 on error and 0 on success.
 */
static int handleTCPData(int sock, void *data)
{
    ComHandle_t * com;

    if (!(com = getComHandle(sock, TCP_PROTOCOL))) {
	mlog("%s: com handle not found\n", __func__);
	return 1;
    }

    if (com->HandleFunc == NULL ) {
	handleNewData(com);
    } else {
	com->HandleFunc(com->socket, data);
    }
    return 0;
}

int tcpDoSend(int sock, const char *caller)
{
    if (sock < 0) {
	mlog("%s(%s): invalid socket %i\n", __func__, caller, sock);
	return -1;
    }

    ComHandle_t *com = findComHandle(sock, TCP_PROTOCOL);
    if (!com) {
	mlog("%s(%s): no com handle for socket %i\n", __func__, caller, sock);
	return -1;
    }

    if (!com->outBuf || !com->dataSize) {
	mdbg(PSMOM_LOG_WARN, "%s(%s): empty buffer for socket %i\n", __func__,
	     caller, sock);
	return -1;
    }

    mdbg(PSMOM_LOG_TCP, "%s(%s): socket %i len %zu msg '%s'\n", __func__,
	 caller, sock, com->dataSize, com->outBuf);

    size_t sent;
    ssize_t ret = PSCio_sendPProg(sock, com->outBuf, com->dataSize, &sent);
    if (ret < 0) mwarn(errno, "%s: on socket %i", __func__, sock);
    if (sent != com->dataSize) {
	mlog("%s(%s): not all data could be sent, written %zu len %zu\n",
	     __func__, caller, sent, com->dataSize);
    }

    if (com->outBuf) {
	ufree(com->outBuf);
	com->outBuf = NULL;
	com->bufSize = 0;
	com->dataSize = 0;
    }

    return ret;
}

/**
 * @brief Accept a new tcp connection.
 *
 * @param asocket The socket with the new connection.
 *
 * @param data Not used, should be NULL.
 *
 * @return Retruns 1 on error and 0 on success.
 */
static int tcpAcceptClient(int asocket, void *data)
{
    unsigned int clientlen;
    struct sockaddr_in SAddr;
    int socket = -1, rPort, lPort;
    char rAddr[MAX_ADDR_SIZE], lAddr[MAX_ADDR_SIZE];
    unsigned long ip;

    /* accept new TCP connection */
    clientlen = sizeof(SAddr);

    if ((socket = accept(asocket, (void *)&SAddr, &clientlen)) == -1) {
	mwarn(errno, "%s error accepting new tcp connection ", __func__);
	return 1;
    }

    /* get connection infos */
    getRemoteAddrInfo(TCP_PROTOCOL, socket, rAddr, sizeof(rAddr), &rPort, &ip);
    getLocalAddrInfo(socket, lAddr, sizeof(lAddr), &lPort);

    /* check if the remote port can only be used by root. */
    if (rPort > IPPORT_RESERVED) {
	mlog("%s: refused connection from non privileged port:%i\n",
	    __func__, rPort);
	close(socket);
	return 1;
    }

    /* check if client is allowed to connect */
    if (!(isAuthIP(ip))) {
	mlog("%s: refused connection from non authorized addr:%s\n",
	    __func__, rAddr);
	close(socket);
	return 1;
    }

    /* TODO: check if a client is already connected */
    mdbg(PSMOM_LOG_TCP,"%s: from %s socket:%i lPort:%i rPort:%i\n", __func__,
	inet_ntoa(SAddr.sin_addr), socket, lPort, rPort);

    Selector_register(socket, handleTCPData, NULL);
    return 0;
}

int tcpBind(int port)
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
    Selector_register(sock, tcpAcceptClient, NULL);

    return sock;
}

int tcpConnect(int port, char *addr, int priv)
{
    struct sockaddr_in saBind;
    struct addrinfo *result, *rp;
    char cPort[100];
    int sock = -1, ret, bindFailed = 0, reConnect = 0;
    int privPort = IPPORT_RESERVED -1;

    snprintf(cPort, sizeof(cPort), "%d", port);

RETRY_BIND:

    if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
	mwarn(errno, "%s: socket failed, port:%i addr:%s ", __func__,
		port, addr);
	return -1;
    }

    if (priv) {
	/* need privileged port for authentication (proof that we are root) */
	saBind.sin_family = AF_INET;
	saBind.sin_addr.s_addr = 0;
	saBind.sin_port = htons(privPort);

	if (!bindFailed) {
	    if ((bindresvport(sock, &saBind)) == -1) {
		bindFailed = 1;
		close(sock);
		goto RETRY_BIND;
	    }
	} else {
	    /* on some systems bindresvport() is not working, so we have to try
	     *	it our self */
	    while ((bind(sock, (struct sockaddr *)&saBind, sizeof(saBind)))< 0) {
		privPort --;
		if (privPort <= 0) {
		    mwarn(errno, "%s: bind to privileged port failed ",
			    __func__);
		    close(sock);
		    return -1;
		}
		saBind.sin_port = htons(privPort);
	    }
	}
    }

    /* set up the sockaddr structure */
    if ((ret = getaddrinfo(addr, cPort, NULL, &result)) != 0) {
	mlog("%s: getaddrinfo(%s:%s) failed : %s\n", __func__,
		addr, cPort, gai_strerror(ret));
	close(sock);
	return -1;
    }

    mdbg(PSMOM_LOG_TCP,"%s: to %s port:%i socket:%i\n", __func__,
	addr, port, sock);

    for (rp = result; rp != NULL; rp = rp->ai_next) {
	if ((connect(sock, rp->ai_addr, rp->ai_addrlen)) != -1) {
	    if (errno == EINTR) {
		if (reConnect > TCP_RECONNECT) break;

		/* connecting is taken too long, lets start over */
		errno = 0;
		close(sock);
		reConnect++;
		goto RETRY_BIND;
	    }
	    break;
	}
    }
    freeaddrinfo(result);

    /* bind to priviled port failed */
    if (priv && (errno == EADDRNOTAVAIL || errno == EINVAL)) {
	if (bindFailed) privPort--;
	bindFailed = 1;
	close(sock);
	goto RETRY_BIND;
    }

    if (rp == NULL) {
	mwarn(errno, "%s: failed(%i) addr:%s port:%i priv:%i ", __func__,
	    errno, addr, port, privPort);
	close(sock);
	return -1;
    }

    /* add socket to psid selector */
    Selector_register(sock, handleTCPData, NULL);

    return sock;
}

/**
* @brief Close a socket and remove it from the selector facility.
*
* @param sock The tcp socket to close.
*
* @return No return value.
*/
static void doClose(int sock)
{
    /* remove socket from psid selector */
    Selector_remove(sock);

    close(sock);
}

int tcpClose(int sock)
{
    ComHandle_t *com;

    if (sock < 0) return -1;

    if ((com = findComHandle(sock, TCP_PROTOCOL)) == NULL) {
	/* mlog("%s: msg handle not found for sock:%i\n", __func__, sock);*/
	doClose(sock);
	return -1;
    }

    mdbg(PSMOM_LOG_TCP, "%s: addr:%s port:%i sock:%i\n", __func__,
	com->remoteAddr, com->remotePort, sock);

    /* send left over data out */
    if (com->outBuf != NULL) {
	mdbg(PSMOM_LOG_TCP, "%s: sending data left in buffer: %s\n", __func__,
	    com->outBuf);
	tcpDoSend(sock, __func__);
    }

    doClose(sock);
    return 1;
}

ssize_t tcpRead(int sock, char *buffer, ssize_t size, const char *caller)
{
    ssize_t read;

    /* no data received from client */
    if (!(read = recv(sock, buffer, size, 0))) {
        mdbg(PSMOM_LOG_TCP, "%s: no data on socket:%i\n", __func__, sock);
	tcpClose(sock);
	return -1;
    }

    /* socket error occured */
    if (read < 0) {
	if (errno == EINTR) {
	    return tcpRead(sock, buffer, size, caller);
	}
	mwarn(errno, "%s: tcp read on socket:%i failed ", __func__, sock);
        return -1;
    }

    /* FIXME: produces to much output, unreadable
    mdbg(PSMOM_LOG_TCP, "%s: sock:%i size:%zu msg:%s\n", __func__, sock,
	size, buffer);
    */

    return read;
}

int tcpWrite(int sock, void *msg, size_t len, const char *caller)
{
    ComHandle_t *com;
    size_t newlen;

    if (sock < 0) {
	mlog("%s(%s): invalid socket '%i'\n", __func__, caller, sock);
	return -1;
    }

    if ((com = findComHandle(sock, TCP_PROTOCOL)) == NULL) {
	mlog("%s(%s): msg handle not found for socket '%i'\n", __func__, caller,
		sock);
	return -1;
    }

    if (!com->outBuf) {
	if (len < TCP_BUFFER_ALLOC) {
	    com->outBuf = umalloc(TCP_BUFFER_ALLOC);
	    com->bufSize = TCP_BUFFER_ALLOC;
	} else {
	    com->outBuf = umalloc(len +1);
	    com->bufSize = len +1;
	}
	memcpy(com->outBuf, msg, len);
	com->outBuf[len] = '\0';
	com->dataSize = len;
    } else {
	newlen = com->dataSize + len +1;

	if (newlen > com->bufSize) {
	    com->outBuf = urealloc(com->outBuf, newlen + TCP_BUFFER_GROW);
	    com->bufSize = newlen + TCP_BUFFER_GROW;
	}

	memmove(com->outBuf + com->dataSize, msg, len);
	com->outBuf[newlen] = '\0';
	com->dataSize += len;
    }
    return len;
}
