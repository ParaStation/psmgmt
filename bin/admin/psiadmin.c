/*
 *               ParaStation3
 * psiadmin.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psiadmin.c,v 1.53 2003/03/06 13:29:21 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psiadmin.c,v 1.53 2003/03/06 13:29:21 eicker Exp $";
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

static char psiadmversion[] = "$Revision: 1.53 $";
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
    DDContactMsg_t msg;

    msg.header.type = PSP_DD_CONTACTNODE;
    msg.header.len = sizeof(msg);
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = PSC_getTID(-1, 0);

    INFO_request_hoststatus(hoststatus, PSC_getNrOfNodes(), 1);

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;
    for (i = first; i < last; i++) {
	if (hoststatus[i]) {
	    printf("%d already up.\n",i);
	} else {
	    printf("starting node %d\n",i);
	    msg.partner = i;
	    PSI_sendMsg(&msg);
	}
    }

    /* @ todo check the success and repeat the startup */
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

void PSIADM_RDPStat(int first, int last)
{
    int i;
    char s[255];

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;
    for (i = first; i < last; i++) {
	INFO_request_rdpstatus(i,s,sizeof(s), 1);
	printf("%s",s);
    }

    return;
}

void PSIADM_MCastStat(int first, int last)
{
    int i;
    char s[256];

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;
    for (i = first; i < last; i++) {
	INFO_request_mcaststatus(i, s, sizeof(s), 1);
	printf("%s", s);
    }

    return;
}

void PSIADM_CountStat(int first, int last)
{
    int i;
    unsigned int j;
    struct {
	unsigned int hwStatus;
	PSHALInfoCounter_t ic;
    } countstat;

    INFO_request_nodelist(nodelist, nodelistSize, 1);

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    printf("%6s ", "NODE");
    /* Get header info from daemon with MyriNet */
    memset(&countstat, 0, sizeof(countstat));
    for (i=0; i<PSC_getNrOfNodes(); i++) {
	if (nodelist[i].hwType & PSHW_MYRINET) {
	    INFO_request_countstatus(i, &countstat, sizeof(countstat), 0);
	    if (countstat.hwStatus & PSHW_MYRINET) {
		for (j=0 ; j<countstat.ic.n; j++){
		    printf("%8s ", countstat.ic.counter[j].name);
		}
		break;
	    }
	}
    }
    printf("\n");

    for (i = first; i < last; i++) {
	printf("%4d ", i);

	if (nodelist[i].up) {
	    if (! (nodelist[i].hwType & PSHW_MYRINET)) {
		printf("    No card present\n");
	    } else if (INFO_request_countstatus(i, &countstat,
						sizeof(countstat), 1) != -1) {
		if (countstat.hwStatus & PSHW_MYRINET) {
		    for (j=0; j<countstat.ic.n; j++){
			char ch[10];
			/* calc column size from name length */
			sprintf(ch, "%%%du ", (int) MAX(strlen(
			    countstat.ic.counter[j].name),8));
			printf(ch, countstat.ic.counter[j].value);
		    }
		    printf("\n");
		} else {
		    printf("    No card present\n");
		}
	    }
	} else {
	    printf("\tdown\n");
	}
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
		printf("%4d ", i);
		printf("%22s ", PSC_printTID(taskinfo[j].tid));
		printf("%22s ", PSC_printTID(taskinfo[j].ptid));
		printf("%2d  %5d ", taskinfo[j].connected, taskinfo[j].uid);
		printf("%s\n",
		       taskinfo[j].group==TG_ADMIN ? "(A)" :
		       taskinfo[j].group==TG_LOGGER ? "(L)" :
		       taskinfo[j].group==TG_FORWARDER ? "(F)" :
		       taskinfo[j].group==TG_SPAWNER ? "(S)" : "");
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
		   i, nodelist[i].numCPU, PSHW_printType(nodelist[i].hwType));
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
    msg.header.type = PSP_DD_SETOPTION;
    msg.header.len = sizeof(msg);
    msg.header.sender = PSC_getMyTID();
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
	ret = INFO_request_option(0, 1, &option, &proclimit, 1);

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
    msg.header.type = PSP_DD_SETOPTION;
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
	    printf("limited to user : ANY\n");
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
	    printf("limited to user : %s\n", name);
	    free(name);
	}
    }

    return;
}

