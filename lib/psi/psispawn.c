/*
 *               ParaStation3
 * psispawn.c
 *
 * Spawning of processes and helper functions.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psispawn.c,v 1.24 2002/07/26 15:33:16 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psispawn.c,v 1.24 2002/07/26 15:33:16 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <netdb.h>
#include <ctype.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>

#include "pscommon.h"
#include "psprotocol.h"
#include "pshwtypes.h"
#include "pstask.h"

#include "psi.h"
#include "psilog.h"
#include "info.h"
#include "psienv.h"

#include "psispawn.h"

/*
 * The name for the environment variable setting the nodes used
 * when spawning.
 */
#define ENV_NODE_PRIV      "__PSI_NODES_PRIV"
#define ENV_NODE_NODES     "PSI_NODES"
#define ENV_NODE_HOSTS     "PSI_HOSTS"
#define ENV_NODE_HOSTFILE  "PSI_HOSTFILE"
#define ENV_NODE_HOSTS_LSF "LSB_HOSTS"
#define ENV_NODE_SORT      "PSI_NODES_SORT"
#define ENV_NODE_RARG      "PSI_RARG_PRE_%d"

short *PSI_Partition = NULL;  /** The partition to use.
				  Initialize via PSI_getPartition() */
int PSI_PartitionSize = 0;    /** The size of the partition to use. */
int PSI_PartitionIndex = 0;   /** Index of the next node to use. */

static char errtxt[256];

int PSI_dospawn(int count, short *dstnodes, char *workingdir,
		int argc, char **argv,
		unsigned int loggernode, unsigned short loggerport,
		int rank, int *errors, long *tids);

long PSI_spawn(short dstnode, char *workdir, int argc, char **argv,
	       unsigned int loggernode, unsigned short loggerport,
	       int rank, int *error)
{
    int ret;
    long tid;

    snprintf(errtxt, sizeof(errtxt), "PSI_spawn()");
    PSI_errlog(errtxt, 10);

    if (dstnode<0) {
	if (!PSI_Partition) {
	    snprintf(errtxt, sizeof(errtxt), "PSI_spawn():"
		     " you have to call PSI_getPartition() beforehand.");
	    PSI_errlog(errtxt, 0);
	    return -1;
	}

	dstnode = PSI_Partition[PSI_PartitionIndex];

	PSI_PartitionIndex++;
	PSI_PartitionIndex %= PSI_PartitionSize;
    }

    ret = PSI_dospawn(1, &dstnode, workdir, argc, argv,
		      loggernode, loggerport, rank, error, &tid);

    if (ret<0) return ret;

    return tid;
}

int PSI_spawnM(int count, short *dstnodes, char *workdir,
	       int argc, char **argv,
	       unsigned int loggernode, unsigned short loggerport,
	       int rank, int *errors, long *tids)
{
    short *mydstnodes=NULL;
    int ret, i;

    snprintf(errtxt, sizeof(errtxt), "PSI_spawnM()");
    PSI_errlog(errtxt, 10);

    if (count<=0) return 0;

    if (!dstnodes) {
	if (!PSI_Partition) {
	    snprintf(errtxt, sizeof(errtxt), "PSI_spawnM():"
		     " you have to call PSI_getPartition() beforehand.");
	    PSI_errlog(errtxt, 0);
	    return -1;
	}

	mydstnodes = (short*) malloc(count*sizeof(short));

	for (i=0; i<count; i++) {
	    mydstnodes[i] = PSI_Partition[PSI_PartitionIndex];
	    PSI_PartitionIndex++;
	    PSI_PartitionIndex %= PSI_PartitionSize;
	}
    } else {
	mydstnodes = dstnodes;
    }

    snprintf(errtxt, sizeof(errtxt), "PSI_spawnM(): will spawn to:");
    for (i=0; i<count; i++) {
	snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
		 " %2d", mydstnodes[i]);
    }
    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt), ".");
    PSI_errlog(errtxt, 1);

    ret = PSI_dospawn(count, mydstnodes, workdir, argc, argv,
		      loggernode, loggerport, rank, errors, tids);

    /*
     * if I allocated mydstnodes myself, free() it now
     */
    if (!dstnodes) free(mydstnodes);

    return ret;
}

