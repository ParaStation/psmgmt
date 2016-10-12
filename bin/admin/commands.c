/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/resource.h>

#include "pscommon.h"
#include "parser.h"
#include "psprotocol.h"
#include "pstask.h"
#include "timer.h"

#include "psi.h"
#include "psiinfo.h"
#include "psispawn.h"

#include "adminparser.h"
#include "psiadmin.h"

#include "commands.h"

char commandsversion[] = "$Revision$";

/* @todo PSI_sendMsg(): Wrapper, control if sendMsg was successful or exit */


/** Simple array with current size attached */
typedef struct {
    size_t actSize;  /**< The actual size of the array @ref list */
    char *list;      /**< The array. */
} sizedList_t;

/**
 * @brief Extend an array.
 *
 * Extend the array @a list to provide at least @a size bytes of
 * content. If @list is already larger than @a size, do nothing.
 *
 * @param list The array to enlarge.
 *
 * @param size The requested minimal size.
 *
 * @param caller String used for better error logging.
 *
 * @return On success, i.e. if @a list was large enough or the
 * extension of @a list was possible, 1 is returned. Otherwise 0 is
 * returned.
 */
static int extendList(sizedList_t *list, size_t size, const char *caller)
{
    if (list->actSize < size) {
	char *tmp = list->list;
	list->actSize = size;
	list->list = realloc(list->list, list->actSize);
	if (!list->list) {
	    printf("%s: %s: out of memory\n", caller, __func__);
	    free(tmp);
	    list->actSize=0;
	    return 0;
	}
    }
    return 1;
}

/**
 * @brief Get a full list.
 *
 * Get a full list from the daemon. Therefore a list of type @a what is
 * stored to @a list. Each item of the list is expected to have size
 * @a itemSize. In order to provide this function with the correct @a
 * itemSize, please refer to the documentation within psiinfo.h.
 *
 * This function expects the list to be of length returned by @ref
 * PSC_getNrOfNodes().
 *
 *
 * @param list A sized list to store the result to.
 *
 * @param what The type of information to retrieve.
 *
 * @param itemSize The size of each item within @a list.
 *
 * @return On success, 1 is returned, or 0, if an error occurred.
 */
static int getFullList(sizedList_t *list, PSP_Info_t what, size_t itemSize)
{
    int recv, hosts;
    char funcStr[256];

    snprintf(funcStr, sizeof(funcStr),
	     "%s(%s)", __func__, PSP_printInfo(what));
    if (!extendList(list, itemSize*PSC_getNrOfNodes(), funcStr)) return 0;

    recv = PSI_infoList(-1, what, NULL, list->list, list->actSize, 1);
    hosts = recv/itemSize;

    if (hosts != PSC_getNrOfNodes()) {
	printf("%s: failed.\n", funcStr);
	return 0;
    }

    return 1;
}

/** List used for storing of host stati. */
static sizedList_t hostStatus = { .actSize = 0, .list = NULL };

/** Simple wrapper for retrieval of host status */
static inline int getHostStatus(void)
{
    return getFullList(&hostStatus, PSP_INFO_LIST_HOSTSTATUS, sizeof(char));
}

/** List used for storing of hardware stati. */
static sizedList_t hwList = { .actSize = 0, .list = NULL };

/** Simple wrapper for retrieval of hardware statuses */
static inline int getHWStat(void)
{
    return getFullList(&hwList, PSP_INFO_LIST_HWSTATUS, sizeof(uint32_t));
}

/** List used for storing of normal task number informations. */
static sizedList_t tnnList = { .actSize = 0, .list = NULL };
/** List used for storing of full task number informations. */
static sizedList_t tnfList = { .actSize = 0, .list = NULL };
/** List used for storing of allocated task number informations. */
static sizedList_t tnaList = { .actSize = 0, .list = NULL };

/** Simple wrapper for retrieval of task numbers */
static inline int getTaskNum(PSP_Info_t what)
{
    sizedList_t *list;

    switch (what) {
    case PSP_INFO_LIST_NORMJOBS:
	list = &tnnList;
	break;
    case PSP_INFO_LIST_ALLJOBS:
	list = &tnfList;
	break;
    case PSP_INFO_LIST_ALLOCJOBS:
	list = &tnaList;
	break;
    default:
	printf("%s: Unknown type %s\n", __func__, PSP_printInfo(what));
	return 0;
    }

    return getFullList(list, what, sizeof(uint16_t));
}

/** List used for storing of load informations. */
static sizedList_t ldList = { .actSize = 0, .list = NULL };

/** Simple wrapper for retrieval of loads */
static inline int getLoads(void)
{
    return getFullList(&ldList, PSP_INFO_LIST_LOAD, 3 * sizeof(float));
}

/** List used for storing of memory informations. */
static sizedList_t memList = { .actSize = 0, .list = NULL };

/** Simple wrapper for retrieval of memory info */
static inline int getMem(void)
{
    return getFullList(&memList, PSP_INFO_LIST_MEMORY, 2 * sizeof(uint64_t));
}

/** List used for storing of physical CPU informations. */
static sizedList_t pcpuList = { .actSize = 0, .list = NULL };
/** List used for storing of virtual CPU informations. */
static sizedList_t vcpuList = { .actSize = 0, .list = NULL };

/** Simple wrapper for retrieval of physical CPU numbers */
static inline int getPhysCPUs(void)
{
    return getFullList(&pcpuList, PSP_INFO_LIST_PHYSCPUS, sizeof(uint16_t));
}

/** Simple wrapper for retrieval of virtual CPU numbers */
static inline int getVirtCPUs(void)
{
    return getFullList(&vcpuList, PSP_INFO_LIST_VIRTCPUS, sizeof(uint16_t));
}


/** List used for storing of exclusive flag informations. */
static sizedList_t exclList = { .actSize = 0, .list = NULL };

/** Simple wrapper for retrieval of exclusive flags */
static inline int getExclusiveFlags(void)
{
    return getFullList(&exclList, PSP_INFO_LIST_EXCLUSIVE, sizeof(int8_t));
}

static char * nodeString(PSnodes_ID_t node)
{
    static char nodeStr[128];

    if (paramHostname) {
	struct in_addr hostaddr;
	struct hostent *hp;
	char *ptr;
	int err = PSI_infoUInt(-1, PSP_INFO_NODE, &node, &hostaddr.s_addr, 0);
	if (err || (hostaddr.s_addr == INADDR_ANY)) {
	    snprintf(nodeStr, sizeof(nodeStr), "<unknown>(%d)", node);
	    return nodeStr;
	}

	hp = gethostbyaddr(&hostaddr.s_addr, sizeof(hostaddr.s_addr), AF_INET);
	if (hp) {
	    if ((ptr = strchr(hp->h_name, '.'))) *ptr = '\0';
	    return hp->h_name;
	} else {
	    snprintf(nodeStr, sizeof(nodeStr), "%s", inet_ntoa(hostaddr));
	}
    } else {
	snprintf(nodeStr, sizeof(nodeStr), "%4d", node);
    }

    return nodeStr;
}

/* ---------------------------------------------------------------------- */

/** Delay between starting nodes in msec. @todo Make this configurable. */
static const int delay = 50;

void PSIADM_AddNode(char *nl)
{
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_DAEMONSTART,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getTID(-1, 0),
	    .len = sizeof(msg.header) },
	.buf = { 0 } };
    PSnodes_ID_t node;

    if (geteuid()) {
	printf("Insufficient privilege\n");
	return;
    }

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;

	if (hostStatus.list[node]) {
	    /* printf("%d already up.\n", node); */
	} else {
	    printf("starting node %s\n", nodeString(node));
	    msg.header.len = sizeof(msg.header);
	    PSP_putMsgBuf(&msg, __func__, "node ID", &node, sizeof(node));
	    PSI_sendMsg(&msg);
	    usleep(delay * 1000);
	}
    }
    /* @todo check the success and repeat the startup */
}

