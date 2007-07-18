/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2007 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "pscommon.h"
#include "psprotocol.h"
#include "psnodes.h"

#include "psi.h"
#include "psilog.h"
#include "psiinfo.h"

#include "psipartition.h"

/** Flag used to mark environment originating from batch-system */
static int batchPartition = 0;

/**
 * The name of the environment variable defining a nodelist from a
 * nodestring, i.e. a string containing a comma separated list of node
 * ranges.
 */
#define ENV_NODE_NODES     "PSI_NODES"

/**
 * The name of the environment variable defining a nodelist from a
 * hoststring, i.e. a string containing a whitespace separated list of
 * resolvable hostnames.
 */
#define ENV_NODE_HOSTS     "PSI_HOSTS"

/**
 * The name of the environment variable defining a nodelist from a
 * hostfile, i.e. a file containing a list of resolvable hostnames.
 */
#define ENV_NODE_HOSTFILE  "PSI_HOSTFILE"

/**
 * Name of the environment variable steering the sorting of nodes
 * within building the partition. Possible values are:
 *
 * - LOAD, LOAD_1: Use the 1 minute load average for sorting.
 *
 * - LOAD_5: Use the 5 minute load average for sorting.
 *
 * - LOAD_15: Use the 15 minute load average for sorting.
 *
 * - PROC: Use the number of processes controlled by ParaStation.
 *
 * - PROC+LOAD: Use PROC + LOAD for sorting.
 *
 * - NONE: No sorting at all.
 *
 * The value is considered case-insensitive.
 */
#define ENV_NODE_SORT      "PSI_NODES_SORT"

/**
 * Name of the evironment variable used in order to enable a
 * partitions PART_OPT_NODEFIRST option.
 */
#define ENV_PART_LOOPNODES "PSI_LOOP_NODES_FIRST"

/**
 * Name of the evironment variable used in order to enable a
 * partitions PART_OPT_EXCLUSIVE option.
 */
#define ENV_PART_EXCLUSIVE "PSI_EXCLUSIVE"

/**
 * Name of the evironment variable used in order to enable a
 * partitions PART_OPT_OVERBOOK option.
 */
#define ENV_PART_OVERBOOK  "PSI_OVERBOOK"

/**
 * Name of the evironment variable used in order to enable a
 * partitions PART_OPT_WAIT option.
 */
#define ENV_PART_WAIT      "PSI_WAIT"

/**
 * Name of the evironment variable used in order to enable a special
 * mode removing all multiple consecutive entry from within a
 * hostfile.
 */
#define ENV_HOSTS_UNIQUE    "PSI_HOSTS_UNIQUE"

/**
 * Name of the environment variable used by LSF in order to keep the
 * hostnames of the nodes reserved for the batch job.
*/
#define ENV_NODE_HOSTS_LSF "LSB_HOSTS"

void PSI_LSF(void)
{
    char *lsf_hosts=NULL;

    PSI_log(PSI_LOG_VERB, "%s()\n", __func__);

    lsf_hosts = getenv(ENV_NODE_HOSTS_LSF);
    if (lsf_hosts) {
	setenv(ENV_NODE_SORT, "none", 1);
	unsetenv(ENV_NODE_NODES);
	setenv(ENV_NODE_HOSTS, lsf_hosts, 1);
	unsetenv(ENV_NODE_HOSTFILE);
	setenv(ENV_PART_LOOPNODES, "1", 1);
	batchPartition = 1;
    }
}

/**
 * Name of the environment variable used by OpenPBS, Torque and PBSPro
 * in order to keep the filename of the hostfile. This file contains a
 * list of hostnames of the nodes reserved for the batch job.
*/
#define ENV_NODE_HOSTFILE_PBS "PBS_NODEFILE"

