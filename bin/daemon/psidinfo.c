/*
 *               ParaStation
 * psidinfo.c
 *
 * Handle info requests to the ParaStation daemon.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidinfo.c,v 1.8 2004/01/28 14:03:06 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidinfo.c,v 1.8 2004/01/28 14:03:06 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>

#include "mcast.h"
#include "rdp.h"

#include "pscommon.h"
#include "psprotocol.h"
#include "psnodes.h"
#include "hardware.h"

#include "psidutil.h"
#include "psidcomm.h"
#include "psidtask.h"
#include "psidstatus.h"
#include "psidpartition.h"

#include "psidinfo.h"

static char errtxt[256]; /**< General string to create error messages */

extern char psid_cvsid[];

void msg_INFOREQUEST(DDTypedBufferMsg_t *inmsg)
{
    int destID = PSC_getID(inmsg->header.dest);
    int header = 0;
    char funcStr[80];

    snprintf(errtxt, sizeof(errtxt), "%s: type %d for %d from requester %s",
	     __func__, inmsg->type, destID,
	     PSC_printTID(inmsg->header.sender));
    PSID_errlog(errtxt, 1);

    snprintf(funcStr, sizeof(funcStr),
	     "%s(%s)", __func__, PSP_printInfo(inmsg->type));

    if (destID != PSC_getMyID()) {
	/* request for remote daemon */
	DDErrorMsg_t errmsg = {
	    .header = {
		.type = PSP_CD_ERROR,
		.dest = inmsg->header.sender,
		.sender = PSC_getMyTID(),
		.len = sizeof(errmsg) },
	    .request = inmsg->header.type,
	    .error = 0};

	if (PSnodes_isUp(destID)) {
	    DDTypedMsg_t msg = {
		.header = {
		    .type = PSP_CD_INFORESPONSE,
		    .sender = PSC_getMyTID(),
		    .dest = inmsg->header.sender,
		    .len = sizeof(msg) },
		.type = PSP_INFO_UNKNOWN };

	    if (PSC_getID(inmsg->header.sender) == PSC_getMyID()) {
		/* Test for correct protocol version */
		PStask_t *requester = PStasklist_find(managedTasks,
						      inmsg->header.sender);
		if (!requester) {
		    snprintf(errtxt, sizeof(errtxt),
			     "%s: requester %s not found",
			     funcStr, PSC_printTID(inmsg->header.sender));
		    PSID_errlog(errtxt, 0);
		    inmsg = (DDTypedBufferMsg_t *)&msg;
		} else {
		    /* Test for protocol changes */
		    if (requester->protocolVersion < 329) {
			switch (inmsg->type) {
			case PSP_INFO_LIST_VIRTCPUS:
			case PSP_INFO_LIST_PHYSCPUS:
			case PSP_INFO_LIST_HWSTATUS:
			case PSP_INFO_LIST_LOAD:
			case PSP_INFO_LIST_ALLJOBS:
			case PSP_INFO_LIST_NORMJOBS:
			    inmsg = (DDTypedBufferMsg_t *)&msg;
			    break;
			default:
			    ;
			}
		    }
		    if (requester->protocolVersion > 327) {
			switch (inmsg->type) {
			case PSP_INFO_NODELIST:
			case PSP_INFO_PARTITION:
			    inmsg = (DDTypedBufferMsg_t *)&msg;
			    break;
			default:
			    ;
			}
		    }
		}
	    }
	    /* transfer to remote daemon */
	    if (sendMsg(inmsg) == -1 && errno != EWOULDBLOCK) {
		/* system error */
		errmsg.error = EHOSTUNREACH;
		sendMsg(&errmsg);
	    }
	} else {
	    /* node is down */
	    errmsg.error = EHOSTDOWN;
	    sendMsg(&errmsg);
	}
    } else {
	/* a request for my own Information*/
	DDTypedBufferMsg_t msg = {
	    .header = {
		.type = PSP_CD_INFORESPONSE,
		.sender = PSC_getMyTID(),
		.dest = inmsg->header.sender,
		.len = sizeof(msg.header) + sizeof(msg.type) },
	    .type = inmsg->type,
	    .buf = { 0 } };
	int err=0;

	switch((PSP_Info_t) inmsg->type){
	case PSP_INFO_LIST_TASKS:
	    if (PSC_getPID(inmsg->header.dest)) {
		/* request info for a special task */
		PStask_t *task = PStasklist_find(managedTasks,
						 inmsg->header.dest);
		if (task) {
		    PSP_taskInfo_t *taskinfo = (PSP_taskInfo_t *)msg.buf;
		    taskinfo->tid = task->tid;
		    taskinfo->ptid = task->ptid;
		    taskinfo->loggertid = task->loggertid;
		    taskinfo->uid = task->uid;
		    taskinfo->group = task->group;
		    taskinfo->rank = task->rank;
		    taskinfo->connected = (task->fd != -1);

		    msg.header.len += sizeof(PSP_taskInfo_t);
		    sendMsg(&msg);
		    msg.header.len -= sizeof(PSP_taskInfo_t);
		}
	    } else {
		/* request info for all tasks */
		PStask_t *task;
		for (task=managedTasks; task; task=task->next) {
		    PSP_taskInfo_t *taskinfo = (PSP_taskInfo_t *)msg.buf;
		    taskinfo->tid = task->tid;
		    taskinfo->ptid = task->ptid;
		    taskinfo->loggertid = task->loggertid;
		    taskinfo->uid = task->uid;
		    taskinfo->group = task->group;
		    taskinfo->rank = task->rank;
		    taskinfo->connected = (task->fd != -1);

		    msg.header.len += sizeof(PSP_taskInfo_t);
		    sendMsg(&msg);
		    msg.header.len -= sizeof(PSP_taskInfo_t);
		}
	    }

	    /*
	     * send a EndOfList Sign
	     */
	    msg.type = PSP_INFO_LIST_END;
	    break;
	case PSP_INFO_LIST_NORMTASKS:
	case PSP_INFO_LIST_ALLTASKS:
	{
	    /* request info for all normal tasks */
	    PStask_t *task;
	    PSP_taskInfo_t *taskinfo = (PSP_taskInfo_t *)msg.buf;
	    for (task=managedTasks; task; task=task->next) {
		if ((PSP_Info_t) inmsg->type == PSP_INFO_LIST_NORMTASKS && (
			task->group == TG_FORWARDER
			|| task->group == TG_SPAWNER
			|| task->group == TG_GMSPAWNER
			|| task->group == TG_MONITOR )) continue;
		taskinfo->tid = task->tid;
		taskinfo->ptid = task->ptid;
		taskinfo->loggertid = task->loggertid;
		taskinfo->uid = task->uid;
		taskinfo->group = task->group;
		taskinfo->rank = task->rank;
		taskinfo->connected = (task->fd != -1);

		msg.header.len += sizeof(PSP_taskInfo_t);
		sendMsg(&msg);
		msg.header.len -= sizeof(PSP_taskInfo_t);
	    }

	    /*
	     * send a EndOfList Sign
	     */
	    msg.type = PSP_INFO_LIST_END;
	    break;
	}
 	case PSP_INFO_COUNTHEADER:
	    header = 1;
	case PSP_INFO_COUNTSTATUS:
	{
	    int hw = *(int *) inmsg->buf;

	    *msg.buf = '\0';
	    if (PSnodes_getHWStatus(PSC_getMyID()) & (1<<hw)) {
		PSID_getCounter(hw, msg.buf, sizeof(msg.buf), header);
	    } else {
		snprintf(msg.buf, sizeof(msg.buf), "Not available");
	    }
	    msg.header.len += strlen(msg.buf) + 1;
	    break;
	}
	case PSP_INFO_RDPSTATUS:
	    getStateInfoRDP(*(PSnodes_ID_t *) inmsg->buf,
			    msg.buf, sizeof(msg.buf));
	    msg.header.len += strlen(msg.buf)+1;
	    break;
	case PSP_INFO_MCASTSTATUS:
	    getStateInfoMCast(*(PSnodes_ID_t *) inmsg->buf,
			      msg.buf, sizeof(msg.buf));
	    msg.header.len += strlen(msg.buf)+1;
	    break;
	case PSP_INFO_HOST:
	    *(PSnodes_ID_t *)msg.buf =
		PSnodes_lookupHost(*(unsigned int *) inmsg->buf);
	    msg.header.len += sizeof(PSnodes_ID_t);
	    break;
	case PSP_INFO_NODE:
	{
	    int *node = (int *) inmsg->buf;
	    if ((*node >= 0) && (*node < PSC_getNrOfNodes())) {
		*(unsigned int *)msg.buf = PSnodes_getAddr(*node);
	    } else {
		*(unsigned int *)msg.buf = INADDR_ANY;
	    }
	    msg.header.len += sizeof(unsigned int);
	    break;
	}
	case PSP_INFO_NODELIST:
	case PSP_INFO_PARTITION:
	{
	    int i, j;
	    unsigned int hwType;
	    PStask_t *requester = NULL;

	    if ((! config->useMCast) && (PSC_getMyID() != getMasterID())) {
		/* Handled by master node -> forward */
		inmsg->header.dest = PSC_getTID(getMasterID(), 0);
		msg_INFOREQUEST(inmsg);
		return;
	    }

	    if (PSC_getID(inmsg->header.sender) == PSC_getMyID()) {
		requester = PStasklist_find(managedTasks,
					    inmsg->header.sender);

		if (!requester) {
		    snprintf(errtxt, sizeof(errtxt),
			     "%s: requester %s not found", funcStr,
			     PSC_printTID(inmsg->header.sender));
		    PSID_errlog(errtxt, 0);
		    err = 1;
		    break;
		}
	    }

	    if (!requester || requester->protocolVersion < 328) {
		static NodelistEntry_t *nodelist = NULL;
		static int nodelistlen = 0;
		int maxNodes =
		    (256 < PSC_getNrOfNodes()) ? 256 : PSC_getNrOfNodes();
		/* Limit these requests to 256 nodes due to message length */

		if (nodelistlen < maxNodes) {
		    nodelistlen = maxNodes;
		    nodelist = realloc(nodelist,
				       nodelistlen * sizeof(*nodelist));
		}

		hwType = *(unsigned int *) inmsg->buf;

		for (i=0, j=0; i<PSC_getNrOfNodes() && j<maxNodes; i++) {
		    PSID_NodeStatus_t status = getStatus(i);

		    if ((inmsg->type == PSP_INFO_NODELIST)
			|| ((!hwType || PSnodes_getHWStatus(i) & hwType)
			    && (PSnodes_getUser(i) == PSNODES_ANYUSER
				|| PSnodes_getUser(i) == requester->uid
				|| !requester->uid)
			    && (PSnodes_getProcs(i) == PSNODES_ANYPROC
				|| PSnodes_getProcs(i) > status.jobs.normal)
			    && (PSnodes_getGroup(i) == PSNODES_ANYGROUP
				|| PSnodes_getGroup(i) == requester->gid
				|| !requester->gid)
			    && PSnodes_runJobs(i))) {

			nodelist[j].id = i;
			nodelist[j].up = PSnodes_isUp(i);
			nodelist[j].numCPU = PSnodes_getVirtCPUs(i);
			nodelist[j].hwStatus = PSnodes_getHWStatus(i);

			nodelist[j].load[0] = status.load.load[0];
			nodelist[j].load[1] = status.load.load[1];
			nodelist[j].load[2] = status.load.load[2];
			nodelist[j].totalJobs = status.jobs.total;
			nodelist[j].normalJobs = status.jobs.normal;
			nodelist[j].maxJobs = PSnodes_getProcs(i);

			j++;
		    }
		}

		if (j<maxNodes) {
		    nodelist[j].id = -1;
		    j++;
		}

		memcpy(msg.buf, nodelist, j * sizeof(*nodelist));
		msg.header.len += j * sizeof(*nodelist);
	    } else {
		msg.type = PSP_INFO_UNKNOWN;
	    }

	    break;
	}
	case PSP_INFO_INSTDIR:
	    strncpy(msg.buf, PSC_lookupInstalldir(), sizeof(msg.buf));
	    msg.buf[sizeof(msg.buf)-1] = '\0';
	    msg.header.len += strlen(msg.buf)+1;
	    break;
	case PSP_INFO_DAEMONVER:
	    strncpy(msg.buf, psid_cvsid, sizeof(msg.buf));
	    msg.buf[sizeof(msg.buf)-1] = '\0';
	    msg.header.len += strlen(msg.buf)+1;
	    break;
	case PSP_INFO_NROFNODES:
	    *(int *)msg.buf = PSC_getNrOfNodes();
	    msg.header.len += sizeof(int);
	    break;
	case PSP_INFO_HWNUM:
	    *(int *)msg.buf = HW_num();
	    msg.header.len += sizeof(int);
	    break;
	case PSP_INFO_HWINDEX:
	    *(int *)msg.buf = HW_index(inmsg->buf);
	    msg.header.len += sizeof(int);
	    break;
	case PSP_INFO_HWNAME:
	{
	    char *name = HW_name(*(int *) inmsg->buf);
	    if (name) {
		strncpy(msg.buf, name, sizeof(msg.buf));
		msg.buf[sizeof(msg.buf)-1] = '\0';

		msg.header.len += strlen(msg.buf) + 1;
	    }
	    break;
	}
	case PSP_INFO_RANKID:
	case PSP_INFO_TASKSIZE:
	{
	    PStask_ID_t tid = PSC_getPID(inmsg->header.dest) ?
		inmsg->header.dest : inmsg->header.sender;
	    PStask_t *task = PStasklist_find(managedTasks, tid);
	    if (task) {
		if (task->ptid) {
		    msg.header.type = inmsg->header.type;
		    msg.header.dest = task->ptid;
		    msg.header.sender = inmsg->header.sender;
		    if (msg.type == PSP_INFO_RANKID) {
			*(int *)msg.buf = *(int *)inmsg->buf;
			msg.header.len += sizeof(int);
		    }
		    msg_INFOREQUEST(&msg);
		    return;
		} else {
		    if (msg.type == PSP_INFO_RANKID) {
			unsigned int rank = *(unsigned int *) inmsg->buf;
			if (rank >= task->partitionSize) {
			    /* @todo Think about how to use OVERBOOK */
			    if (task->options & PART_OPT_OVERBOOK) {
				*(PSnodes_ID_t *)msg.buf =
				    task->partition[rank%task->partitionSize];
			    } else {
				*(PSnodes_ID_t *)msg.buf = -1;
			    }
			} else {
			    *(PSnodes_ID_t *)msg.buf = task->partition[rank];
			}
			msg.header.len += sizeof(PSnodes_ID_t);
		    } else {
			*(int *)msg.buf = task->nextRank;
			msg.header.len += sizeof(int);
		    }
		}
	    } else {
		*(int *)msg.buf = -1;
		msg.header.len += sizeof(int);
	    }
	    break;
	}
	case PSP_INFO_TASKRANK:
	case PSP_INFO_PARENTTID:
	case PSP_INFO_LOGGERTID:
	{
	    PStask_t *task=PStasklist_find(managedTasks, inmsg->header.sender);

	    if (!task) {
		snprintf(errtxt, sizeof(errtxt), "%s: task %s not found",
			 funcStr, PSC_printTID(inmsg->header.sender));
		PSID_errlog(errtxt, 0);
		err = 1;
		break;
	    }

	    switch (inmsg->type) {
	    case PSP_INFO_TASKRANK:
		*(int32_t *)msg.buf = task->rank;
		msg.header.len += sizeof(int32_t);
		break;
	    case PSP_INFO_PARENTTID:
		*(PStask_ID_t *)msg.buf = task->ptid;
		msg.header.len += sizeof(PStask_ID_t);
		break;
	    case PSP_INFO_LOGGERTID:
		*(PStask_ID_t *)msg.buf = task->loggertid;
		msg.header.len += sizeof(PStask_ID_t);
		break;
	    }
	    break;
	}
	case PSP_INFO_LIST_LOAD:
	case PSP_INFO_LIST_ALLJOBS:
	case PSP_INFO_LIST_NORMJOBS:
	case PSP_INFO_LIST_ALLOCJOBS:
	    if ((! config->useMCast) && (PSC_getMyID() != getMasterID())) {
		/* Handled by master node -> forward */
		inmsg->header.dest = PSC_getTID(getMasterID(), 0);
		msg_INFOREQUEST(inmsg);
		return;
	    }
	case PSP_INFO_LIST_HOSTSTATUS:
	case PSP_INFO_LIST_VIRTCPUS:
	case PSP_INFO_LIST_PHYSCPUS:
	case PSP_INFO_LIST_HWSTATUS:
	{
	    PStask_t *requester = NULL;
	    PSnodes_ID_t node;

	    if (PSC_getID(inmsg->header.sender) == PSC_getMyID()) {
		requester = PStasklist_find(managedTasks,
					    inmsg->header.sender);

		if (!requester) {
		    snprintf(errtxt, sizeof(errtxt),
			     "%s: requester %s not found", funcStr,
			     PSC_printTID(inmsg->header.sender));
		    PSID_errlog(errtxt, 0);
		    err = 1;
		    break;
		}
	    }

	    if (requester && requester->protocolVersion < 329) {
		switch (inmsg->type) {
		case PSP_INFO_LIST_HOSTSTATUS:
		    for (node=0; node<PSC_getNrOfNodes(); node++) {
			msg.buf[node] = PSnodes_isUp(node);
		    }
		    msg.header.len += sizeof(*msg.buf) * PSC_getNrOfNodes();
		default:
		    msg.type = PSP_INFO_UNKNOWN;
		}
	    } else {
		const size_t chunkSize = 1024;
		PSID_NodeStatus_t status;
		size_t size = 0;
		unsigned int idx = 0;
		for (node=0; node<PSC_getNrOfNodes(); node++) {
		    switch (inmsg->type) {
		    case PSP_INFO_LIST_HOSTSTATUS:
			((char *)msg.buf)[idx] = PSnodes_isUp(node);
			size = sizeof(char);
			break;
		    case PSP_INFO_LIST_VIRTCPUS:
			((uint16_t *)msg.buf)[idx] = PSnodes_getVirtCPUs(node);
			size = sizeof(uint16_t);
			break;
		    case PSP_INFO_LIST_PHYSCPUS:
			((uint16_t *)msg.buf)[idx] = PSnodes_getPhysCPUs(node);
			size = sizeof(uint16_t);
			break;
		    case PSP_INFO_LIST_HWSTATUS:
			((uint32_t *)msg.buf)[idx] = PSnodes_getHWStatus(node);
			size = sizeof(uint32_t);
			break;
		    case PSP_INFO_LIST_LOAD:
			status = getStatus(node);
			((float *)msg.buf)[3*idx+0] = status.load.load[0];
			((float *)msg.buf)[3*idx+1] = status.load.load[1];
			((float *)msg.buf)[3*idx+2] = status.load.load[2];
			size = 3*sizeof(float);
			break;
		    case PSP_INFO_LIST_ALLJOBS:
			status = getStatus(node);
			((uint16_t *)msg.buf)[idx] = status.jobs.total;
			size = sizeof(uint16_t);
			break;
		    case PSP_INFO_LIST_NORMJOBS:
			status = getStatus(node);
			((uint16_t *)msg.buf)[idx] = status.jobs.normal;
			size = sizeof(uint16_t);
			break;
		    case PSP_INFO_LIST_ALLOCJOBS:
			((uint16_t *)msg.buf)[idx] = getAllocJobs(node);
			size = sizeof(uint16_t);
			break;
		    default:
			msg.type = PSP_INFO_UNKNOWN;
			size = 0;
		    }
		    idx++;
		    if (size && (idx == chunkSize/size)) {
			msg.header.len += idx * size;
			sendMsg(&msg);
			msg.header.len -= idx * size;
			idx=0;
		    }
		}
		if (idx) {
		    msg.header.len += idx * size;
		    sendMsg(&msg);
		    msg.header.len -= idx * size;
		}
		msg.type = PSP_INFO_LIST_END;
	    }
	    break;
	}
	default:
	    msg.type = PSP_INFO_UNKNOWN;
	}
	if (!err) sendMsg(&msg);
    }
}
