/*
 *               ParaStation3
 * psispawn.c
 *
 * Spawning of processes and helper functions.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psispawn.c,v 1.15 2002/02/19 09:33:10 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psispawn.c,v 1.15 2002/02/19 09:33:10 eicker Exp $";
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

#include "psp.h"
#include "psitask.h"
#include "psilog.h"
#include "logger.h"
#include "psi.h"
#include "info.h"
#include "psienv.h"

#include "psispawn.h"

/*
 * The name for the environment variable setting the nodes used
 * when spawning.
 */
#define ENV_NODE_SELECT  "PSI_NODES"
#define ENV_NODE_HOSTFILENAME "PSI_HOSTFILE"
#define ENV_NODE_HOSTS   "PSI_HOSTS"
#define ENV_NODE_HOSTS_LSF "LSB_HOSTS"
#define ENV_NODE_SORT  "PSI_NODES_SORT"
#define ENV_NODE_RARG  "PSI_RARG_PRE_%d"

short* PSI_Partition = NULL;
short PSI_PartitionSize = 0;
short PSI_PartitionIndex = 0;

int PSI_SortNodesInPartition(short nodes[], int maxnodes);

#define PSHOSTACTIVE(nodeno) (nodeno>=0 && nodeno<PSI_nrofnodes && \
                              (PSI_hoststatus[nodeno] & PSPHOSTUP))

long PSI_spawn(short dstnode, char*workdir, int argc, char**argv,
	       unsigned int loggernode, unsigned short loggerport,
	       int rank, int *error)
{
    int ret;
    short dstnodes[1];
    int errors[1];
    long tids[1];

    if (dstnode<0) {
	if (PSI_Partition==NULL) {
	    if (PSI_getPartition()<0) {
		errno = EINVAL;
		return -1;
	    }
	}
	if(PSI_Partition!=NULL){
	    int count=0;
	    while(dstnode<0){
		count++;
		dstnode = PSI_Partition[PSI_PartitionIndex];
		PSI_PartitionIndex++;
		if(PSI_PartitionIndex >= PSI_PartitionSize){
		    PSI_PartitionIndex =0;
		}
		if (!PSHOSTACTIVE(dstnode))
		    dstnode=-1;
		if(count>PSI_PartitionSize){
		    errno = EHOSTUNREACH;
		    return -1;
		}
	    }
	}else{
	    errno = EHOSTUNREACH;
	    return -1;
	}
    }
    errors[0]=0;
    dstnodes[0]=dstnode;

    ret = PSI_dospawn(1, dstnodes, workdir, argc, argv,
		      loggernode, loggerport, rank, 0, errors,tids);
    *error = errors[0];
    if(ret<0)
	return ret;
    else
	return tids[0];
}

int PSI_spawnM(int count, short *dstnodes, char *workdir,
	       int argc, char **argv,
	       unsigned int loggernode, unsigned short loggerport,
	       int rank, long parenttid, int *errors, long *tids)
{
    short* mydstnodes=NULL;
    int ret;
    if(count < 0)
	return 0;
    if(dstnodes==NULL){
	if (PSI_Partition==NULL) {
	    if (PSI_getPartition()<0) {
		errno = EINVAL;
		return -1;
	    }
	}
	if(PSI_Partition!=NULL){
	    int i;
	    mydstnodes= (short*) malloc(count*sizeof(short));
	    PSI_PartitionIndex=rank;

	    for(i=0;i<count;i++){
		int pcount=0;
		do{
		    pcount++;
		    mydstnodes[i] = PSI_Partition[PSI_PartitionIndex];
		    PSI_PartitionIndex++;
		    if(PSI_PartitionIndex >= PSI_PartitionSize){
			PSI_PartitionIndex =0;
		    }
		    if( pcount > PSI_PartitionSize ){
			/* couldn't find a node which is active */
			errno = EHOSTUNREACH;
			return -1;
		    }
		}while(!PSHOSTACTIVE(mydstnodes[i]));
	    }
	}else{
	    errno = EHOSTUNREACH;
	    return -1;
	}

    }else
	mydstnodes = dstnodes;

    ret = PSI_dospawn(count, mydstnodes, workdir, argc, argv,
		      loggernode, loggerport, rank, parenttid, errors, tids);

    /*
     * if I allocated mydstnodes myself
     * => destroy it again
     */
    if (dstnodes==NULL) free(mydstnodes);

    return ret;
}

int PSIisalive(long tid)
{
    return INFO_request_taskinfo(tid, INFO_ISALIVE);
}

/* used for sorting the nodes */
typedef struct {
    int    id;
    int    status;
    double load;
} sort_block;