/* used for sorting the nodes */
typedef struct {
    int    id;
    double rating;
} sort_block;

static int compareNodes(const void *entry1, const void *entry2)
{
    int ret;

    sort_block *sb1 = (sort_block *) entry1, *sb2 = (sort_block *) entry2;

    if (sb2->rating < sb1->rating)
	ret = 1;
    else if (sb2->rating > sb1->rating)
	ret =  -1;
    else if (sb2->id < sb1->id)
	ret =  1;
    else
	ret = -1;

    return ret;
}

enum sortType {none, proc, load_1, load_5, load_15};

/*-----------------------------------------------------------------------------
 * PSI_SortNodesInPartition
 *
 * Sort the nodes in the array depending on
 * - their load if PSI_NODES_SORT is load or empty
 * - nothing otherwise
 * (and if they are alive).
 * Parameter: nodes    : an array of nodenumbers.
 *            maxnodes : number of nodes to be sorted, starting with nodes[0]
 * Returns:   0 if OK
 *           -1 on error
 */
/* Help function for sorting the nodes with qsort */

static int sortNodes(short nodes[], int numNodes, NodelistEntry_t nodelist[])
{
    int i;
    sort_block *node_entry;
    char *env_sort;
    double (*loadfunc)(unsigned short, int) = NULL;

    /* get the way to sort from the environment */
    enum sortType sort = none;

    if (!(env_sort = getenv(ENV_NODE_SORT))) {
	/* default now PROC jh 2001-12-21 */
	env_sort = "PROC";
    }

    if (strcasecmp(env_sort,"LOAD")==0
	|| strcasecmp(env_sort,"LOAD_1")==0) {
	sort = load_1;
    } else if (strcasecmp(env_sort,"LOAD_5")==0) {
	sort = load_5;
    } else if (strcasecmp(env_sort,"LOAD_15")==0) {
	sort = load_15;
    } else if (strcasecmp(env_sort,"PROC")==0) {
	sort = proc;
    }

    if (sort != none) {
	node_entry = (sort_block *)malloc(numNodes * sizeof(sort_block));

	if (!node_entry) {
	    snprintf(errtxt, sizeof(errtxt), "sortNodes():"
		     " not enough memory.");
	    PSI_errlog(errtxt, 0);
	    return -1;
	}

	/* Create the struct to sort */
	for (i=0; i<numNodes; i++) {
	    node_entry[i].id = nodes[i];
	}

	switch (sort) {
	case load_1:
	    for (i=0; i<numNodes; i++) {
		node_entry[i].rating =
		    nodelist[nodes[i]].load[0] - nodelist[nodes[i]].numCPU;
	    }
	    break;
	case load_5:
	    for (i=0; i<numNodes; i++) {
		node_entry[i].rating =
		    nodelist[nodes[i]].load[1] - nodelist[nodes[i]].numCPU;
	    }
	    break;
	case load_15:
	    for (i=0; i<numNodes; i++) {
		node_entry[i].rating =
		    nodelist[nodes[i]].load[2] - nodelist[nodes[i]].numCPU;
	    }
	    break;
	case proc:
	    for (i=0; i<numNodes; i++) {
		node_entry[i].rating =
		    nodelist[nodes[i]].normalJobs - nodelist[nodes[i]].numCPU;
	    }
	    break;
	default:
	    snprintf(errtxt, sizeof(errtxt), "sortNodes():"
		     " unknown value for sort = %d.", sort);
	    PSI_errlog(errtxt, 0);
	    return -1;
	}

	/* Sort the nodes */
	qsort(node_entry, numNodes, sizeof(sort_block), compareNodes);

	/* Transfer the results */
	for ( i=0; i<numNodes; i++ ) {
	    nodes[i] = node_entry[i].id;
	}
	free(node_entry);
    }

    return 0;
}

