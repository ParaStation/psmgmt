/*
 * ParaStation
 *
 * Copyright (C) 2011-2018 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psmompbsserver.h"

#include <netinet/in.h>
#include <string.h>
#include <sys/time.h>

#include "timer.h"
#include "pluginconfig.h"
#include "pluginmalloc.h"

#include "psmomauth.h"
#include "psmomconfig.h"
#include "psmomlog.h"
#include "psmomproto.h"
#include "psmomrpp.h"

/** timer id to check periodically the server connection status */
static int serverConnectionID = -1;

/** timer id to periodically poll for rpp changes */
static int pollTimerID = -1;

/** timer id to periodically send update (keep-alive) messages to all servers */
static int serverUpdateID = -1;

/** keep alive time interval */
static int keepAliveTime;

Server_t ServerList;

void initServerList()
{
    INIT_LIST_HEAD(&ServerList.list);
}

static Server_t *addServer(char *addr)
{
    Server_t *serv;

    serv = (Server_t *) umalloc(sizeof(Server_t));
    serv->addr = ustrdup(addr);
    serv->com = NULL;
    serv->lastContact = 0;
    serv->haveConnection = 0;

    list_add_tail(&(serv->list), &ServerList.list);

    return serv;
}

void clearServerList()
{
    list_t *pos, *tmp;
    Server_t *serv;

    if (serverConnectionID != -1) {
	Timer_remove(serverConnectionID);
	serverConnectionID = -1;
    }

    if (serverUpdateID != -1) {
	Timer_remove(serverUpdateID);
	serverUpdateID = -1;
    }

    if (pollTimerID != -1) {
	Timer_remove(pollTimerID);
	pollTimerID = -1;
    }

    if (list_empty(&ServerList.list)) return;

    list_for_each_safe(pos, tmp, &ServerList.list) {
	if ((serv = list_entry(pos, Server_t, list)) == NULL) {
	    return;
	}

	ufree(serv->addr);
	list_del(&serv->list);
	ufree(serv);
    }
}

Server_t *findServerByrAddr(char *addr)
{
    list_t *s;

    if (!addr) return NULL;

    list_for_each(s, &ServerList.list) {
	Server_t *serv = list_entry(s, Server_t, list);

	if (!strcmp(serv->addr, addr)) return serv;
	if (!strcmp(serv->com->remoteAddr, addr)) return serv;
    }

    return NULL;
}

Server_t *findServer(ComHandle_t *com)
{
    list_t *pos;
    Server_t *serv;

    if (list_empty(&ServerList.list)) return NULL;

    list_for_each(pos, &ServerList.list) {
	if ((serv = list_entry(pos, Server_t, list)) == NULL) return NULL;

	if (serv->com == com) return serv;
    }
    return NULL;
}

static void checkServerConnections(void)
{
    list_t *pos;
    Server_t *serv;

    if (list_empty(&ServerList.list)) return;

    list_for_each(pos, &ServerList.list) {
	if ((serv = list_entry(pos, Server_t, list)) == NULL) return;

	/* did we ever speak to the server? */
	if (serv->lastContact > 0) {

	    if (serv->lastContact + keepAliveTime < time(NULL)) {

		/* do we have currently a connection? */
		if (serv->haveConnection) {

		    /* request an update from the server */
		    mdbg(PSMOM_LOG_VERBOSE, "%s: long time no see, requesting "
			 "update from %s\n", __func__, serv->com->remoteAddr);

		    /* after successful InitIS -> haveConnection = 1 */
		    serv->lastContact = time(NULL);
		    serv->haveConnection = 0;
		    InitIS(serv->com);
		} else {
		    mlog("%s: lost connection to PBS server '%s', resetting "
			 "connection\n", __func__, serv->com->remoteAddr);
		    serv->lastContact = 0;
		    wReconnect(serv->com);
		}
	    }

	} else if (!serv->haveConnection) {
	    wReconnect(serv->com);
	}
    }
}

void sendStatusUpdate(void)
{
    list_t *pos;
    Server_t *serv;

    if (list_empty(&ServerList.list)) return;

    list_for_each(pos, &ServerList.list) {
	if ((serv = list_entry(pos, Server_t, list)) == NULL) {
	    return;
	}

	if (serv->lastContact) {
	    if ((updateServerState(serv->com)) == -1) {
		mlog("%s: updating server status for '%s' failed\n", __func__,
		    serv->addr);
	    }
	}
    }
}

static void callrppPoll(void)
{
    rppPoll(/* dummy */ 0, /* dummy */ 0);
}

int openServerConnections()
{
    char *allServers, *toksave, *copy, *next;
    const char delimiters[] =", ";
    int serverPort, Time;
    struct timeval Timer = {0,0};
    ComHandle_t *com;
    Server_t *serv;
    struct sockaddr_in* lAddr;

    allServers = getConfValueC(config, "PBS_SERVER");
    if (!allServers) {
	mlog("%s: PBS server not configured, cannot continue\n", __func__);
	return 1;
    }

    serverPort = getConfValueI(config, "PORT_SERVER");

    copy = ustrdup(allServers);
    next = strtok_r(copy, delimiters, &toksave);

    while (next) {
	serv = addServer(next);
	if (!(com = wConnect(serverPort, next, RPP_PROTOCOL))) {
	    mlog("%s: connection to PBS server '%s:%i' failed\n",
		__func__, next, serverPort);
	} else {
	    mdbg(PSMOM_LOG_VERBOSE, "%s: connecting to server '%s:%i'\n",
		__func__, next, serverPort);
	    com->info = ustrdup("PBS server");

	    if (!(lAddr = rppGetAddr(com->socket))) {
		mlog("%s: resolving addr for PBS server '%s:%i' failed\n",
		    __func__, next, serverPort);
	    } else {
		/* insert into authorized server list */
		addAuthIP(lAddr->sin_addr.s_addr);
	    }

	    wEOM(com);
	    InitIS(com);
	}
	serv->com = com;
	next = strtok_r(NULL, delimiters, &toksave);
    }
    ufree(copy);

    /* check peridically if server connection is still in place */
    keepAliveTime = getConfValueI(config, "TIME_KEEP_ALIVE");

    if (keepAliveTime > 0) {
	Timer.tv_sec = 30;
	Timer.tv_usec = 0;
	if ((serverConnectionID = Timer_register(&Timer,
	    checkServerConnections)) == -1) {
	    mlog("%s: registering server tracking timer failed\n", __func__);
	    return 1;
	}
    } else {
	/* we need polling then ... */
	Timer.tv_sec = 2;
	Timer.tv_usec = 0;
	if ((pollTimerID = Timer_register(&Timer, callrppPoll)) == -1) {
	    mlog("%s: registering polling timer failed\n", __func__);
	    return 1;
	}
    }

    /* send periodic update messages (also used as keep alive) to all servers */
    Time = getConfValueI(config, "TIME_UPDATE");
    Timer.tv_sec = Time;
    Timer.tv_usec = 0;
    if ((serverUpdateID = Timer_register(&Timer, sendStatusUpdate)) == -1) {
	mlog("%s: registering server update timer failed\n", __func__);
	return 1;
    }

    return 0;
}