int PSI_CompareNodesInPartionsByLoad(const void *entry1, const void *entry2)
{
    int ret;
    if((((sort_block *)entry2)->load < ((sort_block *)entry1)->load))
	ret = 1;
    else if((((sort_block *)entry2)->load > ((sort_block *)entry1)->load))
	ret =  -1;
    else if (((sort_block *)entry2)->id < ((sort_block *)entry1)->id)
	ret =  1;
    else
	ret = -1;
    if(((sort_block *)entry2)->status == 0) return 1;
    if(((sort_block *)entry1)->status == 0) return -1;

    return ret;
}

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

int PSI_SortNodesInPartition(short nodes[], int maxnodes)
{
    int i;
    sort_block *node_entry;
    char *env_sort, *env_str;
    double (*loadfunc)(unsigned short) = NULL;

    /* get the sorting routine from the environment */
    if (!(env_sort = getenv(ENV_NODE_SORT))) {
	/* default now PROC jh 2001-12-21 */
	env_sort = "PROC";
    }

    if (strcasecmp(env_sort,"LOAD")==0) {
	loadfunc = INFO_request_load;
    }

    if (strcasecmp(env_sort,"PROC")==0) {
	loadfunc = INFO_request_proc;
    }

    if (loadfunc) {
	node_entry = (sort_block *)malloc(maxnodes * sizeof(sort_block));

	if (!node_entry) {
	    return -1;
	}

	/* Create the structs to sort */
	for(i=0; i<maxnodes; i++){
	    node_entry[i].id = nodes[i];
	    /* Get the status. Or rather, set it. */
	    node_entry[i].status = 1;
	    node_entry[i].load = loadfunc((unsigned short) nodes[i]);
	}

	/* Sort the nodes */
	qsort(node_entry, maxnodes, sizeof(sort_block),
	      PSI_CompareNodesInPartionsByLoad);

	/* Transfer the results */
	for( i=0; i<maxnodes; i++ ){
	    nodes[i] = node_entry[i].id;
	}
	free(node_entry);
    }

    /* Propagate nodes to clients */
    env_str = (char *) malloc(maxnodes * 5 + 1);
    if (env_str) {
	for (i=0; i<maxnodes; i++) {
	    sprintf(&env_str[5*i], "%4d,", nodes[i]);
	}
	env_str[5*maxnodes-1] = 0;

	setPSIEnv(ENV_NODE_SELECT, env_str, 1);
	setPSIEnv(ENV_NODE_SORT, "none", 1);
	/* Only sort once */
	setenv(ENV_NODE_SORT, "none", 1);

	free(env_str);
    } else {
	return -1;
    }

    return 0;
}

/* Get white-space seperatet field. Return value must be freed
 * with free(). If next is set, *next return the beginning of the next field */
char *get_wss_entry(char *str,char **next)
{
    char *start=str;
    char *end=NULL;
    char *ret=NULL;
    if (!str) goto no_str;
    while(isspace(*start)) start++;
    end=start;
    while( (!isspace(*end)) && (*end)) end++;
    if (start != end){
	ret=(char*)malloc(end-start +1);
	strncpy(ret,start,end-start);
	ret[end-start]=0;
    }
 no_str:
    if (next) *next=end;
    return ret;
}

void PSI_LSF(void)
{
    char *lsf_hosts=NULL;
    lsf_hosts=getenv(ENV_NODE_HOSTS_LSF);
    if (lsf_hosts){
/*  	char *p=lsf_hosts; */
/*  	char *ret,*host,*first; */
	setenv(ENV_NODE_SORT, "none", 1);
	unsetenv(ENV_NODE_HOSTFILENAME);
	unsetenv(ENV_NODE_SELECT);
	/* Move first hostname to the end of the list */
/*  	ret=(char*)malloc(strlen(lsf_hosts)+1); */
/*  	*ret=0; */
/*  	first=get_wss_entry(p,&p); */
/*  	if (first){ */
/*  	    while ((host=get_wss_entry(p,&p))){ */
/*  		strcat(ret,host); */
/*  		strcat(ret," "); */
/*  		free(host); */
/*  	    } */
/*  	    strcat(ret,first); */
/*  	    free(first); */
/*  	} */
/*  	setenv(ENV_NODE_HOSTS,ret,1); */
/*  	free(ret); */
	setenv(ENV_NODE_HOSTS, lsf_hosts, 1);
    }
}