/* Get white-space seperatet field. Return value must be freed
 * with free(). If next is set, *next return the beginning of the next field */
char *get_wss_entry(char *str, char **next)
{
    char *start=str;
    char *end=NULL;
    char *ret=NULL;

    if (!str) goto no_str;

    while (isspace(*start)) start++;
    end=start;
    while ((!isspace(*end)) && (*end)) end++;

    if (start != end) {
	ret = (char*)malloc(end-start + 1);
	strncpy(ret, start, end-start);
	ret[end-start]=0;
    }

 no_str:
    if (next) *next=end;
    return ret;
}

void PSI_LSF(void)
{
    char *lsf_hosts=NULL;

    snprintf(errtxt, sizeof(errtxt), "PSI_LSF()");
    PSI_errlog(errtxt, 10);

    lsf_hosts=getenv(ENV_NODE_HOSTS_LSF);
    if (lsf_hosts) {
	setenv(ENV_NODE_SORT, "none", 1);
	unsetenv(ENV_NODE_HOSTFILE);
	unsetenv(ENV_NODE_NODES);
	setenv(ENV_NODE_HOSTS, lsf_hosts, 1);
    }
}

void PSI_RemoteArgs(int Argc, char **Argv, int *RArgc, char ***RArgv)
{
    int new_argc=0;
    char **new_argv;
    char env_name[ sizeof(ENV_NODE_RARG) + 20];
    int cnt;
    int i;

    snprintf(errtxt, sizeof(errtxt), "PSI_RemoteArgs()");
    PSI_errlog(errtxt, 10);

    cnt=0;
    for (;;) {
	snprintf(env_name, sizeof(env_name), ENV_NODE_RARG, cnt);
	if (getenv(env_name)) {
	    cnt++;
	} else {
	    break;
	}
    }

    if (cnt) {
	new_argc=cnt+Argc;
	new_argv=malloc(sizeof(char *)*(new_argc+1));
	new_argv[new_argc]=NULL;

	for (i=0; i<cnt; i++) {
	    snprintf(env_name, sizeof(env_name), ENV_NODE_RARG, i);
	    new_argv[i] = getenv(env_name);
	    /* Propagate the environment */
	    setPSIEnv(env_name, new_argv[i], 1);
	}
	for (i=0; i<Argc; i++) {
	    new_argv[i+cnt] = Argv[i];
	}
	*RArgc=new_argc;
	*RArgv=new_argv;
    } else {
	*RArgc=Argc;
	*RArgv=Argv;
    }
    return;
}

static int node_ok(short node, unsigned hwType, NodelistEntry_t *nodelist)
{
    if ( node < 0 || node >= PSC_getNrOfNodes()) {
	snprintf(errtxt, sizeof(errtxt), "node_ok():"
		 " node %d out of range.", node);
	PSI_errlog(errtxt, 0);
	return 0;
    }

    if (!nodelist) {
	/* This must be from priv_str. Always ok */
	return 1;
    }

    if (! nodelist[node].up) {
	snprintf(errtxt, sizeof(errtxt), "node_ok():"
		 " node %d not UP, excluding from partition.", node);
	PSI_errlog(errtxt, 8);
	return 0;
    }

    if ((hwType & nodelist[node].hwType) != hwType) {
	snprintf(errtxt, sizeof(errtxt), "node_ok():"
		 " node %d lacks requested HW, excluding from partition.",
		 node);
	PSI_errlog(errtxt, 8);
	return 0;
    }

    return 1;
}

