/*
 *               ParaStation
 * commands.c
 *
 * Commands of the ParaStation adminstration tool
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: commands.c,v 1.3 2003/10/23 16:19:27 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char lexid[] __attribute__(( unused )) = "$Id: commands.c,v 1.3 2003/10/23 16:19:27 eicker Exp $";
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

#ifndef MIN
#define MIN(a,b)      (((a)<(b))?(a):(b))
#endif

#include "pscommon.h"
#include "parser.h"
#include "psprotocol.h"
#include "pstask.h"

#include "psi.h"
#include "info.h"
#include "psispawn.h"

#include "commands.h"

char commandsversion[] = "$Revision: 1.3 $";

static int doRestart = 0;

static char *hoststatus = NULL;

static NodelistEntry_t *nodelist = NULL;
static size_t nodelistSize = 0;

/* @todo PSI_sendMsg(): Wrapper, control if sendMsg was successful or exit */

void PSIADM_Init(void) {
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
}

void PSIADM_AddNode(char *nl)
{
    int i;
    DDBufferMsg_t msg;

    if (geteuid()) {
	printf("Insufficient priviledge\n");
	return;
    }

    msg.header.type = PSP_CD_DAEMONSTART;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = PSC_getTID(-1, 0);
    msg.header.len = sizeof(msg.header) + sizeof(unsigned short);

    INFO_request_hoststatus(hoststatus, PSC_getNrOfNodes(), 1);

    for (i = 0; i < PSC_getNrOfNodes(); i++) {
	if (nl && !nl[i]) continue;

	if (hoststatus[i]) {
	    printf("%d already up.\n",i);
	} else {
	    printf("starting node %d\n",i);
	    *(unsigned short *)msg.buf = i;
	    PSI_sendMsg(&msg);
	}
    }
    /* @todo check the success and repeat the startup */
}

void PSIADM_ShutdownNode(char *nl)
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

    for (i = 0; i < PSC_getNrOfNodes(); i++) {
	if (nl && !nl[i]) continue;

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
}

void PSIADM_HWStart(int hw, char *nl)
{
    int i, hwnum = INFO_request_hwnum(1);
    DDBufferMsg_t msg;

    if (geteuid()) {
	printf("Insufficient priviledge\n");
	return;
    }

    if (hw < -1 || hw >= hwnum) return;

    msg.header.type = PSP_CD_HWSTART;
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg.header) + sizeof(int);
    *(int *)msg.buf = hw;

    INFO_request_hoststatus(hoststatus, PSC_getNrOfNodes(), 1);

    for (i = 0; i < PSC_getNrOfNodes(); i++) {
	if (nl && !nl[i]) continue;

	if (hoststatus[i]) {
	    msg.header.dest = PSC_getTID(i, 0);
	    PSI_sendMsg(&msg);
	} else {
	    printf("%4d down.\n",i);
	}
    }
}

void PSIADM_HWStop(int hw, char *nl)
{
    int i, hwnum = INFO_request_hwnum(1);
    DDBufferMsg_t msg;

    if (geteuid()) {
	printf("Insufficient priviledge\n");
	return;
    }

    if (hw < -1 || hw >= hwnum) return;

    msg.header.type = PSP_CD_HWSTOP;
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg.header) + sizeof(int);
    *(int *)msg.buf = hw;

    INFO_request_hoststatus(hoststatus, PSC_getNrOfNodes(), 1);

    for (i = 0; i < PSC_getNrOfNodes(); i++) {
	if (nl && !nl[i]) continue;

	if (hoststatus[i]) {
	    msg.header.dest = PSC_getTID(i, 0);
	    PSI_sendMsg(&msg);
	} else {
	    printf("%4d down.\n",i);
	}
    }
}

void PSIADM_NodeStat(char *nl)
{
    int i;

    INFO_request_hoststatus(hoststatus, PSC_getNrOfNodes(), 1);

    for (i = 0; i < PSC_getNrOfNodes(); i++) {
	if (nl && !nl[i]) continue;

	if (hoststatus[i]) {
	    printf("%4d up.\n",i);
	} else {
	    printf("%4d down.\n",i);
	}
    }
}

