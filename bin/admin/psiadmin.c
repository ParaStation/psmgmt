/*
 *               ParaStation3
 * psiadmin.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psiadmin.c,v 1.61 2003/06/25 16:47:54 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psiadmin.c,v 1.61 2003/06/25 16:47:54 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netdb.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>
#include <termios.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <popt.h>

#include <psport.h>

#include "pscommon.h"
#include "psprotocol.h"
#include "pshwtypes.h"
#include "pstask.h"

#include "psi.h"
#include "info.h"
#include "psispawn.h"

#include "psiadmin.h"

static int PARSE_DONE = 0;

#define yy_scan_string admin_scan_string
#define yy_delete_buffer admin_delete_buffer

void *yy_scan_string(char *line);
void yyparse(void);
void yy_delete_buffer(void *line_state);

static char psiadmversion[] = "$Revision: 1.61 $";
static int doRestart = 0;

static char *hoststatus = NULL;

static NodelistEntry_t *nodelist = NULL;
static size_t nodelistSize = 0;


/* @todo PSI_sendMsg(): Wrapper, control if sendMsg was successful or exit */

int PSIADM_LookUpNodeName(char* hostname)
{
    struct hostent *hp;       /* host pointer */
    struct sockaddr_in sa;    /* socket address */

    if ((hp = gethostbyname(hostname)) == NULL) {
	return -1;
    }
    memcpy(&sa.sin_addr, hp->h_addr, hp->h_length);

    return INFO_request_host(sa.sin_addr.s_addr, 1);
}

void PSIADM_AddNode(int first, int last)
{
    int i;
    DDBufferMsg_t msg;

    msg.header.type = PSP_CD_DAEMONSTART;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = PSC_getTID(-1, 0);
    msg.header.len = sizeof(msg.header) + sizeof(long);

    INFO_request_hoststatus(hoststatus, PSC_getNrOfNodes(), 1);

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;
    for (i = first; i < last; i++) {
	if (hoststatus[i]) {
	    printf("%d already up.\n",i);
	} else {
	    printf("starting node %d\n",i);
	    *(long *)msg.buf = i;
	    PSI_sendMsg(&msg);
	}
    }

    /* @todo check the success and repeat the startup */
    return;
}

void PSIADM_NodeStat(int first, int last)
{
    int i;

    INFO_request_hoststatus(hoststatus, PSC_getNrOfNodes(), 1);

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;
    for (i = first; i < last; i++) {
	if (hoststatus[i]) {
	    printf("%4d up.\n",i);
	} else {
	    printf("%4d down.\n",i);
	}
    }

    return;
}

static char statusline[1024];

void PSIADM_RDPStat(int first, int last)
{
    int i;

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;
    for (i = first; i < last; i++) {
	INFO_request_rdpstatus(i, statusline, sizeof(statusline), 1);
	printf("%s", statusline);
    }

    return;
}

void PSIADM_MCastStat(int first, int last)
{
    int i;

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;
    for (i = first; i < last; i++) {
	INFO_request_mcaststatus(i, statusline, sizeof(statusline), 1);
	printf("%s", statusline);
    }

    return;
}

void PSIADM_CountStat(int first, int last)
{
    int i, hw, hwnum;
    unsigned int j;

    INFO_request_nodelist(nodelist, nodelistSize, 1);

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    hwnum = INFO_request_hwnum(1);

    for (hw=0; hw<hwnum; hw++) {
	int ret = 0;
	for (i=first; i<last; i++) {
	    if (nodelist[i].up && nodelist[i].hwStatus & 1<<hw) {
		ret = INFO_request_countheader(i, hw, &statusline,
					       sizeof(statusline), 1);
		if (ret) {
		    char *hwname = INFO_request_hwname(hw, 1);
		    int last = strlen(statusline)-1;

		    printf("Counter for hardware type '%s':\n\n",
			   hwname ? hwname : "unknown");
		    printf("%6s ", "NODE");

		    if (statusline[(last>0) ? last : 0] == '\n') {
			printf("%s", statusline);
		    } else {
			printf("%s\n", statusline);
		    }
		    break;
		}
	    }
	}

	if (ret) {
	    for (i=first; i<last; i++) {
		printf("%6d ", i);

		if (nodelist[i].up) {
		    if (! (nodelist[i].hwStatus & 1<<hw)) {
			printf("    No card present\n");
		    } else {
			ret = INFO_request_countstatus(i, hw, &statusline,
						       sizeof(statusline), 1);
			if (ret) {
			    int last = strlen(statusline)-1;
			    if (statusline[(last>0) ? last : 0] == '\n') {
				printf("%s", statusline);
			    } else {
				printf("%s\n", statusline);
			    }
			} else {
			    printf("    Counter unavailable\n");
			}
		    }
		} else {
		    printf("\tdown\n");
		}
	    }
	}
	printf("\n");
    }

    return;
}