static int getNodesFromNodeStr(char *node_str,
			       unsigned int hwType, NodelistEntry_t *nodelist)
{
    int next_node;
    char* tmp_node_str, *tmp_node_str_begin, *tmp_node_str_end;

    tmp_node_str = tmp_node_str_begin = strdup(node_str);

    if (! tmp_node_str) {
	snprintf(errtxt, sizeof(errtxt), "getNodesFromNodeStr():"
		 " not enough memory to parse '%s'.", ENV_NODE_NODES);
	PSI_errlog(errtxt, 0);
	return 0;
    }

    /* first guess the size of the partition */
    PSI_PartitionSize = 0;
    while ((tmp_node_str_end = strchr(tmp_node_str,','))) {
	PSI_PartitionSize++;
	tmp_node_str = tmp_node_str_end+1;
    }
    /* Another entry after the last ',' */
    PSI_PartitionSize++;

    /* Allocate PSI_Partition with the correct size */
    PSI_Partition = realloc(PSI_Partition, sizeof(short) * PSI_PartitionSize);
    if (!PSI_Partition) {
	snprintf(errtxt, sizeof(errtxt), "getNodesFromNodeStr():"
		 " not enough memory for PSI_Partition.");
	PSI_errlog(errtxt, 0);
	return 0;
    }

    /* Reset some stuff for the real parsing */
    PSI_PartitionSize = 0;
    tmp_node_str = tmp_node_str_begin;

    /* Now do the real parsing */
    while ((tmp_node_str_end = strchr(tmp_node_str,','))) {
	/* while there are more node numbers in string */
	*tmp_node_str_end = '\0';

	if (sscanf(tmp_node_str, "%d", &next_node)>0) {
	    if (node_ok(next_node, hwType, nodelist)) {
		PSI_Partition[PSI_PartitionSize] = next_node;
		PSI_PartitionSize++;
	    }
	} else {
	    snprintf(errtxt, sizeof(errtxt), "getNodesFromNodeStr():"
		     " ID '%s' not correct.", tmp_node_str);
	    PSI_errlog(errtxt, 0);
	    free(tmp_node_str_begin);
	    return 0;
	}

	tmp_node_str = tmp_node_str_end+1;
    }

    /* Check if the last element is a node_nr */
    if (sscanf(tmp_node_str, "%d", &next_node)>0) {
	if (node_ok(next_node, hwType, nodelist)) {
	    PSI_Partition[PSI_PartitionSize] = next_node;
	    PSI_PartitionSize++;
	}
    } else {
	snprintf(errtxt, sizeof(errtxt), "getNodesFromNodeStr():"
		 " ID '%s' not correct.", tmp_node_str);
	PSI_errlog(errtxt, 0);
	free(tmp_node_str_begin);
	return 0;
    }

    free(tmp_node_str_begin);
    return 1;
}
	
static int getNodesFromHostStr(char *host_str,
			       unsigned int hwType, NodelistEntry_t *nodelist)
{
    /* parse host_str for nodenames */
    int next_node;
    char *hostname;
    char *p = host_str;
    struct in_addr sin_addr;
    struct hostent *hp;

    /* first guess the size of the partition */
    PSI_PartitionSize = 0;
    while (get_wss_entry(p, &p)) {
	PSI_PartitionSize++;
    }

    /* Allocate PSI_Partition with the correct size */
    PSI_Partition = realloc(PSI_Partition, sizeof(short) * PSI_PartitionSize);
    if (!PSI_Partition) {
	snprintf(errtxt, sizeof(errtxt), "getNodesFromHostStr():"
		 " not enough memory for PSI_Partition.");
	PSI_errlog(errtxt, 0);
	return 0;
    }

    /* Reset some stuff for the real parsing */
    PSI_PartitionSize = 0;
    p = host_str;

    /* Now do the real parsing */
    while ((hostname=get_wss_entry(p, &p))) {
	hp = gethostbyname(hostname);
	memcpy(&sin_addr, hp->h_addr, hp->h_length);
	next_node = INFO_request_host(sin_addr.s_addr, 0);

	if (next_node != -1) {
	    if (node_ok(next_node, hwType, nodelist)) {
		PSI_Partition[PSI_PartitionSize] = next_node;
		PSI_PartitionSize++;
	    }
	} else {
	    snprintf(errtxt, sizeof(errtxt), "getNodesFromHostStr():"
		     " cannot get ParaStation ID for host '%s'.", hostname);
	    PSI_errlog(errtxt, 0);
	    free(hostname);
	    endhostent();
	    return 0;
	}
	free(hostname);
    }
    endhostent();

    return 1;
}