void PSIADM_ShutdownNode(int silent, char *nl)
{
    DDMsg_t msg = {
	.type = PSP_CD_DAEMONSTOP,
	.sender = PSC_getMyTID(),
	.dest = 0,
	.len = sizeof(msg) };
    PSnodes_ID_t node;
    int send_local = 0;

    if (geteuid()) {
	printf("Insufficient privilege\n");
	return;
    }

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;

	if (!hostStatus.list[node]) {
	    if (!silent) printf("%s\talready down\n", nodeString(node));
	} else {
	    if (node == PSC_getMyID()) {
		send_local = 1;
	    } else {
		msg.dest = PSC_getTID(node, 0);
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
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_HWSTART,
	    .sender = PSC_getMyTID(),
	    .dest = 0,
	    .len = sizeof(msg.header) } };
    PSnodes_ID_t node;
    int hwnum, err;
    int32_t hw32 = hw;

    if (geteuid()) {
	printf("Insufficient privilege\n");
	return;
    }

    err = PSI_infoInt(-1, PSP_INFO_HWNUM, NULL, &hwnum, 1);
    if (err || hw < -1 || hw >= hwnum) return;

    PSP_putMsgBuf(&msg, __func__, "hardware type", &hw32, sizeof(hw32));

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;

	if (hostStatus.list[node]) {
	    msg.header.dest = PSC_getTID(node, 0);
	    PSI_sendMsg(&msg);
	} else {
	    printf("%s\tdown\n", nodeString(node));
	}
    }
}

void PSIADM_HWStop(int hw, char *nl)
{
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_HWSTOP,
	    .sender = PSC_getMyTID(),
	    .dest = 0,
	    .len = sizeof(msg.header) } };
    PSnodes_ID_t node;
    int hwnum, err;
    int32_t hw32 = hw;

    if (geteuid()) {
	printf("Insufficient privilege\n");
	return;
    }

    err = PSI_infoInt(-1, PSP_INFO_HWNUM, NULL, &hwnum, 1);
    if (err || hw < -1 || hw >= hwnum) return;

    PSP_putMsgBuf(&msg, __func__, "hardware type", &hw32, sizeof(hw32));

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;

	if (hostStatus.list[node]) {
	    msg.header.dest = PSC_getTID(node, 0);
	    PSI_sendMsg(&msg);
	} else {
	    printf("%s\tdown\n", nodeString(node));
	}
    }
}

void PSIADM_NodeStat(char *nl)
{
    PSnodes_ID_t node;

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;

	printf("%s\t", nodeString(node));
	if (hostStatus.list[node]) {
	    printf("up\n");
	} else {
	    printf("down\n");
	}
    }
}

void PSIADM_SummaryStat(char *nl, int max)
{
    static char *nlDown = NULL;
    PSnodes_ID_t node;
    int upNodes = 0, downNodes = 0;

    if (!nlDown) {
	nlDown = malloc(PSC_getNrOfNodes());
	if (!nlDown) {
	    parser_exit(errno, "%s: unable to initialize nlDown", __func__);
	}
    }
    memset(nlDown, 0, PSC_getNrOfNodes());

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;

	if (hostStatus.list[node]) {
	    upNodes++;
	} else {
	    nlDown[node] = 1;
	    downNodes++;
	}
    }
    printf("Node status summary:  %d up   %d down  of %d total\n",
	   upNodes, downNodes, upNodes+downNodes);

    /* Also print list of down nodes if sufficiently less */
    if (downNodes && (downNodes < max)) {
	printf("Down nodes are: ");
	PSC_printNodelist(nlDown);
	printf("\n");
    }
}

void PSIADM_StarttimeStat(char *nl)
{
    PSnodes_ID_t node;

    if (! getHostStatus()) return;

    printf("%4s\t%16s\n", "Node", "Start-time ");
    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;

	printf("%s\t", nodeString(node));
	if (hostStatus.list[node]) {
	    int64_t secs;
	    int err;

	    err = PSI_infoInt64(node, PSP_INFO_STARTTIME, NULL, &secs, 0);
	    if (!err) {
		time_t startTime = (time_t) secs;
		printf(" %s", ctime(&startTime));
	    }
	} else {
	    printf("down\n");
	}
    }
}

void PSIADM_ScriptStat(PSP_Info_t type, char *nl)
{
    PSnodes_ID_t node;
    char scriptName[1500];
    size_t scriptWidth = PSC_getWidth()-8;

    if (! getHostStatus()) return;

    printf("%4s\t%s\n", "Node", "Script");
    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;
	printf("%s\t", nodeString(node));
	if (hostStatus.list[node]) {
	    int err = PSI_infoString(node, type, NULL, scriptName,
				     sizeof(scriptName), 1);
	    if (!err) {
		size_t len = strlen(scriptName);
		if (len) {
		    if (len <= scriptWidth) {
			printf("%s\n", scriptName);
		    } else {
			printf("...%s\n", scriptName+len-(scriptWidth-3));
		    }
		} else {
		    printf("<none>\n");
		}
	    } else {
		printf("\n");
	    }
	} else {
	    printf("down\n");
	}
    }
}

void PSIADM_SomeStat(char *nl, char mode)
{
    PSnodes_ID_t node;

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	int printIt = 0;

	if (nl && !nl[node]) continue;

	switch (mode) {
	case 'u':
	    if (hostStatus.list[node]) printIt = 1;
	    break;
	case 'd':
	    if (!hostStatus.list[node]) printIt = 1;
	    break;
	default:
	    printf("Unknown mode '%c'\n", mode);
	    return;
	}
	if (printIt) {
	    u_int32_t hostaddr;
	    struct hostent *hp;
	    char *ptr;
	    int err = PSI_infoUInt(-1, PSP_INFO_NODE, &node, &hostaddr, 0);
	    if (err || (hostaddr == INADDR_ANY)) {
		printf(" <unknown>(id %d)", node);
		continue;
	    }

	    hp = gethostbyaddr(&hostaddr, sizeof(hostaddr), AF_INET);
	    if (!hp) {
		printf(" <unknown>(id %d)", node);
		continue;
	    }

	    if ((ptr = strchr (hp->h_name, '.'))) *ptr = '\0';
	    printf("%s\n", hp->h_name);
	}
    }
}


static char line[1024];

void PSIADM_RDPStat(char *nl)
{
    PSnodes_ID_t node, partner;

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;
	printf("%s:\n", nodeString(node));
	if (hostStatus.list[node]) {
	    for (partner=0; partner<PSC_getNrOfNodes(); partner++) {
		int err = PSI_infoString(node, PSP_INFO_RDPSTATUS, &partner,
					 line, sizeof(line), 1);
		if (!err) printf("%s\n", line);
	    }
	    printf("\n");
	} else {
	    printf("\tdown\n\n");
	}
    }
}

void PSIADM_RDPConnStat(char *nl)
{
    PSnodes_ID_t node, partner;

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;
	printf("%s:\n", nodeString(node));
	if (hostStatus.list[node]) {
	    for (partner=0; partner<PSC_getNrOfNodes(); partner++) {
		int err = PSI_infoString(node, PSP_INFO_RDPCONNSTATUS, &partner,
					 line, sizeof(line), 1);
		if (!err) printf("%s\n", line);
	    }
	    printf("\n");
	} else {
	    printf("\tdown\n\n");
	}
    }
}

void PSIADM_MCastStat(char *nl)
{
    PSnodes_ID_t node;

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	int err;
	if ((nl && !nl[node])) continue;
	err = PSI_infoString(-1, PSP_INFO_MCASTSTATUS, &node,
			     line, sizeof(line), 1);
	if (!err) printf("%s\n", line);
    }
}

