/*
 * ParaStation
 *
 * Copyright (C) 2010-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "psmomlog.h"
#include "pluginmalloc.h"
#include "psmomtcp.h"
#include "psmomrpp.h"
#include "psmomjob.h"
#include "psmomscript.h"
#include "psmomlocalcomm.h"
#include "psmompbsserver.h"

#include "selector.h"
#include "timer.h"

#include "psmomcomm.h"

ComHandle_t ComList;

void initComList()
{
    INIT_LIST_HEAD(&ComList.list);
}

char *protocolType2String(int type)
{
    switch(type) {
	case TCP_PROTOCOL:
	    return "TCP";
	case RPP_PROTOCOL:
	    return "RPP";
	case UNIX_PROTOCOL:
	    return "UNIX";
    }

    return NULL;
}

int getLocalAddrInfo(int socket, char *Iaddr, size_t addrSize, int *Iport)
{
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    if (getsockname(socket, (struct sockaddr*)&addr, &len) == -1) {
	//mwarn(errno, "get local port for '%i' failed\n", socket);
	return 1;
    }

    *Iport = ntohs(addr.sin_port);
    inet_ntop(AF_INET, &addr.sin_addr, Iaddr, addrSize);

    return 0;
}

int getRemoteAddrInfo(Protocol_t type, int socket, char *Iaddr,
			size_t addrSize, int *Iport, unsigned long *lAddr)
{
    struct sockaddr_in addr;
    struct sockaddr_in *addr_ptr;
    socklen_t len = sizeof(addr);

    switch (type) {
	case TCP_PROTOCOL:
	case UNIX_PROTOCOL:
	    if ((getpeername(socket, (struct sockaddr*)&addr, &len)) == -1) {
		return 1;
	    }
	    break;
	case RPP_PROTOCOL:
	    if (!(addr_ptr = rppGetAddr(socket))) {
		return 1;
	    }
	    addr = *addr_ptr;
	    break;
	default:
	    return 1;
    }

    if (lAddr) *lAddr = addr.sin_addr.s_addr;
    *Iport = ntohs(addr.sin_port);
    inet_ntop(AF_INET, &addr.sin_addr, Iaddr, addrSize);

    return 0;
}

ComHandle_t *getComHandle(int socket, Protocol_t type)
{
    ComHandle_t *com;
    /*
    int localPort = getLocalPort(socket);
    int remotePort = getRemotePort(socket);

    mlog("socket:%i lport:%i rport:%i\n", socket, localPort, remotePort);
    */
    if ((com = findComHandle(socket, type)) == NULL) {
	com = (ComHandle_t *) umalloc( sizeof(ComHandle_t));
	initComHandle(com, type, socket);
	list_add_tail(&(com->list), &ComList.list);

    }
    return com;
}

int isValidComHandle(ComHandle_t *testCom)
{
    list_t *pos, *tmp;
    ComHandle_t *com;

    if (list_empty(&ComList.list)) return 0;

    list_for_each_safe(pos, tmp, &ComList.list) {
	if ((com = list_entry(pos, ComHandle_t, list)) == NULL) {
	    return 0;
	}

	if (com == testCom) {
	    return 1;
	}
    }
    return 0;
}

ComHandle_t *findComHandleByJobid(char *jobid)
{
    list_t *pos, *tmp;
    ComHandle_t *com;

    if (list_empty(&ComList.list)) return NULL;

    list_for_each_safe(pos, tmp, &ComList.list) {
	if ((com = list_entry(pos, ComHandle_t, list)) == NULL) {
	    return NULL;
	}

	if (!com->jobid) continue;
	if (!(strcmp(jobid, com->jobid))) {
	    return com;
	}
    }
    return NULL;
}

ComHandle_t *findComHandle(int socket, Protocol_t type)
{
    list_t *pos, *tmp;
    ComHandle_t *com;
    char remoteAddr[MAX_ADDR_SIZE], localAddr[MAX_ADDR_SIZE];
    int remotePort = -1, localPort = -1;

    getRemoteAddrInfo(type, socket, remoteAddr, sizeof(remoteAddr),
			&remotePort, NULL);
    getLocalAddrInfo(socket, localAddr, sizeof(localAddr), &localPort);

    if (list_empty(&ComList.list)) return NULL;

    list_for_each_safe(pos, tmp, &ComList.list) {
	if ((com = list_entry(pos, ComHandle_t, list)) == NULL) return NULL;

	/* need special handling for unix domain sockets */
	if (com->socket == socket && com->type == type &&
	    (type == UNIX_PROTOCOL || type == RPP_PROTOCOL)) {
	    return com;
	}
	if (com->socket == socket && com->type == type &&
	    com->localPort == localPort && com->remotePort == remotePort) {
	    return com;
	}
    }
    return NULL;
}

