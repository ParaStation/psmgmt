/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psipartition.h"

#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>

#include "pscommon.h"
#include "psnodes.h"
#include "psserial.h"

#include "psi.h"
#include "psilog.h"
#include "psiinfo.h"
#include "psienv.h"

/** Flag used to mark environment originating from batch-system */
static bool batchPartition = false;

/** Flag used to mark ENV_PART_WAIT was set during PSI_createPartition() */
static bool waitForPartition = false;

/** Flag used to mark PART_OPT_WAIT was set in PSI_getReservation() */
static bool waitForReservation = false;

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
	PSI_log("Found batch system of %s flavour."
		" Ignoring any choices of nodes or hosts.\n",
		batchType ? batchType : "some");
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
    PSI_fdbg(PSI_LOG_VERB, "\n");

    char *pbs_hostfile = getenv(ENV_NODE_HOSTFILE_PBS);
    if (pbs_hostfile) {
	checkOtherSettings("PBS");
	setenv(ENV_NODE_SORT, "NONE", 1);
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
    PSI_fdbg(PSI_LOG_VERB, "\n");

    char *ll_hosts = getenv(ENV_NODE_HOSTS_LL);
    if (ll_hosts) {
	checkOtherSettings("LoadLeveler");
	setenv(ENV_NODE_SORT, "NONE", 1);
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
    PSI_fdbg(PSI_LOG_VERB, "\n");

    char *sge_pefile = getenv(ENV_NODE_PEFILE_SGE);
    if (sge_pefile) {
	checkOtherSettings("GridEngine");
	setenv(ENV_NODE_SORT, "NONE", 1);
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

    PSI_flog("Unknown criterium '%s'\n", env_sort);

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
    if (getenv(ENV_PART_DYNAMIC)) options |= PART_OPT_DYNAMIC;
    if (getenv(ENV_PART_FULL)) options |= PART_OPT_FULL_LIST;

    return options;
}

/** Structure to hold a nodelist */
typedef struct {
    uint32_t used;        /**< Actual number of valid entries within nodes[] */
    uint32_t size;        /**< Maximum number of entries within nodes[] */
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
    PSI_fdbg(PSI_LOG_VERB, "node %d)\n", node);

    if (nl->used == nl->size) {
	void *old = nl->nodes;
	nl->size += 128;
	nl->nodes = realloc(nl->nodes, nl->size * sizeof(*nl->nodes));
	if (!nl->nodes) {
	    free(old);
	    PSI_flog("no memory\n");
	    return false;
	}
    }

    nl->nodes[nl->used++] = node;

    return true;
}

static void freeNodelist(nodelist_t *nl)
{
    if (!nl) return;
    free(nl->nodes);
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
    if (!range) return 0;

    char *start = strsep(&range, "-"), *end;
    if (!start) return 0;

    long first = strtol(start, &end, 0), last;
    if (*end != '\0') return 0;
    if (!PSC_validNode(first)) {
	PSI_flog("node %ld out of range\n", first);
	return 0;
    }

    if (range) {
	last = strtol(range, &end, 0);
	if (*end != '\0') return 0;
	if (!PSC_validNode(last)) {
	    PSI_flog("node %ld out of range\n", last);
	    return 0;
	}
    } else {
	last = first;
    }

    /* Now put the range into the nodelist */
    for (PSnodes_ID_t n = first; n <= last; n++) {
	if (!addNode(n, nodelist)) return 0;
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
    return nodelist->used;
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
    PSnodes_ID_t node = PSI_resolveNodeID(host);
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
		    setPSIEnv("PSP_NETWORK", buf);
		} else {
		    setPSIEnv("PSP_NETWORK", host);
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
	PSI_flog("cannot open file <%s>\n", fileName);
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
		PSI_fdbg(PSI_LOG_PART, "'%s' discarded\n", line);
		continue;
	    } else {
		strcpy(lastline, line);
	    }
	}

	if (nodelistFromHostStr(line, nodelist) != 1) {
	    PSI_flog("syntax error at: '%s'\n", line);
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
	PSI_flog("cannot open file <%s>\n", fileName);
	return 0;
    }

    while (fgets(line, sizeof(line), file)) {
	const char delimiters[] =", \f\n\r\t\v";
	char *work, *host, *numStr, *end;
	long num, i;
	PSnodes_ID_t node;

	if (line[0] == '#') continue;

	host = strtok_r(line, delimiters, &work);
	if (!host || ! *host) goto ERROR;

	node = PSI_resolveNodeID(host);
	if (node < 0) goto ERROR;

	numStr = strtok_r(NULL, delimiters, &work);
	if (!numStr || ! *numStr) {
	    PSI_flog("number of processes missing for host '%s'\n", host);
	    goto ERROR;
	}
	num = strtol(numStr, &end, 0);
	if (*end) {
	    PSI_flog("cannot determine number of processes for host '%s'"
		     " from '%s'\n", host, numStr);
	    goto ERROR;
	}

	for (i=0; i<num; i++) {
	    if (!addNode(node, nodelist)) goto ERROR;
	}
	total += num;
    }

    fclose(file);
    return total;

ERROR:
    fclose(file);
    return 0;
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
 * expected environment variables is set. To indicate an error,
 * nodelist != NULL with nodelist->nodes == NULL is returned.
 */
static nodelist_t *getNodelist(void)
{
    PSI_fdbg(PSI_LOG_VERB, "\n");

    if (getenv(ENV_PSID_BATCH)) return NULL;

    char *nodeStr = getenv(ENV_NODE_NODES);
    char *hostStr = getenv(ENV_NODE_HOSTS);
    char *hostfileStr = getenv(ENV_NODE_HOSTFILE);
    char *pefileStr = getenv(ENV_NODE_PEFILE);

    if (! (nodeStr || hostStr || hostfileStr || pefileStr)) return NULL;

    nodelist_t *nodelist = malloc(sizeof(nodelist_t));
    if (!nodelist) {
	PSI_flog("no memory\n");
	return NULL;
    }
    *nodelist = (nodelist_t) {
	.used = 0,
	.size = 0,
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
    PSI_flog("failed from %s '%s': Please check your environment\n",
	     nodeStr ? ENV_NODE_NODES :
	     hostStr ? ENV_NODE_HOSTS :
	     hostfileStr ? ENV_NODE_HOSTFILE : ENV_NODE_PEFILE,
	     nodeStr ? nodeStr : hostStr ? hostStr :
	     hostfileStr ? hostfileStr : pefileStr);

    free(nodelist->nodes);
    nodelist->nodes = NULL;

    return nodelist;
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
	int idx;
	int err = PSI_infoInt(-1, PSP_INFO_HWINDEX, hw, &idx, false);
	if (!err && (idx >= 0) && (idx < ((int)sizeof(hwType) * 8))) {
	    hwType |= (uint32_t)1 << idx;
	} else {
	    PSI_flog("Unknown hardware type '%s'. Ignore environment %s\n",
		     hw, ENV_HW_TYPE);
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
	PSI_flog("empty PSI_TPP / OMP_NUM_THREADS\n");
	return 1;
    }

    tpp = strtoul(env, &end, 0);
    if (*end) {
	PSI_flog("Unable to determine threads per process from '%s'\n", env);
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
    size_t listSize = itemSize*PSC_getNrOfNodes();
    char **myList = list;

    if (!*myList) *myList = malloc(listSize);
    if (!*myList) {
	PSI_flog("%s: out of memory\n", PSP_printInfo(what));
	return false;
    }

    int recv = PSI_infoList(-1, what, NULL, *myList, listSize, true);
    int hosts = recv/itemSize;

    if (hosts != PSC_getNrOfNodes()) {
	PSI_flog("%s: failed\n", PSP_printInfo(what));
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

    if (!request || !nodelist || !nodelist->nodes) return;

    if (nodelist->used < request->size * request->tpp) {
	PSI_flog("only %d in nodelist of %d requested\n", nodelist->used,
		 request->size * request->tpp);
	goto end;
    }

    if (!getFullList(&nStat, PSP_INFO_LIST_HOSTSTATUS, sizeof(*nStat))) {
	PSI_flog("cannot retrieve node-states\n");
	goto end;
    }
    if (!getFullList(&hwStat, PSP_INFO_LIST_HWSTATUS, sizeof(*hwStat))) {
	PSI_flog("cannot retrieve hardware-states\n");
	goto end;
    }
    if (!getFullList(&allocJobs, PSP_INFO_LIST_ALLOCJOBS, sizeof(*allocJobs))) {
	PSI_flog("cannot retrieve allocated jobs\n");
	goto end;
    }
    if (!(numThreads = calloc(PSC_getNrOfNodes(), sizeof(*numThreads)))) {
	PSI_flog("cannot get memory for thread counting\n");
	goto end;
    }

    for (uint32_t n = 0; n < nodelist->used; n++) {
	PSnodes_ID_t node = nodelist->nodes[n];

	/* Test for down nodes */
	if (!nStat[node]) {
	    PSI_log("node %d is down\n", node);
	    nStat[node] = 1; /* Only report once */
	}
	/* Test for overloaded nodes */
	if (allocJobs[node]) {
	    PSI_log("node %d has %d allocated jobs\n", node, allocJobs[node]);
	    allocJobs[node] = 0; /* Only report once */
	}
	/* Test for nodes with wrong hardware */
	if (request->hwType && !(hwStat[node] & request->hwType)) {
	    PSI_log("node %d does not support requested hardware\n", node);
	    hwStat[node] = (uint32_t) -1; /* Only report once */
	}
	numThreads[node]++;
    }

    /* More analysis in case of tpp */
    if (request->tpp > 1) {
	for (uint32_t n = 0; n < nodelist->used; n++) {
	    PSnodes_ID_t node = nodelist->nodes[n];

	    /* Test for correct tpp */
	    if (numThreads[node] && (numThreads[node] % request->tpp)) {
		PSI_flog("num threads %d on node %d does not match tpp %d\n",
			 numThreads[node], node, request->tpp);
		numThreads[node] = 0; /* Only report once */
	    }
	}
    }

end:
    free(nStat);
    free(hwStat);
    free(allocJobs);
    free(numThreads);
}

static bool alarmCalled = false;

static void alarmHandlerPart(int sig)
{
    if (waitForPartition) {
	time_t now = time(NULL);
	char *timeStr = ctime(&now);
	alarmCalled = true;
	timeStr[strlen(timeStr)-1] = '\0';
	PSI_log("%s -- Waiting for partition\n", timeStr);
    } else {
	PSI_log("Wait for partition timed out. Exiting...\n");
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
	PSI_dbg(alarmCalled ? PSI_LOG_VERB : -1,
		"%s -- Waiting for reservation\n", timeStr);
    } else {
	PSI_log("Wait for reservation timed out. Exiting...\n");
	exit(1);
    }
}

int PSI_resolveHWList(char **hwList, uint32_t *hwType)
{
    int ret = 0;

    *hwType = 0;

    while (hwList && *hwList) {
	int idx;
	int err = PSI_infoInt(-1, PSP_INFO_HWINDEX, *hwList, &idx, false);
	if (!err && (idx >= 0) && (idx < ((int)sizeof(*hwType) * 8))) {
	    *hwType |= (uint32_t)1 << idx;
	} else {
	    ret = -1;
	}
	hwList++;
    }

    return ret;
}

int PSI_createPartition(uint32_t size, uint32_t hwType)
{
    PSI_fdbg(PSI_LOG_VERB, "\n");

    if (!size) {
	PSI_flog("size %d too small\n", size);
	return -1;
    }

    PSpart_request_t *request = PSpart_newReq();
    if (!request) {
	PSI_flog("no memory for request\n");
	return -1;
    }

    int retVal = -1;
    nodelist_t *nl = getNodelist();
    if (nl && !nl->nodes) goto end;

    request->size = size;
    request->hwType = hwType;
    request->sort = getSortMode();
    request->options = getPartitionOptions();
    request->priority = 0; /* Not used */
    request->num = nl ? nl->used : 0;

    if (request->sort == PART_SORT_UNKNOWN) goto end;

    request->tpp = getTPPEnv();

    PSI_fdbg(PSI_LOG_PART,
	     "size %d tpp %d hwType %#x sort %#x options %#x priority %d\n",
	     request->size, request->tpp, request->hwType,
	     request->sort, request->options, request->priority);

    if (request->options & PART_OPT_FULL_LIST) {
	if (!nl) {
	    PSI_flog("full partition requires explicit resources\n");
	    goto end;
	}
	request->size = nl->used;   // we want all nodes in nodelist
	request->tpp = 1;           // ignore tpp; we get full nodes anyhow
    }

    uint32_t hwEnv = getHWEnv();
    if (hwEnv && !(request->hwType = (hwType ? hwType:0xffffffffU) & hwEnv)) {
	PSI_flog("no intersection between hwType (%#x) and environment (%#x)\n",
		 hwType, hwEnv);
	goto end;
    }

    PS_SendDB_t reqMsg;
    initFragBuffer(&reqMsg, PSP_CD_REQUESTPART, 0);
    setFragDest(&reqMsg, PSC_getTID(-1, 0));

    if (!PSpart_addToMsg(request, &reqMsg)) {
	PSI_flog("PSpart_addToMsg() failed\n");
	goto end;
    }

    if (nl) addDataToMsg(nl->nodes, nl->used * sizeof(*nl->nodes), &reqMsg);
    if (sendFragMsg(&reqMsg) == -1) {
	PSI_flog("sendFragMsg() failed\n");
	goto end;
    }

    if (request->options & PART_OPT_WAIT) {
	waitForPartition = true;
	alarm(2);
    } else {
	alarm(60);
    }
    PSC_setSigHandler(SIGALRM, alarmHandlerPart);

    DDBufferMsg_t msg;
    ssize_t ret = PSI_recvMsg(&msg, sizeof(msg), PSP_CD_PARTITIONRES, false);
    int eno = errno;
    alarm(0);
    PSC_setSigHandler(SIGALRM, SIG_DFL);

    if (ret == -1 && eno != ENOMSG) {
	PSI_fwarn(eno, "PSI_recvMsg");
    } else if (msg.header.type != PSP_CD_PARTITIONRES) {
	PSI_flog("unexpected message %s\n", PSP_printMsg(msg.header.type));
    } else if (((DDTypedMsg_t *)&msg)->type) {
	PSI_fwarn(((DDTypedMsg_t *)&msg)->type, "got");
	if (batchPartition) analyzeError(request, nl);
    } else {
	if (alarmCalled) {
	    time_t now = time(NULL);
	    char *timeStr = ctime(&now);
	    timeStr[strlen(timeStr)-1] = '\0';
	    PSI_log("%s -- Starting now...\n", timeStr);
	}
	retVal = request->size;
    }

end:
    freeNodelist(nl);
    if (request) PSpart_delReq(request);

    return retVal;
}

PSrsrvtn_ID_t PSI_getReservation(uint32_t nMin, uint32_t nMax, uint16_t ppn,
				 uint16_t tpp, uint32_t hwType,
				 PSpart_option_t options, uint32_t *got)
{
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_GETRESERVATION,
	    .dest = PSC_getTID(-1, 0),
	    .sender = PSC_getMyTID(),
	    .len = 0 } };

    PSI_fdbg(PSI_LOG_PART, "min %d max %d", nMin, nMax);
    if (ppn) PSI_dbg(PSI_LOG_PART, " ppn %d", ppn);
    if (tpp != 1) PSI_dbg(PSI_LOG_PART, " tpp %d", tpp);
    if (hwType) PSI_dbg(PSI_LOG_PART, " hwType %#x", hwType);
    if (options) PSI_dbg(PSI_LOG_PART, " options %#x", options);
    PSI_dbg(PSI_LOG_PART, "\n");

    if (!tpp) {
	PSI_flog("Adapt tpp to 1\n");
	tpp = 1;
    }

    PSP_putMsgBuf(&msg, "nMin", &nMin, sizeof(nMin));
    PSP_putMsgBuf(&msg, "nMax", &nMax, sizeof(nMax));
    PSP_putMsgBuf(&msg, "tpp", &tpp, sizeof(tpp));
    PSP_putMsgBuf(&msg, "hwType", &hwType, sizeof(hwType));
    PSP_putMsgBuf(&msg, "options", &options, sizeof(options));
    PSP_putMsgBuf(&msg, "ppn", &ppn, sizeof(ppn));

    if (PSI_sendMsg(&msg) < 0) {
	PSI_fwarn(errno, "PSI_sendMsg");
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

    ssize_t ret = PSI_recvMsg(&msg, sizeof(msg), PSP_CD_RESERVATIONRES, false);
    int32_t eno = errno;
    alarm(0);
    PSC_setSigHandler(SIGALRM, SIG_DFL);

    if (got) *got = 0;
    if (ret == -1 && eno != ENOMSG) {
	PSI_fwarn(eno, "PSI_recvMsg");
	return 0;
    }
    if (msg.header.type != PSP_CD_RESERVATIONRES) {
	PSI_flog("unexpected message %s\n", PSP_printMsg(msg.header.type));
	return 0;
    }

    size_t used = 0;
    PSrsrvtn_ID_t rid = 0;
    PSP_getMsgBuf(&msg, &used, "rid", &rid, sizeof(rid));
    if (rid && got) {
	PSP_getMsgBuf(&msg, &used, "got", got, sizeof(*got));
    } else if (PSP_getMsgBuf(&msg, &used, "eno", &eno, sizeof(eno))){
	PSI_fwarn(eno, "got");
	if (got) *got = eno;
    } else {
	PSI_flog("unknown error\n");
    }

    if (rid && alarmCalled) {
	time_t now = time(NULL);
	char *timeStr = ctime(&now);
	timeStr[strlen(timeStr)-1] = '\0';
	PSI_log("%s -- Starting now...\n", timeStr);
    }

    return rid;
}

bool PSI_finReservation(env_t env)
{
    PSI_fdbg(PSI_LOG_PART, "\n");

    PS_SendDB_t msg;
    initFragBuffer(&msg, PSP_CD_FINRESERVATION, -1);
    setFragDest(&msg, PSC_getTID(-1, 0));

    addStringArrayToMsg(envGetArray(env), &msg);

    if (sendFragMsg(&msg) == -1) {
	PSI_flog("sending failed\n");
	return false;
    }

    return true;
}

int PSI_requestSlots(uint16_t num, PSrsrvtn_ID_t resID)
{
    if (num > NODES_CHUNK) {
	PSI_flog("too many slots (%d/%d)\n", num, NODES_CHUNK);
	return -1;
    }

    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_GETSLOTS,
	    .dest = PSC_getTID(-1, 0),
	    .sender = PSC_getMyTID(),
	    .len = 0, } };
    PSP_putMsgBuf(&msg, "resID", &resID, sizeof(resID));
    PSP_putMsgBuf(&msg, "num", &num, sizeof(num));

    PSI_fdbg(PSI_LOG_VERB, "%d, %#x\n", num, resID);

    if (PSI_sendMsg(&msg) < 0) {
	PSI_fwarn(errno, "PSI_sendMsg");
	return -1;
    }

    return 0;
}

int PSI_extractSlots(DDBufferMsg_t *msg, uint16_t num, PSnodes_ID_t *nodes)
{
    if (msg->header.type != PSP_CD_SLOTSRES) {
	PSI_flog("wrong type %s from %s\n", PSP_printMsg(msg->header.type),
		 PSC_printTID(msg->header.sender));
	return -1;
    }

    size_t used = 0;
    int32_t ret = -1;
    PSP_getMsgBuf(msg, &used, "ret", &ret, sizeof(ret));
    if (ret < 0) {
	int32_t eno;
	if (PSP_getMsgBuf(msg, &used, "eno", &eno, sizeof(eno))) {
	    PSI_fwarn(eno, "cannot get %d slots from %s",
		      num, PSC_printTID(msg->header.sender));
	} else {
	    PSI_flog("cannot get %d slots from %s\n",
		     num, PSC_printTID(msg->header.sender));
	}
    } else {
	memcpy(nodes, msg->buf + used, num*sizeof(*nodes));
    }

    return ret;
}