static int getHeaderLine(int32_t hw)
{
    PSnodes_ID_t node;
    uint32_t *hwStatus = (uint32_t *)hwList.list;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (hostStatus.list[node] && hwStatus[node] & 1<<hw) {
	    int err = PSI_infoString(node, PSP_INFO_COUNTHEADER, &hw,
				     line, sizeof(line), 1);
	    if (!err) {
		char name[40];
		int last = strlen(line)-1;

		err = PSI_infoString(-1, PSP_INFO_HWNAME, &hw,
				     name, sizeof(name), 1);

		printf("Counter for hardware type '%s':\n\n",
		       err ? "unknown" : name);
		printf("%6s ", "NODE");

		if (line[(last>0) ? last : 0] == '\n') {
		    printf("%s", line);
		} else {
		    printf("%s\n", line);
		}
		break;
	    }
	}
    }
    return node;
}

void PSIADM_CountStat(int hw, char *nl)
{
    PSnodes_ID_t node;
    uint32_t *hwStatus;
    int hwnum, err;
    int first = 0, last;

    err = PSI_infoInt(-1, PSP_INFO_HWNUM, NULL, &hwnum, 1);
    if (err || hw < -1 || hw >= hwnum) return;

    if (hw != -1) {
	first = last = hw;
    } else {
	last = hwnum - 1;
    }

    if (! getHostStatus()) return;
    if (! getHWStat()) return;
    hwStatus = (uint32_t *)hwList.list;

    for (hw=first; hw<=last; hw++) {
	if (getHeaderLine(hw) == PSC_getNrOfNodes()) continue;

	for (node=0; node<PSC_getNrOfNodes(); node++) {
	    if (nl && !nl[node]) continue;

	    printf("%s\t", nodeString(node));
	    if (hostStatus.list[node]) {
		if (hwStatus[node] & 1<<hw) {
		    int err = PSI_infoString(node, PSP_INFO_COUNTSTATUS, &hw,
					     line, sizeof(line), 1);
		    if (!err) {
			int last = strlen(line)-1;
			if (line[(last>0) ? last : 0] == '\n') {
			    printf("%s", line);
			} else {
			    printf("%s\n", line);
			}
		    } else {
			printf("Counter unavailable\n");
		    }
		} else {
		    printf("No card present\n");
		}
	    } else {
		printf("down\n");
	    }
	}
	printf("\n");
    }
}

/** List used for storing of task information. */
static sizedList_t tiList = { .actSize = 0, .list = NULL };

void PSIADM_ProcStat(int count, int full, char *nl)
{
    PSnodes_ID_t node;
    PSP_Info_t what = full ? PSP_INFO_QUEUE_ALLTASK : PSP_INFO_QUEUE_NORMTASK;
    PSP_taskInfo_t *taskInfo;
    int width = PSC_getWidth(), usedWidth;

    if (! getHostStatus()) return;

    if (!extendList(&tiList,
		    ((count<0) ? 20 : (count+1)) * sizeof(PSP_taskInfo_t),
		    __func__)) return;
    taskInfo = (PSP_taskInfo_t *)tiList.list;

    usedWidth = printf("%s\t%22s %22s %3s %5s %5s %3s ", "Node", "TaskId",
		       "ParentTaskId", "Con", "UID", "rank", "Cls");
    printf("%.*s\n", (width-usedWidth) > 0 ? width-usedWidth : 0, "Cmd");
    for (node=0; node<PSC_getNrOfNodes(); node++) {
	int numTasks = 0, task, displdTasks;

	if (nl && !nl[node]) continue;

	printf("%.*s\n", (width) > 0 ? width : 0,
	       "---------------------------------------------------------"
	       "---------------------------------------------------------");
	if (!hostStatus.list[node]) {
	    printf("%s\tdown\n", nodeString(node));
	    continue;
	}

	if (PSI_infoQueueReq(node, what, NULL) < 0) {
	    printf("Error!!\n");
	}

	/* Receive full queue, no output yet */
	/* This has to be splitted from the actual output due to
	 * PSI_infoString() calls there */
	while (PSI_infoQueueNext(what, &taskInfo[numTasks],
				 sizeof(*taskInfo), 1) > 0) {
	    if (taskInfo[numTasks].group==TG_FORWARDER && !full) continue;
	    if (taskInfo[numTasks].group==TG_SPAWNER && !full) continue;
	    if (taskInfo[numTasks].group==TG_GMSPAWNER && !full) continue;
	    if (taskInfo[numTasks].group==TG_PSCSPAWNER && !full) continue;
	    if (taskInfo[numTasks].group==TG_MONITOR && !full) continue;
	    if (taskInfo[numTasks].group==TG_SERVICE && !full) continue;
	    if (taskInfo[numTasks].group==TG_SERVICE_SIG && !full) continue;
	    if (taskInfo[numTasks].group==TG_KVS && !full) continue;
	    if (taskInfo[numTasks].group==TG_ACCOUNT && !full) continue;
	    if (taskInfo[numTasks].group==TG_DELEGATE && !full) continue;
	    numTasks++;
	    if (numTasks*sizeof(*taskInfo) >= tiList.actSize) {
		if (extendList(&tiList, tiList.actSize * 2, __func__)) {
		    taskInfo = (PSP_taskInfo_t *)tiList.list;
		} else {
		    return;
		}
	    }
	}

	/* Now do the output */
	displdTasks = (count<0) ? numTasks : (numTasks<count) ? numTasks:count;
	for (task=0; task < displdTasks; task++) {
	    usedWidth = printf("%s\t", nodeString(node));
	    /* Adapt to actual width due to <TAB> */
	    usedWidth = ((usedWidth-1)/8 + 1) * 8;
	    usedWidth += printf("%22s ", PSC_printTID(taskInfo[task].tid));
	    usedWidth += printf("%22s ", PSC_printTID(taskInfo[task].ptid));
	    usedWidth += printf("%2d  ", taskInfo[task].connected);
	    usedWidth += printf("%5d ", taskInfo[task].uid);
	    usedWidth += printf("%5d ", taskInfo[task].rank);
	    usedWidth += printf("%s",
		   taskInfo[task].group==TG_ADMIN ? "(A)" :
		   taskInfo[task].group==TG_LOGGER ? "(L)" :
		   taskInfo[task].group==TG_FORWARDER ? "(F)" :
		   taskInfo[task].group==TG_SPAWNER ? "(S)" :
		   taskInfo[task].group==TG_GMSPAWNER ? "(S)" :
		   taskInfo[task].group==TG_PSCSPAWNER ? "(S)" :
		   taskInfo[task].group==TG_MONITOR ? "(M)" :
		   taskInfo[task].group==TG_ADMINTASK ? "(*)" :
		   taskInfo[task].group==TG_KVS ? "(K)" :
		   taskInfo[task].group==TG_SERVICE ? "(S)" :
		   taskInfo[task].group==TG_SERVICE_SIG ? "(S)" :
		   taskInfo[task].group==TG_ACCOUNT ? "(C)" :
		   taskInfo[task].group==TG_DELEGATE ? "(D)" :
		   " ");

	    {
		pid_t pid = PSC_getPID(taskInfo[task].tid);
		char cmdline[8096] = { '\0' };
		PSI_infoString(node, PSP_INFO_CMDLINE, &pid,
			       cmdline, sizeof(cmdline), 0);
		printf("%.*s",
		       (width-usedWidth) > 0 ? width-usedWidth : 0, cmdline);
	    }
	    printf("\n");
	}
	if (numTasks>displdTasks) {
	    printf(" + %d more tasks\n", numTasks-displdTasks);
	}
    }
}

void PSIADM_LoadStat(char *nl)
{
    PSnodes_ID_t node;
    float *loads;
    uint16_t *taskNumFull, *taskNumNorm, *taskNumAlloc;
    uint8_t *exclusiveFlag;

    if (! getHostStatus()) return;
    if (! getLoads()) return;
    loads = (float *)ldList.list;
    if (! getTaskNum(PSP_INFO_LIST_ALLJOBS)) return;
    taskNumFull = (uint16_t *) tnfList.list;
    if (! getTaskNum(PSP_INFO_LIST_NORMJOBS)) return;
    taskNumNorm = (uint16_t *) tnnList.list;
    if (! getTaskNum(PSP_INFO_LIST_ALLOCJOBS)) return;
    taskNumAlloc = (uint16_t *) tnaList.list;
    if (! getExclusiveFlags()) return;
    exclusiveFlag = (uint8_t *) exclList.list;

    printf("Node\t\t Load\t\t     Jobs\n");
    printf("\t  1 min\t  5 min\t 15 min\t  tot.\t norm.\talloc.\texclusive\n");

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;
	printf("%s\t", nodeString(node));
	if (hostStatus.list[node]) {
	    printf("%7.2f\t%7.2f\t%7.2f\t%6d\t%6d\t%6d\t%5d\n",
		   loads[3*node+0], loads[3*node+1], loads[3*node+2],
		   taskNumFull[node], taskNumNorm[node], taskNumAlloc[node],
		   exclusiveFlag[node]);
	} else {
	    printf("down\n");
	}
    }
}

