/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char lexid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>

#include "pscommon.h"
#include "parser.h"
#include "psprotocol.h"
#include "pstask.h"

#include "psi.h"
#include "psiinfo.h"
#include "psispawn.h"

#include "commands.h"

char commandsversion[] = "$Revision: 1.16 $";

/* @todo PSI_sendMsg(): Wrapper, control if sendMsg was successful or exit */


/** Simple array with current size attached */
typedef struct {
    size_t actSize;  /**< The actual size of the array @ref list */
    char *list;      /**< The array. */
} sizedList_t;

/**
 * @brief Extend a array.
 *
 * Extend the array @a list to provide at least @a size bytes of
 * content. If @list is allready larger than @a size, do nothing.
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

/** Simple wrapper for retrieval of host stati */
static inline int getHostStatus(void)
{
    return getFullList(&hostStatus, PSP_INFO_LIST_HOSTSTATUS, sizeof(char));
}

/** List used for storing of hardware stati. */
static sizedList_t hwList = { .actSize = 0, .list = NULL };

/** Simple wrapper for retrieval of hardware stati */
static inline int getHWStat(void)
{
    return getFullList(&hwList, PSP_INFO_LIST_HWSTATUS, sizeof(uint32_t));
}

/** List used for storing of task informations. */
static sizedList_t tiList = { .actSize = 0, .list = NULL };

/**
 * @brief Get a task list.
 *
 * Get a task list of node @a node. Up to @a count task information
 * structures are received from the daemon and stored to @ref
 * tiList. If the flag @a full is different from 0, information on all
 * kind of tasks is received. Otherwise only normal tasks, i.e. tasks
 * of task group @ref TG_ANY, are considered.
 *
 *
 * @param node The node from which the task list should be retrieved.
 *
 * @param count The number of tasks to store to @ref tiList.
 *
 * @param full Flag to mark, if all jobs or only "normal" jobs should
 * be stored to @ref tiList.
 *
 * @return On success, the number of task infos received and stored to
 * @a taskInfo is returned, or 0, if an error occurred.
 */
static inline int getTaskInfo(PSnodes_ID_t node, int count, int full)
{
    int tasks;
    PSP_Info_t what = full ? PSP_INFO_LIST_ALLTASKS : PSP_INFO_LIST_NORMTASKS;

    if (!extendList(&tiList, count * sizeof(PSP_taskInfo_t), __func__))
	return 0;

    tasks = PSI_infoList(node, what, NULL,
			 tiList.list, count * sizeof(PSP_taskInfo_t), 1);

    if (tasks < 0) {
	printf("%s: failed.\n", __func__);
	return 0;
    }
    return tasks / sizeof(PSP_taskInfo_t);
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


/* ---------------------------------------------------------------------- */

/** Delay between starting nodes in msec. @todo Make this configurable. */
const int delay = 50;

void PSIADM_AddNode(char *nl)
{
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_DAEMONSTART,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getTID(-1, 0),
	    .len = sizeof(msg.header) + sizeof(PSnodes_ID_t) },
	.buf = { 0 } };
    PSnodes_ID_t node;

    if (geteuid()) {
	printf("Insufficient priviledge\n");
	return;
    }

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;

	if (hostStatus.list[node]) {
	    /* printf("%d already up.\n", node); */
	} else {
	    printf("starting node %d\n", node);
	    *(PSnodes_ID_t *)msg.buf = node;
	    PSI_sendMsg(&msg);
	    usleep(delay * 1000);
	}
    }
    /* @todo check the success and repeat the startup */
}