#define NUMTASKS 20

void PSIADM_ProcStat(int first, int last, int full)
{
    INFO_taskinfo_t taskinfo[NUMTASKS];
    int i, j, num;

    INFO_request_hoststatus(hoststatus, PSC_getNrOfNodes(), 1);

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    printf("%4s %22s %22s %3s %9s\n", "Node", "TaskId",
	   "ParentTaskId", "Con", "UserId");
    for (i = first; i < last; i++) {
	printf("---------------------------------------------------------"
	       "---------\n");
	if (hoststatus[i]) {
	    num = INFO_request_tasklist(i, taskinfo, sizeof(taskinfo), 1);
	    for (j=0; j<MIN(num,NUMTASKS); j++) {
		if (taskinfo[j].group==TG_FORWARDER && !full) continue;
		if (taskinfo[j].group==TG_SPAWNER && !full) continue;
		if (taskinfo[j].group==TG_GMSPAWNER && !full) continue;
		printf("%4d ", i);
		printf("%22s ", PSC_printTID(taskinfo[j].tid));
		printf("%22s ", PSC_printTID(taskinfo[j].ptid));
		printf("%2d  %5d ", taskinfo[j].connected, taskinfo[j].uid);
		printf("%s\n",
		       taskinfo[j].group==TG_ADMIN ? "(A)" :
		       taskinfo[j].group==TG_LOGGER ? "(L)" :
		       taskinfo[j].group==TG_FORWARDER ? "(F)" :
		       taskinfo[j].group==TG_SPAWNER ? "(S)" :
		       taskinfo[j].group==TG_GMSPAWNER ? "(S)" : "");
	    }
	    if (num>NUMTASKS) {
		printf(" + %d more tasks\n", num-NUMTASKS);
	    }
	} else {
	    printf("%4d\tdown\n", i);
	}
    }

    return;
}

void PSIADM_LoadStat(int first, int last)
{
    int i;

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    INFO_request_nodelist(nodelist, nodelistSize, 1);
    printf("Node\t\t Load\t\t     Jobs\n");
    printf("\t 1 min\t 5 min\t15 min\t tot.\tnorm.\n");
    for (i = first; i < last; i++) {
	if (nodelist[i].up) {
	    printf("%4d\t%2.4f\t%2.4f\t%2.4f\t%4d\t%4d\n", i,
		   nodelist[i].load[0], nodelist[i].load[1],
		   nodelist[i].load[2],
		   nodelist[i].totalJobs, nodelist[i].normalJobs);
	} else {
	    printf("%4d\t down\n", i);
	}
	
    }

    return;
}

void PSIADM_HWStat(int first, int last)
{
    int i;

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    INFO_request_nodelist(nodelist, nodelistSize, 1);
    printf("Node\t CPUs\t Available Hardware\n");
    for (i = first; i < last; i++) {
	if (nodelist[i].up) {
	    printf("%4d\t %d\t %s\n",
		   i, nodelist[i].numCPU,
		   INFO_printHWType(nodelist[i].hwStatus));
	} else {
	    printf("%4d\t down\n", i);
	}
	
    }

    return;
}

void PSIADM_SetMaxProc(int count, int first, int last)
{
    int i;
    DDOptionMsg_t msg;

    if(geteuid()){
	printf("Sorry, only root access\n");
	return;
    }

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    /*
     * prepare the message to send it to the daemon
     */
    msg.header.type = PSP_CD_SETOPTION;
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg);
    msg.count = 1;
    msg.opt[0].option = PSP_OP_PROCLIMIT;
    msg.opt[0].value = count;

    for (i = first; i < last; i++) {
	msg.header.dest = PSC_getTID(i, 0);
	PSI_sendMsg(&msg);
    }

    return;
}

void PSIADM_ShowMaxProc(int first, int last)
{
    int i, ret;
    long option = PSP_OP_PROCLIMIT, proclimit;

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    for (i = first; i < last; i++) {
	printf("%3d:  ", i);
	ret = INFO_request_option(i, 1, &option, &proclimit, 1);

	if (ret==-1) {
	    printf("Can't get max. processes.\n");
	} else if (proclimit==-1) {
	    printf("max. processes: ANY\n");
	} else {
	    printf("max. processes: %ld\n", proclimit);
	}
    }

    return;
}

