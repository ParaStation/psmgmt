
/*
 *      @(#)psid.c    1.00 (Karlsruhe) 10/4/95
 *
 *      written by Joachim Blum
 *
 *
 * This is the ParaStation daemon.
 * It manages together with all other daemons in the cluster the
 * whole ParaStation cluster.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>
#ifdef __osf__
#include <sys/table.h>      /* Used for table call for LOAD_AVG*/
#endif
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>

#include <pshal.h>

#include "parse.h"
#include "rdp.h"
#include "psp.h"
#include "psi.h"
#include "psidutil.h"
#include "psilog.h"

int CalledFromRSelect=0;

struct timeval maintimer;
struct timeval selecttimer;
struct timeval shutdowntimer;
struct timeval killclientstimer;
struct timeval pingtimer;

#define timerset(tvp,fvp)        {(tvp)->tv_sec  = (fvp)->tv_sec;\
                                  (tvp)->tv_usec = (fvp)->tv_usec;}
#define timerop(tvp,sec,usec,op) {(tvp)->tv_sec  = (tvp)->tv_sec op sec;\
                                  (tvp)->tv_usec = (tvp)->tv_usec op usec;}
#define mytimeradd(tvp,sec,usec) timerop(tvp,sec,usec,+)

int UIDLimit = -1;   /* not limited to any user */
int MAXPROCLimit = -1;   /* not limited to any number of processes */

/*------------------------------
 * CLIENTS
 */
/* possible values of clients.flags */
#define INITIALCONTACT  0x00000001    /* just after accept. No message received so far */

struct client_t{
    long tid;    /* task id of the client process;
		  *  this is a combination of nodeno and OS pid
		  *  partner daemons are connected with pid==0
		  */
    union{
	PStask_t* task;     /* pointer to a task, if the client is
			       associated with a task.
			       The right object can be decided by the id:
			       PSI_getpid(id)==0 => daemon
			       PSI_getpid(id)!=0 => task  */
	struct daemon_t* daemon; /* pointer to a daemon, if the client is
				    associated with a daemon */
    }ob;
    long flags;
};
struct client_t clients[FD_SETSIZE];


typedef struct {
    double load[3];
}Load_t;

/*------------------------------
 * DAEMONS
 */
struct daemon_t{
    u_short node;            /* node number of the daemon */
    int     fd;              /* file descr. to the daemon */
    long    status;          /* status of this node */
    PStask_t* tasklist;      /* tasklist of that node */
    struct timeval pongtime; /* the time of receiving last ping */
    Load_t  load;            /* the current load of the node */
};

struct daemon_t daemons[FD_SETSIZE];

/*-----------------------------
 * tasklist of tasks which
 * are spawned, but haven't connected to the daemon yet
 */
PStask_t* spawned_tasks_waiting_for_connect;

/*----------------------------------------------------------------------*/
/* states of the daemons                                                */
/*----------------------------------------------------------------------*/
#define PSP_DSTATE_RESET_HW              0x0002
#define PSP_DSTATE_RESET_INACTION        0x0004
#define PSP_DSTATE_RESET_WAIT            0x0008
#define PSP_DSTATE_DORESET               0x0010
#define PSP_DSTATE_SHUTDOWN              0x0020
#define PSP_DSTATE_SHUTDOWN2             0x0040

#define PSP_DSTATE_NOCONNECT (PSP_DSTATE_RESET_HW | PSP_DSTATE_RESET_INACTION \
                              | PSP_DSTATE_RESET_WAIT \
			      | PSP_DSTATE_SHUTDOWN | PSP_DSTATE_SHUTDOWN2)

int nfds;

fd_set openfds;			/* active file descriptor set */

u_long rawdata_is_used=0;
int NoOfConnectedDaemons=0;
int RDPSocket=-1;;

/*----------------------------------------------------------------------*/
/* needed prototypes                                                    */
/*----------------------------------------------------------------------*/
void client_delete(int fd);
void CheckFileTable();
void CloseConnection(int fd);
void DeclareDaemonDead(int node);
void InitDaemon(int fd, int id);
int DaemonIsUp(int node);
int send_DAEMONCONNECT(int id);

void TaskDeleteSendSignals(PStask_t* oldtask);
void TaskDeleteSendSignalsToParent(long tid,long ptid);

int
TOTALsend(int fd,void* buffer,int msglen)
{
    int nnnn;int iiii;
    for(nnnn=0,iiii=1;(nnnn<msglen)&&(iiii>0);){
	iiii=send(fd,&(((char*)buffer)[nnnn]),msglen-nnnn,0);
	if(iiii<=0){
	    if(errno!=EINTR){
		sprintf(PSI_txt,"got error %d on socket %d\n",errno,fd);
		PSI_logerror(PSI_txt);
		client_delete(fd);
		return iiii;
	    }
	}else
	    nnnn+=iiii;
    }
    return nnnn;
}

/******************************************
 * int MsgSend(DDMsg_t* msg)
 */
int MsgSend(void* amsg)
{
    DDMsg_t* msg = (DDMsg_t*)amsg;
    int fd=FD_SETSIZE;
    if (PSI_isoption(PSP_ODEBUG)){
	sprintf(PSI_txt,"MsgSend(type %s (len=%ld) to task 0x%lx[%d,%d]\n",
		PSPctrlmsg(msg->type),msg->len,msg->dest,
		msg->dest==-1?-1:PSI_getnode(msg->dest),
		PSI_getpid(msg->dest));
	SYSLOG(6,(LOG_ERR,PSI_txt));
    }

    if((PSI_getnode(msg->dest)==PSI_myid) || /* my own node */
       (PSI_getnode(msg->dest)==PSI_nrofnodes)){ /* remotely connected */
	for(fd=0;fd<FD_SETSIZE;fd++){
	    /* find the FD for the dest */
	    if(clients[fd].tid==msg->dest)
		break;
	}
    }else{
	if(PSI_getnode(msg->dest)<PSI_nrofnodes){
	    int ret;
	    if((ret = Rsendto(PSI_getnode(msg->dest),msg,msg->len))<msg->len){
		sprintf(PSI_txt,
			"MsgSend(type %s (len=%ld) to task 0x%lx[%d,%d] "
			"return %d ERROR %d\n",
			PSPctrlmsg(msg->type),msg->len,msg->dest,
			msg->dest==-1?-1:PSI_getnode(msg->dest),
			PSI_getpid(msg->dest),ret,errno);
		SYSLOG(1,(LOG_ERR,PSI_txt));
	    }
	    return ret;
	}
    }
    if(fd <FD_SETSIZE)
	return TOTALsend(fd,msg,msg->len);
    else
	return -1;
}

/******************************************
 *  MsgReceive()
 */
int MsgReceive(int fd, DDMsg_t* msg,int size)
{
    int n;
    int count =0;
    int fromnode=-1;
    if(fd == RDPSocket){
	fromnode=-1;
	n = Rrecvfrom(&fromnode,msg,size);
	if (PSI_isoption(PSP_ODEBUG)){
	    if(n>0)
		sprintf(PSI_txt,"MsgReceive(fd %d type %s (len=%ld) "
			"from task 0x%lx[%d,%d] to 0x%lx\n",
			fd,PSPctrlmsg(msg->type),msg->len,msg->sender,
			msg->sender==-1?-1:PSI_getnode(msg->sender),
			PSI_getpid(msg->sender),msg->dest);
	    else if(n==0)
		sprintf(PSI_txt,"MsgReceive(RDPSocket) returns 0\n");
	    else
		sprintf(PSI_txt,"MsgReceive(RDPSocket) returns -1\n");
	    SYSLOG(6,(LOG_ERR,PSI_txt));
	}
	return n;
    }else{
	/* it is a connection to a client */
	/* so use the regular OS receive */
	if(clients[fd].flags & INITIALCONTACT){
	    /* if this is the first contact of the client,
	     * the client may use an incompatible msg format
	     */
	    n = count = read(fd,msg,sizeof(DDInitMsg_t));
	    if(count!=msg->len){
		/* if wrong msg format initiate a disconnect */
		sprintf(PSI_txt,"%d=MsgReceive(fd %d) PANIC received an "
			"initial message with incompatible msg type.(%ld)\n",
			n,fd,msg->len);
		SYSLOG(0,(LOG_ERR,PSI_txt));
		count=n=0;
	    }
	}else
	    do{
		if(count==0)
		    n = read(fd,msg,sizeof(*msg));
		else
		    n = read(fd,&((char*)msg)[count], msg->len-count);
		if(n>0)count+=n;
		else if(n<0 && (errno==EINTR))
		    continue;
		else break;
	    }while((msg->len >count));
    }

    if(n==-1){
	char* errtxt;
	errtxt=strerror(errno);
	SYSLOG(1,(LOG_ERR,"MsgReceive(%d): error(%d) while read: %s\n",
		  fd,errno,errtxt?errtxt:"UNKNOWN errno"));
    }else if (PSI_isoption(PSP_ODEBUG)){
	if(n==0)
	    sprintf(PSI_txt,"%d=MsgReceive(fd %d)\n",n,fd);
	else
	    sprintf(PSI_txt,"%d=MsgReceive(fd %d type %s (len=%ld) "
		    "from task 0x%lx[%d,%d] to 0x%lx\n",
		    n,fd,PSPctrlmsg(msg->type),msg->len,msg->sender,
		    msg->sender==-1?-1:PSI_getnode(msg->sender),
		    PSI_getpid(msg->sender),msg->dest);
	SYSLOG(6,(LOG_ERR,PSI_txt));
    }

    if(count==msg->len)
	return msg->len;
    else
	return n;
}

/******************************************
 * int MsgBroadcast(DDMsg_t* msg)
 */
int MsgBroadcast(void* amsg)
{
    DDMsg_t* msg = (DDMsg_t*) amsg;
    int count=1;
    int i;
    if (PSI_isoption(PSP_ODEBUG)){
	sprintf(PSI_txt,"MsgBroadcast(type %s (len=%ld)\n",
		PSPctrlmsg(msg->type),msg->len);
	SYSLOG(6,(LOG_ERR,PSI_txt));
    }

    /* broadcast to every daemon and the sender */
    for (i=0;i<PSI_nrofnodes;i++)
	if (DaemonIsUp(i) && i!= PSI_myid){
	    msg->dest = PSI_gettid(i,0);
	    if(MsgSend(msg)>=0)
		count++;
	}
    return count;
}

/******************************************
 *  DaemonIsUp()
 * returns if a daemon is up
 */
int DaemonIsUp(int node)
{
    RDP_ConInfo info;

    if(node<0 || node >=PSI_nrofnodes)
	return 0;

    RDP_GetInfo(node,&info);
    daemons[node].load.load[0] = info.load.load[0];
    daemons[node].load.load[1] = info.load.load[1];
    daemons[node].load.load[2] = info.load.load[2];
    if(info.state == ACTIVE)
	return 1;

    return 0;
}

/******************************************
 *  BlockSig()
 */
void BlockSig(int block,int sig)
{
    sigset_t newset, oldset;
    int result;

    sigemptyset(&newset);
    sigaddset(&newset, sig);
    result = sigprocmask(block ? SIG_BLOCK : SIG_UNBLOCK, &newset, &oldset);
    if (result)
	PSI_logerror("BlockSig(): sigprocmask() ");
}

/******************************************
 *  CheckCluster()
 */
void CheckCluster()
{
    int i;        /* count variable */
    int ClusterWasUp;

    ClusterWasUp = (PSI_nrofnodes == NoOfConnectedDaemons);

    NoOfConnectedDaemons = 0;

    for (i=0;i<PSI_nrofnodes;i++)
	if (DaemonIsUp(i)) {
	    NoOfConnectedDaemons++;
	    PSID_hoststatus[i] |= PSPHOSTUP;
	}else
	    PSID_hoststatus[i] &= ~PSPHOSTUP;
}

/******************************************
 *  StartOtherDaemons()
 * if an applications contacts the daemon, the daemon trys
 * to start the other daemons on the cluster.
 */
void StartOtherDaemons()
{
    int i;
    u_long addr;

    if(fork()==0) {
	/* fork a process which starts all other daemons */
	BlockSig(0,SIGALRM);
	for(i=0;i<PSI_nrofnodes;i++)
	    if (!DaemonIsUp(i)) {
		addr = PSID_hostaddress(i);
		PSI_startdaemon(addr);
	    }
	exit(0);
    }
}

/******************************************
 *  kill_clients()
 *
 * killing client processes:
 *  - phase 0: just send them a SIGTERM signal to the processes which are not in group TG_ADMIN
 *  - phase 1: just send them a SIGTERM signal. Hopefully they will end until I reach phase 1
 *  - phase 2: send them a SIGKILL signal. Return and check their disconnect
 *  - phase 3: send them a SIGKILL signal. Clean up their open connections.
 */