void PSIADM_ShutdownNode(char *nl)
{
    DDMsg_t msg = {
	.type = PSP_CD_DAEMONSTOP,
	.sender = PSC_getMyTID(),
	.dest = 0,
	.len = sizeof(msg) };
    PSnodes_ID_t node;
    int send_local = 0;

    if (geteuid()) {
	printf("Insufficient priviledge\n");
	return;
    }

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;

	if (hostStatus.list[node]) {
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
	    .len = sizeof(msg.header) + sizeof(int32_t) },
	.buf = { 0 } };
    PSnodes_ID_t node;
    int hwnum, err;

    if (geteuid()) {
	printf("Insufficient priviledge\n");
	return;
    }

    err = PSI_infoInt(-1, PSP_INFO_HWNUM, NULL, &hwnum, 1);
    if (err || hw < -1 || hw >= hwnum) return;

    *(int32_t *)msg.buf = hw;

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;

	if (hostStatus.list[node]) {
	    msg.header.dest = PSC_getTID(node, 0);
	    PSI_sendMsg(&msg);
	} else {
	    printf("%4d down.\n", node);
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
	    .len = sizeof(msg.header) + sizeof(int32_t) },
	.buf = { 0 } };
    PSnodes_ID_t node;
    int hwnum, err;

    if (geteuid()) {
	printf("Insufficient priviledge\n");
	return;
    }

    err = PSI_infoInt(-1, PSP_INFO_HWNUM, NULL, &hwnum, 1);
    if (err || hw < -1 || hw >= hwnum) return;

    *(int32_t *)msg.buf = hw;

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;

	if (hostStatus.list[node]) {
	    msg.header.dest = PSC_getTID(node, 0);
	    PSI_sendMsg(&msg);
	} else {
	    printf("%4d down.\n", node);
	}
    }
}

void PSIADM_NodeStat(char *nl)
{
    PSnodes_ID_t node;

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;

	if (hostStatus.list[node]) {
	    printf("%4d up.\n", node);
	} else {
	    printf("%4d down.\n", node);
	}
    }
}

void PSIADM_SummaryStat(char *nl)
{
    PSnodes_ID_t node;
    int upNodes = 0, downNodes = 0;

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;

	if (hostStatus.list[node]) {
	    upNodes++;
	} else {
	    downNodes++;
	}
    }
    printf("Node status summary:  %d up   %d down  of %d total\n",
	   upNodes, downNodes, upNodes+downNodes);

    /* Also print list of down nodes if sufficiently less */
    if (downNodes && (downNodes < 20)) {
	printf("Down nodes are:");
	for (node=0; node<PSC_getNrOfNodes(); node++) {
	    if (nl && !nl[node]) continue;

	    if (!hostStatus.list[node]) {
		printf(" %d", node);
	    }
	}
	printf("\n");
    }
}

static char line[1024];

