#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <netdb.h>

#include "psp.h"
#include "psitask.h"
#include "psilog.h"
#include "logger.h"
#include "psi.h"
#include "info.h"

#include "psispawn.h"

/*
 * The name for the environment variable setting the nodes used
 * when spawning.
 */
#define ENV_NODE_SELECT  "PSI_NODES"
#define ENV_NODE_HOSTFILENAME "PSI_HOSTFILE"
#define ENV_NODE_SORT  "PSI_NODES_SORT"

short* PSI_Partition = NULL;
short PSI_PartitionSize = 0;
short PSI_PartitionIndex = 0;

int PSI_SortNodesInPartition(short nodes[], int maxnodes);

#define PSHOSTACTIVE(nodeno) (nodeno>=0 && nodeno<PSI_nrofnodes && \
                              (PSI_hoststatus[nodeno] & PSPHOSTUP))

/*----------------------------------------------------------------------*/
/*
 * PSIspawn()
 *
 *  creates a new process on ParaStation node DSTNODE.
 *  The WORKINGDIR can either be absolute or realativ to the actual
 *  working directory of the spawning process.
 *  ARGC is the number of arguments and ARGV are the arguments as known
 *  from main(argc,argv)
 *  ERROR returns an errorcode if the spawning failed.
 *  RETURN -1 on failure
 *         TID of the new process on success
 */
