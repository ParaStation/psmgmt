/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#include "pscommon.h"
#include "psprotocol.h"
#include "psnodes.h"

#include "psi.h"
#include "psilog.h"
#include "psiinfo.h"
#include "psienv.h"

#include "psipartition.h"

/** Flag used to mark environment originating from batch-system */
static bool batchPartition = false;

/** Flag used to mark ENV_PART_WAIT was set during PSI_createPartition() */
static bool waitForPartition = false;

/** Flag used to mark PART_OPT_WAIT was set in PSI_getReservation() */
static bool waitForReservation = false;

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
 * Name of the evironment variable used in order to enable a
 * partitions PART_OPT_RESPORTS option.
 */
#define ENV_PART_OMPI	   "PSI_OPENMPI"

/**
 * Name of the evironment variable used in order to enable a special
 * mode removing all multiple consecutive entry from within a
 * hostfile.
 */
#define ENV_HOSTS_UNIQUE   "PSI_HOSTS_UNIQUE"

/**
 * Name of the evironment variable used in order to enable a
 * dynamic resource allocation in cooperation with an external RMS.
 */
#define ENV_PART_DYNAMIC   "PSI_DYNAMIC"

/**
 * @param Warn on ignored setting
 *
 * This one creates some warnings on ignored settings via command-line
 * or environment, if a batch-systems of flavor @a batchType was
 * detected.
 *
 * @param batchType Type of batch-sytem that was detected to be
 * printed out in the warning.
 *
 * @return No return value.
 */
static void checkOtherSettings(char *batchType)
{
    char *envStr = getenv(ENV_NODE_NODES);
    if (!envStr) envStr = getenv(ENV_NODE_HOSTS);
    if (!envStr) envStr = getenv(ENV_NODE_HOSTFILE);
    if (!envStr) envStr = getenv(ENV_NODE_PEFILE);

    if (envStr) {
	PSI_log(-1, "Found batch system of %s flavour."
		" Ignoring any choices of nodes or hosts.\n",
		batchType ? batchType : "some");
    }
}

/**
 * Name of the environment variable used by LSF in order to keep the
 * filename of the hostfile. This file contains a list of hostnames of
 * the nodes reserved for the batch job.
*/
#define ENV_NODE_HOSTFILE_LSF "LSB_DJOB_HOSTFILE"

/**
 * Name of the environment variable used by LSF in order to keep the
 * hostnames of the nodes reserved for the batch job. This is ignored,
 * if @ref ENV_NODE_HOSTFILE_LSF is given as newer versions of LSF do.
*/
#define ENV_NODE_HOSTS_LSF "LSB_HOSTS"

void PSI_LSF(void)
{
    char *lsf_hosts, *lsf_hostfile;

    PSI_log(PSI_LOG_VERB, "%s()\n", __func__);

    lsf_hosts = getenv(ENV_NODE_HOSTS_LSF);
    lsf_hostfile = getenv(ENV_NODE_HOSTFILE_LSF);
    if (lsf_hosts || lsf_hostfile) {
	checkOtherSettings("LSF");
	setenv(ENV_NODE_SORT, "none", 1);
	unsetenv(ENV_NODE_NODES);
	if (lsf_hostfile) {
	    unsetenv(ENV_NODE_HOSTS);
	    setenv(ENV_NODE_HOSTFILE, lsf_hostfile, 1);
	} else {
	    setenv(ENV_NODE_HOSTS, lsf_hosts, 1);
	    unsetenv(ENV_NODE_HOSTFILE);
	}
	unsetenv(ENV_NODE_PEFILE);
	batchPartition = true;
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
	checkOtherSettings("PBS");
	setenv(ENV_NODE_SORT, "none", 1);
	unsetenv(ENV_NODE_NODES);
	unsetenv(ENV_NODE_HOSTS);
	setenv(ENV_NODE_HOSTFILE, pbs_hostfile, 1);
	unsetenv(ENV_NODE_PEFILE);
	batchPartition = true;
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
	checkOtherSettings("LoadLeveler");
	setenv(ENV_NODE_SORT, "none", 1);
	unsetenv(ENV_NODE_NODES);
	setenv(ENV_NODE_HOSTS, ll_hosts, 1);
	unsetenv(ENV_NODE_HOSTFILE);
	unsetenv(ENV_NODE_PEFILE);
	batchPartition = true;
    }
}