void PSI_RemoteArgs(int Argc,char **Argv,int *RArgc,char ***RArgv){
    int new_argc=0;
    char **new_argv;
    char env_name[ sizeof(ENV_NODE_RARG) + 20];
    int cnt;
    int i;
    cnt=0;
    for(;;){
	snprintf(env_name,sizeof(env_name)-1,
		 ENV_NODE_RARG,cnt);
	if (getenv(env_name)){
	    cnt++;
	}else{
	    break;
	}
    }
    if (cnt){
	new_argc=cnt+Argc;
	new_argv=malloc(sizeof(char *)*(new_argc+1));
	new_argv[new_argc]=NULL;
	
	for(i=0;i<cnt;i++){
	    snprintf(env_name,sizeof(env_name)-1,ENV_NODE_RARG,i);
	    new_argv[i]=getenv(env_name);
	    /* Propagate the environment */
	    setPSIEnv(env_name, new_argv[i], 1);
	}
	for(i=0;i<Argc;i++){
	    new_argv[i+cnt]=Argv[i];
	}
	*RArgc=new_argc;
	*RArgv=new_argv;
    }else{
	*RArgc=Argc;
	*RArgv=Argv;
    }
    return;
}

short PSI_getPartition(void)
{
    int maxnodes;
    char* hostfilename=NULL;
    char *node_str=NULL;
    char *hosts_str=NULL;

    maxnodes = PSI_getnrofnodes();

    /* Get the selected nodes */
    if (! (node_str = getenv(ENV_NODE_SELECT))) {
	if (! (hosts_str = getenv(ENV_NODE_HOSTS))) {
	    hostfilename = getenv(ENV_NODE_HOSTFILENAME);
	}
    }

    if (PSI_Partition) {
	free(PSI_Partition);
    }

    /*
     * allocate 4 times the maximum clustersize. This enables to add one
     * node several times. So you can tell the system to use one node more
     * often then another. */
    PSI_Partition = (short*)malloc(sizeof(short)*4*maxnodes);
    PSI_PartitionSize = 0;

    if (node_str) {
	int next_node;
	char* tmp_node_str, *tmp_node_str_begin, *tmp_node_str_end;

	tmp_node_str = tmp_node_str_begin = strdup(node_str);
	if(tmp_node_str==NULL)
	    return -1;

	while((tmp_node_str_end = strchr(tmp_node_str,','))!=NULL
	      && PSI_PartitionSize < 4*maxnodes){
	    /* while there are more node numbers in string */
	    *tmp_node_str_end = '\0';
	    if((sscanf(tmp_node_str, "%d", &next_node)>0)
	       && (next_node < maxnodes)){
		PSI_Partition[PSI_PartitionSize] = next_node;
		PSI_PartitionSize++;
	    }
	    tmp_node_str = tmp_node_str_end+1;
	}
	/* Check if the last element is a node_nr */
	if((sscanf(tmp_node_str, "%d", &next_node)>0)
	   && (next_node < maxnodes)
	   && (PSI_PartitionSize < 4*maxnodes)){
	    PSI_Partition[PSI_PartitionSize] = next_node;
	    PSI_PartitionSize++;
	}
	free(tmp_node_str_begin);
    }else if (hosts_str){
	/* parse hosts_str for nodenames */
	char *hostname;
	char *p=hosts_str;
	struct in_addr sin_addr;
	struct hostent *hp;
	PSI_PartitionSize = 0;
	while((hostname=get_wss_entry(p,&p))
	      && PSI_PartitionSize<4*maxnodes){
	    hp = gethostbyname(hostname);
	    bcopy((char *)hp->h_addr, (char*)&sin_addr, hp->h_length);
	    if ((PSI_Partition[PSI_PartitionSize] =
		 INFO_request_host(sin_addr.s_addr)) != -1){
		/* printf("Host #%d '%s' PSID: %d\n",
		       PSI_PartitionSize,hostname,
		       PSI_Partition[PSI_PartitionSize]); */
		PSI_PartitionSize++;
	    }else{
		/* printf("Host #%d '%s' PSID: ???\n",
		   PSI_PartitionSize,hostname); */
	    }
	    free(hostname);
	}
	endhostent();
    }else if(hostfilename){
	/* either in ENV or in PSI_ENV there exists
	   the name of the hostfile
	   => open it and read the hostnames
	*/
	char hostname[1024];
	FILE* file;
	if((file = fopen(hostfilename,"r"))!=NULL){
	    struct in_addr sin_addr;
	    struct hostent *hp;
	    PSI_PartitionSize= 0;
	    while((fscanf(file,"%s",hostname)>0)
		  && PSI_PartitionSize<4*maxnodes){
		if(hostname[0] == '#')
		    continue;
		hp = gethostbyname(hostname);
		memcpy(&sin_addr, hp->h_addr, hp->h_length);
		if ((PSI_Partition[PSI_PartitionSize] =
		     INFO_request_host(sin_addr.s_addr)) != -1){
		    PSI_PartitionSize++;
		}
	    }
	    endhostent();
	    fclose(file);
	}else
	    return -1;
    }else{
	/* No variable found - get list from daemon */
	struct {
	    int numNodes;
	    short nodelist[256];
	} nodeinfo;

	// printf("Get hostlist\n");
	INFO_request_hostlist(&nodeinfo, sizeof(nodeinfo));
	// printf("Got hostlist of size %d\n", nodeinfo.numNodes);
	// localnode = PSI_getnode(-1);
	if (!nodeinfo.numNodes) {
	    printf("Can't get any hosts with MyriNet interface. Exiting...\n");
	    exit(1);
	}

	PSI_PartitionSize = 0;
	for (PSI_PartitionSize=0; PSI_PartitionSize<nodeinfo.numNodes;
	     PSI_PartitionSize++) {
	    PSI_Partition[PSI_PartitionSize] =
		nodeinfo.nodelist[PSI_PartitionSize];
	}
    }

    if (PSI_SortNodesInPartition(PSI_Partition, PSI_PartitionSize) == -1) {
	return -1;
    } else {
	return PSI_PartitionSize;
    }
}