void PSIADM_MemStat(char *nl)
{
    PSnodes_ID_t node;
    uint64_t *memory;

    if (! getHostStatus()) return;
    if (! getMem()) return;
    memory = (uint64_t *)memList.list;

    printf("Node\t\t\tMemory\n");
    printf("\t\t total\t\t free\n");

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;
	printf("%s\t", nodeString(node));
	if (hostStatus.list[node]) {
	    printf("%15llu\t%15llu\n",
		   (long long unsigned int) memory[2*node+0],
		   (long long unsigned int) memory[2*node+1]);
	} else {
	    printf("down\n");
	}
    }
}

void PSIADM_HWStat(char *nl)
{
    PSnodes_ID_t node;
    uint32_t *hwStatus;
    uint16_t *physCPUs, *virtCPUs;

    if (! getHostStatus()) return;
    if (! getHWStat()) return;
    hwStatus = (uint32_t *)hwList.list;
    if (! getPhysCPUs()) return;
    physCPUs = (uint16_t *)pcpuList.list;
    if (! getVirtCPUs()) return;
    virtCPUs = (uint16_t *)vcpuList.list;

    printf("Node\t CPUs\t Available Hardware\n");
    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;

	printf("%s\t", nodeString(node));
	if (hostStatus.list[node]) {
	    printf("%2d/%2d\t %s\n", virtCPUs[node], physCPUs[node],
		   PSI_printHWType(hwStatus[node]));
	} else {
	    printf("down\n");
	}
    }
}

void PSIADM_PluginStat(char *nl)
{
    PSnodes_ID_t node;
    PSP_Info_t what = PSP_INFO_QUEUE_PLUGINS;
    int width = PSC_getWidth(), usedWidth;
    char line[512], *nodeStr;

    if (! getHostStatus()) return;
    if (width < 20) {
	printf("Line too short\n");
	return;
    }

    usedWidth = printf(" %s\t", "Node");
    /* Adapt to actual width due to <TAB> */
    usedWidth = ((usedWidth-1)/8 + 1) * 8;
    usedWidth += printf("%16s   %3s   ", "Plugin", "Ver");
    printf("%.*s\n", (width-usedWidth) > 0 ? width-usedWidth : 0, "Used by");
    for (node=0; node<PSC_getNrOfNodes(); node++) {
	int firstline = 1;

	if (nl && !nl[node]) continue;

	printf("%.*s\n", (width) > 0 ? width : 0,
	       "---------------------------------------------------------"
	       "---------------------------------------------------------");
	nodeStr = nodeString(node);

	if (!hostStatus.list[node]) {
	    printf("%s\tdown\n", nodeStr);
	    continue;
	}

	if (PSI_infoQueueReq(node, what, NULL) < 0) {
	    printf("Error!!\n");
	}

	while (PSI_infoQueueNext(what, line, sizeof(line), 1) > 0) {
	    if (firstline) {
		usedWidth = printf("%s", nodeStr);
		firstline = 0;
	    } else {
		usedWidth = printf("%*s", usedWidth, "");
	    }
	    printf("\t%.*s\n", width-usedWidth, line);
	}
    }
}

void PSIADM_EnvStat(char *key, char *nl)
{
    PSnodes_ID_t node;
    PSP_Info_t what = PSP_INFO_QUEUE_ENVS;
    int width = PSC_getWidth(), usedWidth;
    char line[BufTypedMsgSize], *nodeStr;

    if (! getHostStatus()) return;
    if (width < 20) {
	printf("Line too short\n");
	return;
    }

    usedWidth = printf("%4s  %s\n", "Node", "<key>=<value>");
    for (node=0; node<PSC_getNrOfNodes(); node++) {
	int firstline = 1;

	if (nl && !nl[node]) continue;

	printf("%.*s\n", (width) > 0 ? width : 0,
	       "---------------------------------------------------------"
	       "---------------------------------------------------------");
	nodeStr = nodeString(node);

	if (!hostStatus.list[node]) {
	    printf("%s\tdown\n", nodeStr);
	    continue;
	}

	if (PSI_infoQueueReq(node, what, key) < 0) {
	    printf("Error!!\n");
	}

	while (PSI_infoQueueNext(what, line, sizeof(line), 1) > 0) {
	    if (firstline) {
		usedWidth = printf("%s", nodeStr);
		firstline = 0;
	    } else {
		usedWidth = printf("%*s", usedWidth, "");
	    }
	    printf("\t%s\n", line);
	}
    }
}

static void printNodes(int num, PSnodes_ID_t *nodes, int width, int offset)
{
    int i=0, pos=0;
    int myWidth = (width<20) ? 20 : width;
    char range[128];

    printf(" ");
    while (i < num) {
	PSnodes_ID_t cur = nodes[i], loopCur;
	int rep = 0, loopStep=0, loopRep;

	while(i<num && nodes[i] == cur) {
	    rep++;
	    i++;
	}
	snprintf(range, sizeof(range), "%d", cur);
	if (nodes[i] == cur+1 || nodes[i] == cur-1) loopStep = nodes[i] - cur;

	loopCur = cur + loopStep;
	while (i<num && nodes[i]==loopCur) {
	    int j=i;
	    loopRep = 0;

	    while(j<num && nodes[j] == loopCur) {
		loopRep++;
		j++;
	    }
	    if (loopRep != rep) break;
	    i=j;
	    loopCur += loopStep;
	}
	if (loopCur != cur+loopStep) {
	    snprintf(range+strlen(range), sizeof(range)-strlen(range),
		     "-%d", loopCur-loopStep);
	}
	if (rep>1) {
	    snprintf(range+strlen(range), sizeof(range)-strlen(range),
		     "(#%d)", rep);
	}
	pos+=strlen(range)+1;
	if (pos > myWidth) {
	    printf("\n%*s", offset, "");
	    pos = strlen(range)+1;
	}
	printf("%s%s", range, (i < num) ? "," : "");
    }
}

static void printOldSlots(int num, PSpart_oldSlot_t *slots, int width,
			  int offset)
{
    int i=0, pos=0;
    int myWidth = (width<20) ? 20 : width;
    char range[128];

    printf(" ");
    while (i < num) {
	PSnodes_ID_t cur = slots[i].node, loopCur;
	int rep = 0, loopStep=0, loopRep;

	while(i<num && slots[i].node == cur) {
	    rep++;
	    i++;
	}
	snprintf(range, sizeof(range), "%d", cur);
	if (slots[i].node == cur+1 || slots[i].node == cur-1)
	    loopStep = slots[i].node - cur;

	loopCur = cur + loopStep;
	while (i<num && slots[i].node==loopCur) {
	    int j=i;
	    loopRep = 0;

	    while(j<num && slots[j].node == loopCur) {
		loopRep++;
		j++;
	    }
	    if (loopRep != rep) break;
	    i=j;
	    loopCur += loopStep;
	}
	if (loopCur != cur+loopStep) {
	    snprintf(range+strlen(range), sizeof(range)-strlen(range),
		     "-%d", loopCur-loopStep);
	}
	if (rep>1) {
	    snprintf(range+strlen(range), sizeof(range)-strlen(range),
		     "(%d)", rep);
	}
	pos+=strlen(range)+1;
	if (pos > myWidth) {
	    printf("\n%*s", offset, "");
	    pos = strlen(range)+1;
	}
	printf("%s%s", range, (i < num) ? "," : "");
    }
}

