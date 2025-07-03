/*
 * ParaStation
 *
 * Copyright (C) 2023-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "psbyteorder.h"
#include "pscommon.h"
#include "psidhook.h"
#include "psidnodes.h"
#include "psstrbuf.h"
#include "rdp.h"

#include "plugin.h"
#include "pluginpsconfig.h"
#include "dynIPlog.h"

/** psid plugin requirements */
char name[] = "dynIP";
int version = 1;
int requiredAPI = 147;
plugin_dep_t dependencies[] = { { NULL, 0 } };

/** Description of dynIP's configuration parameters */
static const pluginConfigDef_t confDef[] = {
    { "DebugMask", PLUGINCONFIG_VALUE_NUM, "Mask to steer debug output" },
    { NULL, PLUGINCONFIG_VALUE_NONE, NULL }
};

static pluginConfig_t config = NULL;

static bool evalValue(const char *key, const pluginConfigVal_t *val,
		      const void *info)
{
    if (!strcmp(key, "DebugMask")) {
	uint32_t mask = val ? val->val.num : 0;
	maskLogger(mask);
	mdbg(DYNIP_LOG_DEBUG, "debugMask set to %#x\n", mask);
    } else {
	flog("unknown key '%s'\n", key);
    }

    return true;
}

static void initConfig(void)
{
    pluginConfig_new(&config);
    pluginConfig_setDef(config, confDef);

    pluginConfig_addStr(config, "DebugMask", "0");
    pluginConfig_verify(config);

    /* Activate configuration values */
    pluginConfig_traverse(config, evalValue, NULL);
}

void finalizeConfig(void)
{
    pluginConfig_destroy(config);
    config = NULL;
}

static bool nodeVisitor(struct sockaddr_in *saddr, void *info)
{
    PSnodes_ID_t *nodeID = info;
    PSIDnodes_setAddr(*nodeID, saddr->sin_addr.s_addr);

    char hostIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &saddr->sin_addr, hostIP, INET_ADDRSTRLEN);
    flog("set IP %s for node %i\n", hostIP, *nodeID);

    return true;
}

/**
 * @brief Resolve an unkown node
 *
 * @param id Node ID to resolve
 *
 * @return Always returns 0
 */
static int resolveUnknownNode(void *id)
{
    PSnodes_ID_t nodeID = *(PSnodes_ID_t *)id;
    const char *host = PSIDnodes_getHostname(nodeID);
    if (!host) {
	flog("get hostname for node ID %u failed\n", nodeID);
	return 0;
    }

    fdbg(DYNIP_LOG_DEBUG, "resolve %s with node ID %u\n", host, nodeID);

    int rc = PSC_traverseHostInfo(host, nodeVisitor, &nodeID, NULL);
    if (rc) {
	fdbg(DYNIP_LOG_DEBUG, "getaddrinfo(%s) failed: %s\n", host,
	     gai_strerror(rc));
    }

    return 0;
}

typedef struct {
    PSnodes_ID_t nodeID;
    struct sockaddr_in *senderIP;
} senderData_t;

static bool senderVisitor(struct sockaddr_in *saddr, void *info)
{
    const senderData_t *data = info;

    if (data->senderIP->sin_addr.s_addr != saddr->sin_addr.s_addr) return false;

    PSIDnodes_setAddr(data->nodeID, saddr->sin_addr.s_addr);
    RDP_updateNode(data->nodeID, saddr->sin_addr.s_addr);

    char hostIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &saddr->sin_addr, hostIP, INET_ADDRSTRLEN);
    flog("set IP %s for node %i\n", hostIP, data->nodeID);

    return true;
}