int kill_clients(int phase)
{
    int i;
    int pid;

    if(timercmp(&maintimer,&killclientstimer,<)) {
	if (PSI_isoption(PSP_ODEBUG)) {
	    sprintf(PSI_txt,
		    "kill_clients(PHASE %d) timer not ready [%d:%d] < [%d:%d]\n",
		    phase,(int)maintimer.tv_sec,(int)maintimer.tv_usec,
		    (int)killclientstimer.tv_sec,
		    (int)killclientstimer.tv_usec);
	    PSI_logerror(PSI_txt);
	}
	return 0;
    }

    SYSLOG(4,(LOG_ERR,"kill_clients(PHASE %d)\n",phase));

    timerset(&killclientstimer,&maintimer);
    mytimeradd(&killclientstimer,0,200000);

    for(i=0;i<FD_SETSIZE;i++)
	if(FD_ISSET(i,&openfds) && (i!=PSI_msock) && (i!=RDPSocket)) {
	    /* if a client process send SIGTERM */
	    if((clients[i].tid != -1)
	       && (PSI_getnode(clients[i].tid)==PSI_myid) /* client proc */
	       && ((phase>0) || (clients[i].ob.task->group!=TG_ADMIN)))
		/* in phase 1-3 all */
		/* in phase 0 only process not in TG_ADMIN group */
	    {
		pid = PSI_getpid(clients[i].tid);
		SYSLOG(4,(LOG_ERR,
			  "kill_clients():sending tid%lx pid%d index[%d] %s\n",
			  clients[i].tid,pid,i,
			  ((phase<2)?"SIGTERM":"SIGKILL")));
		if(pid >0)
		    kill(pid,((phase<2)?SIGTERM:SIGKILL));
		if(phase>2) {
		    client_delete(i);
		}
	    }
	}
    SYSLOG(4,(LOG_ERR,"kill_clients(PHASE %d) done.\n",phase));
    return 1;
}

/******************************************
 *  shutdown_node()
 *
 * shut down my node:
 *  - phase 1: killing client processes and switch to DSTATE_SHUTDOWN
 *  - phase 2: kill all clients, where killing wasn't sucessful,
 *             Close connection to other nodes
 *             Close my own Master socket
 *  - phase 3: exit
 */
int shutdown_node(int phase)
{
    int i;

    if(timercmp(&maintimer,&shutdowntimer,<)){
	if(PSI_isoption(PSP_ODEBUG)){
	    sprintf(PSI_txt,"shutdown_node(PHASE %d) timer not ready [%d:%d]"
		    " < [%d:%d]\n", phase, (int)maintimer.tv_sec,
		    (int)maintimer.tv_usec, (int)shutdowntimer.tv_sec,
		    (int)shutdowntimer.tv_usec);
	    PSI_logerror(PSI_txt);
	}
	return 0;
    }

    SYSLOG(0,(LOG_ERR, "shutdown_node(PHASE %d)timer are main[%d:%d]"
	      " shutdown [%d:%d]\n", phase, (int)maintimer.tv_sec,
	      (int)maintimer.tv_usec, (int)shutdowntimer.tv_sec,
	      (int)shutdowntimer.tv_usec));

    timerset(&shutdowntimer,&maintimer);
    mytimeradd(&shutdowntimer,1,0);

    daemons[PSI_myid].status |= PSP_DSTATE_SHUTDOWN;

    if(phase >1)
	daemons[PSI_myid].status |= PSP_DSTATE_SHUTDOWN2;

    if(phase >1){
	/*
	 * close the Master socket -> no new connections
	 */
	shutdown(PSI_msock,2);
	close(PSI_msock);
	FD_CLR(PSI_msock,&openfds);
    }
    /*
     * kill all clients
     */
    kill_clients(phase);

    if(phase >1) {
	/*
	 * close all sockets to the other daemons
	 * and the sockets to the clients
	 */
	for(i=0;i<FD_SETSIZE;i++)
	    if(FD_ISSET(i,&openfds)&&(i!=PSI_msock)&&(i!=RDPSocket)) {
		CloseConnection(i);
	    }
    }
    if(phase >2) {
	RDPexit();
	PSID_CardStop();
	SYSLOG(0,(LOG_ERR,"shutdown_node() good bye\n"));
	exit(1);
    }
    return 1;
}

/******************************************
 *  InitDaemon()
 * Initializes a daemon structure
 */
void InitDaemon(int fd, int id)
{
    daemons[id].fd = fd;
    daemons[id].node = id;
    daemons[id].status = 0;
    daemons[id].tasklist = 0;
    timerset(&daemons[id].pongtime,&maintimer);

    PSID_hoststatus[id] |= PSPHOSTUP;
}

/******************************************
 * CloseConnection()
 * shutdown the filedescriptor and reinit the
 * clienttable on that filedescriptor
 */
void CloseConnection(int fd)
{
    if(fd<0)fd=-fd;

    clients[fd].ob.daemon = NULL;

    clients[fd].tid=-1;
    clients[fd].ob.task=NULL;
    shutdown(fd,2);
    (void) close(fd);
    FD_CLR(fd, &openfds);
}
/******************************************
 * DeclareDaemonDead()
 * is called when a connection to a daemon is lost
 */
void DeclareDaemonDead(int node)
{
    PStask_t* oldtask;
    SYSLOG(2,(LOG_ERR,"Lost connection to daemon of node %d (fd:%d)\n",
	      node,daemons[node].fd));

    daemons[node].fd = 0;
    daemons[node].status = 0;
    PSID_hoststatus[node] &= ~PSPHOSTUP;

    oldtask = PStasklist_dequeue(&(daemons[node].tasklist), -1);

    while(oldtask) {
	SYSLOG(9,(LOG_ERR,"Cleaning task TID%lx\n",oldtask->tid));

	TaskDeleteSendSignals(oldtask);
	TaskDeleteSendSignalsToParent(oldtask->tid,oldtask->ptid);
	PStask_delete(oldtask);
	oldtask = PStasklist_dequeue(&(daemons[node].tasklist), -1);
    }
    /*  PStasklist_delete(&(daemons[node].tasklist));*/
    SYSLOG(2,(LOG_ERR,
	      "Lost connection to daemon of node %d (fd:%d) returns\n",
	      node, daemons[node].fd));
}

/******************************************
*  contactdaemon()
*/
int send_DAEMONCONNECT(int id)
{
  DDMsg_t msg;

  msg.type = PSP_DD_DAEMONCONNECT;
  msg.sender = PSI_gettid(PSI_myid,0);
  msg.dest = PSI_gettid(id,0);
  msg.len = sizeof(msg);
  if(MsgSend(&msg)== msg.len)
    /* successful connection request is sent */
      return 0;
  return -1;
}

/******************************************
*  send_TASKLIST()
*   send the TASKLIST of the NODE to the requesting FD
*
*/
void send_TASKLIST(DDMsg_t *inmsg)
{
    PStask_t* task;
    DDBufferMsg_t msg;
    int success=1;
    int node = PSI_getnode(inmsg->dest);

    if((node<0) && (node >= PSI_nrofnodes))
	return;

    if (PSI_isoption(PSP_ODEBUG)){
	sprintf(PSI_txt,"send_TASKLIST(%lx,%lx)\n",inmsg->sender,inmsg->dest);
	SYSLOG(8,(LOG_ERR,PSI_txt));
    }
    for(task=daemons[node].tasklist; task && (success>0);task=task->link){
	/*
	 * send all tasks in the tasklist to the fd
	 */
	task->error = 0;
	task->nodeno = node;

	msg.header.len = sizeof(DDMsg_t);
	msg.header.len += PStask_encode(msg.buf, task);
	if (PSI_isoption(PSP_ODEBUG)){
	    PStask_sprintf(PSI_txt,task);
	    SYSLOG(8,(LOG_ERR,PSI_txt));
	}
	/*
	 * put the type of the msg in the head
	 * put the length of the whole msg to the head of the msg
	 * and return this value
	 */
	msg.header.type = PSP_CD_TASKINFO;
	msg.header.sender = PSI_gettid(PSI_myid,0);
	msg.header.dest = inmsg->sender;
	/* send the msg */
	success = MsgSend(&msg);
    }
    /*
     * send a EndOfList Sign
     */
    if(success>0){
	DDMsg_t msg;
	msg.len = sizeof(msg);
	msg.type = PSP_CD_TASKINFOEND;
	msg.sender = PSI_gettid(PSI_myid,0);
	msg.dest = inmsg->sender;
	MsgSend(&msg);
    }
}
/******************************************
*  send_PROCESS()
*   send a NEWPROCESS or DELETEPROCESS msg to all other daemons
*
*/
void send_PROCESS(long tid, long msgtype, PStask_t* oldtask)
{
    PStask_t* task;
    DDBufferMsg_t msg;

    if(oldtask)
	task = oldtask;
    else
	task = PStasklist_find(daemons[PSI_myid].tasklist,tid);
    if(task){
	/*
	 * broadcast the creation of a new task
	 */
	task->error = 0;
	task->nodeno = PSI_myid;

	msg.header.len = sizeof(msg.header);
	msg.header.len +=  PStask_encode(msg.buf, task);

	if (PSI_isoption(PSP_ODEBUG)){
	    sprintf(PSI_txt,"broadcast %sPROCESS(%lx[%d:%d]):\n",
		    msgtype== PSP_DD_DELETEPROCESS?"DELETE":"NEW",
		    tid,tid==-1?-1:PSI_getnode(tid),PSI_getpid(tid));
	    SYSLOG(2,(LOG_ERR,PSI_txt));
	    PStask_sprintf(PSI_txt,task);
	    SYSLOG(2,(LOG_ERR,PSI_txt));
	}
	/*
	 * put the type of the msg in the head
	 * put the length of the whole msg to the head of the msg
	 * and return this value
	 */
	msg.header.type = msgtype;
	msg.header.sender = tid;

	MsgBroadcast(&msg);
    }else{
	sprintf(PSI_txt,
		"send_PROCESS%s(%lx[%d:%d]): couldn't find task struct\n",
		msgtype== PSP_DD_DELETEPROCESS?"DELETE":"NEW",
		tid,tid==-1?-1:PSI_getnode(tid),PSI_getpid(tid));
	SYSLOG(2,(LOG_ERR,PSI_txt));
    }
}

/******************************************
 *  client_task_delete()
 *   remove the client task struct and clean up its ressources
 *
 */
void client_task_delete(long thisclienttid)
{
    PStask_t* oldtask;       /* the task struct to be deleted */
    int pid;                        /* temporary process id */

    if(PSI_isoption(PSP_ODEBUG))
	SYSLOG(3,(LOG_ERR,
		  "clientdelete():closing connection to T%lx[%d:%ld]\n",
		  thisclienttid,
		  thisclienttid==-1?-1:PSI_getnode(thisclienttid),
		  PSI_getpid(thisclienttid)));

    oldtask = PStasklist_dequeue(&daemons[PSI_myid].tasklist,thisclienttid);
    if(oldtask){
	send_PROCESS(oldtask->tid,PSP_DD_DELETEPROCESS,oldtask);
	/*
	 * send all task which want to receive a signal
	 * the signal they want to receive
	 */
	TaskDeleteSendSignals(oldtask);
	/*
	 * Check the parent task
	 */
	TaskDeleteSendSignalsToParent(oldtask->tid,oldtask->ptid);

	PStask_delete(oldtask);
    }else{
	SYSLOG(1,(LOG_ERR,"client_task_delete() PANIC:"
		  "task(T%lx[%d:%ld]) not in my tasklist,",
		  thisclienttid,
		  thisclienttid==-1?-1:PSI_getnode(thisclienttid),
		  PSI_getpid(thisclienttid)));
    }
    pid = PSI_getpid(thisclienttid);

    return;
}

/******************************************
 *  client_delete(int fd)
 */
void client_delete(int fd)
{
    if(fd<0)fd=-fd;

    if (PSI_isoption(PSP_ODEBUG)){
	sprintf(PSI_txt,"client_delete (%d)\n",fd);
	SYSLOG(4,(LOG_ERR,PSI_txt));
    }
    if(PSI_getnode(clients[fd].tid) != PSI_myid){
	int node = PSI_getnode(clients[fd].tid);
	if(node>=0 && node < PSI_nrofnodes){
	    /*
	     * It's another daemon.
	     */
	    DeclareDaemonDead(node);

	    CheckCluster();
	    kill_clients(0); /* killing all clients with group != TG_ADMIN */
	}
    }else{
	/*
	 * it's a task on my node
	 */
	long thisclienttid;

	thisclienttid = clients[fd].tid;
	CloseConnection(fd);
	if(thisclienttid==-1)
	    return;
	client_task_delete(thisclienttid);
    }
}

/******************************************
*  Do_NewReset()
*/
int Do_NewReset()
{
    sprintf(PSI_txt,"Do_NewReset() status %s\n",
	    daemons[PSI_myid].status & PSP_DSTATE_RESET_HW?"Hardware ":"");
    SYSLOG(9,(LOG_ERR,PSI_txt));
    /*
     * Check if there are clients
     * If there are clients, first kill them with phase 0
     *   and set a state to return back to DORESET
     *   When already returned, kill them with phase 2
     *   After that They are no more existent
     */
    if((daemons[PSI_myid].status & PSP_DSTATE_DORESET)==PSP_DSTATE_DORESET){
	if(kill_clients(3)==0)
	    return 0; /* kill client with error: try again. */
    }else{
	kill_clients(1);
	daemons[PSI_myid].status |= PSP_DSTATE_DORESET;
	usleep(200000); /* sleep for a while to let the clients react */
	return 0;
    }

    /*
     * reset the hardware if demanded
     *--------------------------------
     */
    if(daemons[PSI_myid].status & PSP_DSTATE_RESET_HW){
	if (PSI_isoption(PSP_ODEBUG)){
	    sprintf(PSI_txt,"Do_Reset() resetting the hardware\n");
	    SYSLOG(2,(LOG_ERR,PSI_txt));
	}
	PSID_ReConfig(PSI_myid, PSI_nrofnodes, ConfigLicensekey, ConfigModule,
		      ConfigRoutefile);
    }
    /*
     * change the state
     *----------------------------
     */
    daemons[PSI_myid].status = (daemons[PSI_myid].status
				& ~(PSP_DSTATE_RESET_WAIT
				    | PSP_DSTATE_RESET_INACTION
				    | PSP_DSTATE_RESET_HW));
    daemons[PSI_myid].status &= ~PSP_DSTATE_DORESET;

    sprintf(PSI_txt,"Do_NewReset() returns success\n");
    SYSLOG(9,(LOG_ERR,PSI_txt));
    return 1;
}