static void printSlots(int num, PSpart_slot_t *slots, int width, int offset)
{
    int i=0, pos=0;
    int myWidth = (width<20) ? 20 : width;
    char range[128];

    /* @todo pinning Smarter output: Print which slots to use. */

    printf(" ");
    while (i < num) {
	PSnodes_ID_t cur = slots[i].node, loopCur;
	int rep = 0, loopStep=0, loopRep;

	while(i<num && slots[i].node == cur) {
	    rep++;
	    i++;
	}
	snprintf(range, sizeof(range), "%d", cur);
	if (slots[i].node == cur+1 || slots[i].node == cur-1)
	    loopStep = slots[i].node - cur;

	loopCur = cur + loopStep;
	while (i<num && slots[i].node==loopCur) {
	    int j=i;
	    loopRep = 0;

	    while(j<num && slots[j].node == loopCur) {
		loopRep++;
		j++;
	    }
	    if (loopRep != rep) break;
	    i=j;
	    loopCur += loopStep;
	}
	if (loopCur != cur+loopStep) {
	    snprintf(range+strlen(range), sizeof(range)-strlen(range),
		     "-%d", loopCur-loopStep);
	}
	if (rep>1) {
	    snprintf(range+strlen(range), sizeof(range)-strlen(range),
		     "(%d)", rep);
	}
	pos+=strlen(range)+1;
	if (pos > myWidth) {
	    printf("\n%*s", offset, "");
	    pos = strlen(range)+1;
	}
	printf("%s%s", range, (i < num) ? "," : "");
    }
}

static int getMasterProtocolVersion(int daemonProto)
{
    PSnodes_ID_t master = -1;
    PSP_Option_t opt = PSP_OP_MASTER;
    PSP_Optval_t val;
    int err, protoVersion = 0;

    /* Identify master */
    err = PSI_infoOption(-1, 1, &opt, &val, 0);
    if (err != -1) {
	switch (opt) {
	case PSP_OP_MASTER:
	    master = val;
	    break;
	case PSP_OP_UNKNOWN:
	    printf(" PSP_OP_MASTER unknown\n");
	    break;
	default:
	    printf(" got option type %d\n", opt);
	}
    } else {
	printf(" error getting info\n");
    }

    if (master < 0) return -1;

    /* Get master's PSprotocol version */
    opt = daemonProto ? PSP_OP_DAEMONPROTOVERSION : PSP_OP_PROTOCOLVERSION;
    err = PSI_infoOption(master, 1, &opt, &val, 0);
    if (err != -1) {
	switch (opt) {
	case PSP_OP_PROTOCOLVERSION:
	    protoVersion = val;
	    break;
	case PSP_OP_DAEMONPROTOVERSION:
	    protoVersion = val;
	    break;
	case PSP_OP_UNKNOWN:
	    printf(" PSP_OP_PROTOCOLVERSION unknown\n");
	    break;
	default:
	    printf(" got option type %d\n", opt);
	}
    } else {
	printf(" error getting info\n");
    }

    return protoVersion;
}

void PSIADM_InstdirStat(char *nl)
{
    PSnodes_ID_t node;
    char instDir[1500];
    size_t instDirWidth = PSC_getWidth()-8;

    if (! getHostStatus()) return;

    printf("%4s\t%s\n", "Node", "Installation directory");
    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;
	printf("%s\t", nodeString(node));
	if (hostStatus.list[node]) {
	    int err = PSI_infoString(node, PSP_INFO_INSTDIR, NULL, instDir,
				     sizeof(instDir), 1);
	    if (!err) {
		size_t len = strlen(instDir);
		if (len) {
		    if (len <= instDirWidth) {
			printf("%s\n", instDir);
		    } else {
			printf("...%s\n", instDir+len-(instDirWidth-3));
		    }
		} else {
		    printf("<none>\n");
		}
	    } else {
		printf("\n");
	    }
	} else {
	    printf("down\n");
	}
    }
}

void PSIADM_JobStat(PStask_ID_t task, PSpart_list_t opt)
{
    PSP_Info_t what = PSP_INFO_QUEUE_PARTITION;
    char buf[sizeof(PStask_ID_t)
	     +sizeof(PSpart_list_t)
	     +sizeof(PSpart_request_t)];
    int recvd, masterPSPversion = getMasterProtocolVersion(0);
    int found = 0, masterDaemonPSPversion = getMasterProtocolVersion(1);
    PStask_ID_t rootTID, parentTID;
    PSpart_request_t *req;

    int width = PSC_getWidth();

    /* Determine root process of given task */
    rootTID=parentTID=task;
    while (parentTID) {
	int ret;

	ret = PSI_infoTaskID(-1, PSP_INFO_PARENTTID, &rootTID, &parentTID, 1);
	if (ret) {
	    printf("root-task for task '%s' not found:", PSC_printTID(task));
	    printf(" unknown task '%s'\n", PSC_printTID(rootTID));
	    return;
	}
	if (parentTID) rootTID = parentTID;
    }

    if (PSI_infoQueueReq(-1, what, &opt) < 0) {
	printf("Error!!\n");
	return;
    }

    req = PSpart_newReq();

    while ((recvd=PSI_infoQueueNext(what, buf, sizeof(buf), 1)) > 0) {
	static PSpart_slot_t *slots = NULL;
	static char *slotBuf = NULL;
	static size_t slotsSize = 0, slotBufSize = 0;

	PStask_ID_t tid;
	PSpart_list_t flags;
	int len = 0;

	tid = *(PStask_ID_t *)(buf+len);
	len += sizeof(PStask_ID_t);

	flags = *(PSpart_list_t *)(buf+len);
	len += sizeof(PSpart_list_t);

	len += PSpart_decodeReq(buf + len, req, masterDaemonPSPversion);
	if (masterDaemonPSPversion < 401) {
	    req->tpp = 1;
	} else if (len != recvd) {
	    printf("Wrong number of bytes received (used %ld vs. rcvd %ld)!\n",
		   (long)len, (long)recvd);
	    break;
	}

	if (req->num) {
	    size_t itemSize;

	    if (req->num * sizeof(PSpart_slot_t) > slotBufSize) {
		slotBufSize = 2 * req->num * sizeof(PSpart_slot_t);
		slotBuf = realloc(slotBuf, slotBufSize);
	    }
	    if (req->num * sizeof(*slots) > slotsSize) {
		slotsSize = 2 * req->num * sizeof(*slots);
		slots = realloc(slots, slotsSize);
	    }

	    if (!slotBuf || !slots) {
		printf("No memory\n");
		break;
	    }

	    recvd=PSI_infoQueueNext(what, slotBuf, slotBufSize, 1);

	    if (masterPSPversion < 334) {
		itemSize = sizeof(PSnodes_ID_t);
	    } else if (masterDaemonPSPversion < 401) {
		itemSize = sizeof(PSpart_oldSlot_t);
	    } else {
		itemSize = sizeof(PSnodes_ID_t);
		if (masterDaemonPSPversion < 408) {
		    itemSize += PSCPU_bytesForCPUs(32);
		} else {
		    itemSize += *(uint16_t *)slotBuf;
		    recvd -= sizeof(uint16_t);
		}
	    }

	    if ((unsigned int)recvd != req->num * itemSize) {
		printf("Message lost. Suppress node-list\n");
		req->num = 0;
	    }
	}

	if (!task || rootTID == tid) {
	    if (!found) {
		printf("%22s %5s %5s %5s %5s %-*s\n",
		       "RootTaskId", "State", "Size", " UID", " GID",
		       width-47, req->num ? "Target Slots" : "Starttime");
	    }

	    printf("%22s", PSC_printTID(tid));
	    printf("   %c  ", (flags & PART_LIST_PEND) ? 'P' :
		   (flags & PART_LIST_RUN) ? 'R' :
		   (flags & PART_LIST_SUSP) ? 'S' : '?');
	    printf(" %5d", req->size);
	    printf(" %5d", req->uid);
	    printf(" %5d", req->gid);
	    if (req->num) {
		if (masterPSPversion < 334) {
		    printNodes(req->num, (PSnodes_ID_t*)slotBuf, width-47, 47);
		} else if (masterDaemonPSPversion < 401) {
		    printOldSlots(req->num, (PSpart_oldSlot_t *) slotBuf,
				  width-47, 47);
		} else {
		    size_t nBytes, myBytes = PSCPU_bytesForCPUs(PSCPU_MAX);
		    char *ptr = slotBuf;
		    int n;
		    if (masterDaemonPSPversion < 408) {
			nBytes = PSCPU_bytesForCPUs(32);
		    } else {
			nBytes = *(uint16_t *)ptr;
			ptr +=  sizeof(uint16_t);
		    }
		    if (nBytes > myBytes) {
			printf("warning: slots might be truncated");
		    }
		    for (n = 0; n < req->num; n++) {
			slots[n].node = *(PSnodes_ID_t *)ptr;
			ptr += sizeof(PSnodes_ID_t);

			PSCPU_clrAll(slots[n].CPUset);
			PSCPU_inject(slots[n].CPUset, ptr, nBytes);
			ptr += nBytes;
		    }
		    printSlots(req->num, slots, width-47, 47);
		}
		printf("\n");
	    } else if (masterDaemonPSPversion < 407) {
		printf(" unknown\n");
	    } else {
		time_t startTime = req->start;
		printf(" %s", req->start ? ctime(&startTime) : "unknown\n");
	    }

	    found=1;
	}
    }

    if (task && !found)	printf("task '%s' not found.\n", PSC_printTID(task));

    PSpart_delReq(req);
}

