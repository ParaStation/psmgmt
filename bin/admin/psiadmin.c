/*
 * psiadmin: ParaStation Administration tool
 *
 * (C) 1995-1999 ParTec AG Karlsruhe
 *
 * written by Thomas M. Warschko & Joachim M. Blum
 *
 */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netdb.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <readline/readline.h>
#include <readline/history.h>

#include <psport.h>

#include "info.h"
#include "psi.h"
#include "psp.h"
#include "psilog.h"
#include "psispawn.h"

#include "psiadmin.h"

static int PARSE_DONE = 0;

void *yy_scan_string(char *line);
void yyparse(void);
void yy_delete_buffer(void *line_state);

static char psiadmversion[] = "2.9";
static int  DoRestart = 1;

int PSIADM_LookUpNodeName(char* hostname)
{
    struct hostent	*hp;	/* host pointer */
    struct sockaddr_in sa;	/* socket address */ 

    if ((hp = gethostbyname(hostname)) == NULL){
	return -1;
    }
    bcopy((char *)hp->h_addr, (char *)&sa.sin_addr, hp->h_length); 

    return INFO_request_host(sa.sin_addr.s_addr);
}

void PSIADM_AddNode(int node)
{
    int i;
    DDContactMsg_t msg;

    msg.header.type = PSP_DD_CONTACTNODE;
    msg.header.len = sizeof(msg);
    msg.header.sender = PSI_mytid;
    msg.header.dest = PSI_gettid(PSI_myid,0);

    INFO_request_hoststatus(PSI_hoststatus, PSI_nrofnodes);

    if(node== ALLNODES)
	for(i=0; i<PSI_nrofnodes; i++){
	    if(PSI_hoststatus[i]&PSPHOSTUP)
		printf("%d already up.\n",i);
	    else{
		printf("starting node %d\n",i);
		msg.partner = i;
		ClientMsgSend(&msg);
	    }
	}
    else{
	if(PSI_hoststatus[node]&PSPHOSTUP)
	    printf("%d already up.\n",node);
	else{
	    printf("starting node %d\n",node);
	    msg.partner = node;
	    ClientMsgSend(&msg);
	}
    }

    /* check the success and repeat the startup */
    return;
}

void PSIADM_NodeStat(int node)
{
    int i;

    INFO_request_hoststatus(PSI_hoststatus, PSI_nrofnodes);

/*    printf("NodeStat %d\n",node); */
    if(node== ALLNODES)
	for(i=0;i<PSI_nrofnodes;i++){
	    if(PSI_hoststatus[i]&PSPHOSTUP)
		printf("%d up.\n",i);
	    else
		printf("%d down.\n",i);
	}
    else if(PSI_hoststatus[node]&PSPHOSTUP)
	printf("%d up.\n",node);
    else
	printf("%d down.\n",node);

    return;
}

void PSIADM_RDPStat(int node)
{
    int i;
    char s[255];
/*    printf("RDPStat %d\n",node); */

    if(node== ALLNODES)
	for(i=0;i<PSI_nrofnodes;i++){
	    INFO_request_rdpstatus(i,s,sizeof(s));
	    printf("%s",s);
	}
    else{
	INFO_request_rdpstatus(node,s,sizeof(s));
	printf("%s",s);
    }

    return;
}

void PSIADM_CountStat(int node)
{
    int i;
/*    printf("CountStat %d\n",node); */
    
    if(node== ALLNODES){
	INFO_request_countstatus(0,1); /* node 0 with header */
	for(i=1;i<PSI_nrofnodes;i++){
	    INFO_request_countstatus(i,0); /* all other without header */
	}
    }else{
	INFO_request_countstatus(node,1); /* with header */
    }

    return;
}


void PSIADM_ProcStat(int node)
{
    int i;
    printf("NodeNr TaskId(Dec/Hex)     ParentTaskId(Dec/Hex) UserId\n");
    if(node== ALLNODES)
	for(i=0;i<PSI_nrofnodes;i++){
	    INFO_request_tasklist(i);
	}
    else{
	INFO_request_tasklist(node);
    }

    return;
}

void PSIADM_LoadStat(int node)
{
    int i;
/*    printf("LoadStat %d\n",node); */

    double load;

    printf("NodeNr Load\n");
    if(node== ALLNODES)
	for(i=0;i<PSI_nrofnodes;i++){
	    load = PSI_getload(i);
	    printf("%6d %2.4f\n",i,load);
	}
    else{
	load = PSI_getload(node);
	printf("%6d %2.2f\n",node,load);
    }

    return;
}

