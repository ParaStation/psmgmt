/*
 *               ParaStation
 * psipartition.c
 *
 * Setting up partitions for spawning of processes.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psipartition.c,v 1.1 2003/09/12 14:23:21 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psipartition.c,v 1.1 2003/09/12 14:23:21 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "pscommon.h"
#include "psprotocol.h"

#include "psi.h"
#include "psilog.h"
#include "info.h"

#include "psipartition.h"

/*
 * The name for the environment variable setting the nodes used
 * when spawning.
 */
#define ENV_NODE_NODES     "PSI_NODES"
#define ENV_NODE_HOSTS     "PSI_HOSTS"
#define ENV_NODE_HOSTFILE  "PSI_HOSTFILE"
#define ENV_NODE_SORT      "PSI_NODES_SORT"
#define ENV_NODE_HOSTS_LSF "LSB_HOSTS"

static char errtxt[256];

void PSI_LSF(void)
{
    char *lsf_hosts=NULL;

    snprintf(errtxt, sizeof(errtxt), "%s()", __func__);
    PSI_errlog(errtxt, 10);

    lsf_hosts=getenv(ENV_NODE_HOSTS_LSF);
    if (lsf_hosts) {
	setenv(ENV_NODE_SORT, "none", 1);
	unsetenv(ENV_NODE_HOSTFILE);
	unsetenv(ENV_NODE_NODES);
	setenv(ENV_NODE_HOSTS, lsf_hosts, 1);
    }
}

/** @todo docu */
static PSpart_sort_t getSortMode(void)
{
    PSpart_sort_t sort = PART_SORT_PROC;
    char *env_sort = getenv(ENV_NODE_SORT);

    if (!env_sort) return PART_SORT_PROC;

    if (strcasecmp(env_sort,"LOAD")==0 || strcasecmp(env_sort,"LOAD_1")==0) {
	return PART_SORT_LOAD_1;
    } else if (strcasecmp(env_sort,"LOAD_5")==0) {
	return PART_SORT_LOAD_5;
    } else if (strcasecmp(env_sort,"LOAD_15")==0) {
	return PART_SORT_LOAD_15;
    } else if (strcasecmp(env_sort,"PROC")==0) {
	return PART_SORT_PROC;
    } else if (strcasecmp(env_sort,"PROC+LOAD")==0) {
	return PART_SORT_PROCLOAD;
    }

    snprintf(errtxt, sizeof(errtxt), "%s: Unknown criterium '%s'\n", __func__,
	     env_sort);
    PSI_errlog(errtxt, 0);

    return PART_SORT_UNKNOWN;
}

#define ENV_PART_LOOPNODES "PSI_LOOP_NODES_FIRST"
#define ENV_PART_EXCLUSIVE "PSI_EXCLUSIVE"
#define ENV_PART_OVERBOOK  "PSI_OVERBOOK"
#define ENV_PART_WAIT      "PSI_WAIT"

/** @todo docu */
static PSpart_option_t getPartitionOptions(void)
{
    PSpart_option_t options = 0;

    if (getenv(ENV_PART_LOOPNODES)) options |= PART_OPT_NODEFIRST;
    if (getenv(ENV_PART_EXCLUSIVE)) options |= PART_OPT_EXCLUSIVE;
    if (getenv(ENV_PART_OVERBOOK)) options |= PART_OPT_OVERBOOK;
    if (getenv(ENV_PART_WAIT)) options |= PART_OPT_WAIT;

    return options;
}

typedef struct {
    int size;              /**< Number of valid entries within nodes[] */
    int maxsize;           /**< Maximum number of entries within nodes[] */
    unsigned short *nodes; /**< ParaStation IDs of the requested nodes. */
} nodelist_t;

