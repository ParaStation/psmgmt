/*
 *               ParaStation3
 * psi.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psi.c,v 1.17 2002/01/18 15:59:29 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: info.c,v 1.10 2002/01/18 15
:54:22 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "psilog.h"
#include "psitask.h"
#include "psp.h"
#include "info.h"
#include "psienv.h"

#include "psi.h"

int PSI_msock =-1;

short PSI_nrofnodes = -1;
unsigned short PSI_myid = -1;

long PSI_mytid = -1;
int PSI_mypid = -1;

int PSI_masternode = -1;
int PSI_masterport = -1;
int PSI_myrank;

char *PSI_psidversion = NULL;

char *PSI_hoststatus = NULL;

enum TaskOptions PSI_mychildoptions = TaskOption_SENDSTDHEADER;

/****************************************
*  PSI_getpid()
*  returns the value of the local PID of the OS. The TID of a Task in the
*  Cluster is a combination of the Node number and the local pid
*  on the node.
*/
pid_t
PSI_getpid(long tid)
{
    return (tid & 0xFFFF);
}

/****************************************
*  PSI_getnode()
*  return the value of the node number of a TID. The TID of a Task in the
*  Cluster is a combination of the Node number and the local pid
*  on the node.
*/
unsigned short
PSI_getnode(long tid)
{
    if(tid>=0)
	return (tid>>16)&0xFFFF;
    else
	return PSI_myid;
}

/****************************************
*  PSI_getnrofnodes()
* returns the number of nodes
*/
short
PSI_getnrofnodes()
{
    return PSI_nrofnodes;
}

/****************************************
*  PSI_gettid()
*  returns the TID. This is necessary to have unique Task Identifiers in
*  the cluster .The TID of a Task in the Cluster is a combination
*  of the Node number and the local pid on the node.
*  If node=-1 the local nodenr is used
*/
long
PSI_gettid(short node, pid_t pid)
{
    if(node<0)
	return (((PSI_myid&0xFFFF)<<16)|pid);
    else
	return (((node&0xFFFF)<<16)|pid);
}

long PSI_options=0;

/***************************************************************************
 *      PSI_setoption()
 */
int PSI_setoption(long option,char value)
{
    if(value)
	PSI_options = PSI_options | option;
    else
	PSI_options = PSI_options & ~option;
    return PSI_options;
}

/***************************************************************************
 *       PSI_startdaemon()
 *
 *       starts the daemon via the inetd
 */