/******************************************
 *  msg_NewReset()
 */
void
msg_NewReset(DDResetMsg_t* msg)
{
    /*
     * First check if I have to reset myself
     * then clean up my local information for every reseted node
     */
    if(msg->first > PSI_myid || msg->last < PSI_myid)
	return;

    /*
     * reset and change the state of myself to new value
     */
    daemons[PSI_myid].status = (daemons[PSI_myid].status
				& ~(PSP_DSTATE_RESET_WAIT
				    | PSP_DSTATE_RESET_INACTION
				    | PSP_DSTATE_RESET_HW));
    if(msg->action & PSP_RESET_HW)
	daemons[PSI_myid].status = (daemons[PSI_myid].status
				      | PSP_DSTATE_RESET_HW);
    /*
     * Resetting my node
     */

    Do_NewReset();
}


void ParseArguments(char* buf,int size, PStask_t* task)
{
    int i;
    char* pbuf;
    int len;
    /* count arguments */
    task->argc = 0;
    len =0;
    for(;;){
	pbuf = &buf[len];
	if(strlen(pbuf)==0)
	    break;
	if((strlen(pbuf)+len) > size)
	    break;
	task->argc++;
	len +=strlen(pbuf)+1;
    }
    /* NOW: argc == no of arguments */
    if(task->argc==0)
	return;
    task->argv = (char**)malloc((task->argc)*sizeof(char*));
    len =0;
    for(i=0;i<task->argc;i++){
	pbuf = &buf[len];
	task->argv[i] = strdup(pbuf);
	len +=strlen(pbuf)+1;
    }
}

/******************************************
 *  GetProcessProperties()
 *   a client trys to connect to the daemon.
 *   accept the connection request if enough resources are available
 */
void GetProcessProperties(PStask_t* task)
{
#ifdef __osf__
    char buf[400];
    int len,i;
    if(table(TBL_ARGUMENTS,PSI_getpid(task->tid),buf,1,sizeof(buf))<0){
	SYSLOG(4,(LOG_ERR,"GetProcessProperties(%x[%d,%d])"
		  " couldn't get arguments",
		  task->tid,PSI_getnode(task->tid),PSI_getpid(task->tid)));
	return;
    }
    buf[sizeof(buf)-1]=0;
    ParseArguments(buf,sizeof(buf),task);

    SYSLOG(4,(LOG_ERR,"GetProcessProperties(%x[%d,%d]) arg[%d]=%s",
	      task->tid,PSI_getnode(task->tid),PSI_getpid(task->tid),
	      task->argc,buf));

#elif defined(__linux__)
    char filename[50];
    FILE* file;
    char buf[400];
    sprintf(filename, "/proc/%d/cmdline", PSI_getpid(task->tid));
    if((file=fopen(filename,"r"))!=NULL){
	int size;
	size = fread(buf,sizeof(buf),1,file);
	ParseArguments(buf,sizeof(buf),task);
	fclose(file);
    }
    sprintf(filename,"/proc/%d/status",PSI_getpid(task->tid));
    if((file=fopen(filename,"r"))!=NULL){
	char programname[50];
	char programstate[10];
	char statename[20];
	int programpid;
	int programppid;
	int programuid;

	fscanf(file,"Name:\t%s\n",programname);
	fscanf(file,"State:\t%s %s\n",programstate,statename);
	fscanf(file,"Pid:\t%d\n",&programpid);
	fscanf(file,"PPid:\t%d\n",&programppid);
	/*       task->ptid = PSI_gettid(-1,programppid);
		 error: task->ptid only meaningfull for paraSTation parents*/
	fscanf(file,"Uid:\t%d",&programuid);
	task->uid = programuid;

	fclose(file);
    }
    SYSLOG(4,(LOG_ERR, "GetProcessProperties(%lx[%d,%ld]) arg[%d]=%s",
	      task->tid,PSI_getnode(task->tid),PSI_getpid(task->tid),
	      task->argc,task->argv?(task->argv[0]?task->argv[0]:""):""));
#else
#error wrong architecture
#endif
}

/******************************************
 *  msg_CLIENTCONNECT()
 *   a client trys to connect to the daemon.
 *   accept the connection request if enough resources are available
 */
void msg_CLIENTCONNECT(int fd,DDInitMsg_t* msg)
{
    PStask_t* task;
    int NumberProcs;
    PStask_t* tmptask;

    clients[fd].tid = PSI_gettid(-1,msg->header.sender);

    /*    if(PSI_isoption(PSP_ODEBUG))*/
    SYSLOG(3,(LOG_ERR,"connection request from T%lx[%d:%ld] at fd%d, "
	      "group=%ld,version =%ld, uid=%d\n", clients[fd].tid,
	      clients[fd].tid==-1?-1:PSI_getnode(clients[fd].tid),
	      PSI_getpid(clients[fd].tid), fd, msg->group,msg->version,
	      msg->uid));
    /*
     * first check if it is a reconnection
     * this can happen due to a exec call.
     */
    task = PStasklist_find(daemons[PSI_myid].tasklist,clients[fd].tid);
    if(task==0){
	/*
	 * Task not found, maybe it's in spawned_tasks_waiting_for_connect
	 */
	task = PStasklist_dequeue(&spawned_tasks_waiting_for_connect,
				  clients[fd].tid);
	if(task){
	    task->link = NULL;
	    task->rlink = NULL;
	    PStasklist_enqueue(&daemons[PSI_myid].tasklist,task);
	}
    }
    if(task){
	/* reconnection */
	/* use the old task struct and close the old fd */
	/* use old PCB and close all sockets with FD_CLOEXEC flag set.*/
	if (PSI_isoption(PSP_ODEBUG)){
	    sprintf(PSI_txt,"CLIENTCONNECT reconnection to a PCB new "
		    "fd=%d old fd= %d\n",fd,task->fd);
	    PSI_logerror(PSI_txt);
	}
	/* close the previous socket */
	if(task->fd > 0){
	    CloseConnection(task->fd);
	}
	clients[fd].ob.task = task;
	task->fd = fd;
    }else{
	task = PStask_new();
	task->tid = clients[fd].tid;
	task->fd = fd;
	task->uid = msg->uid;
	task->nodeno = PSI_myid;
	task->group = msg->group;
	/* New task => It has to be the master */
	task->rank = 0;
	GetProcessProperties(task);
	PStask_sprintf(PSI_txt,task);
	SYSLOG(9,(LOG_ERR,"Connection request from: %s",PSI_txt));

	PStasklist_enqueue(&daemons[PSI_myid].tasklist,task);
	clients[fd].ob.task = task;
    }
    /*
     * Calculate the number of processes
     */
    for(NumberProcs=0,tmptask=daemons[PSI_myid].tasklist;
	(tmptask!=NULL) && (NumberProcs<1000); /* just to prepend an endless loop*/
	tmptask=tmptask->link,NumberProcs++);

    if((NoOfConnectedDaemons<PSI_nrofnodes) && (msg->group != TG_ADMIN)){
	StartOtherDaemons();
    }
    /*
     * Reject or accept connection
     */
    if (((msg->group ==TG_RESET)
	 ||(msg->group ==TG_RESETABORT)
	 ||((daemons[PSI_myid].status & PSP_DSTATE_NOCONNECT)!=0)
	 ||(task==0)
	 ||(msg->version != PSPprotocolversion)
	 ||((msg->uid!=0) && (UIDLimit !=-1) && (msg->uid != UIDLimit))
	 ||((msg->uid!=0) && (MAXPROCLimit !=-1)
	    && (NumberProcs > MAXPROCLimit)))){
	DDInitMsg_t outmsg;
	outmsg.header.len = sizeof(outmsg);
	/* Connection refused answer message */
	if(msg->version != PSPprotocolversion)
	    outmsg.header.type = PSP_CD_OLDVERSION;
	else if(task==0)
	    outmsg.header.type = PSP_CD_NOSPACE;
	else if((UIDLimit !=-1) && (msg->uid != UIDLimit))
	    outmsg.header.type = PSP_CD_UIDLIMIT;
	else if((MAXPROCLimit !=-1) && (NumberProcs > MAXPROCLimit))
	    outmsg.header.type = PSP_CD_PROCLIMIT;
	else if((daemons[PSI_myid].status & PSP_DSTATE_NOCONNECT)!=0){
	    sprintf(PSI_txt,"CLIENTCONNECT daemon state problems: mystate %lx",
		    daemons[PSI_myid].status);
	    PSI_logerror(PSI_txt);
	    outmsg.header.type = PSP_DD_STATENOCONNECT;
	}else
	    outmsg.header.type = PSP_CD_CLIENTREFUSED;
	outmsg.header.dest = clients[fd].tid;
	outmsg.header.sender = PSI_gettid(PSI_myid,0);
	outmsg.version = PSPprotocolversion;
	outmsg.group = msg->group;

	if((UIDLimit !=-1) && (msg->uid != UIDLimit))
	    outmsg.reason = UIDLimit;
	else if((MAXPROCLimit !=-1) && (NumberProcs > MAXPROCLimit))
	    outmsg.reason = MAXPROCLimit;
	else
	    outmsg.reason = 0;

	if (PSI_isoption(PSP_ODEBUG)){
	    sprintf(PSI_txt,"CLIENTCONNECT connection refused:"
		    "group %ld task %lx version %ld %d "
		    "uid %d %d Procs %d %d\n",
		    msg->group, (long)task, msg->version, PSPprotocolversion,
		    msg->uid, UIDLimit, NumberProcs, MAXPROCLimit);
	    PSI_logerror(PSI_txt);
	}
	MsgSend(&outmsg);

	/* clean up */
	client_delete(fd);

	if((msg->group == TG_RESET) && (msg->uid == 0)){
	    daemons[PSI_myid].status = (daemons[PSI_myid].status
					  & ~(PSP_DSTATE_RESET_WAIT
					      | PSP_DSTATE_RESET_INACTION
					      | PSP_DSTATE_RESET_HW));
	    Do_NewReset();
	}
    }else{
	DDInitMsg_t outmsg;
	clients[fd].flags &= ~INITIALCONTACT;

	outmsg.header.type = PSP_CD_CLIENTESTABLISHED;
	outmsg.header.dest = clients[fd].tid;
	outmsg.header.sender = PSI_gettid(PSI_myid,0);
	outmsg.header.len = sizeof(outmsg);
	outmsg.version = PSPprotocolversion;
	outmsg.nrofnodes = PSI_nrofnodes;
	outmsg.myid = PSI_myid;
	outmsg.masternode = task->masternode;
	outmsg.masterport = task->masterport;
	outmsg.rank = task->rank;
	outmsg.group = msg->group;
	if(MsgSend(&outmsg)>0)
	    send_PROCESS(clients[fd].tid,PSP_DD_NEWPROCESS,NULL);
    }
}

/******************************************
 *  msg_DAEMONSTOP()
 *   sender node requested a psid-stop on the receiver node.
 */
void msg_DAEMONSTOP(DDResetMsg_t* msg)
{
    /*
     * First check if I have to reset myself
     * then clean up my local information for every reseted node
     */
    if(msg->first > PSI_myid || msg->last < PSI_myid)
	return;

    shutdown_node(1);
}

/******************************************
 *  send_OPTIONS()
 * Transfers the options to an other node
 */
int send_OPTIONS(int destnode)
{
    DDOptionMsg_t msg;
    int success=1;
    msg.header.type = PSP_DD_SETOPTION;
    msg.header.sender = PSI_gettid(PSI_myid,0);
    msg.header.dest = PSI_gettid(destnode,0);
    msg.header.len = sizeof(msg);

    msg.count = 1;
    msg.opt[0].option = PSP_OP_PROCLIMIT;
    msg.opt[0].value = MAXPROCLimit;
    success = MsgSend(&msg);

    msg.opt[0].option = PSP_OP_UIDLIMIT;
    msg.opt[0].value = UIDLimit;

    if(success>0)
	if((success=MsgSend(&msg))<0)
	    SYSLOG(2,(LOG_ERR,"sending MAXPROCLimit/UIDLimits errno %d\n",
		      errno));
    return success;
}

/******************************************
 *  msg_DAEMONCONNECT()
 */