void PSI_PBS(void)
{
    char *pbs_hostfile=NULL;

    PSI_log(PSI_LOG_VERB, "%s()\n", __func__);

    pbs_hostfile = getenv(ENV_NODE_HOSTFILE_PBS);
    if (pbs_hostfile) {
	setenv(ENV_NODE_SORT, "none", 1);
	unsetenv(ENV_NODE_NODES);
	unsetenv(ENV_NODE_HOSTS);
	setenv(ENV_NODE_HOSTFILE, pbs_hostfile, 1);
	setenv(ENV_PART_LOOPNODES, "1", 1);
	batchPartition = 1;
    }
}

/**
 * Name of the environment variable used by LoadLeveler in order to
 * keep the hostnames of the nodes reserved for the batch job.
*/
#define ENV_NODE_HOSTS_LL "LOADL_PROCESSOR_LIST"

void PSI_LL(void)
{
    char *ll_hosts=NULL;

    PSI_log(PSI_LOG_VERB, "%s()\n", __func__);

    ll_hosts = getenv(ENV_NODE_HOSTS_LL);
    if (ll_hosts) {
	setenv(ENV_NODE_SORT, "none", 1);
	unsetenv(ENV_NODE_NODES);
	setenv(ENV_NODE_HOSTS, ll_hosts, 1);
	unsetenv(ENV_NODE_HOSTFILE);
	setenv(ENV_PART_LOOPNODES, "1", 1);
	batchPartition = 1;
    }
}

/**
 * @brief Get sort mode.
 *
 * Get the partition's sort mode from the environment variable @ref
 * ENV_NODE_SORT.
 *
 * If the environment variable is not set at all, the default value
 * PART_SORT_PROC is returned. If one of the possible values as
 * discussed in @ref ENV_NODE_SORT is detected, its corresponding
 * value is returned. If it was impossible to detect any valid
 * criterium, PART_SORT_UNKNOWN will be returned.
 *
 * @return The determined sorting criterium as discussed above.
 */
static PSpart_sort_t getSortMode(void)
{
    char *env_sort = getenv(ENV_NODE_SORT);

    if (!env_sort) return PART_SORT_DEFAULT;

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
    } else if (strcasecmp(env_sort,"NONE")==0) {
	return PART_SORT_NONE;
    }

    PSI_log(-1, "%s: Unknown criterium '%s'\n", __func__, env_sort);

    return PART_SORT_UNKNOWN;
}

/**
 * @brief Get options.
 *
 * Get the partition's options from the environment variables @ref
 * ENV_PART_LOOPNODES, @ref ENV_PART_EXCLUSIVE, @ref ENV_PART_OVERBOOK
 * and ENV_PART_WAIT.
 *
 * @return The bitwise OR'ed combination of the detected options.
 */
static PSpart_option_t getPartitionOptions(void)
{
    PSpart_option_t options = 0;

    if (getenv(ENV_PART_LOOPNODES)) options |= PART_OPT_NODEFIRST;
    if (getenv(ENV_PART_EXCLUSIVE)) options |= PART_OPT_EXCLUSIVE;
    if (getenv(ENV_PART_OVERBOOK)) options |= PART_OPT_OVERBOOK;
    if (getenv(ENV_PART_WAIT)) options |= PART_OPT_WAIT;
    if (batchPartition) options |= PART_OPT_EXACT;

    return options;
}

/** Structure to hold a nodelist */
typedef struct {
    int size;             /**< Actual number of valid entries within nodes[] */
    int maxsize;          /**< Maximum number of entries within nodes[] */
    PSnodes_ID_t *nodes;  /**< ParaStation IDs of the requested nodes. */
} nodelist_t;

/**
 * @brief Extend nodelist by node.
 *
 * Extend the nodelist @a nl by one node. If the new node would bust
 * the nodelist's allocated space, it will be extended automatically.
 *
 * @param node The node to be added to the nodelist.
 *
 * @param nl The nodelist to be extended.
 *
 * @return On success, i.e. if the nodelist's allocated space was
 * large enough or if the extension of this space worked well, 1 is
 * returned. Or 0, if something went wrong.
 */