int PSI_dospawn(int count, short *dstnodes, char *workingdir,
		int argc, char **argv,
		unsigned int loggernode, unsigned short loggerport,
		int rank, long parenttid, int *errors, long *tids)
{
    int outstanding_answers=0;
    DDBufferMsg_t msg;
    char myworkingdir[200];
    char* pmyworkingdir = &myworkingdir[0];

    int i;          /* count variable */
    int ret = 0;    /* return value */
    int error = 0;  /* error flag */
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
    if (parenttid) {
	task->ptid = parenttid;
    } else {
	task->ptid = PSI_gettid(-1,getpid());
    }
    // task->ptid = PSI_gettid(-1,getpid());
    task->uid = getuid();
    task->gid = getgid();
    task->group = TG_ANY;
    task->options = PSI_mychildoptions;
    task->loggernode = loggernode;
    task->loggerport = loggerport;

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
    for(i=0;i<task->argc;i++)
	task->argv[i]=strdup(argv[i]);
    task->argv[task->argc]=0;

    task->environ = dumpPSIEnv();

    outstanding_answers=0;
    for (i=0; i<count; i++) {
	/*
	 * check if dstnode is ok
	 */
	if (dstnodes[i] >= PSI_getnrofnodes()) {
	    errors[i] = ENETUNREACH;
	    tids[i] = -1;
	} else {
	    /*
	     * set the correct destination node
	     */
	    task->nodeno = dstnodes[i];
	    /*
	     * set the correct rank
	     */
	    task->rank = rank++;

	    /* pack the task information in the msg */
	    msg.header.len = PStask_encode(msg.buf, task);

	    /*
	     * put the type of the msg in the head
	     * put the length of the whole msg to the head of the msg
	     * and return this value
	     */

	    msg.header.type = PSP_DD_SPAWNREQUEST;
	    msg.header.len += sizeof(msg.header);;
	    msg.header.sender = PSI_mytid;
	    msg.header.dest = PSI_gettid(dstnodes[i],0);
	    ClientMsgSend(&msg);

	    outstanding_answers++;
	}
    }/* for all new processes */

    /*
     * Receive Answer from  my own daemon
     *----------------------------------
     */
    while (outstanding_answers>0) {
	if (ClientMsgRecv(&msg)<0) {
	    perror("PSI_spawn(receiving answer from my daemon)");
	    ret = -1;
	    break;
	}
	switch (msg.header.type) {
	case PSP_DD_SPAWNFAILED:
	case PSP_DD_SPAWNSUCCESS:
	    PStask_decode(msg.buf, task);
	    /*
	     * find the right task request
	     */
	    for (i=0; i<count; i++) {
		if ((dstnodes[i]==task->nodeno) && (tids[i]==0)) {
		    errors[i] = task->error;
		    tids[i] = task->tid;
		    ret++;
		    break;
		}
	    }

	    if (i==count) {
		fprintf(stderr, "Got SPAWNSUCCESS/SPAWNFAILED message from"
			" unknown node %d\n", task->nodeno);
	    }

	    if (msg.header.type==PSP_DD_SPAWNFAILED) {
		fprintf(stderr, "Spawn to node %d failed.\n", task->nodeno);
		error = 1;
	    }
	    break;
	default:
	    sprintf(PSI_txt,"response == UNKNOWN answer\n");
	    PSI_logerror(PSI_txt);
	    errors[0] = 0;
	    error = 1;
	    break;
	}
	outstanding_answers--;
    }

    /*
     * Clean up
     */
    PStask_delete(task);

    if (error) ret = -ret;
    return ret;
}

int PSI_kill(long tid,short signal)
{
    DDSignalMsg_t  msg;

    msg.header.len = sizeof(msg);
    msg.header.type = PSP_DD_TASKKILL;
    msg.header.sender = PSI_gettid(-1,getpid());
    msg.header.dest = tid;
    msg.senderuid = getuid();
    msg.signal = signal;

    ClientMsgSend(&msg);
    return 0;
}