void msg_DAEMONCONNECT(int fd,int number)
{
    DDMsg_t msg;
    int success;

    if (PSI_isoption(PSP_ODEBUG)){
	sprintf(PSI_txt,"msg_DAEMONCONNECT (%d,%d) daemons[%d].fd=%d\n",
		fd,number,number,daemons[number].fd);
	PSI_logerror(PSI_txt);
    }
    /*
     * with RDP all Daemons are sending messages through one socket
     * so no difficult initialization is needed
     */

    SYSLOG(2,(LOG_ERR,"New connection to daemon on node %d (fd:%d)\n",
	      number, fd));

    /*
     * accept this request and send an ESTABLISH msg back to the requester
     */

    InitDaemon(fd,number);

    msg.type = PSP_DD_DAEMONESTABLISHED;
    msg.sender = PSI_gettid(PSI_myid,0);
    msg.dest = PSI_gettid(number,0);
    msg.len = sizeof(msg);
    if((success = MsgSend(&msg))<=0)
	SYSLOG(2,(LOG_ERR,"sending Daemonestablished errno %d\n",errno));

    if(success>0)
	success = send_OPTIONS(number);

    /*
     * checking if the whole cluster is up
     */
    CheckCluster();
}

/******************************************
 *  msg_SPAWNREQUEST()
 */
/*void msg_SPAWNREQUEST(int fd,int msglen)*/
void msg_SPAWNREQUEST(DDBufferMsg_t* msg)
{
    PStask_t *task;
    int err=0;

    char buffer[8096];

    task = PStask_new();

    PStask_decode(msg->buf,task);

    PStask_sprintf(buffer,task);
    SYSLOG(5,(LOG_ERR,"request from %x msglen %ld task %s",
	      msg->header.sender, msg->header.len, buffer));

    if (task->nodeno == PSI_myid){
	/*
	 * this is a request for my node
	 */
	/*
	 * first check if resource for this task is available
	 * and if ok try to start the task
	 */
	err= PSID_taskspawn(task);
	if (PSI_isoption(PSP_ODEBUG)){
	    if(err==0)
		sprintf(PSI_txt,
			"execspawn returned with no error(childpid=%lx)\n",
			task->tid);
	    else
		sprintf(PSI_txt,"execspawn returned with error no %d\n",err);
	    PSI_logerror(PSI_txt);
	}
	msg->header.type = err==0? PSP_DD_SPAWNSUCCESS: PSP_DD_SPAWNFAILED;;

	if(err==0){
	    PStasklist_enqueue(&spawned_tasks_waiting_for_connect,task);
	}
	/*
	 * send the existence or failure of the request
	 */
	task->error = err;
	task->nodeno = PSI_myid;

	msg->header.len =  PStask_encode(msg->buf, task);
	msg->header.len += sizeof(msg->header);

	msg->header.dest = msg->header.sender;
	msg->header.sender = PSI_gettid(PSI_myid,0);

	MsgSend(msg);
	if(err!=0)
	    PStask_delete(task);
    }else{
	/*
	 * this is a request for a remote site.
	 */
	if (DaemonIsUp(task->nodeno)){
	    /* the daemon of the requested node is connected to me */
	    if (PSI_isoption(PSP_ODEBUG)){
		sprintf(PSI_txt, "sending spawnrequest to node %d\n",
			task->nodeno);
		PSI_logerror(PSI_txt);
	    }
	    msg->header.dest = PSI_gettid(task->nodeno,0);
	    MsgSend(msg);
	}else{
	    /*
	     * The address is wrong
	     * or
	     * The daemon is actual not connected
	     * It's not possible to spawn
	     */
	    if ((task->nodeno >= PSI_nrofnodes)){
		task->error = EHOSTUNREACH;
		sprintf(PSI_txt,"node %d does not exist\n",task->nodeno);
	    }else{
		task->error = EHOSTDOWN;
		sprintf(PSI_txt,"node %d is down\n", task->nodeno);
	    }

	    PSI_logerror(PSI_txt);

	    msg->header.len =  PStask_encode(msg->buf, task);
	    msg->header.len += sizeof(msg->header);
	    msg->header.type = PSP_DD_SPAWNFAILED;
	    msg->header.dest = msg->header.sender;
	    msg->header.sender = PSI_gettid(PSI_myid,0);
	    PStask_encode(msg->buf,task);

	    MsgSend(msg);
	}
	PStask_delete(task);
    }
}

/******************************************
 *     msg_TASKLISTINFO();
 *
 * receives information about a task and enqueues the this task in the
 * the list of the daemon it is residing on
 */
void msg_TASKLISTINFO(DDBufferMsg_t* msg)
{
    PStask_t* task=0;
    PStask_t* task2=0;

    task = PStask_new();

    PStask_decode(msg->buf,task);

    if (PSI_isoption(PSP_ODEBUG)){
	sprintf(PSI_txt,"TASKLISTINFO(%lx) with parent(%lx) on node %d \n",
		task->tid,task->ptid,PSI_myid);
	PSI_logerror(PSI_txt);
    }
    /*
     * if already in the PStask_queu-> remove it
     */
    if((task2 = PStasklist_dequeue(&daemons[task->nodeno].tasklist,
				   task->tid))!=0){
	/* found and already dequeued.
	 * delete it */
	PStask_delete(task2);
    }
    PStasklist_enqueue(&daemons[task->nodeno].tasklist,task);
}

/******************************************
 *      msg_TASKINFOREQUEST(fd,tid);
 *
 * find the information about the specified task and send this information
 * to the fd
 */
void msg_TASKINFOREQUEST(DDMsg_t* inmsg)
{
    PStask_t* task=0;
    int node = PSI_getnode(inmsg->dest);

    task = PStasklist_find(daemons[node].tasklist,inmsg->dest);
    if(task){
	DDBufferMsg_t outmsg;

	outmsg.header.len =  PStask_encode(outmsg.buf, task);
	outmsg.header.len += sizeof(outmsg.header);
	outmsg.header.type = PSP_CD_TASKINFO;
	outmsg.header.sender = PSI_gettid(PSI_myid,0);
	outmsg.header.dest = inmsg->sender;

	if (PSI_isoption(PSP_ODEBUG)){
	    PStask_sprintf(PSI_txt,task);
	    PSI_logerror(PSI_txt);
	}
	MsgSend(&outmsg);
    }
    /*
     * send a EndOfList Sign
     */
    inmsg->type = PSP_CD_TASKINFOEND;
    inmsg->dest = inmsg->sender;
    inmsg->sender = PSI_gettid(PSI_myid,0);
    inmsg->len = sizeof(*inmsg);
    MsgSend(inmsg);
}

/******************************************
 *  msg_NEWPROCESS()
 */
void msg_NEWPROCESS(DDBufferMsg_t* msg)
{
    PStask_t* task;
    PStask_t* oldtask;
    task = PStask_new();

    PStask_decode(msg->buf,task);

    if (PSI_isoption(PSP_ODEBUG)){
	sprintf(PSI_txt,
		"PSPNEWPROCESS (%lx: %s) with parent(%lx) on node %d \n",
		task->tid,
		task->argv==0?"(NULL)":task->argv[0]?task->argv[0]:"(NULL)",
		task->ptid,task->nodeno);
	PSI_logerror(PSI_txt);
    }

    if((oldtask
	= PStasklist_dequeue(&daemons[task->nodeno].tasklist,task->tid))!=0){
	long sigtid;
	int sig=-1;
	sprintf(PSI_txt,
		"PSPNEWPROCESS (%lx) old taskstruct found!! old %lx new %lx\n",
		task->tid,(long)oldtask,(long)task);
	PSI_logerror(PSI_txt);
	while((sigtid = PStask_getsignalreceiver(oldtask,&sig))!=0){
	    sprintf(PSI_txt,"PSPNEWPROCESS (%lx) setting PSsignalereceiver"
		    " tid %lx sig %d\n",
		    task->tid,sigtid,sig);
	    PSI_logerror(PSI_txt);
	    PStask_setsignalreceiver(task,sigtid,sig);
	    sig=-1;
	}
	PStask_delete(oldtask);
    }
    PStasklist_enqueue(&daemons[task->nodeno].tasklist,task);
}

/*----------------------------------------------------------------------------
 * void TaskDeleteSendSignals(PStask_t* oldtask)
 *
 * Send the signals to all task which have asked for
 */
void TaskDeleteSendSignals(PStask_t* oldtask)
{
    PStask_t* receivertask;
    int sig;
    long sigtid;
    sig=-1;
    while((sigtid = PStask_getsignalreceiver(oldtask,&sig))!=0){
	/*
	 * if the receiver is a real task
	 * and he is connected to me
	 * => send him a signal
	 */
	int pid = PSI_getpid(sigtid);
	if((pid) && ((receivertask
		      = PStasklist_find(daemons[PSI_myid].tasklist,sigtid))
		     != NULL)){
	    kill(pid,sig);
	    PStask_setsignalsender(receivertask,oldtask->tid,sig);
	    sprintf(PSI_txt,"TaskDeleteSendSignals() sent signal %d to"
		    " tid 0x%lx (pid%d) \n", sig, sigtid, pid);
	}else
	    sprintf(PSI_txt,"TaskDeleteSendSignals() wanted to send"
		    " signal %d to tid 0x%lx (pid%d) but tid not found \n",
		    sig, sigtid, pid);
	SYSLOG(4,(LOG_ERR,PSI_txt));
	sig=-1;
    }
}

/*----------------------------------------------------------------------------
 * void TaskDeleteSendSignalsToParent(long tid, long ptid)
 *
 * Send the signals to the parent if it has asked for
 */
void TaskDeleteSendSignalsToParent(long tid,long ptid)
{
    PStask_t* receivertask;
    if((ptid !=-1) && (PSI_getnode(ptid)==PSI_myid)){
	receivertask = PStasklist_find(daemons[PSI_myid].tasklist,ptid);
	if(receivertask){
	    int pid;
	    pid = PSI_getpid(ptid);
	    sprintf(PSI_txt,"TaskDeleteSendSignalsToParent(ptid = 0x%lx(pid=%d)"
		    " tid= 0x%lx ) signal %d \n",
		    ptid,pid,tid,receivertask->childsignal);
	    SYSLOG(4,(LOG_ERR,PSI_txt));
	    if((receivertask->childsignal)&&(pid>0)){
		kill(pid,receivertask->childsignal);
		PStask_setsignalsender(receivertask,tid,
				       receivertask->childsignal);
	    }
	}
    }
}

/******************************************
 *  msg_DELETEPROCESS()
 */
void msg_DELETEPROCESS(DDBufferMsg_t* msg)
{
    PStask_t* task;
    PStask_t* oldtask;

    task = PStask_new();

    PStask_decode(msg->buf,task);

    if (PSI_isoption(PSP_ODEBUG)){
	sprintf(PSI_txt,
		"msg_PSPDELETEPROCESS (%lx %s) with parent(%lx) on node %d \n",
		task->tid,
		task->argv==0?"(NULL)":task->argv[0]?task->argv[0]:"(NULL)",
		task->ptid,PSI_myid);
	PSI_logerror(PSI_txt);
    }
    if((oldtask
	= PStasklist_dequeue(&daemons[task->nodeno].tasklist,task->tid))!=0){
	TaskDeleteSendSignals(oldtask);
	PStask_delete(oldtask);
    }else if (PSI_isoption(PSP_ODEBUG)){
	sprintf(PSI_txt,"PSPDELETEPROCESS (%lx) couldn't find entry \n",
		task->tid);
	PSI_logerror(PSI_txt);
    }
    /*
     * Check the parent task
     */

    TaskDeleteSendSignalsToParent(task->tid,task->ptid);

    PStask_delete(task);
}
/******************************************
 *  msg_CHILDDEAD()
 */
void msg_CHILDDEAD(DDMsg_t* msg)
{
    PStask_t* task;
    int node;

    /*
     * Check the parent task
     */
    TaskDeleteSendSignalsToParent(msg->sender,msg->dest);

    /*
     * Check if we have a task stored due to a SPAWNSUCCESS
     */
    node = PSI_getnode(msg->sender);

    if((node>=0)&&(node<PSI_nrofnodes) &&
       ((task
	 = PStasklist_dequeue(&daemons[node].tasklist,msg->sender))!=NULL)){
	PStask_delete(task);
    }
}

/******************************************
 *  msg_SPAWNSUCCESS()
 */
void
msg_SPAWNSUCCESS(DDBufferMsg_t *msg)
{
    PStask_t* task;
    PStask_t* oldtask;
    task = PStask_new();

    PStask_decode(msg->buf,task);

    if (PSI_isoption(PSP_ODEBUG)){
	sprintf(PSI_txt,"PSPSPAWNSUCCESS (%lx) with parent(%lx) on node %d \n",
		task->tid,task->ptid,PSI_myid);
	PSI_logerror(PSI_txt);
    }

    if ((task->ptid !=0)
	&&(task->ptid !=-1)
	&&(PSI_getnode(task->ptid) == PSI_myid)){
	/*
	 * the spawn request was sent by a process on my node
	 */
	if (PSI_isoption(PSP_ODEBUG)){
	    sprintf(PSI_txt,
		    "PSPSPAWNSUCCESS sending msg to parent(%lx) on my node\n",
		    task->ptid);
	    PSI_logerror(PSI_txt);
	}
	/*
	 * send the initiator a success msg
	 */
	MsgSend(msg);

	if((oldtask
	    = PStasklist_dequeue(&daemons[task->nodeno].tasklist,task->tid))
	   !=0){
	    if(oldtask->tid == task->tid){
		long sigtid;
		int sig;
		while((sigtid = PStask_getsignalreceiver(oldtask,&sig))!=0){
		    PStask_setsignalreceiver(task,sigtid,sig);
		}
	    }
	    PStask_delete(oldtask);
	}
	/* here there could be a PStask_setsignalreceiver(task,parenttid),
	   but this is not necessary since the parent has to ability to
	   register himself before it does spawning. So parents have to
	   use this feature.
	*/
	PStasklist_enqueue(&daemons[task->nodeno].tasklist,task);
    }else
	PStask_delete(task);
}