static int addNode(PSnodes_ID_t node, nodelist_t *nl)
{
    PSI_log(PSI_LOG_VERB, "%s(%d)\n", __func__, node);

    if (nl->size == nl->maxsize) {
	nl->maxsize += 128;
	nl->nodes = realloc(nl->nodes, nl->maxsize * sizeof(*nl->nodes));
	if (!nl->nodes) {
            PSI_log(-1, "%s: no memory\n", __func__);
            return 0;
	}
    }

    nl->nodes[nl->size] = node;
    nl->size++;

    return 1;
}

static void freeNodelist(nodelist_t *nl)
{
    if (!nl) return;

    if (nl->nodes) free(nl->nodes);
    free(nl);
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
        PSI_log(-1, "%s: node %ld out of range\n", __func__, first);
        return 0;
    }

    if (range) {
        last = strtol(range, &end, 0);
        if (*end != '\0') return 0;
        if (last < 0 || last >= PSC_getNrOfNodes()) {
            PSI_log(-1, "%s: node %ld out of range\n", __func__, last);
            return 0;
        }
    } else {
        last = first;
    }

    /* Now put the range into the nodelist */
    for (i=first; i<=last; i++) {
	if (!addNode(i, nodelist)) return 0;
    }

    return last - first + 1;
}

/**
 * @brief Get nodelist from node-string.
 *
 * Get @a nodelist from @a nodeStr of the form range{,range}*.
 *
 * @param nodeStr Nodestring of the form 'range{,range}*'. The form
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

/**
 * @brief Get nodelist from hostname.
 *
 * Get @a nodelist from single hostname string @a host which contains
 * a resolvable hostname. Naturally the nodelist is extended by only a
 * single node.
 *
 * @param host String containing a resolvable hostname. The resolved
 * IP address has to be registered within the ParaStation system in
 * order to determine its ParaStation ID.
 *
 * @param nodelist Nodelist to be extended.
 *
 * @return On success, the extension of the nodelist size (i.e. 1) is
 * returned. Otherwise 0 is returned.
 */
static int nodelistFromHost(char *host, nodelist_t *nodelist)
{
    struct hostent *hp;
    struct in_addr sin_addr;
    PSnodes_ID_t node;
    int err;

    hp = gethostbyname(host);
    if (!hp) {
	PSI_log(-1, "%s: unknown node '%s'\n", __func__, host);
	return 0;
    }

    memcpy(&sin_addr, hp->h_addr_list[0], hp->h_length);
    err = PSI_infoNodeID(-1, PSP_INFO_HOST, &sin_addr.s_addr, &node, 0);

    if (err || node < 0) {
	PSI_log(-1, "%s: cannot get ParaStation ID for node '%s'\n",
		__func__, host);
	return 0;
    } else if (node >= PSC_getNrOfNodes()) {
	PSI_log(-1, "%s: ParaStation ID %d for node '%s' out of range\n",
		__func__, node, host);
	return 0;
    }

    return addNode(node, nodelist);
}

/**
 * @brief Get nodelist from host-string.
 *
 * Get @a nodelist from @a hostStr containing a whitespace separated
 * list of resolvable hostnames. The nodelist is build via successiv
 * calls to @ref nodelistFromHost().
 *
 * @param hostStr String containing a list of resolvable
 * hostnames. Each hostname is handled via a call to @ref
 * nodelistFromHost().
 *
 * @param nodelist Nodelist to be build.
 *
 * @return On success, the size of the nodelist is returned. Otherwise
 * 0 is returned.
 */
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

/**
 * @brief Get nodelist from host-file.
 *
 * Get @a nodelist from the hostfile @a fileName containing a list of
 * resolvable hostnames. The nodelist is build via successiv calls to
 * @ref nodelistFromHostStr().
 *
 * Each line of the hostfile might contain a whitespace separated list
 * of resolvable hostnames which will be passed to @ref
 * nodelistFromHostStr(). Lines starting with a hash ('#') as the
 * first character within this line will be ignored.
 *
 * @param fileName String containing the name of the file used.
 *
 * @param nodelist Nodelist to be build.
 *
 * @return On success, the size of the nodelist is returned. Otherwise
 * 0 is returned.
 */