void PSIADM_VersionStat(char *nl)
{
    PSnodes_ID_t node;

    if (! getHostStatus()) return;

    printf("%4s\t%8s %16s %6s\n", "Node", "psid  ", "RPM     ", "Protocols");
    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;

	printf("%s\t", nodeString(node));
	if (hostStatus.list[node]) {
	    char psidver[100], rpmrev[100];
	    PSP_Optval_t optVal[2];
	    PSP_Option_t optType[] = { PSP_OP_PROTOCOLVERSION,
				       PSP_OP_DAEMONPROTOVERSION };
	    int err;

	    psidver[0] = rpmrev[0] = '\0';
	    err = PSI_infoString(node, PSP_INFO_DAEMONVER, NULL,
				 psidver, sizeof(psidver), 0);
	    if (err || strlen(psidver) < 11) {
	      strcpy(psidver, "$Revision: unknown$");
	    }
	    err = PSI_infoString(node, PSP_INFO_RPMREV, NULL,
				 rpmrev, sizeof(rpmrev), 0);
	    if (err) strcpy(rpmrev, "unknown");

	    printf("%8s\b  %16s ", psidver+11, rpmrev);

	    err = PSI_infoOption(node, 2, optType, optVal, 1);
	    if (err != -1) {
		int i;
		for (i=0; i<2; i++) {
		    switch (optType[i]) {
		    case PSP_OP_UNKNOWN:
			printf("unknown");
			break;
		    default:
			printf("%3d", optVal[i]);
		    }
		    if (i<2-1) printf("/");
		}
		printf("\n");
	    } else {
		printf(" error getting info\n");
	    }
	} else {
	    printf("down\n");
	}
    }
}

void PSIADM_SetParam(PSP_Option_t type, PSP_Optval_t value, char *nl)
{
    PSnodes_ID_t node;
    DDOptionMsg_t msg = {
	.header = {
	    .type = PSP_CD_SETOPTION,
	    .sender = PSC_getMyTID(),
	    .dest = 0,
	    .len = sizeof(msg) },
	.count = 1,
	.opt = { { .option = 0, .value = 0 } } };

    switch (type) {
    case PSP_OP_PROCLIMIT:
    case PSP_OP_SET_UID:
    case PSP_OP_ADD_UID:
    case PSP_OP_REM_UID:
    case PSP_OP_SET_GID:
    case PSP_OP_ADD_GID:
    case PSP_OP_REM_GID:
    case PSP_OP_SET_ADMUID:
    case PSP_OP_ADD_ADMUID:
    case PSP_OP_REM_ADMUID:
    case PSP_OP_SET_ADMGID:
    case PSP_OP_ADD_ADMGID:
    case PSP_OP_REM_ADMGID:
	if (value < -1) {
	    printf(" value must be -1 <= val\n");
	    return;
	}
	break;
    case PSP_OP_PSIDSELECTTIME:
    case PSP_OP_RDPMAXRETRANS:
    case PSP_OP_RDPMAXACKPEND:
    case PSP_OP_RDPCLSDTMOUT:
    case PSP_OP_RDPRETRANS:
    case PSP_OP_STATUS_BCASTS:
    case PSP_OP_ACCTPOLL:
    case PSP_OP_KILLDELAY:
	if (value<0) {
	    printf(" value must be >= 0.\n");
	    return;
	}
	break;
    case PSP_OP_STATUS_DEADLMT:
    case PSP_OP_RDPRSNDTMOUT:
    case PSP_OP_MAXSTATTRY:
    case PSP_OP_PLUGINUNLOADTMOUT:
	if (value<1) {
	    printf(" value must be > 0.\n");
	    return;
	}
	break;
    case PSP_OP_STATUS_TMOUT:
    case PSP_OP_RDPTMOUT:
	if (value<MIN_TIMEOUT_MSEC) {
	    printf(" value must be >= %d.\n", MIN_TIMEOUT_MSEC);
	    return;
	}
	break;
    case PSP_OP_RDPPKTLOSS:
	if (value<0 || value>100) {
	    printf(" value must be 0 <= val <= 100.\n");
	    return;
	}
	break;
    case PSP_OP_MASTER:
	if (value<0 || value>=PSC_getNrOfNodes()) {
	    printf(" value must be 0 <= val < %d.\n", PSC_getNrOfNodes());
	    return;
	}
	break;
    case PSP_OP_RL_AS:
    case PSP_OP_RL_CORE:
    case PSP_OP_RL_CPU:
    case PSP_OP_RL_DATA:
    case PSP_OP_RL_FSIZE:
    case PSP_OP_RL_LOCKS:
    case PSP_OP_RL_MEMLOCK:
    case PSP_OP_RL_MSGQUEUE:
    case PSP_OP_RL_NOFILE:
    case PSP_OP_RL_NPROC:
    case PSP_OP_RL_RSS:
    case PSP_OP_RL_SIGPENDING:
    case PSP_OP_RL_STACK:
	if (value != (PSP_Optval_t)RLIM_INFINITY && value < 0) {
	    printf(" value must be >= 0 or 'unlimited'.\n");
	    return;
	}
	break;
    case PSP_OP_PSIDDEBUG:
    case PSP_OP_RDPDEBUG:
    case PSP_OP_MCASTDEBUG:
    case PSP_OP_FREEONSUSP:
    case PSP_OP_NODESSORT:
    case PSP_OP_OVERBOOK:
    case PSP_OP_STARTER:
    case PSP_OP_RUNJOBS:
    case PSP_OP_EXCLUSIVE:
    case PSP_OP_PINPROCS:
    case PSP_OP_BINDMEM:
    case PSP_OP_ALLOWUSERMAP:
    case PSP_OP_SUPPL_GRPS:
    case PSP_OP_RDPSTATISTICS:
	break;
    default:
	printf("%s: cannot handle option type %d.\n", __func__, type);
	return;
    }

    /*
     * prepare the message to send it to the daemon
     */
    msg.opt[0] = (DDOption_t) { .option = type, .value = value };

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;

	if (hostStatus.list[node]) {
	    msg.header.dest = PSC_getTID(node, 0);
	    PSI_sendMsg(&msg);
	}
    }
}