static int getNodesFromHostFile(char *hostfile_str,
				unsigned int hwType, NodelistEntry_t *nodelist)
{
    int next_node;
    char hostname[1024];
    FILE* file;
    struct in_addr sin_addr;
    struct hostent *hp;

    /* Try to open the file */
    file = fopen(hostfile_str, "r");
    if (!file) {
	snprintf(errtxt, sizeof(errtxt), "getNodesFromHostFile():"
		 " cannot open file <%s>.", hostfile_str);
	PSI_errlog(errtxt, 0);
	return 0;
    }

    /* guess the size of the partition */
    PSI_PartitionSize = 0;
    while (fscanf(file, "%s", hostname)>0) {
	if (hostname[0] == '#') continue;
	PSI_PartitionSize++;
    }

    /* Allocate PSI_Partition with the correct size */
    PSI_Partition = realloc(PSI_Partition, sizeof(short) * PSI_PartitionSize);
    if (!PSI_Partition) {
	snprintf(errtxt, sizeof(errtxt), "getNodesFromHostFile():"
		 " not enough memory for PSI_Partition.");
	PSI_errlog(errtxt, 0);
	return 0;
    }

    /* Reset some stuff for the real parsing */
    PSI_PartitionSize = 0;
    rewind(file);

    /* Now do the real parsing */
    while(fscanf(file, "%s", hostname)>0) {
	if (hostname[0] == '#') continue;

	hp = gethostbyname(hostname);
	memcpy(&sin_addr, hp->h_addr, hp->h_length);
	next_node = INFO_request_host(sin_addr.s_addr, 0);

	if (next_node != -1) {
	    if (node_ok(next_node, hwType, nodelist)) {
		PSI_Partition[PSI_PartitionSize] = next_node;
		PSI_PartitionSize++;
	    }
	} else {
	    snprintf(errtxt, sizeof(errtxt), "getNodesFromHostFile():"
		     " cannot get ParaStation ID for host '%s'.", hostname);
	    PSI_errlog(errtxt, 0);
	    endhostent();
	    fclose(file);
	    return 0;
	}
    }
    endhostent();
    fclose(file);

    return 1;
}