void PSIADM_SetPsidSelectTime(int val, int first, int last)
{
    int i;
    DDOptionMsg_t msg;

    if (geteuid()) {
	printf("Sorry, only root access\n");
	return;
    }

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    if (val<1) {
	printf(" value must be > 0.\n");
	return;
    }

    /*
     * prepare the message to send it to the daemon
     */
    msg.header.type = PSP_DD_SETOPTION;
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg);
    msg.count = 1;
    msg.opt[0].option = PSP_OP_PSIDSELECTTIME;
    msg.opt[0].value = val;

    for (i = first; i < last; i++) {
	msg.header.dest = PSC_getTID(i, 0);
	PSI_sendMsg(&msg);
    }

    return;
}

void PSIADM_ShowPsidSelectTime(int first, int last)
{
    int i, ret;
    long option = PSP_OP_PSIDSELECTTIME, selecttime;

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    for (i = first; i < last; i++) {
	printf("%3d:  ", i);
	ret = INFO_request_option(i, 1, &option, &selecttime, 1);
	if (ret != -1) {
	    printf("%ld\n", selecttime);
	}
    }

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
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    /*
     * prepare the message to send it to the daemon
     */
    msg.header.type = PSP_DD_SETOPTION;
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg);
    msg.count = 1;
    msg.opt[0].option = PSP_OP_PSIDDEBUG;
    msg.opt[0].value = val;

    for (i = first; i < last; i++) {
	msg.header.dest = PSC_getTID(i, 0);
	PSI_sendMsg(&msg);
    }

    return;
}

void PSIADM_ShowPsidDebug(int first, int last)
{
    int i, ret;
    long option = PSP_OP_PSIDDEBUG, psiddebug;

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    for (i = first; i < last; i++) {
	printf("%3d:  ", i);
	ret = INFO_request_option(i, 1, &option, &psiddebug, 1);
	if (ret != -1) {
	    printf("%ld\n", psiddebug);
	}
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
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    if (val<0) {
	printf(" value must be >= 0.\n");
	return;
    }

    /*
     * prepare the message to send it to the daemon
     */
    msg.header.type = PSP_DD_SETOPTION;
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg);
    msg.count = 1;
    msg.opt[0].option = PSP_OP_RDPDEBUG;
    msg.opt[0].value = val;

    for (i = first; i < last; i++) {
	msg.header.dest = PSC_getTID(i, 0);
	PSI_sendMsg(&msg);
    }

    return;
}

void PSIADM_ShowRDPDebug(int first, int last)
{
    int i, ret;
    long option = PSP_OP_RDPDEBUG, rdpdebug;

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    for (i = first; i < last; i++) {
	printf("%3d:  ", i);
	ret = INFO_request_option(i, 1, &option, &rdpdebug, 1);
	if (ret != -1) {
	    printf("%ld\n", rdpdebug);
	}
    }

    return;
}

void PSIADM_SetRDPPktLoss(int val, int first, int last)
{
    int i;
    DDOptionMsg_t msg;

    if (geteuid()) {
	printf("Sorry, only root access\n");
	return;
    }

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    if (val<0 || val>100) {
	printf(" value must be 0 <= val <=100.\n");
	return;
    }

    /*
     * prepare the message to send it to the daemon
     */
    msg.header.type = PSP_DD_SETOPTION;
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg);
    msg.count = 1;
    msg.opt[0].option = PSP_OP_RDPPKTLOSS;
    msg.opt[0].value = val;

    for (i = first; i < last; i++) {
	msg.header.dest = PSC_getTID(i, 0);
	PSI_sendMsg(&msg);
    }

    return;
}

void PSIADM_ShowRDPPktLoss(int first, int last)
{
    int i, ret;
    long option = PSP_OP_RDPPKTLOSS, pktloss;

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    for (i = first; i < last; i++) {
	printf("%3d:  ", i);
	ret = INFO_request_option(i, 1, &option, &pktloss, 1);
	if (ret != -1) {
	    printf("%ld\n", pktloss);
	}
    }

    return;
}