void PSIADM_SetParamList(PSP_Option_t type, PSIADM_valList_t *val, char *nl)
{
    PSnodes_ID_t node;
    DDOptionMsg_t msg = {
	.header = {
	    .type = PSP_CD_SETOPTION,
	    .sender = PSC_getMyTID(),
	    .dest = 0,
	    .len = sizeof(msg) },
	.count = 0,
	.opt = { { .option = 0, .value = 0 } } };
    unsigned int i;

    if (!val) {
	printf("%s: No value-list given.\n", __func__);
	return;
    }

    if (! getHostStatus()) return;

    switch (type) {
    case PSP_OP_CPUMAP:
	msg.opt[(int) msg.count].option = PSP_OP_CLR_CPUMAP;
	msg.opt[(int) msg.count].value = 0;
	msg.count++;

	for (i=0; i<val->num; i++) {
	    msg.opt[(int) msg.count].option = PSP_OP_APP_CPUMAP;
	    msg.opt[(int) msg.count].value = val->value[i];

	    msg.count++;
	    if (msg.count == DDOptionMsgMax) {
		for (node=0; node<PSC_getNrOfNodes(); node++) {
		    if (nl && !nl[node]) continue;

		    if (hostStatus.list[node]) {
			msg.header.dest = PSC_getTID(node, 0);
			PSI_sendMsg(&msg);
		    }
		}
		msg.count = 0;
	    }
	}

	if (msg.count) {
	    for (node=0; node<PSC_getNrOfNodes(); node++) {
		if (nl && !nl[node]) continue;

		if (hostStatus.list[node]) {
		    msg.header.dest = PSC_getTID(node, 0);
		    PSI_sendMsg(&msg);
		}
	    }
	}
	break;
    default:
	printf("%s: cannot handle option type %d.\n", __func__, type);
	return;
    }
}

void PSIADM_ShowParam(PSP_Option_t type, char *nl)
{
    PSnodes_ID_t node;
    PSP_Optval_t value;
    int ret;

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;

	printf("%s\t", nodeString(node));
	if (hostStatus.list[node]) {
	    PSP_Option_t t = type;
	    ret = PSI_infoOption(node, 1, &t, &value, 1);
	    if (ret != -1) {
		switch (t) {
		case PSP_OP_PROCLIMIT:
		    if (value==-1)
			printf("ANY\n");
		    else
			printf("%d\n", value);
		    break;
		case PSP_OP_PSIDDEBUG:
		case PSP_OP_RDPDEBUG:
		case PSP_OP_MCASTDEBUG:
		    printf("0x%x\n", value);
		    break;
		case PSP_OP_NODESSORT:
		    printf("%s\n", (value == PART_SORT_PROC) ? "PROC" :
			   (value == PART_SORT_LOAD_1) ? "LOAD_1" :
			   (value == PART_SORT_LOAD_5) ? "LOAD_5" :
			   (value == PART_SORT_LOAD_15) ? "LOAD_15" :
			   (value == PART_SORT_PROCLOAD) ? "PROC+LOAD" :
			   (value == PART_SORT_NONE) ? "NONE" : "UNKNOWN");
		    break;
		case PSP_OP_OVERBOOK:
		    if (value==OVERBOOK_AUTO) {
			printf("AUTO\n");
			break;
		    }
		case PSP_OP_FREEONSUSP:
		case PSP_OP_EXCLUSIVE:
		case PSP_OP_RUNJOBS:
		case PSP_OP_STARTER:
		case PSP_OP_PINPROCS:
		case PSP_OP_BINDMEM:
		case PSP_OP_ALLOWUSERMAP:
		case PSP_OP_SUPPL_GRPS:
		    printf("%s\n", value ? "TRUE" : "FALSE");
		    break;
		case PSP_OP_UNKNOWN:
		    printf("unknown option\n");
		    break;
		default:
		    printf("%d\n", value);
		}
	    } else {
		printf("Cannot get\n");
	    }
	}else {
	    printf("down\n");
	}
    }
}

void PSIADM_ShowParamList(PSP_Option_t type, char *nl)
{
    PSnodes_ID_t node;
    DDOption_t options[DDOptionMsgMax];
    int ret;

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	int total=0;
	if (nl && !nl[node]) continue;

	printf("%s\t", nodeString(node));
	if (!hostStatus.list[node]) {
	    printf("down\n");
	    continue;
	}

	ret = PSI_infoOptionList(node, type);
	if (ret == -1) {
	    printf("Cannot get\n");
	    continue;
	}
	do {
	    int i;

	    ret = PSI_infoOptionListNext(options, DDOptionMsgMax, 1);
	    if (ret == -1) {
		printf("error getting info");
		break;
	    }

	    for (i=0; i<ret; i++, total++) {
		switch (options[i].option) {
		case PSP_OP_ACCT:
		    if (total) printf(", ");
		    printf("%s", PSC_printTID(options[i].value));
		    break;
		case PSP_OP_UID:
		case PSP_OP_ADMUID:
		    if (total) printf(", ");
		    if (options[i].value==-1)
			printf("ANY");
		    else {
			struct passwd *passwd = getpwuid(options[i].value);
			if (passwd) {
			    printf("%s", passwd->pw_name);
			} else {
			    printf("uid %d", options[i].value);
			}
		    }
		    break;
		case PSP_OP_GID:
		case PSP_OP_ADMGID:
		    if (total) printf(", ");
		    if (options[i].value==-1)
			printf(" ANY");
		    else {
			struct group *group = getgrgid(options[i].value);
			if (group) {
			    printf("%s", group->gr_name);
			} else {
			    printf("gid %d", options[i].value);
			}
		    }
		    break;
		case PSP_OP_RL_AS:
		case PSP_OP_RL_CORE:
		case PSP_OP_RL_CPU:
		case PSP_OP_RL_DATA:
		case PSP_OP_RL_FSIZE:
		case PSP_OP_RL_LOCKS:
		case PSP_OP_RL_MEMLOCK:
		case PSP_OP_RL_MSGQUEUE:
		case PSP_OP_RL_NOFILE:
		case PSP_OP_RL_NPROC:
		case PSP_OP_RL_RSS:
		case PSP_OP_RL_SIGPENDING:
		case PSP_OP_RL_STACK:
		    if (total) printf(" / ");
		    if (options[i].value == (PSP_Optval_t)RLIM_INFINITY) {
			printf("unlimited");
		    } else {
			printf(paramHexFormat ? "0x%x" : "%d",
			       options[i].value);
		    }
		    break;
		case PSP_OP_CPUMAP:
		    if (total) printf(" ");
		    printf("%d", options[i].value);
		    break;
		case PSP_OP_UNKNOWN:
		    printf("unknown option");
		case PSP_OP_LISTEND:
		    goto next_node;
		    break;
		default:
		    printf("unknown type 0x%x", options[i].option);
		    goto next_node;
		}
	    }
	} while (1);
    next_node:
	printf("\n");
    }
}

/**
 * Flag to mark an ongoing restart of the local daemon. Thus SIGTERM
 * signals sent by the daemon can be ignored if different from 0.
 */
static int doRestart = 0;

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
	    PSIadm_log(-1, "%s: can't contact my own daemon.\n", __func__);
	    exit(-1);
	}
	doRestart = 0;
	signal(SIGTERM, PSIADM_sighandler);

	break;
    }
}