/**
 * Name of the environment variable used by SUN/Oracle/Univa
 * GridEngine in order to keep the hostnames of the nodes reserved for
 * the batch job.
*/
#define ENV_NODE_PEFILE_SGE "PE_HOSTFILE"

void PSI_SGE(void)
{
    char *sge_pefile=NULL;

    PSI_log(PSI_LOG_VERB, "%s()\n", __func__);

    sge_pefile = getenv(ENV_NODE_PEFILE_SGE);
    if (sge_pefile) {
	checkOtherSettings("GridEngine");
	setenv(ENV_NODE_SORT, "none", 1);
	unsetenv(ENV_NODE_NODES);
	unsetenv(ENV_NODE_HOSTS);
	unsetenv(ENV_NODE_HOSTFILE);
	setenv(ENV_NODE_PEFILE, sge_pefile, 1);
	batchPartition = true;
    }
}

/**
 * @brief Get sort mode.
 *
 * Get the partition's sort mode from the environment variable @ref
 * ENV_NODE_SORT.
 *
 * If the environment variable is not set at all, the default value
 * PART_SORT_DEFAULT is returned. If one of the possible values as
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
 * ENV_PART_LOOPNODES, @ref ENV_PART_EXCLUSIVE, @ref ENV_PART_OVERBOOK,
 * @ref ENV_PART_WAIT, @ref ENV_PART_OMPI and @ref ENV_PART_DYNAMIC.
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
    if (getenv(ENV_PART_OMPI)) options |= PART_OPT_RESPORTS;
    if (getenv(ENV_PART_DYNAMIC)) options |= PART_OPT_DYNAMIC;

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
 * large enough or if the extension of this space worked well, true is
 * returned. Or false, if something went wrong.
 */