/******************************************
 *  msg_SPAWNFAILED()
 */
void
msg_SPAWNFAILED(DDBufferMsg_t *msg)
{
    PStask_t* task;

    task = PStask_new();

    PStask_decode(msg->buf,task);

    if (PSI_isoption(PSP_ODEBUG)){
	sprintf(PSI_txt,"PSPSPAWNFAILED error = %ld sending msg to "
		"parent(%lx) on my node\n",task->error,task->ptid);
	PSI_logerror(PSI_txt);
    }

    /*
     * send the initiator a failure msg
     */
    MsgSend(msg);

    PStask_delete(task);
}

/******************************************
 *  msg_TASKKILL()
 */
void
msg_TASKKILL(DDSignalMsg_t* msg)
{
    char* errtxt;

    if((msg->header.dest !=-1)&&(PSI_getnode(msg->header.dest)==PSI_myid)){
	PStask_t* receivertask;
	/* the process to kill is on my own node */
	int pid;

	pid = PSI_getpid(msg->header.dest);

	if (PSI_isoption(PSP_ODEBUG)){
	    sprintf(PSI_txt,"got taskkill for my node; "
		    "process %d ,sender %lx,recv %lx, uid %d\n",
		    pid,msg->header.sender,msg->header.dest,msg->senderuid);
	    PSI_logerror(PSI_txt);
	}
	/*
	 * fork to a new process to change the userid
	 * and get the right errors
	 */
	if((receivertask
	    = PStasklist_find(daemons[PSI_myid].tasklist,msg->header.dest)))
	    /* it's one of my processes */
	    if(fork()==0){
		/*
		 * I'm the killing process
		 * my father is just returning
		 */
		int error;

		/*
		 * change the user id to the appropriate user
		 */
		if(setuid(msg->senderuid)<0){
		    errtxt=strerror(errno);
		    SYSLOG(1,(LOG_ERR,"msg_TASKKILL(setuid %d): [%d] %s",
			      msg->senderuid,errno,
			      errtxt?errtxt:"UNKNOWN errno"));
		    exit(0);
		}
		error = kill(pid,msg->signal);
		if(error){
		    errtxt=strerror(errno);
		    SYSLOG(1,(LOG_ERR,"msg_TASKKILL(kill %d (%x): [%d] %s",
			      pid,msg->header.dest,errno,
			      errtxt?errtxt:"UNKNOWN errno"));
		}else{
		    SYSLOG(1,(LOG_ERR,"msg_TASKKILL(kill %d (%x)): SUCESS",
			      pid,msg->header.dest));
		    PStask_setsignalsender(receivertask,msg->header.sender,
					   msg->signal);
		}
		exit(0);
	    }
    }else if(msg->header.dest!=-1){
	/*
	 * this is a request for a remote site.
	 * find the right fd to send to request
	 */
	if (PSI_isoption(PSP_ODEBUG)){
	    sprintf(PSI_txt,"sending taskkill to node %d\n",
		    PSI_getnode(msg->header.dest));
	    PSI_logerror(PSI_txt);
	}
	MsgSend(msg);
    }
}

/******************************************
 *  msg_INFOREQUEST()
 */
void msg_INFOREQUEST(DDMsg_t *inmsg)
{
    int nodeno;
    nodeno = PSI_getnode(inmsg->dest);
    if (PSI_isoption(PSP_ODEBUG)){
	sprintf(PSI_txt,"INFOREQUEST from node %d for requester %lx[%d,%d]\n",
		nodeno,inmsg->sender,
		inmsg->sender==-1?-1:PSI_getnode(inmsg->sender),
		PSI_getpid(inmsg->sender));
	PSI_logerror(PSI_txt);
    }
    if(nodeno!=PSI_myid){
	/* a request for a remote daemon */
	if(DaemonIsUp(nodeno)){
	    /*
	     * transfer the request to the remote daemon
	     */
	    if(MsgSend(inmsg)<=0){
		/* system error */
		DDErrorMsg_t errmsg;
		errmsg.header.len = sizeof(errmsg);
		errmsg.request = inmsg->type;
		errmsg.err = EHOSTDOWN;
		errmsg.header.type = PSP_DD_SYSTEMERROR;
		errmsg.header.dest = inmsg->sender;
		errmsg.header.sender = PSI_gettid(PSI_myid,0);
		MsgSend(&errmsg);
	    }
	}else{
	    /* node ist unreachable */
	    DDErrorMsg_t errmsg;
	    errmsg.header.len = sizeof(errmsg);
	    errmsg.request = inmsg->type;
	    errmsg.err = EHOSTUNREACH;
	    errmsg.header.type = PSP_DD_SYSTEMERROR;
	    errmsg.header.dest = inmsg->sender;
	    errmsg.header.sender = PSI_gettid(PSI_myid,0);
	    MsgSend(&errmsg);
	}
    }else{
	/* a request for my own Information*/
	DDBufferMsg_t msg;
	int err=0;

	msg.header.sender = PSI_gettid(PSI_myid,0);
	msg.header.dest = inmsg->sender;
	msg.header.len = sizeof(msg.header);

	switch(inmsg->type){
	case PSP_CD_COUNTSTATUSREQUEST:
	{
	    PSHALInfoCounter_t *ic;

	    ic=PSHALSYSGetInfoCounter();

	    bcopy(ic,msg.buf,sizeof(*ic));
	    msg.header.type = PSP_CD_COUNTSTATUSRESPONSE;
	    msg.header.len += sizeof(*ic);
	    break;
	}
	case PSP_CD_RDPSTATUSREQUEST:
	{
	    DDTagedBufferMsg_t* tagedmsg;
	    int nodeid;
	    nodeid = ((DDTagedBufferMsg_t*)inmsg)->tag[0];
	    tagedmsg = (DDTagedBufferMsg_t*)&msg;
	    RDP_StateInfo(nodeid,tagedmsg->buf);
	    tagedmsg->tag[0] = nodeid;
	    msg.header.type = PSP_CD_RDPSTATUSRESPONSE;
	    msg.header.len = sizeof(msg);
	    break;
	}
	case PSP_CD_HOSTSTATUSREQUEST:
	    bcopy(PSID_hoststatus, msg.buf, NrOfNodes);
	    msg.header.len += NrOfNodes;
	    msg.header.type = PSP_CD_HOSTSTATUSRESPONSE;
	    break;
	case PSP_CD_HOSTREQUEST:
	{
	    unsigned int *address;
	    int node;
	    address = (unsigned int *) ((DDBufferMsg_t*)inmsg)->buf;
	    node = PSID_host(*address);
	    bcopy(&node, msg.buf, sizeof(int));
	    msg.header.len += sizeof(int);
	    msg.header.type = PSP_CD_HOSTRESPONSE;
	    break;
	}
	default:
	    err = -1;
	}
	if(err == 0)
	    MsgSend(&msg);
    }
}

/******************************************
 *  msg_SETOPTION()
 */
void
msg_SETOPTION(DDOptionMsg_t *msg)
{
    int i;
    for(i=0;i<msg->count;i++){
	if (PSI_isoption(PSP_ODEBUG)){
	    sprintf(PSI_txt,"SETOPTION()option: %ld value 0x%lx \n",
		    msg->opt[i].option,msg->opt[i].value);
	    SYSLOG(3,(LOG_ERR,PSI_txt));
	}
	switch(msg->opt[i].option){
	case PSP_OP_SMALLPACKETSIZE:
	    if(PSHALSYS_GetSmallPacketSize()!=msg->opt[i].value){
		PSHALSYS_SetSmallPacketSize(msg->opt[i].value);
		ConfigSmallPacketSize = msg->opt[i].value;
	    }
	    break;
	case PSP_OP_RESENDTIMEOUT:
	    if(PSHALSYS_GetResendTimeout()!= msg->opt[i].value){
		PSHALSYS_SetResendTimeout(msg->opt[i].value);
		ConfigResendTimeout = msg->opt[i].value;
	    }
	    break;
	case PSP_OP_PROCLIMIT:
	    MAXPROCLimit = msg->opt[i].value;
	    break;
	case PSP_OP_UIDLIMIT:
	    UIDLimit = msg->opt[i].value;
	    break;
	case PSP_OP_PSIDDEBUG:
	    if((msg->header.dest == PSI_gettid(PSI_myid,0)) /* for me */
	       || (msg->header.dest == -1))                    /* for any */
		PSI_setoption(PSP_ODEBUG,msg->opt[i].value);
	    break;
	case PSP_OP_RDPDEBUG:
	    if((msg->header.dest == PSI_gettid(PSI_myid,0)) /* for me */
	       || (msg->header.dest == -1))                    /* for any */
		RDP_SetDBGLevel(msg->opt[i].value);
	    break;
	default:
	    sprintf(PSI_txt,"SETOPTION()option: unknown option %ld \n",
		    msg->opt[i].option);
	    PSI_logerror(PSI_txt);
	    SYSLOG(1,(LOG_ERR,PSI_txt));
	}
    }
    /* Message is for a remote node */
    if((msg->header.dest != PSI_gettid(PSI_myid,0))
       && (msg->header.dest !=-1))
	MsgSend(msg);
    /* Message is for any node so do a broadcast */
    if(msg->header.dest ==-1)
	MsgBroadcast(msg);
}

/******************************************
 *  msg_GETOPTION()
 */
void
msg_GETOPTION(DDOptionMsg_t* msg)
{
    int i;
    for(i=0;i<msg->count;i++){
	if (PSI_isoption(PSP_ODEBUG)){
	    sprintf(PSI_txt,"GETOPTION() sender %lx option: %ld \n",
		    msg->header.sender, msg->opt[i].option);
	    SYSLOG(3,(LOG_ERR,PSI_txt));
	}
	switch(msg->opt[i].option){
	case PSP_OP_SMALLPACKETSIZE:
	    msg->opt[i].value = PSHALSYS_GetSmallPacketSize();
	    break;
	case PSP_OP_RESENDTIMEOUT:
	    msg->opt[i].value = PSHALSYS_GetResendTimeout();
	    break;
	case PSP_OP_PROCLIMIT:
	    msg->opt[i].value = MAXPROCLimit;
	    break;
	case PSP_OP_UIDLIMIT:
	    msg->opt[i].value = UIDLimit;
	    break;
	case PSP_OP_RDPDEBUG:
	    msg->opt[i].value = RDP_SetDBGLevel(-1);
	    break;
	default:
	    sprintf(PSI_txt,"GETOPTION(): unknown option %ld \n",
		    msg->opt[i].option);
	    PSI_logerror(PSI_txt);
	    SYSLOG(1,(LOG_ERR,PSI_txt));
	    return;
	}
    }
    /*
     * prepare the message to route it to the receiver
     */
    msg->header.len = sizeof(*msg);
    msg->header.type = PSP_DD_SETOPTION;
    msg->header.dest = msg->header.sender;
    msg->header.sender = PSI_gettid(PSI_myid,0);
    MsgSend(msg);
}

/******************************************
 *  msg_NOTIFYDEAD()
 */
void
msg_NOTIFYDEAD(DDSignalMsg_t *msg)
{
    int node;
    PStask_t* task;

    if (PSI_isoption(PSP_ODEBUG)){
	sprintf(PSI_txt,"msg_NOTIFYDEAD(sender = 0x%lx tid= 0x%lx sig %d)\n",
		msg->header.sender, msg->header.dest, msg->signal);
	PSI_logerror(PSI_txt);
    }

    if(msg->header.dest==0){
	task = PStasklist_find(daemons[PSI_myid].tasklist,
			       msg->header.sender);
	if(task){
	    task->childsignal = msg->signal;
	    msg->signal = 0;     /* sucess */
	}else
	    msg->signal = ESRCH; /* failure */

	msg->header.type = PSP_DD_NOTIFYDEADRES;
	msg->header.dest = msg->header.sender;
	msg->header.sender = PSI_gettid(PSI_myid,0);
	msg->header.len = sizeof(*msg);
	MsgSend(msg);
    }else{
	node = PSI_getnode(msg->header.dest);

	if((0>node)||(node>=PSI_nrofnodes)){
	    msg->header.type = PSP_DD_NOTIFYDEADRES;
	    msg->header.dest = msg->header.sender;
	    msg->header.sender = PSI_gettid(PSI_myid,0);
	    msg->signal = EHOSTUNREACH; /* failure */
	    msg->header.len = sizeof(*msg);
	    MsgSend(msg);
	    return;
	}

	task = PStasklist_find(daemons[node].tasklist,msg->header.dest);

	if(task){
	    sprintf(PSI_txt,"msg_NOTIFYDEAD() "
		    "setsignalreceiver (0x%lx,0x%lx,%d)\n",
		    msg->header.dest, msg->header.sender, msg->signal);
	    PSI_logerror(PSI_txt);
	    PStask_setsignalreceiver(task, msg->header.sender, msg->signal);

	    msg->signal = 0; /* sucess */
	    msg->header.type = PSP_DD_NOTIFYDEADRES;
	    msg->header.dest = msg->header.sender;
	    msg->header.sender = PSI_gettid(PSI_myid,0);
	    msg->header.len = sizeof(*msg);
	    MsgSend(msg);
	}else{
	    sprintf(PSI_txt,"msg_NOTIFYDEAD(sender= 0x%lx tid 0x%lx sig %d) "
		    "FATAL error: no task!!\n",
		    msg->header.sender, msg->header.dest, msg->signal);
	    PSI_logerror(PSI_txt);
	    msg->signal = ESRCH; /* failure */

	    msg->header.type = PSP_DD_NOTIFYDEADRES;
	    msg->header.dest = msg->header.sender;
	    msg->header.sender = PSI_gettid(PSI_myid,0);
	    msg->header.len = sizeof(*msg);
	    MsgSend(msg);
	}
    }
}

