/*
 * ParaStation
 *
 * Copyright (C) 2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "psidhook.h"
#include "psidplugin.h"
#include "rdp.h"
#include "psidnodes.h"

#include "plugin.h"
#include "dynIPlog.h"

/** psid plugin requirements */
char name[] = "dynIP";
int version = 1;
int requiredAPI = 139;
plugin_dep_t dependencies[] = { { NULL, 0 } };

/**
 * @brief Resolve an unkown node
 *
 * @param data The ID of the node to resolve
 *
 * @return Always returns 0
 */
int resolveUnknownNode(void *data)
{
    PSnodes_ID_t nodeID = *(PSnodes_ID_t *) data;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
    hints.ai_flags = 0;
    hints.ai_protocol = 0;          /* Any protocol */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    const char *host = PSIDnodes_getHostname(nodeID);
    if (!host) {
	flog("get hostname for node ID %u failed", nodeID);
	return 0;
    }

    struct addrinfo *result;
    int rc = getaddrinfo(host, NULL, &hints, &result);
    if (rc) {
	flog("getaddrinfo(%s) failed: %s", host, gai_strerror(rc));
	return 0;
    }

    for (struct addrinfo *rp = result; rp; rp = rp->ai_next) {
	struct sockaddr_in *saddr;
	switch (rp->ai_family) {
	case AF_INET:
	    saddr = (struct sockaddr_in *)rp->ai_addr;
	    PSIDnodes_setAddr(nodeID, saddr->sin_addr.s_addr);

	    char hostIP[INET_ADDRSTRLEN];
	    inet_ntop(AF_INET, &(saddr->sin_addr.s_addr), hostIP,
		      INET_ADDRSTRLEN);
	    flog("set IP %s for node %i\n", hostIP, nodeID);
	    break;
	case AF_INET6:
	    /* ignore -- don't handle IPv6 yet */
	    continue;
	}
    }
    freeaddrinfo(result);

    return 0;
}

/**
 * @brief Resolve an unknown sender
 *
 * Examine all nodes which do not have a vaild IP and try to find a matching
 * address by resolving the hostname.
 *
 * @param senderAddr The IP address of the unknown sender
 *
 * @return Always returns 0
 */
int resolveUnknownSender(void *senderAddr)
{
    struct sockaddr_in *sender = senderAddr;
    bool foundAddr = false;

    for (PSnodes_ID_t n = 0; n < PSIDnodes_getNum(); n++) {
	in_addr_t nAddr = PSIDnodes_getAddr(n);
	/* skip nodes which have a valid address */
	if (nAddr != INADDR_NONE && nAddr != INADDR_ANY) continue;

	const char *host = PSIDnodes_getHostname(n);

	struct addrinfo hints;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
	hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
	hints.ai_flags = 0;
	hints.ai_protocol = 0;          /* Any protocol */
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;

	struct addrinfo *result;
	int rc = getaddrinfo(host, NULL, &hints, &result);
	if (rc) {
	    fdbg(DYNIP_LOG_DEBUG, "getaddrinfo(%s) failed: %s", host,
		 gai_strerror(rc));
	    continue;
	}

	for (struct addrinfo *rp = result; rp; rp = rp->ai_next) {
	    struct sockaddr_in *saddr;
	    switch (rp->ai_family) {
	    case AF_INET:
		saddr = (struct sockaddr_in *)rp->ai_addr;
		if (sender->sin_addr.s_addr == saddr->sin_addr.s_addr) {
		    PSIDnodes_setAddr(n, saddr->sin_addr.s_addr);
		    RDP_updateNode(n, saddr->sin_addr.s_addr);

		    char hostIP[INET_ADDRSTRLEN];
		    inet_ntop(AF_INET, &(saddr->sin_addr.s_addr), hostIP,
			    INET_ADDRSTRLEN);
		    flog("set IP %s for node %i\n", hostIP, n);
		    foundAddr = true;
		}
		break;
	    case AF_INET6:
		/* ignore -- don't handle IPv6 yet */
		continue;
	    }
	}
	freeaddrinfo(result);

	if (foundAddr) break;
    }

    return 0;
}

/**
 * @brief Remove IP from an unreachable node
 *
 * The IP address might get re-signed to another node therefore it
 * has to be reset for a host becoming unreachable.
 *
 * @param nodeID The ID of the node gone down
 *
 * @return Always returns 0
 */
static int handleNodeDown(void *nodeID)
{
    /* Hook Node Down → PSID_nodesSetAddr(none) + RDP_updateNode() */

    PSnodes_ID_t node = *((PSnodes_ID_t *) nodeID);

    in_addr_t nAddr = PSIDnodes_getAddr(node);
    char hostIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(nAddr), hostIP, INET_ADDRSTRLEN);
    flog("remove IP %s from node %i\n", hostIP, node);

    PSIDnodes_setAddr(node, INADDR_NONE);
    RDP_updateNode(node, INADDR_NONE);

    return 0;
}

int initialize(FILE *logfile)
{
    /* initialize logging facility */
    initLogger(name, logfile);

    if (!PSIDhook_add(PSIDHOOK_NODE_UNKNOWN, resolveUnknownNode)) {
	flog("register 'PSIDHOOK_NODE_UNKNOWN' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_SENDER_UNKNOWN, resolveUnknownSender)) {
	flog("register 'PSIDHOOK_SENDER_UNKNOWN' failed\n");
	return false;
    }

    if (!PSIDhook_add(PSIDHOOK_NODE_DOWN, handleNodeDown)) {
	flog("register 'PSIDHOOK_NODE_DOWN' failed\n");
	return false;
    }

    return 0;
}

void cleanup(void)
{
    if (!PSIDhook_del(PSIDHOOK_NODE_UNKNOWN, resolveUnknownNode)) {
	flog("unregister 'PSIDHOOK_NODE_UNKNOWN' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_SENDER_UNKNOWN, resolveUnknownSender)) {
	flog("unregister 'PSIDHOOK_SENDER_UNKNOWN' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_NODE_DOWN, handleNodeDown)) {
	flog("unregister 'PSIDHOOK_NODE_DOWN' failed\n");
    }
}

/*
char *help(char *key)
{

}

char *set(char *key, char *val)
{

}

char *unset(char *key)
{

}

char *show(char *key)
{

}
*/