void PSIADM_SetMaxProc(int count)
{
    DDOptionMsg_t msg;

    if(geteuid()){
	printf("Sorry, only root access\n");
	return;
    }

    /*
     * prepare the message to send it to the daemon
     */
    msg.header.type = PSP_DD_SETOPTION;
    msg.header.len = sizeof(msg);
    msg.header.sender = PSI_mytid;
    /*msg.header.dest = PULC_gettid(PSI_myid,0);*/
    msg.header.dest = -1 /* broadcast */;
    msg.count =1;
    msg.opt[0].option = PSP_OP_PROCLIMIT;
    msg.opt[0].value = count;
    ClientMsgSend(&msg);

    return;
}

void PSIADM_SetUser(int uid)
{
    DDOptionMsg_t msg;

    if(geteuid()){
	printf("Sorry, only root access\n");
	return;
    }
    /*
     * prepare the message to send it to the daemon
     */
    msg.header.type = PSP_DD_SETOPTION;
    msg.header.len = sizeof(msg);
    msg.header.sender = PSI_mytid;
    /*msg.header.dest = PULC_gettid(PSI_myid,0);*/
    msg.header.dest = -1 /* broadcast */;
    msg.count =1;
    msg.opt[0].option = PSP_OP_UIDLIMIT;
    msg.opt[0].value = uid;
    ClientMsgSend(&msg);

    return;
}

void PSIADM_SetDebugmask(long newmask)
{
    printf("debugmask was %lx\n",PSI_debugmask);

    if(geteuid()){
	printf("Sorry, only root access\n");
	return;
    }
    printf("NOT IMPLEMENTED YET!!\n");
    return;

    /* TODO Norbert: Neue debugmask an daemon senden! Neue Nachricht!! */
    PSI_debugmask = newmask;
    printf("debugmask is now %lx\n",PSI_debugmask);
    return;
}

void PSIADM_SetPsidDebug(int val, int node)
{
    DDOptionMsg_t msg;

    if(geteuid()){
	printf("Sorry, only root access\n");
	return;
    }
    /*
     * prepare the message to send it to the daemon
     */
    msg.header.type = PSP_DD_SETOPTION;
    msg.header.sender = PSI_mytid;
    msg.header.dest = PSI_gettid(node,0);
    msg.header.len = sizeof(msg);
    msg.count =1;
    msg.opt[0].option = PSP_OP_PSIDDEBUG;
    msg.opt[0].value = val;

    ClientMsgSend(&msg);

    return;
}

void PSIADM_SetRdpDebug(int val, int node)
{
    DDOptionMsg_t msg;

    if(geteuid()){
	printf("Sorry, only root access\n");
	return;
    }
    /*
     * prepare the message to send it to the daemon
     */
    msg.header.type = PSP_DD_SETOPTION;
    msg.header.sender = PSI_mytid;
    if(node==-1)
	msg.header.dest = -1;
    else
	msg.header.dest = PSI_gettid(node,0);
    msg.header.len = sizeof(msg);
    msg.count =1;
    msg.opt[0].option = PSP_OP_RDPDEBUG;
    msg.opt[0].value = val;

    ClientMsgSend(&msg);

    return;
}

void PSIADM_Version(void)
{
    printf("PSIADMIN: ParaStation administration tool\n");
    printf("Copyright (C) 1996-2002 ParTec AG Karlsruhe\n");
    printf("\n");
    printf("PSIADMIN: version %s\n", psiadmversion);
    printf("PSID:     version %s\n", PSI_psidversion);
    printf("PSILIB:   version %d\n", PSPprotocolversion);
    return;
}