int
PSI_startdaemon(u_long hostaddr)
{
    int sock;
    struct servent *service;
    struct sockaddr_in sa;
#if defined(DEBUG)
    if(PSP_DEBUGADMIN & (PSI_debugmask )){
	sprintf(PSI_txt,"PSI_startdaemon(%lx)\n", hostaddr);
	PSI_logerror(PSI_txt);
    }
#endif
    /*
     * start the PSI Daemon via inetd
     */
    sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if ((service = getservbyname("psid","tcp")) == NULL){
	fprintf(stderr, "can't get \"psid\" service entry\n");
	shutdown(sock,2);
	close(sock);
	return 0;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = hostaddr;
    sa.sin_port = service->s_port;
    if (connect(sock, (struct sockaddr*) &sa, sizeof(sa)) < 0){
	perror("PSI daemon connect for start with inetd.");
	shutdown(sock,2);
	close(sock);
	return 0;
    }
    usleep(200000);
    shutdown(sock,2);
    close(sock);
    return 1;
}

/***************************************************************************
 *       PSI_daemonsocket()
 */
int
PSI_daemonsocket(u_long hostaddr)
{
    int sock;
    struct servent *service;
    struct sockaddr_in sa;

#if defined(DEBUG)
    if(PSP_DEBUGSTARTUP & (PSI_debugmask )){
	sprintf(PSI_txt,"PSI_daemonsocket(%lx)\n", hostaddr);
	PSI_logerror(PSI_txt);
    }
#endif

    sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(sock <0)
	return -1;

    if ((service = getservbyname("psids","tcp")) == NULL){
	fprintf(stderr, "can't get \"psids\" service entry\n");
	shutdown(sock,2);
	close(sock);
	return -1;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = hostaddr;
    sa.sin_port = service->s_port;

    if(connect(sock, (struct sockaddr*) &sa, sizeof(sa)) < 0){
	shutdown(sock,2);
	close(sock);
	return -1;
    }

    return sock;
}

/***************************************************************************
 *       PSI_daemon_connect()
 *
 *       opens a socket which is the connection to the PSI daemon.
 *       This socket is necessary for the daemon to handle this process
 *       RETURN TRUE on success, FALSE otherwise
 */
static int
PSI_daemon_connect(u_short protocol, u_long hostaddr)
{
    DDInitMsg_t msg;
    int pid;
    int uid;
    int connectfailes;
    int retry_count =0;
    int ret;

#if defined(DEBUG)
    if(PSP_DEBUGSTARTUP & (PSI_debugmask )){
	sprintf(PSI_txt,"PSI_daemon_connect(%d)\n",protocol);
	PSI_logerror(PSI_txt);
    }
#endif

   /*
    * connect to the PSI Daemon service port
    */
    connectfailes = 0;
    retry_count = 0;

 RETRY_CONNECT:

    while((PSI_msock=PSI_daemonsocket(hostaddr))==-1){
	/*
	 * start the PSI Daemon via inetd
	 */
	if(connectfailes++ < 10){
	    PSI_startdaemon(hostaddr);
	}else{
	    perror("PSI daemon connect failed finally");
	    return 0;
	}
    }

    pid = getpid();
    uid = getuid();

    /* local connect */
    msg.header.type = PSP_CD_CLIENTCONNECT;

    msg.header.len = sizeof(msg);
    msg.header.sender = pid;
    msg.header.dest = 0;
    msg.version = PSPprotocolversion;
    msg.pid = pid;
    msg.uid = uid;
    msg.group = protocol;

    ClientMsgSend(&msg);
    if((ret = ClientMsgReceive(&msg))<=0){
	char* errtxt;
	shutdown(PSI_msock,2);
	close(PSI_msock);
	PSI_msock =-1;
	errtxt=strerror(errno);
	if(ret==0)
	    fprintf(stderr,"PSI_daemon_connect(): unexpected "
		    "return message length (%d).\n", ret);
	else
	    fprintf(stderr,"PSI_daemon_connect(): error while "
		    "receiving return message (%d): %s\n",
		    errno,errtxt?errtxt:"UNKNOWN");
	return 0;
    }
    switch (msg.header.type){
    case PSP_DD_STATENOCONNECT  :
	retry_count++;
	shutdown(PSI_msock,2);
	close(PSI_msock);
	PSI_msock =-1;
	if(retry_count <10){
	    sleep(1);
	    goto RETRY_CONNECT;
	}
	fprintf(stderr,"PSI_daemon_connect(): Daemon is in a state"
		" where he doesn't allow new connections.\n");
	return 0;
	break;
    case PSP_CD_CLIENTREFUSED :
	shutdown(PSI_msock,2);
	close(PSI_msock);
	PSI_msock =-1;
	if((protocol!=TG_RESET)&&(protocol!=TG_RESETABORT))
	    fprintf(stderr,"PSI_daemon_connect(): "
		    "local daemon refused connection.\n");
	return 0;
	break;
    case PSP_CD_NOSPACE :
	shutdown(PSI_msock,2);
	close(PSI_msock);
	PSI_msock =-1;
	fprintf(stderr,"PSI_daemon_connect(): "
		"local Shared Memory doesn't have space available\n");
	return 0;
	break;
    case PSP_CD_UIDLIMIT :
	shutdown(PSI_msock,2);
	close(PSI_msock);
	PSI_msock =-1;
	fprintf(stderr,"PSI_daemon_connect(): "
		"Library is limited to user id %d.\n",msg.uid);
	return 0;
	break;
    case PSP_CD_PROCLIMIT :
	shutdown(PSI_msock,2);
	close(PSI_msock);
	PSI_msock =-1;
	fprintf(stderr,"PSI_daemon_connect(): "
		"Library is limited to %d processes.\n",msg.uid);
	return 0;
	break;
    case PSP_CD_OLDVERSION :
	shutdown(PSI_msock,2);
	close(PSI_msock);
	PSI_msock =-1;
	fprintf(stderr,"PSI_daemon_connect(): "
		"local daemon(version %ld) doesn't support this "
		"library version (revision %d). Pleases relink the program.\n",
		msg.version,PSPprotocolversion );
	return 0;
	break;
    case PSP_CD_CLIENTESTABLISHED :
	PSI_nrofnodes = msg.nrofnodes;
	PSI_myid = msg.myid;
	PSI_myrank = msg.rank;
	PSI_masternode = msg.masternode;
	PSI_masterport = msg.masterport;
	PSI_SetInstalldir(msg.instdir);
	if(strcmp(msg.instdir, PSI_LookupInstalldir())){
	    fprintf(stderr,"PSI_daemon_connect(): "
		    "Installation directory '%s' not correct.\n", msg.instdir);
	    return 0;
	}
	if ( ! PSI_psidversion ) {
	    PSI_psidversion = malloc(sizeof(msg.psidvers));
	}
	strncpy(PSI_psidversion, msg.psidvers, sizeof(msg.psidvers));
	PSI_psidversion[sizeof(msg.psidvers)-1] = '\0';
	return 1;
	break;
    default :
	shutdown(PSI_msock,2);
	close(PSI_msock);
	PSI_msock =-1;
	fprintf(stderr,"PSI_daemon_connect(): "
		"unexpected return code %s .\n",PSPctrlmsg(msg.header.type));
	return 0;
	break;
    }

    return 0;
}

/***************************************************************************
 *       PSI_clientinit()
 *
 *       MUST be call by every client process. It does all the necessary
 *       work to connect to the daemon and setup the necessary environment.
 */
int
PSI_clientinit(u_short protocol)
{
    char* envstrvalue;

#if defined(DEBUG)
    PSI_openlog();

    if(PSP_DEBUGSTARTUP & (PSI_debugmask )){
	sprintf(PSI_txt,"PSI_clientinit()\n");
	PSI_logerror(PSI_txt);
    }
#endif

    if (PSI_msock != -1) {
	/* Allready connected */
	return 1;
    }

    /*
     * connect to local PSI daemon
     */
    if (!PSI_daemon_connect(protocol,INADDR_ANY)){
	if((protocol!=TG_RESET)&&(protocol!=TG_RESETABORT))
	    fprintf(stderr,"PSI_clientinit(): can't contact local daemon.\n");
	return 0;
    }

    PSI_mypid = getpid();
    PSI_mytid = PSI_gettid(-1,PSI_mypid);

    if(! PSI_hoststatus)
	PSI_hoststatus = (char *) malloc(PSI_nrofnodes);
    INFO_request_hoststatus(PSI_hoststatus, PSI_nrofnodes);

    /* check if the environment variable PSI_EXPORT is set.
     * If it is set, then take the environment variables
     * mentioned there into the PSI_environment
     */
    if((envstrvalue=getenv("PSI_EXPORTS"))!=NULL){
	char* envstr,*newstr;
	char* envstrstart;
	envstrstart = strdup(envstrvalue);
	if(envstrstart){
	    envstr = envstrstart;
	    while((newstr = strchr(envstr,','))!=NULL){
		newstr[0]=0; /* replace the "," with EOS */
		newstr++;    /* move to the start of the next string */
		if((envstrvalue=getenv(envstr))!=NULL){
		    char putstring[1000];
		    sprintf(putstring,"%s=%s",envstr,envstrvalue);
		    PSI_putenv(putstring);
		}
		envstr = newstr;
	    }
	    if((envstrvalue=getenv(envstr))!=NULL){
		char putstring[1000];
		sprintf(putstring,"%s=%s",envstr,envstrvalue);
		PSI_putenv(putstring);
	    }
	    free(envstrstart);
	}
    }

    return 1;
}

/***************************************************************************
 *       PSI_clientexit()
 *
 *   reconfigs all variable so that a PSI_clientinit() will be successful
 */
int
PSI_clientexit(void)
{
#if defined(DEBUG)
    if(PSP_DEBUGSTARTUP & (PSI_debugmask )){
	sprintf(PSI_txt,"PSI_clientexit()\n");
	PSI_logerror(PSI_txt);
    }
#endif

    if (PSI_msock == -1)
	return 1;


    /*
     * close connection to local PSI daemon
     */
/*   shutdown(PSI_msock,2);*/
    close(PSI_msock);
    PSI_msock = -1;

    return 1;
}

/*----------------------------------------------------------------------*/
/*
 * PSI_notifydead()
 *
 *  PSI_notifydead requests the signal sig, when the child with task
 *  identifier tid dies.
 *
 * PARAMETERS
 *  tid: the task identifier of the child whose death shall be signaled to you
 *  sig: the signal which should be sent to you when the child dies
 * RETURN  0 on success
 *         -1 on error
 */
int PSI_notifydead(long tid, int sig)
{
    DDSignalMsg_t msg;

    msg.header.type = PSP_DD_NOTIFYDEAD;
    msg.header.sender = PSI_mytid;
    msg.header.dest = tid;
    msg.header.len = sizeof(msg);
    msg.signal = sig;

    if (ClientMsgSend(&msg)<0) {
	return -1;
    }

    if (ClientMsgReceive(&msg)<0) {
	return -1;
    } else if(msg.signal!=0) {
	return -1;
    }

    return 0;
}

/*----------------------------------------------------------------------*/
/*
 * PSI_release()
 *
 *  PSI_release() helps parent to survive if task is exiting. Quite usefull
 *       this in PSE_finalize().
 *
 * PARAMETERS
 *  tid: the task identifier of the task that should *not* kill the parent
 *
 * RETURN  0 on success
 *         -1 on error
 */
int PSI_release(long tid)
{
    DDSignalMsg_t msg;

    msg.header.type = PSP_DD_RELEASE;
    msg.header.sender = PSI_mytid;
    msg.header.dest = tid;
    msg.header.len = sizeof(msg);
    msg.signal = -1;

    if (ClientMsgSend(&msg)<0) {
	return -1;
    }

    if(ClientMsgReceive(&msg)<0) {
	return -1;
    } else if(msg.signal!=0) {
	return -1;
    }

    return 0;
}

/*----------------------------------------------------------------------*/
/*
 * PSI_whodied()
 *
 *  PSI_whodied asks the ParaStation system which child's death caused the
 *  last signal to be delivered to you.
 *
 * PARAMETERS
 * RETURN  0 on success
 *         -1 on error
 */
long PSI_whodied(int sig)
{
    DDSignalMsg_t msg;

    msg.header.type = PSP_DD_WHODIED;
    msg.header.sender = PSI_mytid;
    msg.header.dest = 0;
    msg.header.len = sizeof(msg);
    msg.signal = sig;

    if(ClientMsgSend(&msg)<0){
	return -1;
    }

    if(ClientMsgReceive(&msg)<0){
	return(-1);
    }

    return msg.header.sender;
}

/*----------------------------------------------------------------------*/
/*
 * PSI_getload()
 *
 *  PSI_getload asks the ParaStation system of the load for a given node.
 *
 * PARAMETERS
 *         nodenr the number of the node to be asked.
 * RETURN  the load of the given node
 *         -1 on error
 */
/* ToDo Norbert:  Gehoert eigentlich nach info.[ch] -> INFO_request_load */
double PSI_getload(u_short nodenr)
{
    DDBufferMsg_t msg;

    msg.header.type = PSP_CD_LOADREQ;
    msg.header.sender = PSI_mytid;
    msg.header.dest = PSI_gettid(nodenr,0);
    msg.header.len = sizeof(msg.header);

    if(ClientMsgSend(&msg)<0){
	return -1;
    }

    if(ClientMsgReceive(&msg)<0){
	return(-1);
    }

    if(msg.header.type == PSP_CD_LOADRES){
	return ((DDLoadMsg_t*)&msg)->load[0]; /* changed from 5min to 1 min avg load jh 2001-12-21 */
    }else if(msg.header.type == PSP_CD_LOADRES){
	errno =  ((DDErrorMsg_t*)&msg)->err;
    }
    return -1;
}

/* ToDo Jens: Gehoert hier auch nich hin :-) */
/* ToDo Norbert: Die Zahl der Prozesse auf node sollte im Daemon bekannt sein,
                 jedesmal die ganzen Tasks durch die Gegend zu schieben, ist
                 nicht sonderlich effektiv.
                         -> Neuer Msg_t, der dies liefert. */
double PSI_getNumberOfProcs(int node)
{
    DDBufferMsg_t msg;
    int msgtype;
    double ret = 0;
    msg.header.type = PSP_CD_TASKLISTREQUEST;
    msg.header.dest = PSI_gettid(node,0);
    msg.header.sender = PSI_mytid;
    msg.header.len = sizeof(msg);

    if(ClientMsgSend(&msg)<0){
	perror("write");
	exit(-1);
    }
    do{
	if(ClientMsgReceive(&msg)<0){
	    perror("read");
	    exit(-1);
	}
	msgtype = msg.header.type;
	if (msgtype == PSP_CD_TASKINFO ){
	    PStask_t* task;
	    task = PStask_new();
	    PStask_decode(msg.buf,task);
	    if (task->group!=TG_ADMIN){ /* dont count psiadmin */
		ret+=1.0;
	    }else{
		ret+=0.01;
	    }
	    PStask_delete(task);
	}
    }while(msgtype == PSP_CD_TASKINFO);
    return ret;
}



struct installdir_{
    int nr;
    char name[80];
} installdir[] = {
    { 0, "/opt/psm" },
    { 1, "/opt/PSM" },
    { 2, "/opt/parastation" },
    { 3, "/direct/psm" },
    { 4, "/direct/PSM" },
    { 5, "/direct/parastation" },
    { 7, "/usr/psm" },
    { 8, "/usr/PSM" },
    { 9, "/usr/opt/psm" },
    { 10, "/usr/opt/PSM" },
    { 11, "/usr/opt/PSM100" },
    { 12, "/PSM" },
    { -1, "" },
};

static char * PSI_installdir = NULL;

char * PSI_LookupInstalldir(void)
{
    int i=0, found=(PSI_installdir != NULL);
    char *name = NULL, logger[] = "/bin/psiforwarder";
    struct stat sbuf;

    while( (installdir[i].nr != -1) && !found ){
	if(!name)
	    name = (char*) malloc(sizeof(installdir[0].name) + strlen(logger)+1);
	strcpy(name, installdir[i].name);
	strcat(name, logger);
	if(stat(name, &sbuf) != -1){ /* Installdir found */
	    PSI_installdir = installdir[i].name;
	    found=1;
	}
	i++;
    }
    if(name){
	free(name);
	name=NULL;
    }

    return PSI_installdir;
}

void PSI_SetInstalldir(char * installdir)
{
    char *name, logger[] = "/bin/psiforwarder";
    static char *instdir=NULL;
    struct stat sbuf;

    name = (char*) malloc(strlen(installdir) + strlen(logger) + 1);
    strcpy(name,installdir);
    strcat(name,logger);
    if ( stat(name, &sbuf) == 0 ) { /* Installdir valid */
	if (instdir) free(instdir);
	instdir = (char*) malloc(strlen(installdir) + 1);
	strcpy(instdir, installdir);
	PSI_installdir = instdir;
    }
    free(name);
}