static char statusline[1024];

void PSIADM_RDPStat(char *nl)
{
    int i;

    for (i = 0; i < PSC_getNrOfNodes(); i++) {
	if (nl && !nl[i]) continue;

	INFO_request_rdpstatus(i, statusline, sizeof(statusline), 1);
	printf("%s", statusline);
    }
}

void PSIADM_MCastStat(char *nl)
{
    int i;

    for (i = 0; i < PSC_getNrOfNodes(); i++) {
	if (nl && !nl[i]) continue;

	INFO_request_mcaststatus(i, statusline, sizeof(statusline), 1);
	printf("%s", statusline);
    }
}

static int getHeaderLine(int hw)
{
    int i, hwnum = INFO_request_hwnum(1);

    for (i = 0; i < PSC_getNrOfNodes(); i++) {
	if (nodelist[i].up && nodelist[i].hwStatus & 1<<hw) {
	    if (INFO_request_countheader(i, hw, &statusline,
					 sizeof(statusline), 1)) {
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
    return i;
}

void PSIADM_CountStat(int hw, char *nl)
{
    int i, hwnum = INFO_request_hwnum(1);
    int first = 0, last = hwnum-1;

    if (hw < -1 || hw >= hwnum) return;
    if (hw != -1) first = last = hw;

    INFO_request_nodelist(nodelist, nodelistSize, 1);

    for (hw=first; hw<=last; hw++) {

	if (getHeaderLine(hw) < PSC_getNrOfNodes()) {
	    for (i = 0; i < PSC_getNrOfNodes(); i++) {
		if (nl && !nl[i]) continue;

		printf("%6d ", i);
		if (nodelist[i].up) {
		    if (! (nodelist[i].hwStatus & 1<<hw)) {
			printf("    No card present\n");
		    } else {
			if (INFO_request_countstatus(i, hw, &statusline,
						     sizeof(statusline), 1)) {
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
	    printf("\n");
	}
    }
}

#define NUMTASKS 20

void PSIADM_ProcStat(char *nl, int full)
{
    INFO_taskinfo_t taskinfo[NUMTASKS];
    int i, j, num;

    INFO_request_hoststatus(hoststatus, PSC_getNrOfNodes(), 1);

    printf("%4s %22s %22s %3s %9s\n", "Node", "TaskId",
	   "ParentTaskId", "Con", "UserId");
    for (i = 0; i < PSC_getNrOfNodes(); i++) {
	if (nl && !nl[i]) continue;

	printf("---------------------------------------------------------"
	       "---------\n");
	if (hoststatus[i]) {
	    num = INFO_request_tasklist(i, taskinfo, sizeof(taskinfo), 1);
	    for (j=0; j<MIN(num,NUMTASKS); j++) {
		if (taskinfo[j].group==TG_FORWARDER && !full) continue;
		if (taskinfo[j].group==TG_SPAWNER && !full) continue;
		if (taskinfo[j].group==TG_GMSPAWNER && !full) continue;
		if (taskinfo[j].group==TG_MONITOR && !full) continue;
		printf("%4d ", i);
		printf("%22s ", PSC_printTID(taskinfo[j].tid));
		printf("%22s ", PSC_printTID(taskinfo[j].ptid));
		printf("%2d  %5d ", taskinfo[j].connected, taskinfo[j].uid);
		printf("%s\n",
		       taskinfo[j].group==TG_ADMIN ? "(A)" :
		       taskinfo[j].group==TG_LOGGER ? "(L)" :
		       taskinfo[j].group==TG_FORWARDER ? "(F)" :
		       taskinfo[j].group==TG_SPAWNER ? "(S)" :
		       taskinfo[j].group==TG_GMSPAWNER ? "(S)" :
		       taskinfo[j].group==TG_MONITOR ? "(M)" : "");
	    }
	    if (num>NUMTASKS) {
		printf(" + %d more tasks\n", num-NUMTASKS);
	    }
	} else {
	    printf("%4d\tdown\n", i);
	}
    }
}

void PSIADM_LoadStat(char *nl)
{
    int i;

    INFO_request_nodelist(nodelist, nodelistSize, 1);
    printf("Node\t\t Load\t\t     Jobs\n");
    printf("\t 1 min\t 5 min\t15 min\t tot.\tnorm.\n");

    for (i = 0; i < PSC_getNrOfNodes(); i++) {
	if (nl && !nl[i]) continue;
	if (nodelist[i].up) {
	    printf("%4d\t%2.4f\t%2.4f\t%2.4f\t%4d\t%4d\n", i,
		   nodelist[i].load[0], nodelist[i].load[1],
		   nodelist[i].load[2],
		   nodelist[i].totalJobs, nodelist[i].normalJobs);
	} else {
	    printf("%4d\t down\n", i);
	}
	
    }
}

void PSIADM_HWStat(char *nl)
{
    int i;

    INFO_request_nodelist(nodelist, nodelistSize, 1);
    printf("Node\t CPUs\t Available Hardware\n");
    for (i = 0; i < PSC_getNrOfNodes(); i++) {
	if (nl && !nl[i]) continue;

	if (nodelist[i].up) {
	    printf("%4d\t %d\t %s\n", i, nodelist[i].numCPU,
		   INFO_printHWType(nodelist[i].hwStatus));
	} else {
	    printf("%4d\t down\n", i);
	}
    }
}

void PSIADM_SetParam(int type, int value, char *nl)
{
    int i;
    DDOptionMsg_t msg;

    if (geteuid()) {
	printf("Insufficient priviledge\n");
	return;
    }

    switch (type) {
    case PSP_OP_PROCLIMIT:
    case PSP_OP_UIDLIMIT:
    case PSP_OP_GIDLIMIT:
	if (value < -1) {
	    printf(" value must be -1 <= val\n");
	    return;
	}
	break;
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

    /*
     * prepare the message to send it to the daemon
     */
    msg.header.type = PSP_CD_SETOPTION;
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg);
    msg.count = 1;
    msg.opt[0].option = type;
    msg.opt[0].value = value;

    INFO_request_hoststatus(hoststatus, PSC_getNrOfNodes(), 1);

    for (i = 0; i < PSC_getNrOfNodes(); i++) {
	if (nl && !nl[i]) continue;

	if (hoststatus[i]) {
	    msg.header.dest = PSC_getTID(i, 0);
	    PSI_sendMsg(&msg);
	}
    }
}

void PSIADM_ShowParam(int type, char *nl)
{
    int i, ret;
    long option = type, value;

    for (i = 0; i < PSC_getNrOfNodes(); i++) {
	if (nl && !nl[i]) continue;

	printf("%3d:  ", i);
	ret = INFO_request_option(i, 1, &option, &value, 1);
	if (ret != -1) {
	    switch (type) {
	    case PSP_OP_PROCLIMIT:
		if (value==-1)
		    printf("ANY\n");
		else
		    printf("%ld\n", value);
		break;
	    case PSP_OP_UIDLIMIT:
		if (value==-1)
		    printf("ANY\n");
		else {
		    struct passwd *passwd = getpwuid(value);
		    if (passwd) {
			printf("%s\n", passwd->pw_name);
		    } else {
			printf("uid %ld\n", value);
		    }
		}
		break;
	    case PSP_OP_GIDLIMIT:
		if (value==-1)
		    printf("ANY\n");
		else {
		    struct group *group = getgrgid(value);
		    if (group) {
			printf("%s\n", group->gr_name);
		    } else {
			printf("gid %ld\n", value);
		    }
		}
		break;
	    default:
		printf("%ld\n", value);
	    }
	} else {
	    printf("Cannot get\n");
	}
    }
}

void PSIADM_sighandler(int sig)
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
	signal(SIGTERM, PSIADM_sighandler);

	break;
    }
}

void PSIADM_Reset(int reset_hw, char *nl)
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

    for (i = 0; i < PSC_getNrOfNodes(); i++) {
	if (nl && !nl[i]) continue;

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
}

void PSIADM_KillProc(PStask_ID_t tid, int sig)
{
    if (sig == -1) sig = SIGTERM;

    if (sig < 0) {
	fprintf(stderr, "Unknown signal %d.\n", sig);
    } else {
	PSI_kill(tid, sig);
    }
}