void PSIADM_ShowParameter(void) 
{
    DDOptionMsg_t msg;
    int uidlimit=0, proclimit=0, smallpacksize=0, resendtimeout=0;
    int i,n;

/*   printf("ShowParameter\n"); */

    /*
     * prepare the message to send it to the daemon
     */
    msg.header.type = PSP_DD_GETOPTION;
    msg.header.len = sizeof(msg);
    msg.header.sender = PSI_mytid;
    msg.header.dest = PSI_gettid(PSI_myid,0);
    msg.opt[0].value = 0;

    msg.count = 0;
    msg.opt[(int) msg.count].option = PSP_OP_UIDLIMIT;
    msg.count++;

    msg.opt[(int) msg.count].option = PSP_OP_PROCLIMIT;
    msg.count++;

    msg.opt[(int) msg.count].option = PSP_OP_SMALLPACKETSIZE;
    msg.count++;

    msg.opt[(int) msg.count].option = PSP_OP_RESENDTIMEOUT;
    msg.count++;

    ClientMsgSend(&msg);

    if ((n=ClientMsgReceive(&msg)) == 0){
	/*
	 * closing connection 
	 */
	printf("PANIC: lost connection to my daemon!!");
	exit(1);
    }else if(n<0)
	perror("PANIC: error while receiving answer from my daemon.\n");
    else{
	for(i=0; i<msg.count; i++){
	    switch(msg.opt[i].option){
	    case PSP_OP_UIDLIMIT :
		uidlimit = msg.opt[i].value;
		break;
	    case PSP_OP_PROCLIMIT :
		proclimit = msg.opt[i].value;
		break;
	    case PSP_OP_SMALLPACKETSIZE :
		smallpacksize = msg.opt[i].value;
		break;
	    case PSP_OP_RESENDTIMEOUT :
		resendtimeout = msg.opt[i].value;
		break;
	    }
	}
    }
    printf("SmallPacketSize is %d\n",smallpacksize);
    printf("ResendTimeout is %d [us]\n",resendtimeout);
    if(uidlimit==-1)
	printf("max. processes: NONE\n");
    else
	printf("max. processes: %d\n",proclimit);
    if(uidlimit==-1)
	printf("limited to user : NONE\n");
    else
	printf("limited to user : %d\n",uidlimit);

    return;
}

void PSIADM_SetSmallPacketSize(int smallpacketsize) 
{
    DDOptionMsg_t msg;

/*   printf("SetSmallPacketSize to %d us\n",smallpacketsize); */
    if(geteuid()){
	printf("Insufficient priviledge\n");
	return;
    }

    /*
     * prepare the message to send it to the daemon
     */
    msg.header.type = PSP_DD_SETOPTION;
    msg.header.len = sizeof(msg);
    msg.header.sender = PSI_mytid;
    /*msg.header.dest = PSI_gettid(PSI_myid,0);*/
    msg.header.dest = -1 /* broadcast */;
    msg.count =1;
    msg.opt[0].option = PSP_OP_SMALLPACKETSIZE;
    msg.opt[0].value = smallpacketsize;
    ClientMsgSend(&msg);

    return;
}

void PSIADM_SetResendTimeout(int time) 
{
    DDOptionMsg_t msg;

/*   printf("SetResendTimeout to %d [us]\n",time); */
    if(geteuid()){
	printf("Insufficient priviledge\n");
	return;
    }

    /*
     * prepare the message to send it to the daemon
     */
    msg.header.type = PSP_DD_SETOPTION;
    msg.header.len = sizeof(msg);
    msg.header.sender = PSI_mytid;
    /*msg.header.dest = PSI_gettid(PSI_myid,0);*/
    msg.header.dest = -1 /* broadcast */;
    msg.count =1;
    msg.opt[0].option = PSP_OP_RESENDTIMEOUT;
    msg.opt[0].value = time;
    ClientMsgSend(&msg);

    return;
}

/*
 *   what : 1=HW,2 = SHM
 *   first: first node to be reset
 *   last : last node to be reset
 */
void PSIADM_Reset(int what,int first, int last) 
{
    DDResetMsg_t msg;

    if(geteuid()){
	printf("Insufficient priviledge\n");
	return;
    }
    /*
     * prepare the message to send it to the daemon
     */
    /*  msg.header.type = PSP_CD_RESET_START_REQ;*/
    msg.header.type = PSP_CD_RESET;
    msg.header.len = sizeof(msg);
    msg.header.sender = PSI_mytid;
    msg.header.dest = PSI_gettid(PSI_myid,0);
    msg.first = first;
    msg.last = last==-1?PSI_nrofnodes-1:last;
    msg.action = 0;
    if(what&1) msg.action |= PSP_RESET_HW;

    ClientMsgSend(&msg);

    return;
}

void PSIADM_ShutdownCluster(int first,int last) 
{
    int nrofnodes;
    DDResetMsg_t msg;

    if(geteuid()){
	printf("Insufficient priviledge\n");
	return;
    }

    nrofnodes = PSI_nrofnodes;

    DoRestart = 0;

    msg.header.type = PSP_CD_DAEMONSTOP;
    msg.header.len = sizeof(msg);
    msg.header.sender = PSI_mytid;
    msg.header.dest = PSI_gettid(PSI_myid,first);
    msg.first = first==-1?0:first;
    msg.last = first==-1?PSI_nrofnodes-1:last;
    msg.action = 0;

    ClientMsgSend(&msg);
}