/** @todo docu */
static int addNode(unsigned short node, nodelist_t *nl)
{
    snprintf(errtxt, sizeof(errtxt), "%s(%d)", __func__, node);
    PSI_errlog(errtxt, 10);

    if (nl->size == nl->maxsize) {
	nl->maxsize += 128;
	nl->nodes = realloc(nl->nodes, nl->maxsize * sizeof(*nl->nodes));
	if (!nl->nodes) {
            snprintf(errtxt, sizeof(errtxt), "%s: no memory.\n", __func__);
            PSI_errlog(errtxt, 0);
            return 0;
	}
    }

    nl->nodes[nl->size] = node;
    nl->size++;

    return 1;
}

/**
 * @brief Extend nodelist by range.
 *
 * Extend @a nodelist by @a range of the form 'first[-last]'.
 *
 * @param range Character array of the form 'first[-last]'.
 *
 * @param nodelist Nodelist to extend.
 *
 * @return On success, the number of nodes added to the nodelist is
 * returned. Otherwise 0 is returned.
 */
static int nodelistFromRange(char *range, nodelist_t *nodelist)
{
    long first, last, i;
    char *start = strsep(&range, "-"), *end;

    first = strtol(start, &end, 0);
    if (*end != '\0') return 0;
    if (first < 0 || first >= PSC_getNrOfNodes()) {
        snprintf(errtxt, sizeof(errtxt), "node %ld out of range.", first);
        PSI_errlog(errtxt, 0);
        return 0;
    }

    if (range) {
        last = strtol(range, &end, 0);
        if (*end != '\0') return 0;
        if (last < 0 || last >= PSC_getNrOfNodes()) {
            snprintf(errtxt, sizeof(errtxt), "node %ld out of range.", last);
            PSI_errlog(errtxt, 0);
            return 0;
        }
    } else {
        last = first;
    }

    /* Now put the range into the nodelist */
    for (i=first; i<=last; i++) addNode(i, nodelist);

    return last - first + 1;
}

/**
 * @brief Get nodelist from node-string.
 *
 * Get @a nodelist from @a node_str of the form range{,range}*.
 *
 * @param node_str Nodestring of the form 'range{,range}*'. The form
 * of a range is described within @ref nodelistFromRange().
 *
 * @param nodelist Nodelist to be build.
 *
 * @return On success, the size of the nodelist is returned. Otherwise
 * 0 is returned.
 */
static int nodelistFromNodeStr(char *nodeStr, nodelist_t *nodelist)
{
    char *work, *range = strtok_r(nodeStr, ",", &work);

    while (range) {
        if (!nodelistFromRange(range, nodelist)) return 0;
        range = strtok_r(NULL, ",", &work);
    }
    return nodelist->size;
}

/** @todo docu */
static int nodelistFromHost(char *host, nodelist_t *nodelist)
{
    struct hostent *hp;
    struct in_addr sin_addr;
    int node;

    hp = gethostbyname(host);
    if (!hp) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: unknown node '%s'.", __func__, host);
	PSI_errlog(errtxt, 0);
	return 0;
    }

    memcpy(&sin_addr, hp->h_addr_list[0], hp->h_length);
    node = INFO_request_host(sin_addr.s_addr, 0);

    if (node < 0) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: cannot get ParaStation ID for node '%s'.",
		 __func__, host);
	PSI_errlog(errtxt, 0);
	return 0;
    } else if (node >= PSC_getNrOfNodes()) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: ParaStation ID %d for node '%s' out of range.",
		 __func__, node, host);
	PSI_errlog(errtxt, 0);
	return 0;
    }

    addNode(node, nodelist);
    return 1;
}

/** @todo docu */
static int nodelistFromHostStr(char *hostStr, nodelist_t *nodelist)
{
    char *work, *host = strtok_r(hostStr, " \f\n\r\t\v", &work);
    int total = 0;

    while (host) {
	int num = nodelistFromHost(host, nodelist);
	if (!num) return 0;
	total += num;
	host = strtok_r(NULL, " \f\n\r\t\v", &work);
    }
    return total;
}