void PSIADM_SetUser(int uid, int first, int last)
{
    int i;
    DDOptionMsg_t msg;

    if(geteuid()){
	printf("Sorry, only root access\n");
	return;
    }

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    /*
     * prepare the message to send it to the daemon
     */
    msg.header.type = PSP_CD_SETOPTION;
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg);
    msg.count = 1;
    msg.opt[0].option = PSP_OP_UIDLIMIT;
    msg.opt[0].value = uid;

    for (i = first; i < last; i++) {
	msg.header.dest = PSC_getTID(i, 0);
	PSI_sendMsg(&msg);
    }

    return;
}

void PSIADM_ShowUser(int first, int last)
{
    int i, ret;
    long option = PSP_OP_UIDLIMIT, uidlimit;

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    for (i = first; i < last; i++) {
	printf("%3d:  ", i);
	ret = INFO_request_option(i, 1, &option, &uidlimit, 1);

	if (ret==-1) {
	    printf("Can't get user limit.\n");
	} else if (uidlimit==-1) {
	    printf("limited to user: ANY\n");
	} else {
	    char *name;
	    struct passwd *passwd;

	    passwd = getpwuid(uidlimit);
	    if (passwd) {
		name = strdup(passwd->pw_name);
	    } else {
		name = malloc(10*sizeof(char));
		sprintf(name, "%ld", uidlimit);
	    }
	    printf("limited to user: %s\n", name);
	    free(name);
	}
    }

    return;
}

void PSIADM_SetGroup(int gid, int first, int last)
{
    int i;
    DDOptionMsg_t msg;

    if(geteuid()){
	printf("Sorry, only root access\n");
	return;
    }

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    /*
     * prepare the message to send it to the daemon
     */
    msg.header.type = PSP_CD_SETOPTION;
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg);
    msg.count = 1;
    msg.opt[0].option = PSP_OP_GIDLIMIT;
    msg.opt[0].value = gid;

    for (i = first; i < last; i++) {
	msg.header.dest = PSC_getTID(i, 0);
	PSI_sendMsg(&msg);
    }

    return;
}

void PSIADM_ShowGroup(int first, int last)
{
    int i, ret;
    long option = PSP_OP_GIDLIMIT, gidlimit;

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    for (i = first; i < last; i++) {
	printf("%3d:  ", i);
	ret = INFO_request_option(i, 1, &option, &gidlimit, 1);

	if (ret==-1) {
	    printf("Can't get group limit.\n");
	} else if (gidlimit==-1) {
	    printf("limited to group: ANY\n");
	} else {
	    char *name;
	    struct group *group;

	    group = getgrgid(gidlimit);
	    if (group) {
		name = strdup(group->gr_name);
	    } else {
		name = malloc(10*sizeof(char));
		sprintf(name, "%ld", gidlimit);
	    }
	    printf("limited to group: %s\n", name);
	    free(name);
	}
    }

    return;
}

void PSIADM_SetParam(int type, int value, int first, int last)
{
    int i;
    DDOptionMsg_t msg;

    if(geteuid()){
	printf("Insufficient priviledge\n");
	return;
    }

    switch (type) {
    case PSP_OP_PSIDSELECTTIME:
    case PSP_OP_PSIDDEBUG:
    case PSP_OP_RDPDEBUG:
    case PSP_OP_RDPMAXRETRANS:
    case PSP_OP_MCASTDEBUG:
	if (value<0) {
	    printf(" value must be >= 0.\n");
	    return;
	}
	break;
    case PSP_OP_RDPPKTLOSS:
	if (value<0 || value>100) {
	    printf(" value must be 0 <= val <=100.\n");
	    return;
	}
    }

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    /*
     * prepare the message to send it to the daemon
     */
    msg.header.type = PSP_CD_SETOPTION;
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg);
    msg.count = 1;
    msg.opt[0].option = type;
    msg.opt[0].value = value;

    for (i = first; i < last; i++) {
	msg.header.dest = PSC_getTID(i, 0);
	PSI_sendMsg(&msg);
    }

    return;
}

void PSIADM_ShowParam(int type, int first, int last)
{
    int i, ret;
    long option = type, value;

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    for (i = first; i < last; i++) {
	printf("%3d:  ", i);
	ret = INFO_request_option(i, 1, &option, &value, 1);
	if (ret != -1) {
	    printf("%ld\n", value);
	} else {
	    printf("Cannot get\n");
	}
    }

    return;
}