void PSIADM_SetRDPMaxRetrans(int val, int first, int last)
{
    int i;
    DDOptionMsg_t msg;

    if (geteuid()) {
	printf("Sorry, only root access\n");
	return;
    }

    if (val<0) {
	printf(" value must be >= 0.\n");
	return;
    }

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    /*
     * prepare the message to send it to the daemon
     */
    msg.header.type = PSP_DD_SETOPTION;
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg);
    msg.count = 1;
    msg.opt[0].option = PSP_OP_RDPMAXRETRANS;
    msg.opt[0].value = val;

    for (i = first; i < last; i++) {
	msg.header.dest = PSC_getTID(i, 0);
	PSI_sendMsg(&msg);
    }

    return;
}

void PSIADM_ShowRDPMaxRetrans(int first, int last)
{
    int i, ret;
    long option = PSP_OP_RDPMAXRETRANS, maxretrans;

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    for (i = first; i < last; i++) {
	printf("%3d:  ", i);
	ret = INFO_request_option(i, 1, &option, &maxretrans, 1);
	if (ret != -1) {
	    printf("%ld\n", maxretrans);
	}
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
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    if (val<0) {
	printf(" value must be >= 0.\n");
	return;
    }

    /*
     * prepare the message to send it to the daemon
     */
    msg.header.type = PSP_DD_SETOPTION;
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg);
    msg.count = 1;
    msg.opt[0].option = PSP_OP_MCASTDEBUG;
    msg.opt[0].value = val;

    for (i = first; i < last; i++) {
	msg.header.dest = PSC_getTID(i, 0);
	PSI_sendMsg(&msg);
    }

    return;
}