/** @todo docu */
static int nodelistFromHostFile(char *fileName, nodelist_t *nodelist)
{
    FILE* file = fopen(fileName, "r");
    char line[1024];
    int total = 0;

    if (!file) {
	snprintf(errtxt, sizeof(errtxt), "%s: cannot open file <%s>.",
		 __func__, fileName);
	PSI_errlog(errtxt, 0);
	return 0;
    }

    while (fgets(line, sizeof(line), file)) {
	if (line[0] == '#') continue;
	if (nodelistFromHostStr(line, nodelist) != 1) {
	    snprintf(errtxt, sizeof(errtxt), "%s: syntax error at: '%s'.",
		     __func__, line);
	    PSI_errlog(errtxt, 0);
	    fclose(file);
	    return 0;
	} else
	    total++;
    }
    fclose(file);
    return total;
}

/** @todo docu */
static nodelist_t *getNodelist(void)
{
    int i;
    char *nodeStr = NULL;
    char *hostStr = NULL;
    char *hostfileStr = NULL;
    nodelist_t *nodelist;

    snprintf(errtxt, sizeof(errtxt), "%s", __func__);
    PSI_errlog(errtxt, 10);

    if (!(nodeStr = getenv(ENV_NODE_NODES))
	&& !(hostStr = getenv(ENV_NODE_HOSTS))
	&& !(hostfileStr = getenv(ENV_NODE_HOSTFILE)))
	return NULL;

    nodelist = malloc(sizeof(nodelist_t));
    if (!nodelist) {
	snprintf(errtxt, sizeof(errtxt), "%s: no memory.\n", __func__);
	PSI_errlog(errtxt, 0);
	return NULL;
    }
    *nodelist = (nodelist_t) {
	.size = 0,
	.maxsize = 0,
	.nodes = NULL };

    if (nodeStr) {
	if (!nodelistFromNodeStr(nodeStr, nodelist)) goto error;
    } else if (hostStr) {
	if (!nodelistFromHostStr(hostStr, nodelist)) goto error;
    } else if (hostfileStr) {
	if (!nodelistFromHostFile(hostfileStr, nodelist)) goto error;
    }
    endhostent();
    return nodelist;

 error:
    if (nodelist->nodes) {
	free(nodelist->nodes);
	nodelist->nodes = NULL;
    }
    nodelist->size = -1;
    return nodelist;
}