void closeAllConnections()
{
    list_t *pos, *tmp;
    ComHandle_t *com;

    /* don't obit any jobs */
    if (jobObitTimerID != -1) {
	Timer_remove(jobObitTimerID);
	jobObitTimerID = -1;
    }

    /* close all server Connections */
    clearServerList();

    if (list_empty(&ComList.list)) {
	rppShutdown();
	return;
    }

    list_for_each_safe(pos, tmp, &ComList.list) {
	if ((com = list_entry(pos, ComHandle_t, list)) == NULL) {
	    return;
	}
	wClose(com);
    }
    rppShutdown();
}

char *ComHType2String(int type)
{
    switch(type) {
	case TCP_PROTOCOL:
	    return "TCP_PROTOCOL";
	case RPP_PROTOCOL:
	    return "RPP_PROTOCOL";
	case UNIX_PROTOCOL:
	    return "UNIX_PROTOCOL";
    }
    return "Unknown";
}

void initComHandle(ComHandle_t *com, Protocol_t type, int socket)
{
    switch(type) {
	case TCP_PROTOCOL:
	    com->Reconnect = NULL;
	    com->Read = &tcpRead;
	    com->Write = &tcpWrite;
	    com->Close = &tcpClose;
	    com->Bind = &tcpBind;
	    com->DoSend = &tcpDoSend;
	    break;
	case RPP_PROTOCOL:
	    com->Reconnect = &rppReconnect;
	    com->Read = &rppRead;
	    com->Write = &rppWrite;
	    com->Close = &rppClose;
	    com->Bind = &rppBind;
	    com->DoSend = &rppDoSend;
	    break;
	case UNIX_PROTOCOL:
	    com->Read = &localRead;
	    com->Write = &localWrite;
	    com->DoSend = &localDoSend;
	    com->Close = &closeLocalConnetion;
	    break;
	default:
	    mlog("%s: unknown protocol type '%i'\n", __func__, type);
    }
    com->type = type;
    com->socket = socket;
    com->bufSize = 0;
    com->dataSize = 0;
    com->outBuf = NULL;
    com->inBuf = NULL;
    com->HandleFunc = NULL;
    com->jobid = NULL;
    com->info = NULL;
    com->isAuth = 0;

    if ((getLocalAddrInfo(socket, com->addr, sizeof(com->addr),
			    &com->localPort))) {
	com->addr[0] = '\0';
	com->localPort = -1;
    }

    if ((getRemoteAddrInfo(type, socket, com->remoteAddr,
			    sizeof(com->remoteAddr), &com->remotePort,
			    &com->lremoteAddr))) {
	com->remoteAddr[0] = '\0';
	com->remotePort = -1;
    }

    /*
    if (type == 2) {
	mlog("%s: sock:%i rPort:%i lPort:%i type:%s\n", __func__, socket,
	    com->remotePort, com->localPort, ComHType2String(type));
    }
    */
}

ComHandle_t *wConnect(int port, char *addr, Protocol_t type)
{
    ComHandle_t *com;
    int socket;

    switch(type) {
	case TCP_PROTOCOL:
	    if ((socket = tcpConnect(port, addr, 1)) == -1) {
		return NULL;
	    }
	    break;
	case RPP_PROTOCOL:
	    if ((socket = rppOpen(port, addr)) == -1) {
		return NULL;
	    }
	    break;
	default:
	    mlog("%s: unknown protocol\n", __func__);
	    return NULL;
    }

    com = getComHandle(socket, type);

    return com;
}

void wDisable(ComHandle_t *com)
{
    if (!com) {
	mlog("%s: invalid function pointer\n", __func__);
	return;
    }
    Selector_disable(com->socket);
}