short PSI_getPartition(unsigned int hwType, int myRank)
{
    int i;
    char *priv_str=NULL, *node_str=NULL, *host_str=NULL, *hostfile_str=NULL;

    NodelistEntry_t *nodelist=NULL;

    snprintf(errtxt, sizeof(errtxt),
	     "PSI_getPartition([%s], %d)", PSHW_printType(hwType), myRank);
    PSI_errlog(errtxt, 10);

    /* Get the selected nodes */
    if (! (priv_str = getenv(ENV_NODE_PRIV))) {
	if (! (node_str = getenv(ENV_NODE_NODES))) {
	    if (! (host_str = getenv(ENV_NODE_HOSTS))) {
		hostfile_str = getenv(ENV_NODE_HOSTFILE);
	    }
	}
    }

    if (priv_str) {
	/* ENV_NODE_PRIV and ENV_NODE_SELECT have the same syntax */
	node_str = priv_str;
	priv_str = strdup(node_str); /* priv_str will be freed later! */
    } else {
	/* We need to get a nodelist from the daemon */
	int ret;
	size_t nodelist_size = PSC_getNrOfNodes() * sizeof(NodelistEntry_t);
	nodelist = (NodelistEntry_t *)malloc(nodelist_size);

	if (!nodelist) {
	    snprintf(errtxt, sizeof(errtxt), "PSI_getPartition():"
		     " not enough memory for nodelist.");
	    PSI_errlog(errtxt, 0);
	    free(PSI_Partition);
	    PSI_Partition = NULL;
	    return -1;
	}

	ret = INFO_request_nodelist(nodelist, nodelist_size, 0);

	if (ret==-1) {
	    snprintf(errtxt, sizeof(errtxt), "PSI_getPartition():"
		     " error while getting nodelist.");
	    PSI_errlog(errtxt, 0);
	    free(nodelist);
	    free(PSI_Partition);
	    PSI_Partition = NULL;
	    return -1;
	}
    }

    if (node_str) {
	if (!getNodesFromNodeStr(node_str, hwType, nodelist)) {
	    if (nodelist) free(nodelist);
	    if (PSI_Partition) free(PSI_Partition);
	    PSI_Partition = NULL;
	    return -1;
	}
    } else if (host_str) {
	if (!getNodesFromHostStr(host_str, hwType, nodelist)) {
	    if (nodelist) free(nodelist);
	    if (PSI_Partition) free(PSI_Partition);
	    PSI_Partition = NULL;
	    return -1;
	}
    } else if (hostfile_str) {
	if (!getNodesFromHostFile(hostfile_str, hwType, nodelist)) {
	    if (nodelist) free(nodelist);
	    if (PSI_Partition) free(PSI_Partition);
	    PSI_Partition = NULL;
	    return -1;
	}
    } else {
        /* No variable found - get list from daemon */
	/* Allocate PSI_Partition with the correct size */
	PSI_Partition = (short*)realloc(PSI_Partition,
					sizeof(short)*PSC_getNrOfNodes());
	if (!PSI_Partition) {
	    snprintf(errtxt, sizeof(errtxt), "PSI_getPartition():"
		     " not enough memory for PSI_Partition.");
	    PSI_errlog(errtxt, 0);
	    if (nodelist) free(nodelist);
	    return -1;
	}

	PSI_PartitionSize = 0;

	for (i=0; i<PSC_getNrOfNodes(); i++) {
	    if (node_ok(i, hwType, nodelist)) {
		PSI_Partition[PSI_PartitionSize] = i;
		PSI_PartitionSize++;
	    }
	}

        if (!PSI_PartitionSize) {
	    snprintf(errtxt, sizeof(errtxt), "PSI_getPartition():"
		     " cannot get any hosts with correct HW. HW is %s.",
		     PSHW_printType(hwType));
	    PSI_errlog(errtxt, 0);
	    if (nodelist) free(nodelist);
            free(PSI_Partition);
	    PSI_Partition = NULL;
	    return -1;
        }
    }

    if (!priv_str) {
	/* Now sort the nodes as requested */
	if (sortNodes(PSI_Partition, PSI_PartitionSize, nodelist) == -1) {
	    snprintf(errtxt, sizeof(errtxt), "PSI_getPartition():"
		     " sortNodes() failed.");
	    PSI_errlog(errtxt, 0);
	    if (nodelist) free(nodelist);
	    free(PSI_Partition);
	    PSI_Partition = NULL;
	    return -1;
	}
    }

    if (nodelist) free(nodelist);

    /* Set PartitionIndex to the next node */
    PSI_PartitionIndex = (myRank + 1) % PSI_PartitionSize;

    /* Propagate partition to clients */
    if (!priv_str) {
	/* Create priv_str from PSI_Partition */
	priv_str = (char *) malloc(PSI_PartitionSize * 5 + 1);
	if (!priv_str) {
	    snprintf(errtxt, sizeof(errtxt), "PSI_getPartition():"
		     " not enough memory to propagate information.");
	    PSI_errlog(errtxt, 0);
	    free(PSI_Partition);
	    PSI_Partition = NULL;
	    return -1;
	}

	for (i=0; i<PSI_PartitionSize; i++) {
	    sprintf(&priv_str[5*i], "%4d,", PSI_Partition[i]);
	}
	priv_str[5*PSI_PartitionSize-1] = 0;
    }

    setPSIEnv(ENV_NODE_PRIV, priv_str, 1);
    setPSIEnv(ENV_NODE_SORT, "none", 1);

    free(priv_str);

    return PSI_PartitionSize;
}