void PSIADM_Version(void)
{
    printf("PSIADMIN: ParaStation administration tool\n");
    printf("Copyright (C) 1996-2003 ParTec AG Karlsruhe\n");
    printf("\n");
    printf("PSIADMIN:   %s\b \n", psiadmversion+11);
    printf("PSID:       %s\b \n", PSI_getPsidVersion()+11);
    printf("PSProtocol: %d\n", PSprotocolVersion);
    return;
}

/* void PSIADM_ShowConfig(void) */
/* { */
/*     long option[] = { */
/* 	PSP_OP_UIDLIMIT, */
/* 	PSP_OP_PROCLIMIT, */
/* 	PSP_OP_PSM_SPS, */
/* 	PSP_OP_PSM_RTO, */
/* 	PSP_OP_PSM_HNPEND, */
/* 	PSP_OP_PSM_ACKPEND}; */
/*     long value[DDOptionMsgMax]; */
/*     int uidlimit=0, proclimit=0, smallpacksize=0, resendtimeout=0, hnpend=0; */
/*     int ackpend=0; */
/*     int num, i; */

/*     /\* */
/*      * prepare the message to send it to the daemon */
/*      *\/ */
/*     num = sizeof(option)/sizeof(*option); */
/*     if (INFO_request_option(0, num, option, value, 1) != num) { */
/* 	printf("PANIC: Got less options than requested.\n"); */
/*     } */

/*     for(i=0; i<num; i++){ */
/* 	switch(option[i]){ */
/* 	case PSP_OP_UIDLIMIT: */
/* 	    uidlimit = value[i]; */
/* 	    break; */
/* 	case PSP_OP_PROCLIMIT: */
/* 	    proclimit = value[i]; */
/* 	    break; */
/* 	case PSP_OP_PSM_SPS: */
/* 	    smallpacksize = value[i]; */
/* 	    break; */
/* 	case PSP_OP_PSM_RTO: */
/* 	    resendtimeout = value[i]; */
/* 	    break; */
/* 	case PSP_OP_PSM_HNPEND: */
/* 	    hnpend = value[i]; */
/* 	    break; */
/* 	case PSP_OP_PSM_ACKPEND: */
/* 	    ackpend = value[i]; */
/* 	    break; */
/* 	} */
/*     } */
/*     printf("SmallPacketSize is %d\n", smallpacksize); */
/*     printf("ResendTimeout is %d [us]\n", resendtimeout); */
/*     printf("HNPend is %d\n", hnpend); */
/*     printf("AckPend is %d\n", ackpend); */
/*     if(proclimit==-1) */
/* 	printf("max. processes: NONE\n"); */
/*     else */
/* 	printf("max. processes: %d\n", proclimit); */
/*     if(uidlimit==-1) */
/* 	printf("limited to user : NONE\n"); */
/*     else */
/* 	printf("limited to user : %d\n", uidlimit); */

/*     return; */
/* } */

/*
 *   what : 1=HW
 *   first: first node to be reset
 *   last : last node to be reset
 */
void PSIADM_Reset(int reset_hw, int first, int last)
{
    DDBufferMsg_t msg;
    long *action = (long *)msg.buf;
    int i, send_local = 0;

    if (geteuid()) {
	printf("Insufficient priviledge\n");
	return;
    }

    /*
     * prepare the message to send it to the daemon
     */
    msg.header.type = PSP_CD_DAEMONRESET;
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg.header) + sizeof(*action);

    *action = 0;
    if (reset_hw) {
	*action |= PSP_RESET_HW;
	doRestart = 1;
    }

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;
    for (i = first; i < last; i++) {
	if (hoststatus[i]) {
	    if (i == PSC_getMyID()) {
		send_local = 1;
	    } else {
		msg.header.dest = PSC_getTID(i, 0);
		PSI_sendMsg(&msg);
	    }
	}
    }

    if (send_local) {
	msg.header.dest = PSC_getTID(-1, 0);
	PSI_sendMsg(&msg);
    }

    return;
}

