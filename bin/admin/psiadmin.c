/*
 *               ParaStation3
 * psiadmin.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psiadmin.c,v 1.26 2002/02/12 19:07:35 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psiadmin.c,v 1.26 2002/02/12 19:07:35 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

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

static char psiadmversion[] = "$Revision: 1.26 $";
static int  DoRestart = 1;

int PSIADM_LookUpNodeName(char* hostname)
{
    struct hostent *hp;       /* host pointer */
    struct sockaddr_in sa;    /* socket address */

    if ((hp = gethostbyname(hostname)) == NULL) {
	return -1;
    }
    memcpy(&sa.sin_addr, hp->h_addr, hp->h_length);

    return INFO_request_host(sa.sin_addr.s_addr);
}

void PSIADM_AddNode(int first, int last)
{
    int i;
    DDContactMsg_t msg;

    msg.header.type = PSP_DD_CONTACTNODE;
    msg.header.len = sizeof(msg);
    msg.header.sender = PSI_mytid;
    msg.header.dest = PSI_gettid(PSI_myid,0);

    INFO_request_hoststatus(PSI_hoststatus, PSI_nrofnodes);

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSI_nrofnodes : last+1;
    for (i = first; i < last; i++) {
	if (PSI_hoststatus[i]&PSPHOSTUP) {
	    printf("%d already up.\n",i);
	} else {
	    printf("starting node %d\n",i);
	    msg.partner = i;
	    ClientMsgSend(&msg);
	}
    }

    /* check the success and repeat the startup */
    return;
}

void PSIADM_NodeStat(int first, int last)
{
    int i;

    INFO_request_hoststatus(PSI_hoststatus, PSI_nrofnodes);

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSI_nrofnodes : last+1;
    for (i = first; i < last; i++) {
	if (PSI_hoststatus[i]&PSPHOSTUP) {
	    printf("%d up.\n",i);
	} else {
	    printf("%d down.\n",i);
	}
    }

    return;
}

void PSIADM_RDPStat(int first, int last)
{
    int i;
    char s[255];

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSI_nrofnodes : last+1;
    for (i = first; i < last; i++) {
	INFO_request_rdpstatus(i,s,sizeof(s));
	printf("%s",s);
    }

    return;
}

void PSIADM_MCastStat(int first, int last)
{
    int i;
    char s[255];

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSI_nrofnodes : last+1;
    for (i = first; i < last; i++) {
	INFO_request_mcaststatus(i,s,sizeof(s));
	printf("%s",s);
    }

    return;
}

void PSIADM_CountStat(int first, int last)
{
    int i, j;
    struct {
	int present;
	PSHALInfoCounter_t ic;
    } countstat;

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSI_nrofnodes : last+1;

    /* Get header info from own daemon */
    for (i=0; i<PSI_nrofnodes; i++) {
	INFO_request_countstatus(i, &countstat, sizeof(countstat));
	if (countstat.present) break;
    }
    printf("%6s ", "NODE");
    for (j=0 ; j<countstat.ic.n; j++){
	printf("%8s ", countstat.ic.counter[j].name);
    }
    printf("\n");

    for (i = first; i < last; i++) {
	printf("%6u ", i); fflush(stdout);

	if (INFO_request_countstatus(i, &countstat, sizeof(countstat)) != -1) {
	    if (countstat.present) {
		for (j=0; j<countstat.ic.n; j++){
		    char ch[10];
		    /* calc column size from name length */
		    sprintf(ch, "%%%du ",
			    (int) MAX(strlen(countstat.ic.counter[j].name),8));
		    printf(ch, countstat.ic.counter[j].value);
		}
		printf("\n");
	    } else {
		printf("No card present\n");
	    }
	}
    }

    return;
}

#define NUMTASKS 20