/**********************************************************
 *  msg_LOADREQ()
 */
void msg_LOADREQ(DDMsg_t* inmsg)
{
    int nodenr;
    if (PSI_isoption(PSP_ODEBUG)){
	sprintf(PSI_txt,"LOADREQ(from = 0x%lx node %ld\n",
		inmsg->sender, inmsg->dest);
	PSI_logerror(PSI_txt);
    }

    nodenr = PSI_getnode(inmsg->dest);
    if((nodenr<0) || (nodenr>=PSI_nrofnodes)
       || (PSID_hoststatus[nodenr] & PSPHOSTUP) == 0){
	DDErrorMsg_t errmsg;
	errmsg.header.len = sizeof(errmsg);
	errmsg.request = inmsg->type;
	errmsg.err = EHOSTDOWN;
	errmsg.header.len = sizeof(errmsg);
	errmsg.header.type = PSP_DD_SYSTEMERROR;
	errmsg.header.dest = inmsg->sender;
	errmsg.header.sender = PSI_gettid(PSI_myid,0);
	MsgSend(&errmsg);
    }else{
	DDLoadMsg_t msg;
	msg.header.len = sizeof(msg);
	msg.header.type = PSP_CD_LOADRES;
	msg.header.dest = inmsg->sender;
	msg.header.sender = PSI_gettid(PSI_myid,0);
	msg.load[0] = daemons[nodenr].load.load[0];
	msg.load[1] = daemons[nodenr].load.load[1];
	msg.load[2] = daemons[nodenr].load.load[2];
	MsgSend(&msg);
    }
}

/******************************************
*  msg_WHODIED()
*/
void
msg_WHODIED(DDSignalMsg_t* msg)
{
    PStask_t* task;

    if (PSI_isoption(PSP_ODEBUG)){
	sprintf(PSI_txt,"WHODIED(who = %lx sig %d \n",
		msg->header.sender, msg->signal);
	PSI_logerror(PSI_txt);
    }

    task = PStasklist_find(daemons[PSI_myid].tasklist, msg->header.sender);
    if(task){
	long tid;
	tid =  PStask_getsignalsender(task,&msg->signal);

	if (PSI_isoption(PSP_ODEBUG)){
	    sprintf(PSI_txt,"WHODIED( )tid= 0x%lx(pid %d) sig = %d) \n",
		    tid, PSI_getpid(tid), msg->signal);
	    PSI_logerror(PSI_txt);
	}
	msg->header.dest = msg->header.sender;
	msg->header.sender = tid;
	msg->header.len = sizeof(*msg);
	MsgSend(msg);
    }else{
	msg->header.dest = msg->header.sender;
	msg->header.sender = -1;
	msg->header.len = sizeof(*msg);
	MsgSend(msg);
    }
}

/******************************************
 * request_options()
 * request the options of the remote node
 */
int request_options(int node)
{
  int success=0;
  DDOptionMsg_t optmsg;
  DDMsg_t msg;

  msg.dest = PSI_gettid(node,0);;
  msg.sender = PSI_gettid(PSI_myid,0);
  msg.len = sizeof(msg);
  /*
   * request the remote tasklist
   */
  msg.type = PSP_CD_TASKLISTREQUEST;
  if((success = MsgSend(&msg))<=0)
      SYSLOG(1,(LOG_ERR,"msg_DAEMONESTABLISHED() "
		" PSP_DD_DAEMONESTABLISHED requesting "
		"remote tasklist errno %d\n",errno));

  /*
   * request the remote options
   */
  /*
   * fill out the common option msg fields
   */
  optmsg.header.dest = PSI_gettid(node,0);
  optmsg.header.sender = PSI_gettid(PSI_myid,0);
  optmsg.header.type = PSP_DD_GETOPTION;
  optmsg.header.len = sizeof(optmsg);

  optmsg.count = 0;

  optmsg.opt[(int) optmsg.count].option = PSP_OP_SMALLPACKETSIZE;
  optmsg.count++;

  optmsg.opt[(int) optmsg.count].option = PSP_OP_RESENDTIMEOUT;
  optmsg.count++;

  if(success>0)
      if((success = MsgSend(&optmsg))<=0)
	  SYSLOG(1,(LOG_ERR,"msg_DAEMONESTABLISHED()  GETOPTION  requesting "
		    "errno %d\n",errno));
  return success>0?0:-1;
}

/******************************************
 *  msg_DAEMONESTABLISHED()
 */
void
msg_DAEMONESTABLISHED(int fd, DDMsg_t* msg)
{
    int number= PSI_getnode(msg->sender);
    if((abs(daemons[number].fd)!=fd)     /* another fd than the local info suggests */
       &&(daemons[number].fd!=0)){
	/* remove the pointer of the client to the daemon */
	int oldfd;
	oldfd = abs(daemons[number].fd);
	SYSLOG(2,(LOG_ERR,"got DAEMONESTABLISH(%d)on fd %d but had old "
		  "valid connection fd %d \n",number,fd,oldfd));
	DeclareDaemonDead(number);
    }
    InitDaemon(fd,number);

    /*
     * request the remote options
     */
    request_options(number);
    /*
     * send my own tasklist
     *
     * manipulate the msg so that send_tasklist thinks that
     * this is a request to send a tasklist and then send
     * the tasklist
     */
    msg->sender = PSI_gettid(number,0);
    msg->dest = PSI_gettid(PSI_myid,0);
    send_TASKLIST(msg);
    /*
     * checking if the whole cluster is up
     */
    CheckCluster();
}

/******************************************
 *  psicontrol(int fd)
 */
void psicontrol(int fd )
{
    DDBufferMsg_t msg;

    int msglen;
    char* errtxt;

    CalledFromRSelect=1;
    /* read the whole msg */
    msglen = MsgReceive(fd,(DDMsg_t*)&msg,sizeof(msg));
    CalledFromRSelect=0;

    if(msglen==0){
	/*
	 * closing connection
	 */
	if(fd == RDPSocket){
	    SYSLOG(0,(LOG_ERR,"psicontrol(): msglen 0 on RDPsocket\n"));
	}else{
	    if(PSI_isoption(PSP_ODEBUG))
		SYSLOG(4,(LOG_ERR,"psicontrol(%d): closing connection\n",fd));
	    client_delete(fd);
	}
    }else if(msglen==-1){
	if((fd != RDPSocket)||(errno != EAGAIN)){
	    errtxt=strerror(errno);
	    SYSLOG(4,(LOG_ERR,"psicontrol(%d): error(%d) while read: %s\n",
		      fd,errno,errtxt?errtxt:"UNKNOWN errno"));
	}
    }else{
	switch(msg.header.type){
	case PSP_CD_CLIENTCONNECT :
	case PSP_CD_REMOTECONNECT :
	    msg_CLIENTCONNECT(fd,(DDInitMsg_t*)&msg);
	    break;
	case PSP_DD_DAEMONCONNECT:
	    msg_DAEMONCONNECT(fd,PSI_getnode(msg.header.sender));
	    break;
	case PSP_CD_DAEMONSTOP:
	    msg.header.type = PSP_DD_DAEMONSTOP;
	    MsgBroadcast((DDMsg_t*)&msg);
	    /* fall through*/
	case PSP_DD_DAEMONSTOP:
	    msg_DAEMONSTOP((DDResetMsg_t*)&msg);
	    break;
	case PSP_DD_DAEMONESTABLISHED:
	    msg_DAEMONESTABLISHED(fd,(DDMsg_t*)&msg);
	    break;
	case PSP_DD_SPAWNREQUEST :
	    msg_SPAWNREQUEST(&msg);
	    break;
	case PSP_DD_SPAWNSUCCESS :
	    msg_SPAWNSUCCESS(&msg);
	    break;
	case PSP_DD_NEWPROCESS:
	    msg_NEWPROCESS(&msg);
	    break;
	case PSP_DD_DELETEPROCESS:
	    msg_DELETEPROCESS(&msg);
	    break;
	case PSP_DD_CHILDDEAD:
	    msg_CHILDDEAD((DDMsg_t*)&msg);
	    break;
	case PSP_DD_SPAWNFAILED:
	    msg_SPAWNFAILED(&msg);
	    break;
	case PSP_CD_TASKLISTREQUEST:
	    send_TASKLIST((DDMsg_t*)&msg);
	    break;
	case PSP_CD_TASKINFO:
	    msg_TASKLISTINFO(&msg);
	    break;
	case PSP_CD_TASKINFOREQUEST:
	    msg_TASKINFOREQUEST((DDMsg_t*)&msg);
	    break;
	case PSP_DD_TASKKILL:
	    msg_TASKKILL((DDSignalMsg_t*)&msg);
	    break;
	case PSP_CD_TASKINFOEND:
	    /* Ignore */
	    break;
	case PSP_CD_COUNTSTATUSREQUEST:
	case PSP_CD_RDPSTATUSREQUEST:
	case PSP_CD_HOSTSTATUSREQUEST:
	    /*
	     * request to send the information about a specific info
	     */
	    msg_INFOREQUEST((DDMsg_t*)&msg);
	    break;
	case PSP_CD_COUNTSTATUSRESPONSE:
	case PSP_CD_RDPSTATUSRESPONSE:
	case PSP_CD_HOSTSTATUSRESPONSE:
	    /*
	     * request to send the information about a specific info
	     */
	    MsgSend(&msg);
	    break;
	case PSP_DD_SYSTEMERROR:
	    /* Ignore */
	    break;
	case PSP_DD_CONTACTNODE:
	{
	    unsigned short node1;
	    int node2;
	    node1 = PSI_getnode(((DDContactMsg_t*)&msg)->header.dest);
	    node2 = ((DDContactMsg_t*)&msg)->partner;

	    /*
	     * contact the other node if no connection already exist
	     */
	    if (PSI_isoption(PSP_ODEBUG)){
		sprintf(PSI_txt,
			"CONTACTNODE  received ( node1= %d node2 = %d)\n",
			node1, node2);
		PSI_logerror(PSI_txt);
	    }
	    if(node1==PSI_myid){
		if ((node2 >=0 && node2<PSI_nrofnodes)){
		    if((PSID_hoststatus[node2] & PSPHOSTUP) == 0){
			unsigned long addr;
			addr = PSID_hostaddress(node2);
			PSI_startdaemon(addr);
		    }else{
			sprintf(PSI_txt,"CONTACTNODE  received but node2 is "
				"already up ( node1= %d node2 = %d)\n",
				node1,node2);
			PSI_logerror(PSI_txt);
		    }
		}
	    }else{
		if (DaemonIsUp(node1)){
		    /* forward message */
		    MsgSend(&msg);
		}else{
		    sprintf(PSI_txt,"CONTACTNODE  received but node2 could "
			    "not send request ( node1= %d node2 = %d)\n",
			    node1,node2);
		    PSI_logerror(PSI_txt);
		}
	    }
	    break;
	}
	case PSP_DD_SETOPTION:
	    /*
	     * set different options.
	     * If it is a msg form a client distribute the msg to all
	     * other daemons
	     */
	    msg_SETOPTION((DDOptionMsg_t*)&msg);
	    break;
	case PSP_DD_GETOPTION:
	    /*
	     * get different options.
	     * send back the value of the option
	     */
	    msg_GETOPTION((DDOptionMsg_t*)&msg);
	    break;
	case PSP_CD_RESET:
	    msg.header.type = PSP_DD_RESET;
	    MsgBroadcast((DDMsg_t*)&msg);
	    /* fall though to reset yourself */
	case PSP_DD_RESET:
	    /* no sychronisation is needed anymore */
	    msg_NewReset((DDResetMsg_t*)&msg);
	    break;
	case PSP_DD_NOTIFYDEAD:
	    /*
	     * notify this process when the process with tid dies
	     * To notify the process send the signal sig
	     */
	    msg_NOTIFYDEAD((DDSignalMsg_t*)&msg);
	    break;
	case PSP_DD_WHODIED:
	    /*
	     * notify this process when the process with tid dies
	     * To notify the process send the signal sig
	     */
	    msg_WHODIED((DDSignalMsg_t*)&msg);
	    break;
	case PSP_CD_LOADREQ:
	    /*
	     * ask about the current load of a processor
	     */
	    msg_LOADREQ((DDMsg_t*)&msg);
	    break;
	default :
	    SYSLOG(1,(LOG_ERR,"psid: Wrong msgtype %ld on socket %d \n",
		      msg.header.type,fd));
	}
    }
}

/******************************************
 *  RDPCallBack()
 * this function is call by RDP if
 * - a msg could be be sent
 * - a new daemon connects
 * - a daemon is declared as dead
 */