static int nodelistFromHostFile(char *fileName, nodelist_t *nodelist)
{
    FILE* file = fopen(fileName, "r");
    char lastline[1024], line[1024];
    int total = 0;
    int unique = !!getenv(ENV_HOSTS_UNIQUE);

    if (!file) {
	PSI_log(-1, "%s: cannot open file <%s>\n", __func__, fileName);
	return 0;
    }

    lastline[0] = '\0';
    while (fgets(line, sizeof(line), file)) {
	if (line[0] == '#') continue;
	if (unique) {
	    size_t pos = strlen(line)-1;
	    /* Eliminate trailing whitespace */
	    while (pos && (line[pos] == '\n'
			   || line[pos] == '\t'
			   || line[pos] == ' ')) {
		line[pos] = '\0';
		pos--;
	    }
	    if (!strcmp(lastline, line)) {
		PSI_log(PSI_LOG_PART, "%s: '%s' discarded\n", __func__, line);
		continue;
	    } else {
		strcpy(lastline, line);
	    }
	}
	if (nodelistFromHostStr(line, nodelist) != 1) {
	    PSI_log(-1, "%s: syntax error at: '%s'\n", __func__, line);
	    fclose(file);
	    return 0;
	} else
	    total++;
    }
    fclose(file);
    return total;
}

/**
 * @brief Get a nodelist.
 *
 * Get a @a nodelist from the corresponding environment variables.
 *
 * This function test the environment variables @ref ENV_NODE_NODES,
 * @ref ENV_NODE_HOSTS and @ref ENV_NODE_HOSTFILE in this order. If
 * any is set, the nodelist is created via @ref
 * nodelistFromHostNodeStr(), @ref nodelistFromHostStr() or @ref
 * nodelistFromHostFile() respectively.
 *
 * @return On success, the created nodelist is returned. Otherwise
 * NULL is returned. The latter case is also valid, if none of the
 * expected environment variables is set.
 */