void PSIADM_ProcStat(int first, int last)
{
    INFO_taskinfo_t taskinfo[NUMTASKS];
    int i, j, num;

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSI_nrofnodes : last+1;
    printf("%4s %23s %23s %8s\n",
	   "Node", "TaskId(Dec/Hex)", "ParentTaskId(Dec/Hex)", "UserId");
    for (i = first; i < last; i++) {
	printf("---------------------------------------------------------"
	       "----\n");
	num = INFO_request_tasklist(i, taskinfo, sizeof(taskinfo));
	for (j=0; j<MIN(num,NUMTASKS); j++) {
	    printf("%4d %10ld 0x%010lx %10ld 0x%010lx ",
		   taskinfo[j].nodeno, taskinfo[j].tid, taskinfo[j].tid,
		   taskinfo[j].ptid, taskinfo[j].ptid);
	    if (taskinfo[j].uid==-1) {
		printf("%8s\n", "NONE");
	    } else {
		printf("%5d%s\n", taskinfo[j].uid,
		       taskinfo[j].group==TG_ADMIN ? "(A)" :
		       taskinfo[j].group==TG_LOGGER ? "(L)" : "");
	    }
	}

	if (num>NUMTASKS) {
	    printf(" + %d more tasks\n", num-NUMTASKS);
	}
    }

    return;
}

void PSIADM_LoadStat(int first, int last)
{
    int i;
    double load;

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSI_nrofnodes : last+1;
    printf("NodeNr Load\n");
    for (i = first; i < last; i++) {
	load = INFO_request_load(i);
	printf("%6d %2.4f\n",i,load);
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

void PSIADM_SetPsidDebug(int val, int first, int last)
{
    int i;
    DDOptionMsg_t msg;

    if (geteuid()) {
	printf("Sorry, only root access\n");
	return;
    }

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSI_nrofnodes : last+1;

    /*
     * prepare the message to send it to the daemon
     */
    msg.header.type = PSP_DD_SETOPTION;
    msg.header.sender = PSI_mytid;
    msg.header.len = sizeof(msg);
    msg.count = 1;
    msg.opt[0].option = PSP_OP_PSIDDEBUG;
    msg.opt[0].value = val;

    for (i = first; i < last; i++) {
	msg.header.dest = PSI_gettid(i,0);
	ClientMsgSend(&msg);
    }

    return;
}

void PSIADM_SetRDPDebug(int val, int first, int last)
{
    int i;
    DDOptionMsg_t msg;

    if (geteuid()) {
	printf("Sorry, only root access\n");
	return;
    }

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSI_nrofnodes : last+1;

    /*
     * prepare the message to send it to the daemon
     */
    msg.header.type = PSP_DD_SETOPTION;
    msg.header.sender = PSI_mytid;
    msg.header.len = sizeof(msg);
    msg.count = 1;
    msg.opt[0].option = PSP_OP_RDPDEBUG;
    msg.opt[0].value = val;

    for (i = first; i < last; i++) {
	msg.header.dest = PSI_gettid(i, 0);
	ClientMsgSend(&msg);
    }

    return;
}

void PSIADM_SetMCastDebug(int val, int first, int last)
{
    int i;
    DDOptionMsg_t msg;

    if (geteuid()) {
	printf("Sorry, only root access\n");
	return;
    }

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSI_nrofnodes : last+1;

    /*
     * prepare the message to send it to the daemon
     */
    msg.header.type = PSP_DD_SETOPTION;
    msg.header.sender = PSI_mytid;
    msg.header.len = sizeof(msg);
    msg.count = 1;
    msg.opt[0].option = PSP_OP_MCASTDEBUG;
    msg.opt[0].value = val;

    for (i = first; i < last; i++) {
	msg.header.dest = PSI_gettid(i, 0);
	ClientMsgSend(&msg);
    }

    return;
}

void PSIADM_Version(void)
{
    printf("PSIADMIN: ParaStation administration tool\n");
    printf("Copyright (C) 1996-2002 ParTec AG Karlsruhe\n");
    printf("\n");
    printf("PSIADMIN: %s\b \n", psiadmversion+11);
    printf("PSID:     %s\b \n", PSI_psidversion+11);
    printf("PSILIB:   %d\n", PSPprotocolversion);
    return;
}

void PSIADM_ShowConfig(void)
{
    DDOptionMsg_t msg;
    int uidlimit=0, proclimit=0;
    int smallpacksize=0, resendtimeout=0, hnpend=0, ackpend=0;
    int i,n;

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

    msg.opt[(int) msg.count].option = PSP_OP_HNPEND;
    msg.count++;

    msg.opt[(int) msg.count].option = PSP_OP_ACKPEND;
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
	    case PSP_OP_UIDLIMIT:
		uidlimit = msg.opt[i].value;
		break;
	    case PSP_OP_PROCLIMIT:
		proclimit = msg.opt[i].value;
		break;
	    case PSP_OP_SMALLPACKETSIZE:
		smallpacksize = msg.opt[i].value;
		break;
	    case PSP_OP_RESENDTIMEOUT:
		resendtimeout = msg.opt[i].value;
		break;
	    case PSP_OP_HNPEND:
		hnpend = msg.opt[i].value;
		break;
	    case PSP_OP_ACKPEND:
		ackpend = msg.opt[i].value;
		break;
	    }
	}
    }
    printf("SmallPacketSize is %d\n", smallpacksize);
    printf("ResendTimeout is %d [us]\n", resendtimeout);
    printf("HNPend is %d\n", hnpend);
    printf("AckPend is %d\n", ackpend);
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
    msg.header.dest = -1 /* broadcast */;
    msg.count =1;
    msg.opt[0].option = PSP_OP_RESENDTIMEOUT;
    msg.opt[0].value = time;
    ClientMsgSend(&msg);

    return;
}