void RDPCallBack(int msgid, void* buf)
{
    int node;
    u_long hostaddr;
    DDMsg_t* msg;


    if(!CalledFromRSelect)
	SYSLOG(0,(LOG_ERR,"RDPCallBack() PANIC PANIC PANIC PANIC!!!!!!"
		  " Not called from RSelect!!!!\n"));
    switch(msgid) {
    case RDP_NEW_CONNECTION:
	node = *(int*)buf;
	SYSLOG(2,(LOG_ERR,"RDPCallBack(RDP_NEW_CONNECTION,%d). \n",node));
	if(node != PSI_myid) {
	    InitDaemon(0,node);
	    if(send_DAEMONCONNECT(node)<0)
		SYSLOG(2,(LOG_ERR,"RDPCallBack() send_DAEMONCONNECT() "
			  "returned with error %d\n",
			  errno));
	}
	break;
    case RDP_LIC_LOST:
	hostaddr = *(u_long*)buf;
	SYSLOG(2,(LOG_ERR,"RDPCallBack(RDP_LIC_LOST). "
		  "Starting License Server on host 0x%lx[%d.%d.%d.%d]\n",
		  hostaddr,(htonl(hostaddr)&0xFF000000)>>24,
		  (htonl(hostaddr)&0xFF0000)>>16,
		  (htonl(hostaddr)&0xFF00)>>8,(htonl(hostaddr)&0xFF)));
	PSID_startlicenseserver(hostaddr);
	break;
    case RDP_LIC_SHUTDOWN:
	SYSLOG(2,(LOG_ERR,"RDPCallBack(RDP_LIC_SHUTDOWN). \n"));
	shutdown_node(1);
	break;
    case RDP_LOST_CONNECTION:
	node = *(int*)buf;
	SYSLOG(2,(LOG_ERR,"RDPCallBack(RDP_LOST_CONNECTION,%d). \n",node));
	DeclareDaemonDead(node);
	break;
    case RDP_PKT_UNDELIVERABLE:
	msg = (DDMsg_t*)(((RDP_Deadbuf*)buf)->buf);
	SYSLOG(2,(LOG_ERR,"RDPCallBack(RDP_PKT_UNDELIVERABLE,"
		  "dest %x source %x %s). \n",
		  msg->dest,msg->sender,PSPctrlmsg(msg->type)));
	if(PSI_getpid(msg->sender))
	{
	    /* sender is a client (somewhere) */
	    DDErrorMsg_t errmsg;
	    errmsg.header.len = sizeof(errmsg);
	    errmsg.request = msg->type;
	    errmsg.err = EHOSTUNREACH;
	    errmsg.header.type = PSP_DD_SYSTEMERROR;
	    errmsg.header.dest = msg->sender;
	    errmsg.header.sender = PSI_gettid(PSI_myid,0);
	    MsgSend(&errmsg);
	}
	break;
    default:
	SYSLOG(0,(LOG_ERR,"RDPCallBack(%d,%p). Unhandled message.\n",
		  msgid, buf));
    }
}

/******************************************
*  sighandler(signal)
*/
void sighandler(int sig)
{
    switch(sig){
    case SIGSEGV:
	SYSLOG(0,(LOG_ERR,"Received SEGFAULT signal. Shut down.\n"));
	if(daemons[PSI_myid].status & PSP_DSTATE_SHUTDOWN)
	    if(daemons[PSI_myid].status & PSP_DSTATE_SHUTDOWN2)
		shutdown_node(3);
	    else
		shutdown_node(2);
	else
	    shutdown_node(1);
	exit(-1);
	/*signal(SIGSEGV,sighandler);*/
	break;
    case SIGTERM:
	SYSLOG(0,(LOG_ERR,"Received SIGTERM signal. Shut down.\n"));
	if(daemons[PSI_myid].status & PSP_DSTATE_SHUTDOWN)
	    if(daemons[PSI_myid].status & PSP_DSTATE_SHUTDOWN2)
		shutdown_node(3);
	    else
		shutdown_node(2);
	else
	    shutdown_node(1);
	signal(SIGTERM,sighandler);
	break;
    case SIGCHLD:
	/* nothing to do, ignore it*/
    {
	int pid;           /* pid of the child process */
	long tid;          /* tid of the child process */
	int estatus;       /* termination status of the child process */

	while ((pid = waitpid(-1, &estatus, WNOHANG)) > 0){
	    /* Nothing to do right now */
	    /*
	     * I have to clean up when the socket gets closed.
	     * If the task hasn't connected yet, I have to delete him from
	     * the waiting_list_for_connect
	     */
	    /* I'll just report it to the logfile */
	    PStask_t* diedtask=NULL;

	    SYSLOG(0,(LOG_ERR,
		      "Received SIGCHLD for pid %d with exit status %d\n",
		      pid,estatus));
	    /*
	     * remove the task from the waiting list (if it is on list )
	     */
	    tid = PSI_gettid(-1,pid);
	    diedtask = PStasklist_dequeue(&spawned_tasks_waiting_for_connect,
					  tid);
	    /*
	     * if the task hasn't connected yet
	     * inform the node of the parent, that task died
	     */
	    if((diedtask)&&(diedtask->ptid)){
		DDMsg_t msg;
		msg.type = PSP_DD_CHILDDEAD;
		msg.sender = diedtask->tid;
		msg.dest = diedtask->ptid;
		msg.len = sizeof(msg);
		if(PSI_getnode(diedtask->ptid)==PSI_myid){
		    /* send the parent a SIGCHILD signal */
		    msg_CHILDDEAD(&msg);
		}else{
		    /* send spawning node a sign that the new task is dead */
		    MsgSend(&msg);
		}
	    }
	    if(diedtask)
		free (diedtask);
	}
    }
    /* reset the sighandler */
    signal(SIGCHLD,sighandler);
    break;

    case  SIGHUP    : /* hangup, generated when terminal disconnects */
    case  SIGINT    : /* interrupt, generated from terminal special char */
    case  SIGQUIT   : /* (*) quit, generated from terminal special char */
    case  SIGTSTP   : /* (@) interactive stop */
    case  SIGCONT   : /* (!) continue if stopped */
    case  SIGVTALRM : /* virtual time alarm (see setitimer) */
    case  SIGPROF   : /* profiling time alarm (see setitimer) */
    case  SIGWINCH  : /* (+) window size changed */
    case  SIGALRM   : /* alarm clock timeout */
    case  SIGPIPE   : /* write on a pipe with no one to read it */
	SYSLOG(0,(LOG_ERR,"Received  signal %d. Continue.\n",sig));
	signal(sig,sighandler);
	break;
    case  SIGILL    : /* (*) illegal instruction (not reset when caught)*/
    case  SIGTRAP   : /* (*) trace trap (not reset when caught) */
    case  SIGABRT   : /* (*) abort process */
    case  SIGFPE    : /* (*) floating point exception */
    case  SIGBUS    : /* (*) bus error (specification exception) */
#ifdef SIGEMT
    case  SIGEMT    : /* (*) EMT instruction */
#endif
#ifdef SIGSYS
    case  SIGSYS    : /* (*) bad argument to system call */
#endif
#ifdef SIGINFO
    case  SIGINFO   : /* (+) information request */
#endif
#ifdef SIGIOINT
    case  SIGIOINT  : /* printer to backend error signal */
#endif
#ifdef SIGAIO
    case  SIGAIO    : /* base lan i/o */
#endif
#ifdef SIGURG
    case  SIGURG    : /* (+) urgent contition on I/O channel */
#endif
#ifdef SIGIO
    case  SIGIO     : /* (+) I/O possible, or completed */
#endif
    case  SIGTTIN   : /* (@) background read attempted from control terminal*/
    case  SIGTTOU   : /* (@) background write attempted to control terminal */
    case  SIGXCPU   : /* cpu time limit exceeded (see setrlimit()) */
    case  SIGXFSZ   : /* file size limit exceeded (see setrlimit()) */
    case  SIGUSR1   : /* user defined signal 1 */
    case  SIGUSR2   : /* user defined signal 2 */
    default:
	SYSLOG(0,(LOG_ERR,"Received  signal %d. Shut down.\n",sig));
	if(daemons[PSI_myid].status & PSP_DSTATE_SHUTDOWN)
	    if(daemons[PSI_myid].status & PSP_DSTATE_SHUTDOWN2)
		shutdown_node(3);
	    else
		shutdown_node(2);
	else
	    shutdown_node(1);
	signal(sig,sighandler);
	break;
    }
}

/******************************************
*  CheckFileTable()
*/
void CheckFileTable()
{
    fd_set rfds;
    int fd;
    struct timeval tv;

    if(PSI_isoption(PSP_ODEBUG))
	SYSLOG(1,(LOG_ERR,"CheckFileTable()\n"));
    for(fd=0;fd<FD_SETSIZE;) {
	if(FD_ISSET(fd,&openfds)) {
	    bzero(&rfds,sizeof(rfds));
	    FD_SET(fd,&rfds);

	    tv.tv_sec=0;
	    tv.tv_usec=0;
	    if (select(nfds, &rfds, (fd_set *)0, (fd_set *)0, &tv) < 0) {
		/* error : check if it is a wrong fd in the table */
		switch(errno){
		case EBADF :
		    /* if(PSI_isoption(PSP_ODEBUG))*/
		    SYSLOG(1,(LOG_ERR, "CheckFileTable(%d):"
			      " EBADF -> closing connection\n", fd));
		    client_delete(fd);
		    fd++;
		    break;
		case EINTR:
		    SYSLOG(1,(LOG_ERR, "CheckFileTable(%d):"
			      " EINTR -> trying again\n", fd));
		case EINVAL:
		    SYSLOG(1,(LOG_ERR, "CheckFileTable(%d):"
			      " PANIC filenumber(%d) is wrong. Good bye!\n",
			      fd, nfds));
		    shutdown_node(1);
		    break;
		case ENOMEM:
		    SYSLOG(1,(LOG_ERR,"CheckFileTable(%d):"
			      " PANIC not enough memory. Good bye!\n", fd));
		    shutdown_node(1);
		    break;
		default:
		{
		    char* errtxt;
		    errtxt=strerror(errno);
		    SYSLOG(1,(LOG_ERR,"CheckFileTable(%d):"
			      " unrecognized error (%d):%s\n", fd, errno,
			      errtxt?errtxt:"UNKNOWN errno"));
		    fd ++;
		    break;
		}
		}
	    }else
		fd ++;
	}else
	    fd ++;
    }
}

/******************************************
 *  usage()
 */
void usage(void)
{
    fprintf(stderr,"usage: psid [-h] [-d] [-D MASK] [-f file]\n");
}

/******************************************
 *  help()
 */
void help(void)
{
    usage();
    fprintf(stderr,"\n");
    fprintf(stderr," -d       : Activate logging.\n");
    fprintf(stderr," -D  MASK : Activate logging, use MASK for debugging in psilib.\n");
    fprintf(stderr," -f file  : use 'file' as config-file (default is psidir/config/psm.config).\n");
    fprintf(stderr," -h,      : print this screen.\n");
}