static nodelist_t *getNodelist(void)
{
    char *nodeStr = getenv(ENV_NODE_NODES);
    char *hostStr = getenv(ENV_NODE_HOSTS);
    char *hostfileStr = getenv(ENV_NODE_HOSTFILE);
    nodelist_t *nodelist;

    PSI_log(PSI_LOG_VERB, "%s()\n", __func__);

    if (!nodeStr && !hostStr && !hostfileStr) return NULL;

    nodelist = malloc(sizeof(nodelist_t));
    if (!nodelist) {
	PSI_log(-1, "%s: no memory\n", __func__);
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

/**
 * @brief Send a nodelist.
 *
 * Send a @a nodelist to the local daemon using the message buffer @a
 * msg.
 *
 * In order to send the nodelist, it is split into chunks of @ref
 * NODES_CHUNK entries. Each chunk is copied into the message and send
 * separately to the local daemon.
 *
 * This function is typically called from within @ref
 * PSI_createPartition().
 *
 * @param nodelist The nodelist to be send.
 *
 * @param msg The message buffer used to send the nodelist to the
 * daemon.
 *
 * @return If something went wrong, -1 is returned and errno is set
 * appropriately. Otherwise 0 is returned.
 *
 * @see errno(3)
 */
static int sendNodelist(nodelist_t *nodelist, DDBufferMsg_t *msg)
{
    int offset = 0;

    msg->header.type = PSP_CD_CREATEPARTNL;
    while (offset < nodelist->size) {
	int chunk = (nodelist->size-offset > NODES_CHUNK) ?
	    NODES_CHUNK : nodelist->size-offset;
	char *ptr = msg->buf;
	msg->header.len = sizeof(msg->header);

	*(int16_t*)ptr = chunk;
	ptr += sizeof(int16_t);
	msg->header.len += sizeof(int16_t);

	memcpy(ptr, nodelist->nodes+offset, chunk * sizeof(*nodelist->nodes));
	msg->header.len += chunk * sizeof(*nodelist->nodes);
	offset += chunk;
	if (PSI_sendMsg(msg)<0) {
	    PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	    return -1;
	}
    }
    return 0;
}

/**
 * Name of the environment variable used to declare set of hardware
 * types in a space-separated list. A node has to support these
 * hardware types in order to be exepted as part of the partition used
 * to run a job. Keep in mind that for a node it's sufficient to
 * support at least one of the hardware types requested.
*/
#define ENV_HW_TYPE "PSI_HW_TYPE"

/**
 * @brief Get hardware environment
 *
 * Get set of hardware-types used to mask the hardware types requested
 * within the PSI_createPartition() call. The hardware types are taken
 * from a space separated list in the environment given by @ref
 * ENV_HW_TYPE.
 *
 * @return On success, i.e. all hardware types given are known, the
 * hardware-mask is returned. Otherwise 0 is returned.
 *
 *
 */
static uint32_t getHWEnv(void)
{
    uint32_t hwType = 0;
    char *env = getenv(ENV_HW_TYPE);
    char *work, *hw;

    if (!env) return 0;

    hw = strtok_r(env, " \f\n\r\t\v", &work);
    while (hw) {
	int err, idx;
	err = PSI_infoInt(-1, PSP_INFO_HWINDEX, hw, &idx, 0);
	if (!err && (idx >= 0) && (idx < ((int)sizeof(hwType) * 8))) {
	    hwType |= 1 << idx;
	} else {
	    PSI_log(-1, "%s: Unknown hardware type '%s'."
		    " Ignore environment %s\n", __func__, hw, ENV_HW_TYPE);
	    return 0;
	}
	hw = strtok_r(NULL, " \f\n\r\t\v", &work);
    }
    return hwType;
}

static int alarmCalled = 0;
static void alarmHandler(int sig)
{
    time_t now = time(NULL);
    char *timeStr = ctime(&now);
    alarmCalled = 1;
    timeStr[strlen(timeStr)-1] = '\0';
    PSI_log(-1, "%s -- Waiting for ressources\n", timeStr);
}

int PSI_createPartition(unsigned int size, uint32_t hwType)
{
    DDBufferMsg_t msg = (DDBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_CREATEPART,
	    .dest = PSC_getTID(-1, 0),
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg.header)},
	.buf = {0}};
    PSpart_request_t *request = PSpart_newReq();
    nodelist_t *nodelist;
    size_t len;
    uint32_t hwEnv;

    PSI_log(PSI_LOG_VERB, "%s()\n", __func__);

    if (size <= 0) {
	PSI_log(-1, "%s: size %d to small\n", __func__, size);
	return -1;
    }

    request->size = size;
    request->hwType = hwType;
    request->sort = getSortMode();
    request->options = getPartitionOptions();
    request->priority = 0; /* Not used */

    if (request->sort == PART_SORT_UNKNOWN) return -1;

    PSI_log(PSI_LOG_PART,
	    "%s: size %d hwType %x sort %x options %x priority %d\n",
	    __func__, request->size, request->hwType, request->sort,
	    request->options, request->priority);

    nodelist = getNodelist();
    if (nodelist) {
	if (nodelist->size < 0) {
	    free(nodelist);
	    return -1;
	}
	request->num = nodelist->size;
    } else {
	request->num = 0;
    }

    hwEnv = getHWEnv();
    if (hwEnv && !(request->hwType = (hwType ? hwType:0xffffffffU) & hwEnv)) {
	PSI_log(-1, "%s: no intersection between hwType (%x)"
		" and environment (%x)\n", __func__, hwType, hwEnv);
	return -1;
    }

    len = PSpart_encodeReq(msg.buf, sizeof(msg.buf), request);
    PSpart_delReq(request);
    if (len > sizeof(msg.buf)) {
	PSI_log(-1, "%s: PSpart_encodeReq\n", __func__);
	freeNodelist(nodelist);
	return -1;
    }
    msg.header.len += len;

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	freeNodelist(nodelist);
	return -1;
    }

    if (nodelist) {
	int ret = sendNodelist(nodelist, &msg);
	if (ret) return ret;
    }
    freeNodelist(nodelist);

    signal(SIGALRM, alarmHandler);
    alarm(2);
    if (PSI_recvMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
	return -1;
    }
    alarm(0);

    switch (msg.header.type) {
    case PSP_CD_PARTITIONRES:
	if (*(int*)msg.buf) {
	    PSI_warn(-1, *(int*)msg.buf, "%s", __func__);
	    return -1;
	}
	break;
    case PSP_CD_ERROR:
	PSI_warn(-1, ((DDErrorMsg_t*)&msg)->error, "%s: error in command %s",
		 __func__, PSP_printMsg(((DDErrorMsg_t*)&msg)->request));
	return -1;
	break;
    default:
	PSI_log(-1, "%s: received unexpected msgtype '%s'\n",
		__func__, PSP_printMsg(msg.header.type));
	return -1;
    }

    if (alarmCalled) {
	time_t now = time(NULL);
	char *timeStr = ctime(&now);
	timeStr[strlen(timeStr)-1] = '\0';
	PSI_log(-1, "%s -- Starting now...\n", timeStr);
    }

    return size;
}