void PSIADM_RDPStat(char *nl)
{
    PSnodes_ID_t node, partner;

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;
	printf("%4d:\n", node);
	if (hostStatus.list[node]) {
	    for (partner=0; partner<PSC_getNrOfNodes(); partner++) {
		int err = PSI_infoString(node, PSP_INFO_RDPSTATUS, &partner,
					 line, sizeof(line), 1);
		if (!err) printf("%s\n", line);
	    }
	    printf("\n");
	} else {
	    printf("  down\n\n");
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

	    printf("%6d ", node);
	    if (hostStatus.list[node]) {
		if (! hwStatus[node] & 1<<hw) {
		    printf("    No card present\n");
		} else {
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

void PSIADM_ProcStat(int count, int full, char *nl)
{
    PSnodes_ID_t node;
    PSP_taskInfo_t *taskInfo;
    uint16_t *taskNum;
    int task, num;

    if (! getHostStatus()) return;
    if (! getTaskNum(full ? PSP_INFO_LIST_ALLJOBS : PSP_INFO_LIST_NORMJOBS))
	return;
    taskNum = full ? (uint16_t *) tnfList.list : (uint16_t *) tnnList.list;

    printf("%4s %22s %22s %3s %9s\n",
	   "Node", "TaskId", "ParentTaskId", "Con", "UserId");
    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;

	printf("---------------------------------------------------------"
	       "---------\n");
	if (hostStatus.list[node]) {
	    num = getTaskInfo(node, count, full);
	    taskInfo = (PSP_taskInfo_t *) tiList.list;
	    for (task=0; task<num; task++) {
		if (taskInfo[task].group==TG_FORWARDER && !full) continue;
		if (taskInfo[task].group==TG_SPAWNER && !full) continue;
		if (taskInfo[task].group==TG_GMSPAWNER && !full) continue;
		if (taskInfo[task].group==TG_MONITOR && !full) continue;
		printf("%4d ", node);
		printf("%22s ", PSC_printTID(taskInfo[task].tid));
		printf("%22s ", PSC_printTID(taskInfo[task].ptid));
		printf("%2d  ", taskInfo[task].connected);
		printf("%5d ", taskInfo[task].uid);
		printf("%s",
		       taskInfo[task].group==TG_ADMIN ? "(A)" :
		       taskInfo[task].group==TG_LOGGER ? "(L)" :
		       taskInfo[task].group==TG_FORWARDER ? "(F)" :
		       taskInfo[task].group==TG_SPAWNER ? "(S)" :
		       taskInfo[task].group==TG_GMSPAWNER ? "(S)" :
		       taskInfo[task].group==TG_MONITOR ? "(M)" : "");
#if 0
		{
		    pid_t pid = PSC_getPID(taskInfo[task].tid);
		    char cmdline[8096] = { '\0' };
		    PSI_infoString(node, PSP_INFO_CMDLINE, &pid,
				   cmdline, sizeof(cmdline), 0);
		    printf(" %s", cmdline);
		}
#endif
		printf("\n");
	    }
	    if (taskNum[node]>num) {
		printf(" + %d more tasks\n", taskNum[node]-num);
	    }
	} else {
	    printf("%4d\tdown\n", node);
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
    printf("\t 1 min\t 5 min\t15 min\t tot.\tnorm.\talloc.\texclusive\n");

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;
	if (hostStatus.list[node]) {
	    printf("%4d\t%2.4f\t%2.4f\t%2.4f\t%4d\t%4d\t%4d\t%5d\n", node,
		   loads[3*node+0], loads[3*node+1], loads[3*node+2],
		   taskNumFull[node], taskNumNorm[node], taskNumAlloc[node],
		   exclusiveFlag[node]);
	} else {
	    printf("%4d\t down\n", node);
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

	if (hostStatus.list[node]) {
	    printf("%4d\t %2d/%2d\t %s\n", node,
		   virtCPUs[node], physCPUs[node],
		   PSI_printHWType(hwStatus[node]));
	} else {
	    printf("%4d\t down\n", node);
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
    case PSP_OP_RDPMAXRETRANS:
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
	break;
    case PSP_OP_PSIDDEBUG:
    case PSP_OP_RDPDEBUG:
    case PSP_OP_MCASTDEBUG:
    case PSP_OP_FREEONSUSP:
    case PSP_OP_HANDLEOLD:
    case PSP_OP_NODESSORT:
	break;
    default:
	printf("Cannot handle option type %d.\n", type);
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

void PSIADM_ShowParam(PSP_Option_t type, char *nl)
{
    PSnodes_ID_t node;
    PSP_Optval_t value;
    int ret;

    if (! getHostStatus()) return;

    for (node=0; node<PSC_getNrOfNodes(); node++) {
	if (nl && !nl[node]) continue;

	printf("%3d:  ", node);
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
		case PSP_OP_UIDLIMIT:
		    if (value==-1)
			printf("ANY\n");
		    else {
			struct passwd *passwd = getpwuid(value);
			if (passwd) {
			    printf("%s\n", passwd->pw_name);
			} else {
			    printf("uid %d\n", value);
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
			    printf("gid %d\n", value);
			}
		    }
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
			   (value == PART_SORT_PROCLOAD) ? "PROCLOAD" :
			   (value == PART_SORT_NONE) ? "NONE" : "UNKNOWN");
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
	printf("Insufficient priviledge\n");
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