void wEnable(ComHandle_t *com)
{
    if (!com) {
	mlog("%s: invalid function pointer\n", __func__);
	return;
    }
    Selector_enable(com->socket);
}

int __wReconnect(ComHandle_t *com, const char *caller)
{
    if (!com) {
	mlog("%s: invalid function pointer\n", __func__);
	return -1;
    }
    if (!com->Reconnect) return 0;

    return com->Reconnect(com->socket, caller);
}

int __wWrite(ComHandle_t *com, void *msg, size_t len, const char *caller)
{
    if (!com || com->socket == -1) {
	mlog("%s: invalid com handle or socket\n", __func__);
	return -1;
    }
    return com->Write(com->socket, msg, len, caller);
}

static ssize_t wDoRead(ComHandle_t *com, void *buffer, size_t bufsize,
			ssize_t len, const char *caller)
{
    ssize_t ret = 0, read = 0, toread = 0;
    char *ptr;

    if ((size_t) len > bufsize || len < 1) {
	mlog("%s(%s): invalid buffer\n", __func__, caller);
	return -1;
    }

    ptr = buffer;
    toread = len;

    while (toread > 0 &&
		(ret = com->Read(com->socket, ptr, toread, caller)) > 0) {
	read += ret;
	ptr += ret;
	toread -= ret;
    }
    return read;
}

ssize_t __wReadAll(ComHandle_t *com, void *buffer, ssize_t len,
		    const char *caller)
{
    if (len < 1) return -1;
    return com->Read(com->socket, buffer, len, caller);
}

ssize_t __wRead(ComHandle_t *com, void *buffer, ssize_t len, const char *caller)
{
    if (!com || !com->Read) {
	mlog("%s(%s): invalid function pointer\n", __func__, caller);
	return -1;
    }
    if (len < 1) return 0;

    return wDoRead(com, buffer, len, len, caller);
}

ssize_t __wReadT(ComHandle_t *com, char *buffer, size_t bufSize, ssize_t len,
		const char* caller)
{
    ssize_t read;

    if (!com || !com->Read) {
	mlog("%s(%s): invalid com handle\n", __func__, caller);
	return -1;
    }

    if (len < 1) {
	mlog("%s(%s): nothing to do, size to read '%zu'\n", __func__,
		caller, len);
	return 0;
    }

    if ((size_t) len > bufSize -1) {
	mlog("%s(%s): buffer too small\n", __func__, caller);
	return -1;
    }

    buffer[0] = '\0';
    read = wDoRead(com, buffer, bufSize, len, caller);
    buffer[read] = '\0';
    return read;
}

void wClose(ComHandle_t *com)
{
    if (!com) {
	mlog("%s: invalid function pointer\n", __func__);
	return;
    }

    if (!(isValidComHandle(com))) return;

    com->Close(com->socket);
    com->socket = -1;
    com->localPort = -1;
    com->remotePort = -1;

    if (com->jobid) {
	ufree(com->jobid);
	com->jobid = NULL;
    }
    if (com->outBuf) ufree(com->outBuf);
    if (com->info) ufree(com->info);

    list_del(&com->list);
    ufree(com);
}

void wEOM(ComHandle_t *com)
{
    if (!com) {
	mlog("%s: invalid function pointer\n", __func__);
	return;
    }
    if (com->type == RPP_PROTOCOL) {
	rppEOM(com->socket);
    }
}

void wFlush(ComHandle_t *com)
{
    if (!com) {
	mlog("%s: invalid function pointer\n", __func__);
	return;
    }
    if (com->type == RPP_PROTOCOL) {
	rppFlush(com->socket);
    }
}

int __wDoSend(ComHandle_t *com, const char *caller)
{
    if (!com) {
	mlog("%s: invalid function pointer\n", __func__);
	return -1;
    }
    return com->DoSend(com->socket, caller);
}

int wBind(int port, Protocol_t type)
{
    int socket;
    ComHandle_t *com;

    switch(type) {
	case TCP_PROTOCOL:
	    socket = tcpBind(port);
	    com = getComHandle(socket, TCP_PROTOCOL);
	    com->info = ustrdup("listen");
	    return socket;
	case RPP_PROTOCOL:
	    return rppBind(port);
	default:
	    mlog("%s: unknown protocol\n", __func__);
    }
    return -1;
}