/******************************************
*  main(int argc, char **argv)
*/
int
main(int argc, char **argv)
{
    if(!fork()){
	struct servent *service;
	struct sockaddr_in sa;

	int fd;             /* master socket and socket to check connections*/
	struct timeval tv;  /* timeval for waiting on select()*/

	fd_set rfds;        /* read file descriptor set */
	int opt;            /* return value of getopt */

	long debugmask = 0;
	int i;

	int DEBUGGING = 0;
 	long check_reset_status = 0;
	char* errtxt;

	BlockSig(1,SIGCHLD);
	BlockSig(1,SIGALRM);

	openlog("psid",LOG_PID|LOG_CONS,LOG_DAEMON);
	PSI_setoption(PSP_OSYSLOG,1);
	SYSLOG_LEVEL = 9;

	while ((opt = getopt(argc, argv, "dD:f:hH")) != -1){
	    switch (opt){
	    case 'd' : /* DEBUG print out debug informations */
		DEBUGGING = 1;
		debugmask = 0;
		break;
	    case 'D' :
		DEBUGGING = 1;
		sscanf(optarg,"%lx",&debugmask);
		break;
	    case 'f' :
		Configfile = (char *) malloc(strlen(optarg));
		strcpy(Configfile, optarg);
		break;
	    case 'h' : /* help */
	    case 'H' :
		help();
		return 0;
		break;
	    default :
		SYSLOG(0,(LOG_ERR, "usage: %s [-d] [-D MASK] [-f configfile]\n",argv[0]));
		usage();
		return -1;
	    }
	}

	signal(SIGHUP   ,sighandler);
	signal(SIGINT   ,sighandler);
	signal(SIGQUIT  ,sighandler);
	signal(SIGILL	,sighandler);
	signal(SIGTRAP	,sighandler);
	signal(SIGABRT	,sighandler);
	signal(SIGIOT	,sighandler);
	signal(SIGBUS	,sighandler);
	signal(SIGFPE	,sighandler);
	signal(SIGKILL	,sighandler);
	signal(SIGUSR1	,sighandler);
	signal(SIGSEGV	,sighandler);
	signal(SIGUSR2	,sighandler);
	signal(SIGPIPE	,sighandler);
	signal(SIGALRM	,sighandler);
	signal(SIGTERM	,sighandler);
	signal(SIGCHLD	,sighandler);
	signal(SIGCONT	,sighandler);
	signal(SIGSTOP	,sighandler);
	signal(SIGTSTP	,sighandler);
	signal(SIGTTIN	,sighandler);
	signal(SIGTTOU	,sighandler);
	signal(SIGURG	,sighandler);
	signal(SIGXCPU	,sighandler);
	signal(SIGXFSZ	,sighandler);
	signal(SIGVTALRM,sighandler);
	signal(SIGPROF	,sighandler);
	signal(SIGWINCH	,sighandler);
	signal(SIGIO	,sighandler);
#ifdef __osf__
	/* OSF on Alphas */
	signal( SIGSYS  ,sighandler);
	signal( SIGINFO ,sighandler);
	signal( SIGIOINT,sighandler);
	signal( SIGAIO  ,sighandler);
	signal( SIGPTY  ,sighandler);
#elif defined(__alpha)
	/* Linux on Alpha*/
	signal( SIGSYS  ,sighandler);
	signal( SIGINFO ,sighandler);
#endif
#if !defined(__sun)
#if !defined(__osf__)
#if !defined(__alpha)
	signal(SIGSTKFLT,sighandler);
#endif
#endif
#endif

	signal(SIGHUP   ,SIG_IGN);
	signal(SIGCHLD  ,sighandler);
	signal(SIGTERM  ,sighandler);
	/*
	 * Disable stdin,stdout,stderr
	 */
	close(0);
	close(1);
	close(2);

	if(DEBUGGING){
	    PSI_setoption(PSP_ODEBUG, 1);
	    PSI_debugmask = debugmask;
	    SYSLOG(0,(LOG_ERR,"Debugging mode with debugmask 0x%lx\n",
		      debugmask));
	}

	nfds = getdtablesize();

	/*
	 * create the socket to listen to the client
	 */
	PSI_msock = socket(AF_INET,SOCK_STREAM,0);
	if ((service = getservbyname("psids","tcp")) == NULL){
	    SYSLOG(0,(LOG_ERR, "can't get \"psids\" service entry\n"));
	    exit(1);
	}

	/*
	 * bind the socket to the right address
	 */
	bzero((char *)&sa, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = INADDR_ANY;
	sa.sin_port = service->s_port;
	if (bind(PSI_msock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
	    if(DEBUGGING){
		errtxt=strerror(errno);
		SYSLOG(0,(LOG_ERR,
			  "Daemon already running?\n used port: %d error(%d):%s \n",
			  ntohs(sa.sin_port),
			  errno,errtxt?errtxt:"UNKNOWN errno"));
	    }
	    exit(1);
	}
	{
	    int reuse =1;
#ifdef __linux__
	    setsockopt(PSI_msock,SOL_IP,SO_REUSEADDR,&reuse,sizeof(reuse));
#else
	    setsockopt(PSI_msock,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
#endif
	}

	SYSLOG(0,(LOG_ERR, "***************************************"
		  "********************\n"));
	SYSLOG(0,(LOG_ERR, "Starting ParaStation DAEMON V%d"
		  " (c) ParTec AG (www.par-tec.com)\n",
		  PSPprotocolversion));

	if (listen(PSI_msock, 20) < 0){
	    SYSLOG(0,(LOG_ERR,
		      "Error while trying to listen (code %d)\n",errno));
	    exit(1);
	}

	/*
	 * read the config file
	 */
	if (PSID_readconfigfile()){
	    if(ConfigSyslog!=LOG_DAEMON){
		SYSLOG(0,(LOG_ERR,"Changing logging dest from LOG_DAEMON "
			  "to %s\n", ConfigSyslog==LOG_KERN?"LOG_KERN":
			  ConfigSyslog==LOG_LOCAL0?"LOG_LOCAL0":
			  ConfigSyslog==LOG_LOCAL1?"LOG_LOCAL1":
			  ConfigSyslog==LOG_LOCAL2?"LOG_LOCAL2":
			  ConfigSyslog==LOG_LOCAL3?"LOG_LOCAL3":
			  ConfigSyslog==LOG_LOCAL4?"LOG_LOCAL4":
			  ConfigSyslog==LOG_LOCAL5?"LOG_LOCAL5":
			  ConfigSyslog==LOG_LOCAL6?"LOG_LOCAL6":
			  ConfigSyslog==LOG_LOCAL7?"LOG_LOCAL7":
			  "UNKNOWN"));
		closelog();
		openlog("psid",LOG_PID|LOG_CONS,ConfigSyslog);
		SYSLOG(0,(LOG_ERR, "************************************"
			  "***********************\n"));
		SYSLOG(0,(LOG_ERR, "Starting ParaStation DAEMON"
			  " (c) ParTec AG (www.par-tec.com)\n"));
	    }
	    SYSLOG(0,(LOG_ERR,"  Protocol Version %d\n",
		      PSPprotocolversion ));

	    SYSLOG_LEVEL = ConfigSyslogLevel;

	    nfds = getdtablesize();
	    FD_ZERO(&openfds);
	    FD_SET(PSI_msock, &openfds);

	    {
		/* set the memory limits to 256 MB */
		struct rlimit rlp;
		int dummy;
		dummy=getrlimit(RLIMIT_DATA,&rlp);
		if(ConfigRLimitDataSize>0)
		    rlp.rlim_cur=ConfigRLimitDataSize*1024;
		else
		    rlp.rlim_cur = RLIM_INFINITY;
		dummy=setrlimit(RLIMIT_DATA,&rlp);
	    }

	    timerclear(&shutdowntimer);
	    timerclear(&killclientstimer);
	    timerclear(&pingtimer);
	    selecttimer.tv_sec =
		ConfigPsidSelectTime==-1 ? 2 : ConfigPsidSelectTime;
	    selecttimer.tv_usec = 0;
	    gettimeofday(&maintimer,NULL);

	    for(i=0;i<FD_SETSIZE;i++){
		clients[i].tid =-1;
		clients[i].ob.daemon = NULL;
	    }

	    SYSLOG(0,(LOG_ERR,
		      "Local Service Port initialized. Using socket %d\n",
		      PSI_msock));

	    RDP_SetDBGLevel(1);
	    RDPSocket = RDPinit(PSI_nrofnodes, ConfigMgroup,
				1 /* use syslog */,
				RDPCallBack);
	    if(RDPSocket<0){
		SYSLOG(0,(LOG_ERR,
			  "Error while trying init RDP (code %d)\n",errno));
		exit(1);
	    }
	    SYSLOG(0,(LOG_ERR,"RDP initialized. Using socket %d\n",RDPSocket));
	    FD_SET(RDPSocket, &openfds);

	    SYSLOG(2,(LOG_ERR,"Contacting other daemons in the cluster\n"));
	    for(i=0;i<PSI_nrofnodes;i++){
		daemons[i].fd=0;
		daemons[i].node = 0;
		daemons[i].tasklist = 0;
		timerset(&daemons[i].pongtime, &maintimer);
	    }

	    SYSLOG(2,(LOG_ERR,
		      "Contacting other daemons in the cluster. DONE\n"));
	    SYSLOG(1,(LOG_ERR,"SelectTimer=%ld sec DeclareDeadInterval=%ld\n",
		      ConfigPsidSelectTime,ConfigDeclareDeadInterval));

	    spawned_tasks_waiting_for_connect = 0;

	    PSID_hoststatus[PSI_myid] |= PSPHOSTUP;

	    /*
	     * check if the Cluster is ready
	     */
	    CheckCluster();
	    StartOtherDaemons();
	    CheckCluster();

	    /*
	     * Main loop
	     */
	    while (1){

		timerset(&tv,&selecttimer);
		BlockSig(0,SIGCHLD);
		BlockSig(1,SIGCHLD);
		BlockSig(0,SIGALRM);
		bcopy((char *)&openfds, (char *)&rfds, sizeof(rfds));
		CalledFromRSelect=1;

		if (Rselect(nfds, &rfds, (fd_set *)0, (fd_set *)0, &tv) < 0){
		    errtxt=strerror(errno);
		    SYSLOG(1,(LOG_ERR,"Error while Select (code %d) %s\n",
			      errno,errtxt?errtxt:"UNKNOWN errno"));

		    CheckFileTable();
		    SYSLOG(6,(LOG_ERR,"Error while Select continueing\n"));
		    BlockSig(1,SIGALRM);
		    continue;
		}
		CalledFromRSelect=0;
		BlockSig(1,SIGALRM);

		gettimeofday(&maintimer,NULL);
		/*
		 * check the master socket for new requests
		 */
		if (FD_ISSET(PSI_msock, &rfds)){
		    int ssock;  /* slave server socket */
		    int flen = sizeof(sa);

		    if(PSI_isoption(PSP_ODEBUG))
			SYSLOG(4,(LOG_ERR,"accepting new connection\n"));

		    ssock = accept(PSI_msock, (struct sockaddr *)&sa, &flen);
		    if (ssock < 0){
			char* errtxt=strerror(errno);
			SYSLOG(0,(LOG_ERR,"Error while accept (code %d):%s\n",
				  errno,errtxt?errtxt:"UNKNOWN errno"));
			continue;
		    }else{
			char keepalive;
			char linger;
			char reuse;
			socklen_t size;

			clients[ssock].flags = INITIALCONTACT;
			FD_SET(ssock, &openfds);
			if(PSI_isoption(PSP_ODEBUG))
			    SYSLOG(4,(LOG_ERR,
				      "accepting: new socket(%d)\n",ssock));
			size = sizeof(reuse);
			getsockopt(ssock,SOL_SOCKET,SO_REUSEADDR,&reuse,&size);
			size = sizeof(keepalive);
			getsockopt(ssock,SOL_SOCKET,SO_KEEPALIVE,
				   &keepalive,&size);
			size = sizeof(linger);
			getsockopt(ssock,SOL_SOCKET,SO_LINGER,&linger,&size);
			SYSLOG(9,(LOG_ERR,
				  "socketoptions was (linger=%d keepalive=%d)"
				  " setting it to (1,1)\n",linger,keepalive));

			size = sizeof(reuse);
			reuse=1;
			setsockopt(ssock,SOL_SOCKET,SO_REUSEADDR,&reuse,size);

			size = sizeof(keepalive);
			keepalive=1;
			setsockopt(ssock,SOL_SOCKET,SO_KEEPALIVE,
				   &keepalive,size);

			size = sizeof(linger);
			linger=1;
			setsockopt(ssock,SOL_SOCKET,SO_LINGER,&linger,size);
		    }
		}
		/*
		 * check the client sockets for any closing connections
		 * or control msgs
		 */
		for (fd=0; fd<nfds; ++fd)
		    if (fd != PSI_msock      /* handled before */
			&& fd !=RDPSocket     /* handled below */
			&& FD_ISSET(fd, &rfds)){
			psicontrol(fd);
		    }
		/*
		 * Read all RDP messages
		 */
		while(FD_ISSET(RDPSocket,&rfds)){
		    psicontrol(RDPSocket);
		    bzero(&rfds,sizeof(rfds));
		    FD_SET(RDPSocket,&rfds);

		    BlockSig(0,SIGALRM);
		    tv.tv_sec=0;
		    tv.tv_usec=0;
		    CalledFromRSelect=1;
		    if (Rselect(RDPSocket+1,
				&rfds, (fd_set *)0, (fd_set *)0, &tv) < 0)
			break;
		    CalledFromRSelect=0;
		    BlockSig(1,SIGALRM);
		}
		CalledFromRSelect=0;

		/*
		 * Check for reset state
		 */
		if(daemons[PSI_myid].status & PSP_DSTATE_DORESET){
		    Do_NewReset();
		}
		/*
		 * Check if any operation forced me to shutdown
		 */
		if(daemons[PSI_myid].status & PSP_DSTATE_SHUTDOWN)
		    if(daemons[PSI_myid].status & PSP_DSTATE_SHUTDOWN2)
			shutdown_node(3);
		    else
			shutdown_node(2);
		else{
		    /*
		     * checking all the other daemos, if they are still okay
		     */
		    if(timercmp(&pingtimer,&maintimer, <)){
			/* with RDP, pinging is done by RDP itself
			 * if status changes, then the routine RDPCallBack
			 * is called
			 */
			timerset(&pingtimer,&maintimer);
			mytimeradd(&pingtimer,selecttimer.tv_sec,0);
			CheckCluster();
			if((daemons[PSI_myid].status & PSP_DSTATE_NOCONNECT)!=0){
			    if(check_reset_status
			       == (daemons[PSI_myid].status
				   & PSP_DSTATE_NOCONNECT)){
				/* the old reset status was the same so
				   probably the system hangs.
				   Restart the reset again
				*/
				sprintf(PSI_txt, "Reset status isn't changing."
					" So probably something's hanging.");
				PSI_logerror(PSI_txt);
				sprintf(PSI_txt, "I abort this reset "
					"and try to start it again.");
				PSI_logerror(PSI_txt);
				daemons[PSI_myid].status =
				    (daemons[PSI_myid].status
				     & ~(PSP_DSTATE_RESET_WAIT
					 | PSP_DSTATE_RESET_INACTION
					 | PSP_DSTATE_RESET_HW));
				Do_NewReset();
			    }
			    check_reset_status = daemons[PSI_myid].status
				& PSP_DSTATE_NOCONNECT;
			}
		    }
		}
	    }
	}
	else
	    SYSLOG(0,(LOG_ERR,"%s: PSI Daemon: init failed \n",argv[0]));
    }
    return 0;
}