void PSIADM_SetHNPend(int val)
{
    DDOptionMsg_t msg;

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
    msg.header.dest = -1 /* broadcast */;
    msg.count =1;
    msg.opt[0].option = PSP_OP_HNPEND;
    msg.opt[0].value = val;
    ClientMsgSend(&msg);

    return;
}

void PSIADM_SetAckPend(int val)
{
    DDOptionMsg_t msg;

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
    msg.header.dest = -1 /* broadcast */;
    msg.count =1;
    msg.opt[0].option = PSP_OP_ACKPEND;
    msg.opt[0].value = val;
    ClientMsgSend(&msg);

    return;
}

/*
 *   what : 1=HW,2 = SHM
 *   first: first node to be reset
 *   last : last node to be reset
 */
void PSIADM_Reset(int reset_hw, int first, int last)
{
    DDResetMsg_t msg;

    if (geteuid()) {
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
    msg.first = (first==ALLNODES) ? 0 : first;
    msg.last = (last==ALLNODES) ? PSI_nrofnodes-1 : last;
    msg.action = 0;
    if(reset_hw) msg.action |= PSP_RESET_HW;

    ClientMsgSend(&msg);

    return;
}

void PSIADM_ShutdownCluster(int first, int last)
{
    int nrofnodes;
    DDResetMsg_t msg;

    if (geteuid()) {
	printf("Insufficient priviledge\n");
	return;
    }

    nrofnodes = PSI_nrofnodes;

    DoRestart = 0;

    msg.header.type = PSP_CD_DAEMONSTOP;
    msg.header.len = sizeof(msg);
    msg.header.sender = PSI_mytid;
    msg.header.dest = PSI_gettid(PSI_myid,0);
    msg.first = (first==ALLNODES) ? 0 : first;
    msg.last = (last==ALLNODES) ? PSI_nrofnodes : last;
    msg.action = 0;

    ClientMsgSend(&msg);
}

void PSIADM_TestNetwork(int mode)
{
    int mynode;
    int spawnargc;
    char** spawnargs;
    long tid;

    /* printf("TestNetwork\n"); */
    if(geteuid()){
	printf("Insufficient priviledge\n");
	return;
    }
    mynode = PSI_myid;
    spawnargc = 2;
    spawnargs = (char**) malloc(spawnargc*sizeof(char*));

    spawnargs[0]="psiconntest";
    switch (mode) {
    case 0:
	spawnargs[1]="-q";
	break;
    case 1:
	spawnargs[1]="-o";
	break;
    case 2:
	spawnargs[1]="-v";
	break;
    default:
	spawnargs[1]="-o";
    }
    tid = PSI_spawn(mynode, PSI_LookupInstalldir(),
		    spawnargc,spawnargs,-1,-1,0,&errno);
    if(tid<0) {
	char *txt=NULL;
	txt=strerror(errno);
	printf("Couln't spawn the test task. Error <%d>: %s\n",
	       errno,txt!=NULL?txt:"UNKNOWN");
    } else {
	printf("Spawning test task successfull.\n");
    }

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
	if (DoRestart==0) {
	    fprintf(stderr, "\nPSIadmin: Got SIGTERM .... exiting"
		    " ...This seem to be OK. \nBye..\n");
	    exit(0);
	}
	fprintf(stderr,"\nPSIadmin: Got SIGTERM .... exiting"
		" ...wait for a reconnect..\n");
	PSI_clientexit();
	sleep(2);
	fprintf(stderr,"PSIadmin: Restarting...\n");
	if (!PSI_clientinit(TG_ADMIN)) {
	    fprintf(stderr,"can't contact my own daemon.\n");
	    exit(-1);
        }
	signal(SIGTERM,sighandler);

	break;
    }
}