long
PSI_spawn(short dstnode, char*workingdir, int argc, char**argv,
	 int masternode, int masterport, int rank, int *error)
{
    int ret;
    short dstnodes[1];
    int errors[1];
    long tids[1];

    if(dstnode<0){
	if(PSI_Partition==NULL){
	    if(PSI_getPartition()>0)
		PSI_SortNodesInPartition(PSI_Partition, PSI_PartitionSize);
	}
	if(PSI_Partition!=NULL){
	    int count=0;
	    while(dstnode<0){
		count++;
		dstnode = PSI_Partition[PSI_PartitionIndex];
		PSI_PartitionIndex ++;
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

    ret = PSI_dospawn(1, dstnodes, workingdir, argc, argv,
		      masternode, masterport, rank, errors,tids);
    *error = errors[0];
    if(ret<0)
	return ret;
    else
	return tids[0];
}

/*----------------------------------------------------------------------*/
/*
 * PSIspawnM()
 *
 *  creates count new processes on ParaStation node DSTNODES[].
 *  The WORKINGDIR can either be absolute or realativ to the actual
 *  working directory of the spawning process.
 *  ARGC is the number of arguments and ARGV are the arguments as known
 *  from main(argc,argv)
 *  ERROR[] returns an errorcode if the spawning failed.
 *  TIDS[]  returns the tids of the new processes on success
 *  RETURN -1 on failure
 *         >0 nr of processes on success
 */
int
PSI_spawnM(int count, short *dstnodes,char *workingdir, int argc, char **argv,
	   int masternode, int masterport, int rank, int *errors, long *tids)
{
    short* mydstnodes=NULL;
    int ret;
    if(count < 1)
	return 0;
    if(dstnodes==NULL){
	if(PSI_Partition==NULL){
	    if(PSI_getPartition()>0)
		PSI_SortNodesInPartition(PSI_Partition, PSI_PartitionSize);
	}
	if(PSI_Partition!=NULL){
	    int i;
	    mydstnodes= (short*) malloc(count*sizeof(short));

	    for(i=0;i<count;i++){
		int pcount=0;
		do{
		    pcount++;
		    mydstnodes[i] = PSI_Partition[PSI_PartitionIndex];
		    PSI_PartitionIndex ++;
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
	mydstnodes=(short*)dstnodes;

    ret = PSI_dospawn(count, mydstnodes, workingdir, argc, argv,
		      masternode, masterport, rank, errors, tids);

    /*
     * if I allocated mydstnodes myself
     * => destroy it again
     */
    if(dstnodes==NULL)
	free(mydstnodes);

    return ret;
}

/*----------------------------------------------------------------------*/
/*
 * PSIisalive(PSTID tid)
 *
 *  checks if a task is still running
 *
 *  RETURN 0 if the task is not alive
 *         1 if the task is alive
 */
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
int PSI_CompareNodesInPartionsByLoad(const void *entry1, const void *entry2)
{
    int ret;
    if((((sort_block *)entry2)->load < ((sort_block *)entry1)->load))
	ret = 1;
    else if((((sort_block *)entry2)->load > ((sort_block *)entry1)->load))
	ret =  -1;
    else
	ret =  0;
    if(((sort_block *)entry2)->status == 0) return 1;
    if(((sort_block *)entry1)->status == 0) return -1;

    return ret;
}

int PSI_SortNodesInPartition(short nodes[], int maxnodes)
{
    int i;
    double iload;
    sort_block *node_entry;
    char* env_sort;

    /* get the sorting routine from the environment */
    env_sort = getenv(ENV_NODE_SORT);
    if(env_sort!=NULL){
	char* env_entry;
	/* Set the environment variable */
	/* allocate enough space for the env variable */
	if((env_entry = malloc(strlen(ENV_NODE_SORT)+2+strlen(env_sort)))
	   ==NULL )
	    return -1;           /* Failed duplication */
	strcpy(env_entry,ENV_NODE_SORT);
	strcat(env_entry, "=");
	strcat(env_entry, env_sort);

	if(PSI_putenv(env_entry) == -1){
	    free(env_entry);
	    return -1;
	}
	free(env_entry);
    }else{
	env_sort = PSI_getenv(ENV_NODE_SELECT);
    }
    if(env_sort!=NULL)
	if(strcasecmp(env_sort,"LOAD")==0){
	    if((node_entry =
		(sort_block *)malloc(maxnodes * sizeof(sort_block))) == NULL){
		return -1;
	    }

	    /* Create the structs to sort */
	    for(i=0; i<maxnodes; i++){
		node_entry[i].id = nodes[i];
		/* Get the status. Or rather, set it. */
		node_entry[i].status = 1;
		node_entry[i].load = iload = PSI_getload((unsigned short) i);
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

    return 0;
}

/*------------------------------------------------------------
 * PSIGetPartition()
 *
 * Set the available node numbers in an array.
 * and sorts this array due to the enrivonment variable
 * If no partition is given, the whole cluster is used
 * If no sorting algorithm is given the parition is used unsorted
 *
 * RETURN:  the number of nodes in the partition or -1 on error.
 */
short PSI_getPartition()
{
    char* hostfilename=NULL;
    int maxnodes;
    char *node_str;

    maxnodes = PSI_getnrofnodes();

    /* Get the selected nodes */
    if( (node_str = getenv(ENV_NODE_SELECT)) != NULL ){
	char* env_entry;
	/* Set the environment variable */
	/* allocate enough space for the env variable */
	if((env_entry =
	    malloc(strlen(ENV_NODE_SELECT)+2+strlen(node_str)))==NULL)
	    return -1;           /* Failed duplication */
	strcpy(env_entry,ENV_NODE_SELECT);
	strcat(env_entry, "=");
	strcat(env_entry, node_str);

	if( PSI_putenv(env_entry) == -1){
	    free(env_entry);
	    return -1;
	}
	free(env_entry);
    }else if((node_str = PSI_getenv(ENV_NODE_SELECT))==NULL){
	/* no PSI_NODES in ENV or PSI_ENV
	   => check PSI_HOSTFILE */
	hostfilename = getenv(ENV_NODE_HOSTFILENAME);
	if(hostfilename==NULL)
	    hostfilename = PSI_getenv(ENV_NODE_HOSTFILENAME);
    }
    if(PSI_Partition)
	free(PSI_Partition);
    /*
     * allocate 4 times the maximum clustersize. This enables to add one
     * node several times. So you can tell the system to use one node more
     * often then another. */
    PSI_Partition = (short*)malloc(sizeof(short)*4*maxnodes);
    PSI_PartitionSize = 0;

    if( node_str != NULL ){
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
		bcopy((char *)hp->h_addr, (char*)&sin_addr, hp->h_length);
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
	/* No variable found - accept all? */
	int localnode;
	localnode = PSI_getnode(-1);
	PSI_PartitionSize = 0;
	for( PSI_PartitionSize=0; PSI_PartitionSize<maxnodes;
	     PSI_PartitionSize++ ){
	    PSI_Partition[PSI_PartitionSize] =
		(PSI_PartitionSize+localnode+1)%maxnodes;
	}
    }

    return PSI_PartitionSize;
}



/*----------------------------------------------------------------------*/
/*
 * PSI_dospawn()
 *
 *  creates COUNT new processes on ParaStation node DSTNODE[].
 *  The WORKINGDIR can either be absolute or realativ to the actual
 *  working directory of the spawning process.
 *  ARGC is the number of arguments and ARGV are the arguments as known
 *  from main(argc,argv)
 *  ERROR[] returns an errorcode if the spawning failed.
 *  TIDS[] returns an tids if the spawning is successful.
 *  RETURN -1 on failure
 *         nr of processes spawned on success
 */
int
PSI_dospawn(int count, short *dstnodes, char *workingdir,
	     int argc, char **argv, int masternode, int masterport,
	     int firstrank, int *errors, long *tids)
{
    int outstanding_answers=0;
    DDBufferMsg_t msg;
    char myworkingdir[200];
    char* pmyworkingdir = &myworkingdir[0];

    char hostname[256];
    struct in_addr sin_addr;
    struct hostent *hp;

    int i;          /* count variable */
    int ret=0;      /* return value */
    PStask_t* task; /* structure to store the information of the new process */

#ifdef DEBUG
    if(PSP_DEBUGTASK & PSI_debugmask){
	sprintf(PSI_txt,"PSI_spawn(%d,%lx,\"%s\",%d,\"%s\", %x, %x, %x)\n",
		count, (long)dstnodes, workingdir?workingdir:"NULL",
		argc, argc?argv[0]:"NULL", masternode, masterport, firstrank);
	PSI_logerror(PSI_txt);
    }
#endif
    /*
     * Send the request to my own daemon
     *----------------------------------
     */

    for(i=0;i<count;i++){
	errors[i] =0;
	tids[i]=0;
    }

    /*
     * Init the Task structure
     */
    task = PStask_new();
    task->ptid = PSI_gettid(-1,getpid());
    task->uid = getuid();
    task->gid = getgid();
    task->nodeno = dstnodes[0];
    task->group = TG_ANY;
    task->masternode = masternode;
    task->masterport = masterport;
    task->options = PSI_mychildoptions;

    gethostname(hostname, sizeof(hostname));
    hp = gethostbyname(hostname);
    bcopy((char *)hp->h_addr, (char*)&sin_addr, hp->h_length);
    task->loggernode = sin_addr.s_addr;
    task->loggerport = LOGGERspawnlogger();

    if ((workingdir==NULL)||(workingdir[0]!='/')){
	/*
	 * get the working directory
	 */
	pmyworkingdir = getenv("PWD");
	if(pmyworkingdir){
	    strcpy(myworkingdir,pmyworkingdir);
	    pmyworkingdir = myworkingdir;
	}else{
	    getcwd(myworkingdir,sizeof(myworkingdir));
	    strcat(myworkingdir,"/");
	    strcat(myworkingdir,workingdir?workingdir:".");
	    /*
	     * remove automount directory name.
	     */
	    if(strncmp(myworkingdir,"/tmp_mnt",strlen("/tmp_mnt"))==0)
		pmyworkingdir = &myworkingdir[8];
	    else if(strncmp(myworkingdir,"/export",strlen("/export"))==0)
		pmyworkingdir = &myworkingdir[7];
	    else
		pmyworkingdir = &myworkingdir[0];
	}
    }else
	strcpy(myworkingdir,workingdir);
    task->workingdir = strdup(pmyworkingdir);
    task->argc = argc;
    task->argv = (char**)malloc(sizeof(char*)*(task->argc+1));
    for(i=0;i<task->argc;i++)
	task->argv[i]=strdup(argv[i]);
    task->argv[task->argc]=0;

    task->environc = PSI_environc;
    if(PSI_environc){
	task->environ = (char**)malloc(sizeof(char*)*(task->environc));
	for(i=0;i<task->environc;i++)
	    task->environ[i] = PSI_environ[i] ? strdup(PSI_environ[i]):NULL;
    }

    outstanding_answers=0;
    for(i=0;i<count;i++){
	/*
	 * check if dstnode is ok
	 */
	if(dstnodes[i] >= PSI_getnrofnodes()){
	    errors[i] = ENETUNREACH;
	    tids[i] = -1;
	}else{
	    /*
	     * set the correct destination node
	     */
	    task->nodeno = dstnodes[i];
	    /*
	     * set the correct rank
	     */
	    task->rank = ++firstrank;

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
    while(outstanding_answers>0){
	if(ClientMsgReceive(&msg)<0){
	    perror("PSI_spawn(receiving answer from my daemon)");
	    ret = -1;
	    break;
	}
	switch (msg.header.type){
	case PSP_DD_SPAWNSUCCESS:
#ifdef DEBUG
	    if(PSP_DEBUGTASK & PSI_debugmask){
		sprintf(PSI_txt,"PSPSPAWNSUCCESS received: msglen %ld\n",
			msg.header.len);
		PSI_logerror(PSI_txt);
	    }
#endif
	    PStask_decode(msg.buf,task);
	    /*
	     * find the right task request
	     */
	    for(i=0;i<count;i++)
		if((dstnodes[i]==task->nodeno) && (tids[i]==0)){
		    errors[i] = task->error;
		    tids[i] = task->tid;
		    ret++;
		    break;
		}

#ifdef DEBUG
	    if(PSP_DEBUGTASK & PSI_debugmask){
		sprintf(PSI_txt,"PSPSPAWNSUCCESS with no (%lx)"
			" and parent(%lx) of user (%d). "
			"It was request nr %d\n", task->tid, task->ptid,
			task->uid,i);
		PSI_logerror(PSI_txt);
	    }
#endif
	    break;
	case PSP_DD_SPAWNFAILED:
#ifdef DEBUG
	    if(PSP_DEBUGTASK & PSI_debugmask){
		sprintf(PSI_txt,"PSPMSGSPAWNFAILED received: msglen %ld\n",
			msg.header.len);
		PSI_logerror(PSI_txt);
	    }
#endif
	    PStask_decode(msg.buf,task);
	    /*
	     * find the right task request
	     */
	    for(i=0;i<count;i++)
		if((dstnodes[i]==task->nodeno) && (tids[i]==0)){
		    errors[i] = task->error;
		    tids[i] = -1;
		    break;
		}
#ifdef DEBUG
	    if(PSP_DEBUGTASK & PSI_debugmask){
		sprintf(PSI_txt,"PSPMSGSPAWNFAILED error = %ld parent(%lx)\n",
			task->error, task->ptid);
		PSI_logerror(PSI_txt);
	    }
#endif
	    break;
	default:
	    sprintf(PSI_txt,"response == UNKNOWN answer\n");
	    PSI_logerror(PSI_txt);
	    errors[0] = 0;
	    ret = -1;
	    break;
	}
	outstanding_answers--;
    }

    /*
     * Clean up
     */
    PStask_delete(task);

    return ret;
}

/*----------------------------------------------------------------------*/
/*
 * PSI_kill()
 *
 *  kill a task on any node of the cluster.
 *  TID is the task identifier of the task, which shall receive the signal
 *  SIGNAL is the signal to be sent to the task
 *
 *  RETURN -1 on failure
 *         0 on sucess
 */
int
PSI_kill(long tid,short signal)
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