void PSIADM_ShowMCastDebug(int first, int last)
{
    int i, ret;
    long option = PSP_OP_MCASTDEBUG, mcastdebug;

    first = (first==ALLNODES) ? 0 : first;
    last  = (last==ALLNODES) ? PSC_getNrOfNodes() : last+1;

    for (i = first; i < last; i++) {
	printf("%3d:  ", i);
	ret = INFO_request_option(i, 1, &option, &mcastdebug, 1);
	if (ret != -1) {
	    printf("%ld\n", mcastdebug);
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
    printf("PSProtocol: %d\n", PSprotocolversion);
    return;
}

void PSIADM_ShowConfig(void)
{
    long option[] = {
	PSP_OP_UIDLIMIT,
	PSP_OP_PROCLIMIT,
	PSP_OP_SMALLPACKETSIZE,
	PSP_OP_RESENDTIMEOUT,
	PSP_OP_HNPEND,
	PSP_OP_ACKPEND};
    long value[DDOptionMsgMax];
    int uidlimit=0, proclimit=0, smallpacksize=0, resendtimeout=0, hnpend=0;
    int ackpend=0;
    int num, i;

    /*
     * prepare the message to send it to the daemon
     */
    num = sizeof(option)/sizeof(*option);
    if (INFO_request_option(0, num, option, value, 1) != num) {
	printf("PANIC: Got less options than requested.\n");
    }

    for(i=0; i<num; i++){
	switch(option[i]){
	case PSP_OP_UIDLIMIT:
	    uidlimit = value[i];
	    break;
	case PSP_OP_PROCLIMIT:
	    proclimit = value[i];
	    break;
	case PSP_OP_SMALLPACKETSIZE:
	    smallpacksize = value[i];
	    break;
	case PSP_OP_RESENDTIMEOUT:
	    resendtimeout = value[i];
	    break;
	case PSP_OP_HNPEND:
	    hnpend = value[i];
	    break;
	case PSP_OP_ACKPEND:
	    ackpend = value[i];
	    break;
	}
    }
    printf("SmallPacketSize is %d\n", smallpacksize);
    printf("ResendTimeout is %d [us]\n", resendtimeout);
    printf("HNPend is %d\n", hnpend);
    printf("AckPend is %d\n", ackpend);
    if(proclimit==-1)
	printf("max. processes: NONE\n");
    else
	printf("max. processes: %d\n", proclimit);
    if(uidlimit==-1)
	printf("limited to user : NONE\n");
    else
	printf("limited to user : %d\n", uidlimit);

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
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = -1 /* broadcast */;
    msg.count =1;
    msg.opt[0].option = PSP_OP_SMALLPACKETSIZE;
    msg.opt[0].value = smallpacketsize;
    PSI_sendMsg(&msg);

    return;
}

void PSIADM_ShowSmallPacketSize(void)
{
    int ret;
    long option = PSP_OP_SMALLPACKETSIZE, smallpacksize;

    ret = INFO_request_option(0, 1, &option, &smallpacksize, 1);

    if (ret==-1) {
	printf("Can't get SmallPacketSize.\n");
    } else {
	printf("SmallPacketSize is %ld\n", smallpacksize);
    }

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
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = -1 /* broadcast */;
    msg.count =1;
    msg.opt[0].option = PSP_OP_RESENDTIMEOUT;
    msg.opt[0].value = time;
    PSI_sendMsg(&msg);

    return;
}

void PSIADM_ShowResendTimeout(void)
{
    int ret;
    long option = PSP_OP_RESENDTIMEOUT, resendtimeout;

    ret = INFO_request_option(0, 1, &option, &resendtimeout, 1);

    if (ret==-1) {
	printf("Can't get ResendTimeout.\n");
    } else {
	printf("ResendTimeout is %ld\n", resendtimeout);
    }

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
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = -1 /* broadcast */;
    msg.count =1;
    msg.opt[0].option = PSP_OP_HNPEND;
    msg.opt[0].value = val;
    PSI_sendMsg(&msg);

    return;
}

void PSIADM_ShowHNPend(void)
{
    int ret;
    long option = PSP_OP_HNPEND, hnpend;

    ret = INFO_request_option(0, 1, &option, &hnpend, 1);

    if (ret==-1) {
	printf("Can't get HNPend.\n");
    } else {
	printf("HNPend is %ld\n", hnpend);
    }

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
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = -1 /* broadcast */;
    msg.count =1;
    msg.opt[0].option = PSP_OP_ACKPEND;
    msg.opt[0].value = val;
    PSI_sendMsg(&msg);

    return;
}

void PSIADM_ShowAckPend(void)
{
    int ret;
    long option = PSP_OP_ACKPEND, ackpend;

    ret = INFO_request_option(0, 1, &option, &ackpend, 1);

    if (ret==-1) {
	printf("Can't get AckPend.\n");
    } else {
	printf("AckPend is %ld\n", ackpend);
    }

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
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = PSC_getTID(-1, 0);
    msg.first = (first==ALLNODES) ? 0 : first;
    msg.last = (last==ALLNODES) ? PSC_getNrOfNodes()-1 : last;
    msg.action = 0;
    if (reset_hw) {
	msg.action |= PSP_RESET_HW;
	doRestart = 1;
    }

    PSI_sendMsg(&msg);

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

    nrofnodes = PSC_getNrOfNodes();

    msg.header.type = PSP_CD_DAEMONSTOP;
    msg.header.len = sizeof(msg);
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = PSC_getTID(-1, 0);
    msg.first = (first==ALLNODES) ? 0 : first;
    msg.last = (last==ALLNODES) ? PSC_getNrOfNodes() : last;
    msg.action = 0;

    PSI_sendMsg(&msg);
}

void PSIADM_TestNetwork(int mode)
{
    char *dir;
    char command[100];
    dir = PSC_lookupInstalldir();
    if (dir) {
	chdir (dir);
    } else {
	printf("Cant find 'test_nodes'.\n");
	return;
    }
    snprintf(command, sizeof(command),
	     "./bin/test_nodes -np %d", PSC_getNrOfNodes());
    if (system(command) < 0) {
	printf("Cant execute %s : %s\n", command, strerror(errno));
    }
    return;
    
    
#if 0
    int mynode;
    int spawnargc;
    char** spawnargs;
    long tid;

    /* printf("TestNetwork\n"); */
    if (geteuid()) {
	printf("Insufficient priviledge\n");
	return;
    }
    mynode = PSC_getMyID();
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
    tid = PSI_spawn(mynode, PSC_lookupInstalldir(),
		    spawnargc, spawnargs, -1, -1, 0, &errno);
    if (tid<0) {
	char *errstr = strerror(errno);
	printf("Couln't spawn the test task. Error <%d>: %s\n",
	       errno, errstr ? txt : "UNKNOWN");
    } else {
	printf("Spawning test task successfull.\n");
    }

    free(spawnargs);
    return;
#endif
}

void PSIADM_KillProc(int id)
{
    PSI_kill(id, SIGTERM);
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