void PSIADM_ShutdownCluster(int first, int last)
{
    DDMsg_t msg;
    int i, send_local = 0;

    if (geteuid()) {
	printf("Insufficient priviledge\n");
	return;
    }

    msg.type = PSP_CD_DAEMONSTOP;
    msg.sender = PSC_getMyTID();
    msg.len = sizeof(msg);

    INFO_request_hoststatus(hoststatus, PSC_getNrOfNodes(), 1);

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;
    for (i = first; i < last; i++) {
	if (hoststatus[i]) {
	    if (i == PSC_getMyID()) {
		send_local = 1;
	    } else {
		msg.dest = PSC_getTID(i, 0);
		PSI_sendMsg(&msg);
	    }
	}
    }

    if (send_local) {
	msg.dest = PSC_getTID(-1, 0);
	PSI_sendMsg(&msg);
    }

    return;
}

void PSIADM_TestNetwork(int mode)
{
    char *dir;
    char command[100];
    dir = PSC_lookupInstalldir();
    if (dir) {
	chdir (dir);
    } else {
	printf("Cannot find 'test_nodes'.\n");
	return;
    }
    snprintf(command, sizeof(command),
	     "./bin/test_nodes -np %d", PSC_getNrOfNodes());
    if (system(command) < 0) {
	printf("Cant execute %s : %s\n", command, strerror(errno));
    }
    return;
}

void PSIADM_KillProc(long tid, int sig)
{
    if (sig == -1) sig = SIGTERM;

    if (sig < 0) {
	fprintf(stderr, "Unknown signal %d.\n", sig);
    } else {
	PSI_kill(tid, sig);
    }

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
	if (!doRestart) {
	    fprintf(stderr, "\nPSIadmin: Got SIGTERM .... exiting\n");
	    exit(0);
	}

	fprintf(stderr, "\nPSIadmin: Got SIGTERM .... exiting"
		" ...wait for a reconnect..\n");
	PSI_exitClient();
	sleep(2);
	fprintf(stderr, "PSIadmin: Restarting...\n");
	if (!PSI_initClient(TG_ADMIN)) {
	    fprintf(stderr, "can't contact my own daemon.\n");
	    exit(-1);
        }
	doRestart = 0;
	signal(SIGTERM, sighandler);

	break;
    }
}

/*
 * Print version info
 */
static void printVersion(void)
{
    fprintf(stderr, "psiadmin %s\b \n", psiadmversion+11);
}

int main(int argc, const char **argv)
{
    void *line_state = NULL;
    char *copt = NULL, *line = (char *) NULL, line_field[256];
    int rc, len, echo=0, version=0, reset=0;

    poptContext optCon;   /* context for parsing command-line options */

    struct poptOption optionsTable[] = {
	{ "command", 'c', POPT_ARG_STRING, &copt, 0,
	  "execute a single <command> and exit", "command"},
	{ "echo", 'e', POPT_ARG_NONE, &echo, 0,
	  "echo each executed command to stdout", NULL},
	{ "reset", 'r', POPT_ARG_NONE, &reset, 0,
	  "do a reset of the ParaStation daemons on startup", NULL},
  	{ "version", 'v', POPT_ARG_NONE, &version, -1,
	  "output version information and exit", NULL},
	POPT_AUTOHELP
	{ NULL, '\0', 0, NULL, 0, NULL, NULL}
    };

    optCon = poptGetContext(NULL, argc, argv, optionsTable, 0);
    rc = poptGetNextOpt(optCon);

    if (rc < -1) {
	/* an error occurred during option processing */
	poptPrintUsage(optCon, stderr, 0);
	fprintf(stderr, "%s: %s\n",
		poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
		poptStrerror(rc));
	return 1;
    }

    if (version) {
	printVersion();
	return 0;
    }

    if (reset) {
	if (geteuid()) {
	    printf("Insufficient priviledge for resetting\n");
	    exit(-1);
	}
	printf("Initiating RESET.\n");
	PSI_initClient(TG_RESET);
	PSI_exitClient();
	printf("Waiting for reset.\n");
	sleep(1);
	printf("Trying to reconnect.\n");
    }

    if (!PSI_initClient(TG_ADMIN)) {
	fprintf(stderr,"can't contact my own daemon.\n");
	exit(-1);
    }

    hoststatus = (char *)malloc(sizeof(char) * PSC_getNrOfNodes());
    if (!hoststatus) {
	printf("node memory\n");
	exit(1);
    }

    nodelistSize = sizeof(NodelistEntry_t) * PSC_getNrOfNodes();
    nodelist = (NodelistEntry_t *) malloc(nodelistSize);
    if (!nodelist) {
	printf("nodelist memory\n");
	exit(1);
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
		if (echo) printf("%s\n", line);
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