void PSIADM_TestNetwork(int mode) 
{
    int mynode;
    int spawnargc;
    char** spawnargs;
    long tid;

/*    printf("TestNetwork\n"); */
    if(geteuid()){
	printf("Insufficient priviledge\n");
	return;
    }
    mynode = PSI_myid;
    spawnargc = 2;
    spawnargs = (char**) malloc(spawnargc*sizeof(char*));

    spawnargs[0]="psiconntest";
    switch(mode){
    case 0: spawnargs[1]="-q";break;
    case 1: spawnargs[1]="-o";break;
    case 2: spawnargs[1]="-v";break;
    default: spawnargs[1]="-o";break;
    }
    tid = PSI_spawn(mynode, PSI_LookupInstalldir(),
		    spawnargc,spawnargs,-1,-1,0,&errno);
    if(tid<0){
	char *txt=NULL;
	txt=strerror(errno);
	printf("Couln't spawn the test task. Error <%d>: %s\n",
	       errno,txt!=NULL?txt:"UNKNOWN");
    }else
	printf("Spawning test task successfull.\n");

    free(spawnargs);
    return;
}

void PSIADM_KillProc(int id)
{
    PSI_kill(id,SIGTERM);
    return;
}

void PSIADM_Exit(void)
{
    PARSE_DONE = 1;
    return;
}

void sighandler(int sig)
{
    switch(sig){
    case SIGTERM:
	if(DoRestart==0){
	    fprintf(stderr, "\nPSIadmin: Got SIGTERM .... exiting"
		    " ...This seem to be OK. \nBye..\n");
	    exit(0);
	}
	fprintf(stderr,"\nPSIadmin: Got SIGTERM .... exiting"
		" ...wait for a reconnect..\n");
	PSI_clientexit();
	sleep(2);
	fprintf(stderr,"PSIadmin: Restarting...\n");
	if(!PSI_clientinit(TG_ADMIN))
      	{
	    fprintf(stderr,"can't contact my own daemon.\n");
	    exit(-1);
        }
	signal(SIGTERM,sighandler);

	break;
    }
}

void usage(void)
{
    fprintf(stderr,"usage: psiadmin [-h] [-r] [-c command]\n");
}

/******************************************
 *  help()
 */
void help(void)
{
    usage();
    fprintf(stderr,"\n");
    fprintf(stderr," -r         : Do reset on startup (only as root).\n");
    fprintf(stderr," -c command : Execute a single command and exit.\n");
    fprintf(stderr," -h         : print this screen and exit.\n");
}

int main(int argc, char **argv)
{
    void *line_state = NULL;
    char *copt = NULL, *line = (char *) NULL, line_field[256];
    int opt, len, reset=0;

    while ((opt = getopt(argc, argv, "c:rh")) != -1){
	switch(opt){
	case 'c':
	    copt = optarg;
	    break;
	case 'r':
	    reset=1;
	    break;
	case 'h':
	    help();
	    return 0;
	    break;
	default:
	    usage();
	    return -1;
	}
    }

    if(reset){
	if(geteuid()){
	    printf("Insufficient priviledge for resetting\n");
	    exit(-1);
	}
	printf("Initiating RESET.\n");fflush(stdout);
	PSI_clientinit(TG_RESETABORT);
	PSI_clientexit();
	printf("Waiting for reset.\n");fflush(stdout);
	sleep(1);
	printf("Trying to reconnect.\n");fflush(stdout);
	PSI_clientinit(TG_RESET);
	printf("Resetting done. Please try to connect regulary\n");
	fflush(stdout);
	exit(0);
    }

    if(!PSI_clientinit(TG_ADMIN)){
	fprintf(stderr,"can't contact my own daemon.\n");
	exit(-1);
    }

    signal(SIGTERM,sighandler);

    /*
     * Single command processing
     */
    if(copt){
	/* Add trailing newline */
	len=strlen(copt);
	line=(char *)malloc(len+2);
	strcpy(line, copt);
	line[len]='\n';
	line[len+1]='\0';

	/* Process it */
	line_state = yy_scan_string(line);
	yyparse();
	yy_delete_buffer(line_state);

	free(line);

	return 0;
    }

    /*
     * Interactive mode
     */
    using_history();
    add_history("shutdown");

    while(!PARSE_DONE){ 
	/* Get a line from the user. */
	line = readline("PSIadmin>");

	if (line && *line){
	    /* If the line has any text in it, save it on the history. */
	    add_history(line);

	    if(strlen(line) + 2 > sizeof(line_field)){
		printf("Line too long!\n");
	    }else{
		strcpy(line_field, line);
		/* Add trailing newline */
		len=strlen(line_field);
		line_field[len]='\n';
		line_field[len+1]='\0';
		/* Process it */
		line_state = yy_scan_string(line_field);
		yyparse();
		yy_delete_buffer(line_state);
	    }
	}
	free(line);
    };

    printf("PSIadmin: Goodbye\n");

    return 0;
}