int PSI_createPartition(unsigned int size, unsigned int hwType)
{
    DDBufferMsg_t msg = (DDBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_CREATEPART,
	    .dest = PSC_getTID(-1, 0),
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg.header)},
	.buf = {0}};
    PSpart_sort_t sort = getSortMode();
    PSpart_option_t options = getPartitionOptions();
    unsigned int priority = 0; /* Not used */
    nodelist_t *nodelist;
    char *ptr = msg.buf;

    *(unsigned int *)ptr = size;
    ptr += sizeof(size);
    msg.header.len += sizeof(size);

    *(unsigned int *)ptr = hwType;
    ptr += sizeof(hwType);
    msg.header.len += sizeof(hwType);

    if (sort == PART_SORT_UNKNOWN) return -1;
    *(PSpart_sort_t *)ptr = sort;
    ptr += sizeof(sort);
    msg.header.len += sizeof(sort);

    *(PSpart_option_t *)ptr = options;
    ptr += sizeof(options);
    msg.header.len += sizeof(options);

    *(unsigned int *)ptr = priority;
    ptr += sizeof(priority);
    msg.header.len += sizeof(priority);

    nodelist = getNodelist();
    if (nodelist) {
	if (nodelist->size < 0) {
	    free(nodelist);
	    return -1;
	}
	*(int *)ptr = nodelist->size;
	ptr += sizeof(nodelist->size);
	msg.header.len += sizeof(nodelist->size);
    } else {
	*(int *)ptr = 0;
	ptr += sizeof(int);
	msg.header.len += sizeof(int);
    }

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errlog(errtxt, 0);
	return -1;
    }

    if (nodelist) {
	int offset = 0;

	msg.header.type = PSP_CD_CREATEPARTNL;
	while (offset < nodelist->size) {
	    int chunk = (nodelist->size-offset > GETNODES_CHUNK) ?
		GETNODES_CHUNK : nodelist->size-offset;
	    msg.header.len = sizeof(msg.header);
	    ptr = msg.buf;

	    *(int *)ptr = chunk;
	    ptr += sizeof(int);
	    msg.header.len += sizeof(int);

	    memcpy(ptr, nodelist->nodes+offset,
		   chunk * sizeof(*nodelist->nodes));
	    msg.header.len += chunk * sizeof(*nodelist->nodes);
	    offset += chunk;
	    if (PSI_sendMsg(&msg)<0) {
		snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
		PSI_errlog(errtxt, 0);
		return -1;
	    }
	}
    }

    if (PSI_recvMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: recv", __func__);
	PSI_errlog(errtxt, 0);
	return -1;
    }

    switch (msg.header.type) {
    case PSP_CD_PARTITIONRES:
	ptr = msg.buf;
	if (*(int *)ptr) {
	    char *errstr = strerror(((DDErrorMsg_t *)&msg)->error);
	    snprintf(errtxt, sizeof(errtxt), "%s: error: %s\n",
		     __func__, errtxt ? errtxt : "UNKNOWN");
	    return -1;
	}
	break;
    case PSP_CD_ERROR:
    {
	char *errstr = strerror(((DDErrorMsg_t *)&msg)->error);
	snprintf(errtxt, sizeof(errtxt), "%s: error in command %s : %s\n",
		 __func__, PSP_printMsg(((DDErrorMsg_t*)&msg)->request),
		 errtxt ? errtxt : "UNKNOWN");
	return -1;
	break;
    }
    default:
	fprintf(stderr, "%s: received unexpected msgtype '%s'.",
		__func__, PSP_printMsg(msg.header.type));
	return -1;
    }

    return size;
}

int PSI_getNodes(unsigned int num, short *nodes)
{
    DDBufferMsg_t msg = (DDBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_GETNODES,
	    .dest = PSC_getTID(-1, 0),
	    .sender = PSC_getMyTID(),
	    .len = sizeof(DDMsg_t) },
	.buf = { 0 } };
    char *ptr = msg.buf;
    int ret = -1;

    if (num > GETNODES_CHUNK) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: Do not request more than %d nodes.",
		 __func__, GETNODES_CHUNK);
	PSI_errlog(errtxt, 0);
	return -1;
    }

    *(unsigned int *)ptr = num;
    msg.header.len += sizeof(unsigned int);

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: send", __func__);
	PSI_errlog(errtxt, 0);
	return -1;
    }

    if (PSI_recvMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: recv", __func__);
	PSI_errlog(errtxt, 0);
	return -1;
    }

    switch (msg.header.type) {
    case PSP_CD_NODESRES:
    {
	char *ptr = msg.buf;
	ret = *(int *) ptr;
	ptr += sizeof(int);
	if (ret<0) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: Cannot get %d nodes\n", __func__, num);
	    PSI_errlog(errtxt, 0);
	} else {
	    memcpy(nodes, ptr, num*sizeof(*nodes));
	}
	break;
    }
    case PSP_CD_ERROR:
    {
	char *errstr = strerror(((DDErrorMsg_t *)&msg)->error);
	snprintf(errtxt, sizeof(errtxt), "%s: error in command %s : %s\n",
		 __func__, PSP_printMsg(((DDErrorMsg_t*)&msg)->request),
		 errtxt ? errtxt : "UNKNOWN");
	break;
    }
    default:
	fprintf(stderr, "%s: received unexpected msgtype '%s'.",
		__func__, PSP_printMsg(msg.header.type));
    }

    return ret;
}