static bool addNode(PSnodes_ID_t node, nodelist_t *nl)
{
    PSI_log(PSI_LOG_VERB, "%s(%d)\n", __func__, node);

    if (nl->size == nl->maxsize) {
	void *tmp = nl->nodes;
	nl->maxsize += 128;
	nl->nodes = realloc(nl->nodes, nl->maxsize * sizeof(*nl->nodes));
	if (!nl->nodes) {
	    if (tmp) free(tmp);
	    PSI_log(-1, "%s: no memory\n", __func__);
	    return false;
	}
    }

    nl->nodes[nl->size] = node;
    nl->size++;

    return true;
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
    char *start, *end;

    if (!range) return 0;

    start = strsep(&range, "-");
    if (!start) return 0;

    first = strtol(start, &end, 0);
    if (*end != '\0') return 0;
    if (!PSC_validNode(first)) {
	PSI_log(-1, "%s: node %ld out of range\n", __func__, first);
	return 0;
    }

    if (range) {
	last = strtol(range, &end, 0);
	if (*end != '\0') return 0;
	if (!PSC_validNode(last)) {
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
    PSnodes_ID_t node;

    node = PSI_resolveNodeID(host);

    if (node < 0) return 0;

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
    char buf[1024];
    char *envstr;
    const char delimiters[] =", \f\n\r\t\v";
    char *work, *host = strtok_r(hostStr, delimiters, &work);
    int total = 0;

    while (host) {
	if (!strncmp(host, "ifhn=", 5)) {
	    host += 5;
	    if (host && host[0] != '\0') {
		if ((envstr = getenv("PSP_NETWORK"))) {
		    snprintf(buf, sizeof(buf), "%s,%s", envstr, host);
		    setPSIEnv("PSP_NETWORK", buf, 1);
		} else {
		    setPSIEnv("PSP_NETWORK", host, 1);
		}
	    }
	} else {
	    int num = nodelistFromHost(host, nodelist);
	    if (!num) return 0;
	    total += num;
	}
	host = strtok_r(NULL, delimiters, &work);
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
 * @brief Get nodelist from PE-file.
 *
 * Get @a nodelist from the PE-file @a fileName containing a list of
 * resolvable hostnames and number of processes as generated by
 * SUN/Oracle/Unive GridEngine. The nodelist is build via successiv
 * calls to @ref nodelistFromHostStr().
 *
 * Each line of the PE-file shall contains a whitespace separated list
 * consisting of a resolvable hostname, the number of processes to run
 * on this node, the queue-name and a string describing the processors
 * to be used on the node. The two latter will be ignored. Lines
 * starting with a hash ('#') as the first character within this line
 * will be ignored.
 *
 * @param fileName String containing the name of the file used.
 *
 * @param nodelist Nodelist to be build.
 *
 * @return On success, the size of the nodelist is returned. Otherwise
 * 0 is returned.
 */
static int nodelistFromPEFile(char *fileName, nodelist_t *nodelist)
{
    FILE* file = fopen(fileName, "r");
    char line[1024];
    int total = 0;

    if (!file) {
	PSI_log(-1, "%s: cannot open file <%s>\n", __func__, fileName);
	return 0;
    }

    while (fgets(line, sizeof(line), file)) {
	const char delimiters[] =", \f\n\r\t\v";
	char *work, *host, *numStr, *end;
	long num, i;
	PSnodes_ID_t node;

	if (line[0] == '#') continue;

	host = strtok_r(line, delimiters, &work);
	if (!host || ! *host) return 0;

	node = PSI_resolveNodeID(host);
	if (node < 0) return 0;

	numStr = strtok_r(NULL, delimiters, &work);
	if (!numStr || ! *numStr) {
	    PSI_log(-1, "%s: number of processes missing for host '%s'\n",
		    __func__, host);
	    return 0;
	}
	num = strtol(numStr, &end, 0);
	if (*end) {
	    PSI_log(-1, "%s: failed to determine number of processes for"
		    " host '%s' from '%s'\n", __func__, host, numStr);
	    return 0;
	}

	for (i=0; i<num; i++) {
	    if (!addNode(node, nodelist)) {
		fclose(file);
		return 0;
	    }
	}
	total += num;
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
    PSI_log(PSI_LOG_VERB, "%s()\n", __func__);

    if (getenv(ENV_PSID_BATCH)) return NULL;

    char *nodeStr = getenv(ENV_NODE_NODES);
    char *hostStr = getenv(ENV_NODE_HOSTS);
    char *hostfileStr = getenv(ENV_NODE_HOSTFILE);
    char *pefileStr = getenv(ENV_NODE_PEFILE);

    if (! (nodeStr || hostStr || hostfileStr || pefileStr)) return NULL;

    nodelist_t *nodelist = malloc(sizeof(nodelist_t));
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
    } else if (pefileStr) {
	if (!nodelistFromPEFile(pefileStr, nodelist)) goto error;
    }
    return nodelist;

 error:
    if (nodelist->nodes) {
	free(nodelist->nodes);
	nodelist->nodes = NULL;
    }
    nodelist->size = -1;
    PSI_log(-1, "%s: failed from %s '%s': Please check your environment\n",
	    __func__, nodeStr ? ENV_NODE_NODES :
	    hostStr ? ENV_NODE_HOSTS :
	    hostfileStr ? ENV_NODE_HOSTFILE : ENV_NODE_PEFILE,
	    nodeStr ? nodeStr : hostStr ? hostStr :
	    hostfileStr ? hostfileStr : pefileStr);
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

/**
 * @brief Get TPP environment
 *
 * Get the threads per processes requested within the
 * PSI_createPartition() call. This is taken as a number from the
 * PSI_TPP environment.
 *
 * @return On success, the requested number of threads per process is
 * returned. In case of an error detected a warning message is printed
 * and 1 is returned.
 */
static uint16_t getTPPEnv(void)
{
    char *env = getenv("PSI_TPP"), *end;
    uint16_t tpp;

    if (!env) env = getenv("OMP_NUM_THREADS");
    if (!env) return 1;

    /* @todo Test further environments (are there further OMP_* variables?) */

    if (!*env) {
	PSI_log(-1, "%s: empty PSI_TPP / OMP_NUM_THREADS\n", __func__);
	return 1;
    }

    tpp = strtoul(env, &end, 0);
    if (*end) {
	PSI_log(-1, "%s: Unable to determine threads per process from '%s'\n",
		__func__, env);
	return 1;
    }

    return tpp;
}

/**
 * @brief Get a full list.
 *
 * Get a full list from the daemon. Therefore a list of type @a what is
 * stored to @a list. Each item of the list is expected to have size
 * @a itemSize. In order to provide this function with the correct @a
 * itemSize, please refer to the documentation within psiinfo.h.
 *
 * This function expects the list to be of length returned by @ref
 * PSC_getNrOfNodes().
 *
 *
 * @param list The buffer to store the result to.
 *
 * @param what The type of information to retrieve.
 *
 * @param itemSize The size of each item within @a list.
 *
 * @return On success, true is returned, or false if an error occurred.
 */
static bool getFullList(void *list, PSP_Info_t what, size_t itemSize)
{
    int recv, hosts;
    size_t listSize = itemSize*PSC_getNrOfNodes();
    char **myList = list;

    if (!*myList) *myList = malloc(listSize);
    if (!*myList) {
	PSI_log(-1, "%s(%s): out of memory\n", __func__, PSP_printInfo(what));
	return false;
    }

    recv = PSI_infoList(-1, what, NULL, *myList, listSize, 1);
    hosts = recv/itemSize;

    if (hosts != PSC_getNrOfNodes()) {
	PSI_log(-1, "%s(%s): failed\n", __func__, PSP_printInfo(what));
	return false;
    }

    return true;
}

static void analyzeError(PSpart_request_t *request, nodelist_t *nodelist)
{
    char *nStat = NULL;
    uint32_t *hwStat = NULL;
    uint16_t *allocJobs = NULL;
    uint16_t *numThreads = NULL;
    int n;

    if (!request || !nodelist || !nodelist->nodes) return;

    if (nodelist->size < (int) (request->size * request->tpp)) {
	PSI_log(-1, "%d nodes requested but only %d in nodelist\n",
		request->size * request->tpp, nodelist->size);
	goto end;
    }

    if (!getFullList(&nStat, PSP_INFO_LIST_HOSTSTATUS, sizeof(*nStat))) {
	PSI_log(-1, "%s: Unable to retrieve node-states\n", __func__);
	goto end;
    }
    if (!getFullList(&hwStat, PSP_INFO_LIST_HWSTATUS, sizeof(*hwStat))) {
	PSI_log(-1, "%s: Unable to retrieve hardware-states\n", __func__);
	goto end;
    }
    if (!getFullList(&allocJobs, PSP_INFO_LIST_ALLOCJOBS, sizeof(*allocJobs))) {
	PSI_log(-1, "%s: Unable to retrieve allocated jobs\n", __func__);
	goto end;
    }
    if (!(numThreads = calloc(PSC_getNrOfNodes(), sizeof(*numThreads)))) {
	PSI_log(-1, "%s: Unable to get memory for thread counting\n", __func__);
	goto end;
    }

    for (n=0; n < nodelist->size; n++) {
	PSnodes_ID_t node = nodelist->nodes[n];

	/* Test for down nodes */
	if (!nStat[node]) {
	    PSI_log(-1, "node %d is down\n", node);
	    nStat[node] = 1; /* Only report once */
	}
	/* Test for overloaded nodes */
	if (allocJobs[node]) {
	    PSI_log(-1, "node %d has %d allocated jobs\n", node,
		    allocJobs[node]);
	    allocJobs[node] = 0; /* Only report once */
	}
	/* Test for nodes with wrong hardware */
	if (request->hwType && !(hwStat[node] & request->hwType)) {
	    PSI_log(-1, "node %d does not support requested hardware\n", node);
	    hwStat[node] = (uint32_t) -1; /* Only report once */
	}
	numThreads[node]++;
    }

    /* More analysis in case of tpp */
    if (request->tpp > 1) {
	for (n=0; n < nodelist->size; n++) {
	    PSnodes_ID_t node = nodelist->nodes[n];

	    /* Test for correct tpp */
	    if (numThreads[node] && (numThreads[node] % request->tpp)) {
		PSI_log(-1, "num threads %d on node %d does not match"
			" tpp %d\n", numThreads[node], node, request->tpp);
		numThreads[node] = 0; /* Only report once */
	    }
	}
    }

end:
    if (nStat) free(nStat);
    if (hwStat) free(hwStat);
    if (allocJobs) free(allocJobs);
    if (numThreads) free(numThreads);
}

static bool alarmCalled = false;

static void alarmHandlerPart(int sig)
{
    if (waitForPartition) {
	time_t now = time(NULL);
	char *timeStr = ctime(&now);
	alarmCalled = true;
	timeStr[strlen(timeStr)-1] = '\0';
	PSI_log(-1, "%s -- Waiting for partition\n", timeStr);
    } else {
	PSI_log(-1, "Wait for partition timed out. Exiting...\n");
	exit(1);
    }
}

static void alarmHandlerRes(int sig)
{
    if (waitForReservation) {
	time_t now = time(NULL);
	char *timeStr = ctime(&now);
	alarmCalled = true;
	timeStr[strlen(timeStr)-1] = '\0';
	PSI_log(alarmCalled ? PSI_LOG_VERB : -1,
		"%s -- Waiting for reservation\n", timeStr);
    } else {
	PSI_log(-1, "Wait for reservation timed out. Exiting...\n");
	exit(1);
    }
}

int PSI_resolveHWList(char **hwList, uint32_t *hwType)
{
    int ret = 0;

    *hwType = 0;

    while (hwList && *hwList) {
	int err, idx;
	err = PSI_infoInt(-1, PSP_INFO_HWINDEX, *hwList, &idx, 0);
	if (!err && (idx >= 0) && (idx < ((int)sizeof(*hwType) * 8))) {
	    *hwType |= 1 << idx;
	} else {
	    ret = -1;
	}
	hwList++;
    }

    return ret;
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
    nodelist_t *nodelist = NULL;
    uint32_t hwEnv;
    int ret = -1;

    PSI_log(PSI_LOG_VERB, "%s()\n", __func__);

    if (!request) {
	PSI_log(-1, "%s: No memory for request\n", __func__);
	goto end;
    }

    if (size <= 0) {
	PSI_log(-1, "%s: size %d to small\n", __func__, size);
	goto end;
    }

    request->size = size;
    request->hwType = hwType;
    request->sort = getSortMode();
    request->options = getPartitionOptions();
    request->priority = 0; /* Not used */

    if (request->sort == PART_SORT_UNKNOWN) goto end;

    request->tpp = getTPPEnv();

    PSI_log(PSI_LOG_PART,
	    "%s: size %d tpp %d hwType %#x sort %#x options %#x priority %d\n",
	    __func__, request->size, request->tpp, request->hwType,
	    request->sort, request->options, request->priority);

    nodelist = getNodelist();
    if (nodelist) {
	if (nodelist->size < 0) goto end;
	request->num = nodelist->size;
    } else {
	request->num = 0;
    }

    hwEnv = getHWEnv();
    if (hwEnv && !(request->hwType = (hwType ? hwType:0xffffffffU) & hwEnv)) {
	PSI_log(-1, "%s: no intersection between hwType (%#x)"
		" and environment (%#x)\n", __func__, hwType, hwEnv);
	goto end;
    }

    if (!PSpart_encodeReq(&msg, request)) {
	PSI_log(-1, "%s: PSpart_encodeReq\n", __func__);
	goto end;
    }

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	goto end;
    }

    if (nodelist) {
	ret = sendNodelist(nodelist, &msg);
	if (ret) goto end;
	/* reset ret */
	ret = -1;
    }

    if (request->options & PART_OPT_WAIT) {
	waitForPartition = true;
	alarm(2);
    } else {
	alarm(60);
    }
    PSC_setSigHandler(SIGALRM, alarmHandlerPart);
recv_retry:
    if (PSI_recvMsg((DDMsg_t *)&msg, sizeof(msg))<0) {
	PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
	goto end;
    }
    alarm(0);
    PSC_setSigHandler(SIGALRM, SIG_DFL);

    switch (msg.header.type) {
    case PSP_CD_PARTITIONRES:
	if (*(int*)msg.buf) {
	    PSI_warn(-1, *(int*)msg.buf, "%s", __func__);
	    if (batchPartition) analyzeError(request, nodelist);
	    goto end;
	}
	break;
    case PSP_CD_SENDSTOP:
    case PSP_CD_SENDCONT:
	goto recv_retry;
	break;
    case PSP_CD_ERROR:
	PSI_warn(-1, ((DDErrorMsg_t*)&msg)->error, "%s: error in command %s",
		 __func__, PSP_printMsg(((DDErrorMsg_t*)&msg)->request));
	goto end;
	break;
    default:
	PSI_log(-1, "%s: received unexpected msgtype '%s'\n",
		__func__, PSP_printMsg(msg.header.type));
	goto end;
    }

    if (alarmCalled) {
	time_t now = time(NULL);
	char *timeStr = ctime(&now);
	timeStr[strlen(timeStr)-1] = '\0';
	PSI_log(-1, "%s -- Starting now...\n", timeStr);
    }

    ret = size;

end:
    if (nodelist) freeNodelist(nodelist);
    if (request) PSpart_delReq(request);

    return ret;
}

int PSI_getNodes(uint32_t num, uint32_t hwType, uint16_t tpp,
		 PSpart_option_t options, PSnodes_ID_t *nodes)
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

    PSP_putMsgBuf(&msg, __func__, "num", &num, sizeof(num));
    PSP_putMsgBuf(&msg, __func__, "hwType", &hwType, sizeof(hwType));
    PSP_putMsgBuf(&msg, __func__, "options", &options, sizeof(options));
    PSP_putMsgBuf(&msg, __func__, "tpp", &tpp, sizeof(tpp));

    if (hwType || tpp != 1 || options) {
	PSI_log(PSI_LOG_VERB, "%s:", __func__);
	if (hwType) PSI_log(PSI_LOG_VERB, " hwType %d", hwType);
	if (tpp != 1) PSI_log(PSI_LOG_VERB, " tpp %d", tpp);
	if (options) PSI_log(PSI_LOG_VERB, " options %#x", options);
	PSI_log(PSI_LOG_VERB, "\n");
    }

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	return -1;
    }

recv_retry:
    if (PSI_recvMsg((DDMsg_t *)&msg, sizeof(msg))<0) {
	PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
	return -1;
    }

    switch (msg.header.type) {
    case PSP_CD_NODESRES:
	ret = *(int32_t*)ptr;
	ptr += sizeof(int32_t);
	if (ret<0) {
	    PSI_log(-1, "%s: Cannot get %d nodes with %d threads\n", __func__,
		    num, tpp);
	} else {
	    memcpy(nodes, ptr, num*sizeof(*nodes));
	}
	break;
    case PSP_CD_SENDSTOP:
    case PSP_CD_SENDCONT:
	goto recv_retry;
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

int PSI_getRankNode(int32_t rank, PSnodes_ID_t *node)
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

    if (rank < 0) {
	PSI_log(-1, "%s: Rank %d not allowed.\n", __func__, rank);
	errno = EINVAL;
	return -1;
    }

    PSP_putMsgBuf(&msg, __func__, "rank", &rank, sizeof(int32_t));

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	return -1;
    }

recv_retry:
    if (PSI_recvMsg((DDMsg_t *)&msg, sizeof(msg))<0) {
	PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
	return -1;
    }

    switch (msg.header.type) {
    case PSP_CD_NODESRES:
	ret = *(int32_t*)ptr;
	ptr += sizeof(int32_t);
	if (ret<0) {
	    PSI_log(-1, "%s: Cannot get node for rank %d\n", __func__, rank);
	} else {
	    memcpy(node, ptr, sizeof(*node));
	}
	break;
    case PSP_CD_SENDSTOP:
    case PSP_CD_SENDCONT:
	goto recv_retry;
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

PSrsrvtn_ID_t PSI_getReservation(uint32_t nMin, uint32_t nMax, uint16_t ppn,
				 uint16_t tpp, uint32_t hwType,
				 PSpart_option_t options, uint32_t *got)
{
    DDBufferMsg_t msg = (DDBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_GETRESERVATION,
	    .dest = PSC_getTID(-1, 0),
	    .sender = PSC_getMyTID(),
	    .len = sizeof(DDMsg_t) },
	.buf = { 0 } };
    PSrsrvtn_ID_t rid = 0;
    size_t used = 0;

    PSI_log(PSI_LOG_PART, "%s(min %d max %d", __func__, nMin, nMax);
    if (ppn) PSI_log(PSI_LOG_PART, " ppn %d", ppn);
    if (tpp != 1) PSI_log(PSI_LOG_PART, " tpp %d", tpp);
    if (hwType) PSI_log(PSI_LOG_PART, " hwType %#x", hwType);
    if (options) PSI_log(PSI_LOG_PART, " options %#x", options);
    PSI_log(PSI_LOG_PART, ")\n");

    if (!tpp) {
	PSI_log(-1, "%s: Adapt tpp to 1\n", __func__);
	tpp = 1;
    }

    PSP_putMsgBuf(&msg, __func__, "nMin", &nMin, sizeof(nMin));
    PSP_putMsgBuf(&msg, __func__, "nMax", &nMax, sizeof(nMax));
    PSP_putMsgBuf(&msg, __func__, "tpp", &tpp, sizeof(tpp));
    PSP_putMsgBuf(&msg, __func__, "hwType", &hwType, sizeof(hwType));
    PSP_putMsgBuf(&msg, __func__, "options", &options, sizeof(options));
    PSP_putMsgBuf(&msg, __func__, "ppn", &ppn, sizeof(ppn));

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	return 0;
    }

    PSC_setSigHandler(SIGALRM, alarmHandlerRes);
    alarmCalled = false;
    if (options & PART_OPT_WAIT) {
	waitForReservation = true;
	alarm(2);
    } else {
	alarm(60);
    }

recv_retry:
    if (PSI_recvMsg((DDMsg_t *)&msg, sizeof(msg)) < 0) {
	PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
	return 0;
    }
    alarm(0);
    PSC_setSigHandler(SIGALRM, SIG_DFL);

    if (got) *got = 0;
    switch (msg.header.type) {
    case PSP_CD_RESERVATIONRES:
	PSP_getMsgBuf(&msg, &used, __func__, "rid", &rid, sizeof(rid));
	if (rid && got) {
	    PSP_getMsgBuf(&msg, &used, __func__, "got", got, sizeof(*got));
	} else {
	    int32_t eno;
	    if (PSP_getMsgBuf(&msg, &used, __func__, "eno", &eno, sizeof(eno))){
		PSI_warn(-1, eno, "%s", __func__);
		if (got) *got = eno;
	    } else {
		PSI_log(-1, "%s: unknown error\n", __func__);
	    }
	}
	break;
    case PSP_CD_SENDSTOP:
    case PSP_CD_SENDCONT:
	goto recv_retry;
	break;
    case PSP_CD_ERROR:
	PSI_warn(-1, ((DDErrorMsg_t*)&msg)->error, "%s: error in command %s",
		 __func__, PSP_printMsg(((DDErrorMsg_t*)&msg)->request));
	break;
    default:
	PSI_log(-1, "%s: received unexpected msgtype '%s'\n",
		__func__, PSP_printMsg(msg.header.type));
    }

    if (rid && alarmCalled) {
	time_t now = time(NULL);
	char *timeStr = ctime(&now);
	timeStr[strlen(timeStr)-1] = '\0';
	PSI_log(-1, "%s -- Starting now...\n", timeStr);
    }

    return rid;
}

int PSI_getSlots(uint16_t num, PSrsrvtn_ID_t resID, PSnodes_ID_t *nodes)
{
    DDBufferMsg_t msg = (DDBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_GETSLOTS,
	    .dest = PSC_getTID(-1, 0),
	    .sender = PSC_getMyTID(),
	    .len = sizeof(DDMsg_t) },
	.buf = { 0 } };
    int32_t ret = -1;
    size_t used = 0;

    if (num > NODES_CHUNK) {
	PSI_log(-1, "%s: Do not request more than %d nodes\n", __func__,
		NODES_CHUNK);
	return -1;
    }

    PSP_putMsgBuf(&msg, __func__, "resID", &resID, sizeof(resID));
    PSP_putMsgBuf(&msg, __func__, "num", &num, sizeof(num));

    PSI_log(PSI_LOG_VERB, "%s(%d, %#x)\n", __func__, num, resID);

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	return -1;
    }

recv_retry:
    if (PSI_recvMsg((DDMsg_t *)&msg, sizeof(msg))<0) {
	PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
	return -1;
    }

    switch (msg.header.type) {
    case PSP_CD_SLOTSRES:
	PSP_getMsgBuf(&msg, &used, __func__, "ret", &ret, sizeof(ret));
	if (ret<0) {
	    int32_t eno;
	    if (PSP_getMsgBuf(&msg, &used, __func__, "eno", &eno, sizeof(eno))){
		PSI_warn(-1, eno, "%s: Cannot get %d slots from %#x",
			 __func__, num, resID);
	    } else {
		PSI_log(-1, "%s: Cannot get %d slots from %#x\n", __func__,
			num, resID);
	    }
	} else {
	    memcpy(nodes, msg.buf + used, num*sizeof(*nodes));
	}
	break;
    case PSP_CD_SENDSTOP:
    case PSP_CD_SENDCONT:
	goto recv_retry;
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