/*
 * Print version info
 */
static void version(void)
{
    fprintf(stderr, "psiadmin %s\b \n", psiadmversion+11);
}

/*
 * Print usage message
 */
static void usage(void)
{
    fprintf(stderr,"usage: psiadmin [-h] [-v] [-c command] [-r]\n");
}

/*
 * Print more detailed help message
 */
static void help(void)
{
    usage();
    fprintf(stderr,"\n");
    fprintf(stderr," -r         : Do reset on startup (only as root).\n");
    fprintf(stderr," -c command : Execute a single command and exit.\n");
    fprintf(stderr," -v,      : output version information and exit.\n");
    fprintf(stderr," -h,      : display this help and exit.\n");
}

int main(int argc, char **argv)
{
    void *line_state = NULL;
    char *copt = NULL, *line = (char *) NULL, line_field[256];
    int opt, len, reset=0;

    while ((opt = getopt(argc, argv, "hHvVc:r")) != -1) {
	switch (opt) {
	case 'c':
	    copt = optarg;
	    break;
	case 'r':
	    reset=1;
	    break;
	case 'h':
	case 'H':
	    help();
	    return 0;
	    break;
	case 'v':
	case 'V':
	    version();
	    return 0;
	    break;
	default:
	    usage();
	    return -1;
	}
    }

    if (reset) {
	if (geteuid()) {
	    printf("Insufficient priviledge for resetting\n");
	    exit(-1);
	}
	printf("Initiating RESET.\n"); fflush(stdout);
	PSI_clientinit(TG_RESETABORT);
	PSI_clientexit();
	printf("Waiting for reset.\n"); fflush(stdout);
	sleep(1);
	printf("Trying to reconnect.\n"); fflush(stdout);
	PSI_clientinit(TG_RESET);
	printf("Resetting done. Please try to connect regulary\n");
	fflush(stdout);
	exit(0);
    }

    if (!PSI_clientinit(TG_ADMIN)) {
	fprintf(stderr,"can't contact my own daemon.\n");
	exit(-1);
    }

    signal(SIGTERM,sighandler);

    /*
     * Single command processing
     */
    if (copt) {
	/* Add some trailing newlines. Needed for NULLOP */
	len = strlen(copt);
	line = (char *)malloc(len+2);
	strcpy(line, copt);
	line[len]   = '\n';
	line[len+1] = '\0';

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

    while (!PARSE_DONE) {
	/* Get a line from the user. */
	line = readline("PSIadmin>");

	if (line && *line) {
	    /* If the line has any text in it, save it on the history. */
	    add_history(line);

	    if (strlen(line) + 2 > sizeof(line_field)) {
		printf("Line too long!\n");
	    } else {
		strcpy(line_field, line);
		/* Add some trailing newlines. Needed for NULLOP */
		len = strlen(line_field);
		line_field[len]   = '\n';
		line_field[len+1] = '\0';
		/* Process it */
		line_state = yy_scan_string(line_field);
		yyparse();
		yy_delete_buffer(line_state);
	    }
	}
	free(line);
    }

    printf("PSIadmin: Goodbye\n");

    return 0;
}