int PSI_getNodes(unsigned int num, PSnodes_ID_t *nodes)
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

    if (num > NODES_CHUNK) {
	PSI_log(-1, "%s: Do not request more than %d nodes\n", __func__,
		NODES_CHUNK);
	return -1;
    }

    *(uint32_t*)ptr = num;
    msg.header.len += sizeof(uint32_t);

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	return -1;
    }

    if (PSI_recvMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
	return -1;
    }

    switch (msg.header.type) {
    case PSP_CD_NODESRES:
	ptr = msg.buf;
	ret = *(int32_t*)ptr;
	ptr += sizeof(int32_t);
	if (ret<0) {
	    PSI_log(-1, "%s: Cannot get %d nodes\n", __func__, num);
	} else {
	    memcpy(nodes, ptr, num*sizeof(*nodes));
	}
	break;
    case PSP_CD_ERROR:
	PSI_warn(-1, ((DDErrorMsg_t*)&msg)->error, "%s: error in command %s",
		 __func__, PSP_printMsg(((DDErrorMsg_t*)&msg)->request));
	break;
    default:
	PSI_log(-1, "%s: received unexpected msgtype '%s'\n", __func__,
		PSP_printMsg(msg.header.type));
    }

    return ret;
}

int PSI_getRankNode(int rank, PSnodes_ID_t *node)
{
    DDBufferMsg_t msg = (DDBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_GETRANKNODE,
	    .dest = PSC_getTID(-1, 0),
	    .sender = PSC_getMyTID(),
	    .len = sizeof(DDMsg_t) },
	.buf = { 0 } };
    char *ptr = msg.buf;
    int ret = -1;

    *(int32_t*)ptr = rank;
    ptr += sizeof(int32_t);
    msg.header.len += sizeof(int32_t);

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	return -1;
    }

    if (PSI_recvMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
	return -1;
    }

    switch (msg.header.type) {
    case PSP_CD_NODESRES:
	ptr = msg.buf;
	ret = *(int32_t*)ptr;
	ptr += sizeof(int32_t);
	if (ret<0) {
	    PSI_log(-1, "%s: Cannot get node for rank %d\n", __func__, rank);
	} else {
	    memcpy(node, ptr, sizeof(*node));
	}
	break;
    case PSP_CD_ERROR:
	PSI_warn(-1, ((DDErrorMsg_t*)&msg)->error, "%s: error in command %s",
		 __func__, PSP_printMsg(((DDErrorMsg_t*)&msg)->request));
	break;
    default:
	PSI_log(-1, "%s: received unexpected msgtype '%s'\n", __func__,
		PSP_printMsg(msg.header.type));
    }

    return ret;
}