static bool resolveSender(senderData_t *data)
{
    in_addr_t nAddr = PSIDnodes_getAddr(data->nodeID);
    /* skip nodes which have a valid address */
    if (nAddr != INADDR_NONE && nAddr != INADDR_ANY) {
	fdbg(DYNIP_LOG_DEBUG, "skip node %i with valid address\n", data->nodeID);
	return false;
    }

    bool match = false;
    const char *host = PSIDnodes_getHostname(data->nodeID);
    int rc = PSC_traverseHostInfo(host, senderVisitor, data, &match);
    if (rc) {
	fdbg(DYNIP_LOG_DEBUG, "getaddrinfo(%s) failed: %s\n", host,
	     gai_strerror(rc));
	return false;
    }
    return match;
}

/**
 * @brief Resolve an unknown sender
 *
 * Examine all nodes which do not have a vaild IP and try to find a matching
 * address by resolving the hostnames.
 *
 * @param senderAddr Pointer to sender's sockaddr_in structure
 *
 * @return Always returns 0
 */
static int resolveUnknownSender(void *data)
{
    RDPUnknown_t *senderInfo = data;
    if (senderInfo->slen != sizeof(struct sockaddr_in)) {
	flog("unexpected sockaddr length %d\n", senderInfo->slen);
	return 0;
    }

    senderData_t senderData = {
	.senderIP = (struct sockaddr_in *) senderInfo->sin,
    };

    if (senderInfo->buflen >= sizeof(int32_t)) {
	/* let's try to take benefit from the hint */
	senderData.nodeID = psntoh32(*(int32_t *)senderInfo->buf);
	if (resolveSender(&senderData)) {
	    fdbg(DYNIP_LOG_DEBUG, "resolved from hint\n");
	    return 0;
	}
    }

    for (PSnodes_ID_t n = 0; n < PSIDnodes_getNum(); n++) {
	senderData.nodeID = n;
	if (resolveSender(&senderData)) return 0;
    }

    return 0;
}

/**
 * @brief Remove IP from an unreachable node
 *
 * The IP address might get re-assigned to another node therefore it
 * has to be reset for a host becoming unreachable.
 *
 * @param nodeID The ID of the node gone down
 *
 * @return Always returns 0
 */
static int handleNodeDown(void *nodeID)
{
    PSnodes_ID_t node = *((PSnodes_ID_t *) nodeID);
    if (!PSIDnodes_isDynamic(node)) return 0;

    struct in_addr sin_addr = { .s_addr = PSIDnodes_getAddr(node) };
    char hostIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &sin_addr, hostIP, INET_ADDRSTRLEN);
    flog("remove IP %s from node %i\n", hostIP, node);

    PSIDnodes_setAddr(node, INADDR_NONE);
    RDP_updateNode(node, INADDR_NONE);

    return 0;
}

int initialize(FILE *logfile)
{
    /* initialize logging facility */
    initLogger(name, logfile);

    /* init configuration (ignores psconfig) */
    initConfig();

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

    finalizeConfig();
    finalizeLogger();
}

char *help(char *key)
{
    strbuf_t buf = strbufNew(NULL);

    strbufAdd(buf, "\n# ");
    strbufAdd(buf, name);
    strbufAdd(buf, " configuration options #\n\n");
    pluginConfig_helpDesc(config, buf);

    return strbufSteal(buf);
}

char *set(char *key, char *val)
{
    const pluginConfigDef_t *thisDef = pluginConfig_getDef(config, key);

    if (!thisDef) return strdup(" Unknown option\n");

    if (!pluginConfig_addStr(config, key, val)
	|| !evalValue(key, pluginConfig_get(config, key), NULL)) {
	return strdup(" Illegal value\n");
    }

    return NULL;
}

char *unset(char *key)
{
    pluginConfig_remove(config, key);
    evalValue(key, NULL, config);

    return NULL;
}

char *show(char *key)
{
    strbuf_t buf = strbufNew(NULL);

    if (!key) {
	/* Show the whole configuration */
	strbufAdd(buf, "\n");
	pluginConfig_traverse(config, pluginConfig_showVisitor, buf);
    } else if (!pluginConfig_showKeyVal(config, key, buf)) {
	strbufAdd(buf, " '");
	strbufAdd(buf, key);
	strbufAdd(buf, "' is unknown\n");
    }

    return strbufSteal(buf);
}