void PSIADM_Reset(int reset_hw, char *nl)
{
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_DAEMONRESET,
	    .sender = PSC_getMyTID(),
	    .dest = 0,
	    .len = sizeof(msg.header) + sizeof(int32_t) },
	.buf = { 0 } };
    int32_t *action = (int32_t *)msg.buf;
    PSnodes_ID_t node;
    int send_local = 0;

    if (geteuid()) {
	printf("Insufficient privilege\n");
	return;
    }

    *action = 0;
    if (reset_hw) {
	*action |= PSP_RESET_HW;
	doRestart = 1;
    }

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;

	if (hostStatus.list[node]) {
	    if (node == PSC_getMyID()) {
		send_local = 1;
	    } else {
		msg.header.dest = PSC_getTID(node, 0);
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
    pid_t childPID;
    int status;

    dir = PSC_lookupInstalldir(NULL);
    if (dir) {
	if (chdir(dir)<0) {
	    printf("Cannot change to directory '%s'.\n", dir);
	    return;
	}
    } else {
	printf("Cannot find 'test_nodes'.\n");
	return;
    }

    childPID = fork();
    if (!childPID) {
	/* This is the child */
	char command[100];

	snprintf(command, sizeof(command),
		 "./bin/test_nodes -np %d", PSC_getNrOfNodes());

	setenv("PSI_LOOP_NODES_FIRST", "", 1);
	unsetenv("PSI_NODES");

	if (system(command) < 0) {
	    printf("Cant execute %s : %s\n", command, strerror(errno));
	}

	exit(0);
    }

    waitpid(childPID, &status, 0);
}

void PSIADM_KillProc(PStask_ID_t tid, int sig)
{
    if (sig == -1) sig = SIGTERM;

    if (sig < 0) {
	fprintf(stderr, "Unknown signal %d.\n", sig);
    } else {
	char *errstr;

	int ret = PSI_kill(tid, sig, 0);

	switch (ret) {
	case -2:
	case -1:
	case 0:
	    break;
	default:
	    errstr = strerror(ret);
	    if (!errstr) errstr = "UNKNOWN";
	    printf("%s(%s, %d): %s\n",
		   __func__, PSC_printTID(tid), sig, errstr);
	}
    }
}

void PSIADM_Resolve(char *nl)
{
    PSnodes_ID_t node;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	struct in_addr hostaddr;
	struct hostent *hp;
	char *ptr;
	int err;

	if ((nl && !nl[node])) continue;

	printf("%4d\t", node);
	err = PSI_infoUInt(-1, PSP_INFO_NODE, &node, &hostaddr.s_addr, 0);
	if (err || (hostaddr.s_addr == INADDR_ANY)) {
	    printf("<unknown>\n");
	    continue;
	}

	hp = gethostbyaddr(&hostaddr.s_addr, sizeof(hostaddr.s_addr), AF_INET);
	if (hp) {
	    if ((ptr = strchr (hp->h_name, '.'))) *ptr = '\0';
	    printf("%s\n", hp->h_name);
	} else {
	    printf("%s\n", inet_ntoa(hostaddr));
	}

    }
}

void PSIADM_Plugin(char *nl, char *name, PSP_Plugin_t action)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_PLUGIN,
	    .sender = PSC_getMyTID(),
	    .dest = 0,
	    .len = sizeof(msg.header) + sizeof(msg.type) },
	.buf = { 0 } };
    DDTypedMsg_t answer;
    PSnodes_ID_t node;

    msg.type = action;

    if (!PSP_putTypedMsgBuf(&msg, __func__, "plugin", name, PSP_strLen(name)))
	return;

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;

	if (hostStatus.list[node]) {
	    msg.header.dest = PSC_getTID(node, 0);
	    PSI_sendMsg(&msg);
	    if (PSI_recvMsg((DDMsg_t *)&answer, sizeof(answer)) < 0) {
		printf("%soading plugin '%s' on node %s failed\n",
		       action ? "Unl" : "L", name, nodeString(node));
	    }
	    if (answer.type == -1) {
		printf("Cannot %sload plugin '%s' on node %s\n",
		       action ? "un" : "", name, nodeString(node));
	    } else if (answer.type) {
		printf("Cannot %sload plugin '%s' on node %s: %s\n",
		       action ? "un" : "", name, nodeString(node),
		       strerror(answer.type));
	    }
	} else {
	    printf("%s\tdown\n", nodeString(node));
	}
    }
}

static int recvPluginKeyAnswers(PStask_ID_t src, PSP_Plugin_t action,
				char *nodeStr)
{
    DDTypedBufferMsg_t answer;
    int first = 1;

    while (1) {
	if (PSI_recvMsg((DDMsg_t *)&answer, sizeof(answer)) < 0) {
	    int eno = errno;
	    char *errStr = strerror(eno);
	    printf("%s: failed to receive answer from %s: %s\n", __func__,
		   PSC_printTID(src), errStr ? errStr : "Unknown");
	    break;
	}
	if (answer.header.sender != src) {
	    printf("%s: wrong partner: %s", __func__,
		   PSC_printTID(answer.header.sender));
	    printf(" expected %s\n", PSC_printTID(src));
	    break;
	}

	if ((PSP_Plugin_t)answer.type == action && !strlen(answer.buf))
	    return !first;

	if (first) {
	    printf("%s", nodeStr);
	    first = 0;
	}

	if (answer.type == -1) {
	    printf("Unknown action\n");
	    break;
	} else if ((PSP_Plugin_t)answer.type != action) {
	    printf("wrong action: %d expected %d\n", answer.type, action);
	    break;
	}

	switch ((PSP_Plugin_t)answer.type) {
	case PSP_PLUGIN_AVAIL:
	    printf("\t");
	    break;
	default:
	    break;
	}

	printf("%s", answer.buf);
    }

    return 1;
}


void PSIADM_PluginKey(char *nl, char *name, char *key, char *value,
		      PSP_Plugin_t action)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_PLUGIN,
	    .sender = PSC_getMyTID(),
	    .dest = 0,
	    .len = sizeof(msg.header) + sizeof(msg.type) } };
    PSnodes_ID_t node;
    int width = PSC_getWidth(), separator = 0;

    msg.type = action;

    if (!PSP_putTypedMsgBuf(&msg, __func__, "plugin", name, PSP_strLen(name)))
	return;
    if (!PSP_putTypedMsgBuf(&msg, __func__, "key", key, PSP_strLen(key)))
	return;
    if (!PSP_putTypedMsgBuf(&msg, __func__, "value", value, PSP_strLen(value)))
	return;

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;

	if (separator) {
	    printf("%.*s\n", (width) > 0 ? width : 0,
		   "---------------------------------------------------------"
		   "---------------------------------------------------------");
	}

	if (hostStatus.list[node]) {
	    char *nodeStr = nodeString(node);
	    msg.header.dest = PSC_getTID(node, 0);
	    PSI_sendMsg(&msg);

	    separator = recvPluginKeyAnswers(msg.header.dest, action, nodeStr);
	} else {
	    printf("%s\tdown\n", nodeString(node));
	    separator = 1;
	}
    }
}

static int putEnv(DDTypedBufferMsg_t *msg, char *key, char *value)
{
    char env[sizeof(msg->buf)+2];

    snprintf(env, sizeof(env), "%s=%s", key, value);

    return PSP_putTypedMsgBuf(msg, __func__, "environment", env,
			      PSP_strLen(env));
}


void PSIADM_Environment(char *nl, char *key, char *value, PSP_Env_t action)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_ENV,
	    .sender = PSC_getMyTID(),
	    .dest = 0,
	    .len = sizeof(msg.header) + sizeof(msg.type) },
	.buf = { 0 } };
    DDTypedMsg_t answer;
    PSnodes_ID_t node;

    msg.type = action;
    switch (action) {
    case PSP_ENV_SET:
	if (!putEnv(&msg, key, value)) return;
	break;
    case PSP_ENV_UNSET:
	if (!PSP_putTypedMsgBuf(&msg, __func__, "key", key, PSP_strLen(key)))
	    return;
	break;
    default:
	printf("Unknown action %d\n", action);
	return;
    }

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;

	if (hostStatus.list[node]) {
	    msg.header.dest = PSC_getTID(node, 0);
	    PSI_sendMsg(&msg);
	    if (PSI_recvMsg((DDMsg_t *)&answer, sizeof(answer)) < 0) {
		printf("%ssetting '%s' on node %s failed\n",
		       action ? "Uns" : "S", key, nodeString(node));
	    }
	    if (answer.type == -1) {
		printf("Cannot %sset '%s' on node %s\n",
		       action ? "un" : "", key, nodeString(node));
	    } else if (answer.type) {
		printf("Cannot %sset '%s' on node %s: %s\n",
		       action ? "un" : "", key, nodeString(node),
		       strerror(answer.type));
	    }
	}
    }
}