int PSI_dospawn(int count, short *dstnodes, char *workingdir,
		int argc, char **argv,
		unsigned int loggernode, unsigned short loggerport,
		int rank, int *errors, long *tids)
{
    int outstanding_answers=0;
    DDBufferMsg_t msg;
    DDErrorMsg_t answer;
    char myworkingdir[200];
    char* pmyworkingdir = &myworkingdir[0];

    int i;          /* count variable */
    int ret = 0;    /* return value */
    int error = 0;  /* error flag */
    int fd = 0;
    PStask_t* task; /* structure to store the information of the new process */

    /*
     * Get a SIGTERM if a child dies. May be overwritten by explicit
     * PSI_notifydead() calls.
     */
    PSI_notifydead(0, SIGTERM);

    /*
     * Send the request to my own daemon
     *----------------------------------
     */

    for (i=0; i<count; i++) {
	errors[i] = 0;
	tids[i] = 0;
    }

    /*
     * Init the Task structure
     */
    task = PStask_new();

    task->ptid = PSC_getMyTID();
    task->uid = getuid();
    task->gid = getgid();
    task->aretty = 0;
    if (isatty(STDERR_FILENO)) {
	task->aretty |= (1 << STDERR_FILENO);
	fd = STDERR_FILENO;
    }
    if (isatty(STDOUT_FILENO)) {
	task->aretty |= (1 << STDOUT_FILENO);
	fd = STDOUT_FILENO;
    }
    if (isatty(STDIN_FILENO)) {
	task->aretty |= (1 << STDIN_FILENO);
	fd = STDIN_FILENO;
    }
    if (task->aretty) {
	tcgetattr(fd, &task->termios);
	ioctl(fd, TIOCGWINSZ, &task->winsize);
    }
    task->group = TG_ANY;
    task->loggernode = loggernode;
    task->loggerport = loggerport;
    task->loggertid = 0;

    if (!workingdir || (workingdir[0]!='/')) {
	/*
	 * get the working directory
	 */
	pmyworkingdir = getenv("PWD");
	if (pmyworkingdir) {
	    strcpy(myworkingdir, pmyworkingdir);
	    pmyworkingdir = myworkingdir;
	} else {
	    getcwd(myworkingdir, sizeof(myworkingdir));
	    strcat(myworkingdir, "/");
	    strcat(myworkingdir, workingdir ? workingdir : ".");
	    /*
	     * remove automount directory name.
	     */
	    if (strncmp(myworkingdir,"/tmp_mnt",strlen("/tmp_mnt"))==0)
		pmyworkingdir = &myworkingdir[8];
	    else if (strncmp(myworkingdir,"/export",strlen("/export"))==0)
		pmyworkingdir = &myworkingdir[7];
	    else
		pmyworkingdir = &myworkingdir[0];
	}
    } else
	strcpy(myworkingdir, workingdir);
    task->workingdir = strdup(pmyworkingdir);
    task->argc = argc;
    task->argv = (char**)malloc(sizeof(char*)*(task->argc+1));
    for (i=0;i<task->argc;i++)
	task->argv[i]=strdup(argv[i]);
    task->argv[task->argc]=0;

    task->environ = dumpPSIEnv();

    outstanding_answers=0;
    for (i=0; i<count; i++) {
	/*
	 * check if dstnode is ok
	 */
	if (dstnodes[i] >= PSC_getNrOfNodes()) {
	    errors[i] = ENETUNREACH;
	    tids[i] = -1;
	} else {
	    /*
	     * set the correct rank
	     */
	    task->rank = rank++;

	    /* pack the task information in the msg */
	    msg.header.len = PStask_encode(msg.buf, sizeof(msg.buf), task);

	    /*
	     * put the type of the msg in the head
	     * put the length of the whole msg to the head of the msg
	     * and return this value
	     */
	    msg.header.type = PSP_DD_SPAWNREQUEST;
	    msg.header.len += sizeof(msg.header);;
	    msg.header.sender = PSC_getMyTID();
	    msg.header.dest = PSC_getTID(dstnodes[i],0);

	    if (PSI_sendMsg(&msg)<0) {
		char *errstr = strerror(errno);
		snprintf(errtxt, sizeof(errtxt), "PSI_dospawn():"
			 " PSI_sendMsg() failed: %s",
			 errstr ? errstr : "UNKNOWN");
		PSI_errlog(errtxt, 0);
		return -1;
	    }

	    outstanding_answers++;
	}
    }/* for all new processes */

    PStask_delete(task);

    /*
     * Receive Answer from  my own daemon
     *----------------------------------
     */
    while (outstanding_answers>0) {
	if (PSI_recvMsg(&answer)<0) {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt), "PSI_dospawn():"
		     " PSI_recvMsg() failed: %s",
		     errstr ? errstr : "UNKNOWN");
	    PSI_errlog(errtxt, 0);
	    ret = -1;
	    break;
	}
	switch (answer.header.type) {
	case PSP_DD_SPAWNFAILED:
	case PSP_DD_SPAWNSUCCESS:
	    /*
	     * find the right task request
	     */
	    for (i=0; i<count; i++) {
		if (dstnodes[i]==PSC_getID(answer.header.sender)
		    && !tids[i] && !errors[i]) {
		    /*
		     * We have to test for !errors[i], since daemon on node 0
		     * (which has tid 0) might have returned an error.
		     */
		    errors[i] = answer.error;
		    tids[i] = answer.header.sender;
		    ret++;
		    break;
		}
	    }

	    if (i==count) {
		if (PSC_getID(answer.header.sender)==PSC_getMyID()
		    && answer.error==EACCES && count==1) {
		    /* This might be due to 'starting not allowed' here */
		    errors[0] = answer.error;
		    tids[0] = answer.header.sender;
		    ret++;
		} else {
		    snprintf(errtxt, sizeof(errtxt), "PSI_dospawn():"
			     " got SPAWNSUCCESS/SPAWNFAILED message from"
			     " unknown node %d.",
			     PSC_getID(answer.header.sender));
		    PSI_errlog(errtxt, 0);
		}
	    }

	    if (answer.header.type==PSP_DD_SPAWNFAILED) {
		snprintf(errtxt, sizeof(errtxt), "PSI_dospawn():"
			 " spawn to node %d failed.",
			 PSC_getID(answer.header.sender));
		PSI_errlog(errtxt, 0);
		error = 1;
	    }
	    break;
	default:
	    snprintf(errtxt, sizeof(errtxt), "PSI_dospawn(): UNKNOWN answer");
	    PSI_errlog(errtxt, 0);
	    errors[0] = 0;
	    error = 1;
	    break;
	}
	outstanding_answers--;
    }

    if (error) ret = -ret;
    return ret;
}

int PSI_kill(long tid, short signal)
{
    DDSignalMsg_t  msg;

    snprintf(errtxt, sizeof(errtxt), "PSI_kill(%lx, %d)", tid, signal);
    PSI_errlog(errtxt, 10);

    msg.header.len = sizeof(msg);
    msg.header.type = PSP_DD_SIGNAL;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = tid;
    msg.signal = signal;
    msg.param = getuid();

    if (PSI_sendMsg(&msg)<0) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "PSI_kill():"
		 " PSI_sendMsg() failed: %s",
		 errstr ? errstr : "UNKNOWN");
	PSI_errlog(errtxt, 0);
	return -1;
    }

    return 0;
}
