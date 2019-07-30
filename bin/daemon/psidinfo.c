/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2019 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>

#include "mcast.h"
#include "rdp.h"

#include "pscommon.h"
#include "psprotocol.h"
#include "hardware.h"

#include "psidutil.h"
#include "psidcomm.h"
#include "psidnodes.h"
#include "psidtask.h"
#include "psidstatus.h"
#include "psidpartition.h"
#include "psidhw.h"
#include "psidplugin.h"
#include "psidenv.h"

#include "psidinfo.h"

/**
 * @brief Handle a PSP_CD_INFOREQUEST message.
 *
 * Handle the message @a inmsg of type PSP_CD_INFOREQUEST.
 *
 * This kind of messages is used by client processes (actually most of
 * the time psiadmin processes) in order to get information on the
 * cluster and its current state. After retrieving the requested
 * information one or more PSP_CD_INFORESPONSE messages are generated
 * and send to the client process.
 *
 * Since some information is not available on every node of the
 * cluster, @a inmsg might be forwarded to further nodes.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_INFOREQUEST(DDTypedBufferMsg_t *inmsg)
{
    int destID = PSC_getID(inmsg->header.dest);
    char funcStr[80];

    PSID_log(PSID_LOG_INFO, "%s: type %s for %d from requester %s\n",
	     __func__, PSP_printInfo(inmsg->type), destID,
	     PSC_printTID(inmsg->header.sender));

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

	if (PSIDnodes_isUp(destID)) {
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

	switch((PSP_Info_t) inmsg->type) {
	case PSP_INFO_COUNTHEADER:
	case PSP_INFO_COUNTSTATUS:
	    PSID_getCounter(inmsg);
	    return;
	    break;
	case PSP_INFO_RDPSTATUS:
	    getStateInfoRDP(*(PSnodes_ID_t *) inmsg->buf,
			    msg.buf, sizeof(msg.buf));
	    msg.header.len += strlen(msg.buf)+1;
	    break;
	case PSP_INFO_RDPCONNSTATUS:
	    getConnInfoRDP(*(PSnodes_ID_t *) inmsg->buf,
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
		PSIDnodes_lookupHost(*(unsigned int *) inmsg->buf);
	    msg.header.len += sizeof(PSnodes_ID_t);
	    break;
	case PSP_INFO_NODE:
	{
	    PSnodes_ID_t node = *(PSnodes_ID_t *) inmsg->buf;
	    if (PSC_validNode(node)) {
		*(in_addr_t *)msg.buf = PSIDnodes_getAddr(node);
	    } else {
		*(in_addr_t *)msg.buf = INADDR_ANY;
	    }
	    msg.header.len += sizeof(in_addr_t);
	    break;
	}
	case PSP_INFO_INSTDIR:
	    strncpy(msg.buf, PSC_lookupInstalldir(NULL), sizeof(msg.buf));
	    msg.buf[sizeof(msg.buf)-1] = '\0';
	    msg.header.len += strlen(msg.buf)+1;
	    break;
	case PSP_INFO_DAEMONVER:
	    strncpy(msg.buf, "           no info ", sizeof(msg.buf));
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
	    PStask_t *task = PStasklist_find(&managedTasks, tid), *dlgt;
	    if (!task) {
		*(int *)msg.buf = -1;
		msg.header.len += sizeof(int);
		break;
	    }

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
	    }
	    dlgt = task->delegate ? task->delegate : task;
	    if (msg.type == PSP_INFO_RANKID) {
		/* @todo this does not work any more !! */
		/* @todo rework this for HW-threads and reservations */
		int rank = *(int32_t *) inmsg->buf;
		if (rank < 0) {
		    *(PSnodes_ID_t *)msg.buf = -1;
		} else if (rank >= (int)dlgt->partitionSize) {
		    /* @todo pinning Think about how to use OVERBOOK */
		    if (task->options & PART_OPT_OVERBOOK) {
			*(PSnodes_ID_t *)msg.buf =
			    dlgt->partition[rank%dlgt->partitionSize].node;
		    } else {
			*(PSnodes_ID_t *)msg.buf = -1;
		    }
		} else {
		    *(PSnodes_ID_t *)msg.buf = dlgt->partition[rank].node;
		}
		msg.header.len += sizeof(PSnodes_ID_t);
	    } else {
		*(int *)msg.buf = task->activeChild;
		msg.header.len += sizeof(int);
	    }
	    break;
	}
	case PSP_INFO_TASKRANK:
	case PSP_INFO_PARENTTID:
	case PSP_INFO_LOGGERTID:
	{
	    PStask_ID_t tid = PSC_getPID(inmsg->header.dest) ?
		inmsg->header.dest : inmsg->header.sender;
	    PStask_t *task = PStasklist_find(&managedTasks, tid);

	    if (!task) {
		PSID_log(-1, "%s: task %s not found\n",
			 funcStr, PSC_printTID(inmsg->header.sender));
		/*  Not err=1 ! Send empty message to mark 'task not found'. */
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
	case PSP_INFO_LIST_MEMORY:
	case PSP_INFO_LIST_ALLJOBS:
	case PSP_INFO_LIST_NORMJOBS:
	case PSP_INFO_LIST_ALLOCJOBS:
	case PSP_INFO_LIST_EXCLUSIVE:
	    if (!PSID_config->useMCast && (PSC_getMyID() != getMasterID())) {
		/* Handled by master node -> forward */
		inmsg->header.dest = PSC_getTID(getMasterID(), 0);
		msg_INFOREQUEST(inmsg);
		return;
	    } /* else fallthrough */
	case PSP_INFO_LIST_HOSTSTATUS:
	case PSP_INFO_LIST_VIRTCPUS:
	case PSP_INFO_LIST_PHYSCPUS:
	case PSP_INFO_LIST_HWSTATUS:
	{
	    PStask_t *requester = NULL;
	    PSnodes_ID_t node;

	    if (PSC_getID(inmsg->header.sender) == PSC_getMyID()) {
		requester = PStasklist_find(&managedTasks,
					    inmsg->header.sender);

		if (!requester) {
		    PSID_log(-1, "%s: requester %s not found\n",
			     funcStr, PSC_printTID(inmsg->header.sender));
		    err = 1;
		    break;
		}
	    }

	    const size_t chunkSize = 1024;
	    PSID_NodeStatus_t status;
	    PSID_Mem_t memory;
	    size_t size = 0;
	    unsigned int idx = 0;
	    for (node=0; node<PSC_getNrOfNodes(); node++) {
		switch (inmsg->type) {
		case PSP_INFO_LIST_HOSTSTATUS:
		    ((char *)msg.buf)[idx] = PSIDnodes_isUp(node);
		    size = sizeof(char);
		    break;
		case PSP_INFO_LIST_VIRTCPUS:
		    ((uint16_t *)msg.buf)[idx] = PSIDnodes_getVirtCPUs(node);
		    size = sizeof(uint16_t);
		    break;
		case PSP_INFO_LIST_PHYSCPUS:
		    ((uint16_t *)msg.buf)[idx] = PSIDnodes_getPhysCPUs(node);
		    size = sizeof(uint16_t);
		    break;
		case PSP_INFO_LIST_HWSTATUS:
		    ((uint32_t *)msg.buf)[idx] = PSIDnodes_getHWStatus(node);
		    size = sizeof(uint32_t);
		    break;
		case PSP_INFO_LIST_LOAD:
		    status = getStatusInfo(node);
		    ((float *)msg.buf)[3*idx+0] = status.load.load[0];
		    ((float *)msg.buf)[3*idx+1] = status.load.load[1];
		    ((float *)msg.buf)[3*idx+2] = status.load.load[2];
		    size = 3*sizeof(float);
		    break;
		case PSP_INFO_LIST_MEMORY:
		    memory = getMemoryInfo(node);
		    ((uint64_t *)msg.buf)[2*idx+0] = memory.total;
		    ((uint64_t *)msg.buf)[2*idx+1] = memory.free;
		    size = 2*sizeof(uint64_t);
		    break;
		case PSP_INFO_LIST_ALLJOBS:
		    status = getStatusInfo(node);
		    ((uint16_t *)msg.buf)[idx] = status.jobs.total;
		    size = sizeof(uint16_t);
		    break;
		case PSP_INFO_LIST_NORMJOBS:
		    status = getStatusInfo(node);
		    ((uint16_t *)msg.buf)[idx] = status.jobs.normal;
		    size = sizeof(uint16_t);
		    break;
		case PSP_INFO_LIST_ALLOCJOBS:
		    ((uint16_t *)msg.buf)[idx] = getAssignedThreads(node);
		    size = sizeof(uint16_t);
		    break;
		case PSP_INFO_LIST_EXCLUSIVE:
		    ((int8_t *)msg.buf)[idx] = getIsExclusive(node);
		    size = sizeof(int8_t);
		    break;
		default:
		    msg.type = PSP_INFO_UNKNOWN;
		    size = 0;
		}
		idx++;
		if (size && (idx*size >= chunkSize)) {
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
	    break;
	}
	case PSP_INFO_LIST_PARTITION:
	{
	    PStask_ID_t target = PSC_getPID(inmsg->header.dest) ?
		inmsg->header.dest : inmsg->header.sender;
	    PStask_t *task = PStasklist_find(&managedTasks, target);

	    if (!task) {
		PSID_log(-1, "%s: task %s not found\n",
			 funcStr, PSC_printTID(target));
		err = 1;
		break;
	    }

	    if (task->ptid) {
		PSID_log(PSID_LOG_INFO, "%s: forward to root process %s\n",
			 funcStr, PSC_printTID(task->ptid));
		msg.header.type = inmsg->header.type;
		msg.header.sender = inmsg->header.sender;
		msg.header.dest = task->ptid;
		msg_INFOREQUEST(&msg);
		return;
	    } else {
		const size_t chunkSize = 1024;
		unsigned int idx = 0, n;
		for (n=0; n<task->partitionSize; n++) {
		    ((PSnodes_ID_t *)msg.buf)[idx] = task->partition[n].node;
		    idx++;
		    if (idx >= chunkSize/sizeof(PSnodes_ID_t)) {
			msg.header.len += idx * sizeof(PSnodes_ID_t);
			sendMsg(&msg);
			msg.header.len -= idx * sizeof(PSnodes_ID_t);
			idx=0;
		    }
		}
		if (idx) {
		    msg.header.len += idx * sizeof(PSnodes_ID_t);
		    sendMsg(&msg);
		    msg.header.len -= idx * sizeof(PSnodes_ID_t);
		}
		msg.type = PSP_INFO_LIST_END;
	    }
	    break;
	}
	case PSP_INFO_LIST_RESPORTS:
	{
	    uint16_t count=0, i=0;
	    size_t len;
	    char *mPtr = msg.buf;

	    PStask_ID_t target = PSC_getPID(inmsg->header.dest) ?
		inmsg->header.dest : inmsg->header.sender;
	    PStask_t *task = PStasklist_find(&managedTasks, target);

	    if (!task) {
		PSID_log(-1, "%s: task %s not found\n",
			 funcStr, PSC_printTID(target));
		err = 1;
		break;
	    }

	    if (task->ptid) {
		PSID_log(PSID_LOG_INFO, "%s: forward to root process %s\n",
			 funcStr, PSC_printTID(task->ptid));
		msg.header.type = inmsg->header.type;
		msg.header.sender = inmsg->header.sender;
		msg.header.dest = task->ptid;
		msg_INFOREQUEST(&msg);
		return;
	    }

	    len = msg.header.len;

	    /* we don't have any reserved ports */
	    if (!task->resPorts) {

		*(uint16_t *) mPtr = 0;
		//mPtr += sizeof(uint16_t);
		msg.header.len += sizeof(uint16_t);
		sendMsg(&msg);

		msg.header.len = len;
		msg.type = PSP_INFO_LIST_END;

		break;
	    }

	    /* add number of reserved ports */
	    while (task->resPorts[count] != 0) count++;
	    *(uint16_t *) mPtr = count;
	    mPtr += sizeof(uint16_t);
	    msg.header.len += sizeof(uint16_t);

	    /* add the reserved ports */
	    for (i=0; i<count; i++) {
		*(uint16_t *) mPtr = task->resPorts[i];
		mPtr += sizeof(uint16_t);
		msg.header.len += sizeof(uint16_t);
	    }
	    sendMsg(&msg);

	    msg.header.len = len;
	    msg.type = PSP_INFO_LIST_END;
	    break;
	}
	case PSP_INFO_LIST_RESNODES:
	{
	    PStask_ID_t target = PSC_getPID(inmsg->header.dest) ?
		inmsg->header.dest : inmsg->header.sender;
	    PStask_t *task = PStasklist_find(&managedTasks, target);

	    if (!task) {
		PSID_log(-1, "%s: task %s not found\n",
			 funcStr, PSC_printTID(target));
		msg.type = PSP_INFO_LIST_END;
		break;
	    }

	    if (task->ptid) {
		PSID_log(PSID_LOG_INFO, "%s: forward to root process %s\n",
			 funcStr, PSC_printTID(task->ptid));
		msg.header.type = inmsg->header.type;
		msg.header.sender = inmsg->header.sender;
		msg.header.dest = task->ptid;
		memcpy(msg.buf, inmsg->buf, inmsg->header.len
		       - sizeof(inmsg->header) - sizeof(inmsg->type));
		msg.header.len = inmsg->header.len;
		msg_INFOREQUEST(&msg);
		return;
	    } else {
		PSrsrvtn_ID_t resID;
		size_t used = 0;

		PSP_getTypedMsgBuf(inmsg, &used, __func__, "resID", &resID,
				   sizeof(resID));

		PSIDpart_sendResNodes(resID, task, &msg);
		msg.type = PSP_INFO_LIST_END;
	    }
	    break;
	}
	case PSP_INFO_CMDLINE:
	{
	    PStask_t *task = PStasklist_find(&managedTasks, inmsg->header.dest);
	    if (task) {
		uint32_t i;
		for (i = 0; i < task->argc; i++) {
		    snprintf(&msg.buf[strlen(msg.buf)],
			     sizeof(msg.buf)-strlen(msg.buf),
			     "%s ", task->argv[i]);
		}
		/* Cut the trailing space */
		if (strlen(msg.buf)) msg.buf[strlen(msg.buf)-1]='\0';
	    }
	    msg.header.len += strlen(msg.buf) + 1;
	    break;
	}
	case PSP_INFO_RPMREV:
	    snprintf(msg.buf, sizeof(msg.buf), "%s-%s",
		     VERSION_psmgmt, RELEASE_psmgmt);
	    msg.header.len += strlen(msg.buf)+1;
	    break;
	case PSP_INFO_QUEUE_NORMTASK:
	case PSP_INFO_QUEUE_ALLTASK:
	{
	    /* request info for all normal tasks */
	    PSP_taskInfo_t *taskinfo = (PSP_taskInfo_t *)msg.buf;
	    list_t *t;
	    list_for_each(t, &managedTasks) {
		PStask_t *task = list_entry(t, PStask_t, next);
		if (task->deleted) continue;
		if ((PSP_Info_t) inmsg->type == PSP_INFO_QUEUE_NORMTASK && (
			task->group == TG_FORWARDER
			|| task->group == TG_SPAWNER
			|| task->group == TG_GMSPAWNER
			|| task->group == TG_PSCSPAWNER
			|| task->group == TG_MONITOR
			|| task->group == TG_SERVICE
			|| task->group == TG_SERVICE_SIG )) continue;
		taskinfo->tid = task->tid;
		taskinfo->ptid = task->ptid;
		taskinfo->loggertid = task->loggertid;
		taskinfo->uid = task->uid;
		taskinfo->group = task->group;
		taskinfo->rank = task->rank;
		taskinfo->connected = (task->fd != -1);

		/* Send task info */
		msg.header.len += sizeof(PSP_taskInfo_t);
		sendMsg(&msg);
		msg.header.len -= sizeof(PSP_taskInfo_t);
		/* Send separator */
		msg.type = PSP_INFO_QUEUE_SEP;
		sendMsg(&msg);
		msg.type = inmsg->type;
	    }

	    /* send EndOfQueue */
	    msg.type = PSP_INFO_QUEUE_SEP;
	    break;
	}
	case PSP_INFO_QUEUE_PARTITION:
	    if (PSC_getMyID() != getMasterID()) {
		/* Handled by master node -> forward */
		inmsg->header.dest = PSC_getTID(getMasterID(), 0);
		msg_INFOREQUEST(inmsg);
		return;
	    } else {
		sendRequestLists(inmsg->header.sender,
				 *(PSpart_list_t*)inmsg->buf);
		/* send EndOfQueue */
		msg.type = PSP_INFO_QUEUE_SEP;
	    }
	    break;
	case PSP_INFO_QUEUE_PLUGINS:
	    PSIDplugin_sendList(inmsg->header.sender);
	    /* send EndOfQueue */
	    msg.type = PSP_INFO_QUEUE_SEP;
	    break;
	case PSP_INFO_QUEUE_ENVS:
	    PSID_sendEnvList(inmsg->header.sender, inmsg->buf);
	    /* send EndOfQueue */
	    msg.type = PSP_INFO_QUEUE_SEP;
	    break;
	case PSP_INFO_STARTTIME:
	    *(int64_t *)msg.buf = (int64_t) PSID_getStarttime();
	    msg.header.len += sizeof(int64_t);
	    break;
	case PSP_INFO_STARTUPSCRIPT:
	    if (PSID_config->startupScript) {
		strncpy(msg.buf, PSID_config->startupScript, sizeof(msg.buf));
		msg.buf[sizeof(msg.buf)-1] = '\0';
	    } else {
		*msg.buf = '\0';
	    }
	    msg.header.len += strlen(msg.buf)+1;
	    break;
	case PSP_INFO_NODEUPSCRIPT:
	    if (PSID_config->nodeUpScript) {
		strncpy(msg.buf, PSID_config->nodeUpScript, sizeof(msg.buf));
		msg.buf[sizeof(msg.buf)-1] = '\0';
	    } else {
		*msg.buf = '\0';
	    }
	    msg.header.len += strlen(msg.buf)+1;
	    break;
	case PSP_INFO_NODEDOWNSCRIPT:
	    if (PSID_config->nodeDownScript) {
		strncpy(msg.buf, PSID_config->nodeDownScript, sizeof(msg.buf));
		msg.buf[sizeof(msg.buf)-1] = '\0';
	    } else {
		*msg.buf = '\0';
	    }
	    msg.header.len += strlen(msg.buf)+1;
	    break;
	default:
	    msg.type = PSP_INFO_UNKNOWN;
	}
	if (!err) sendMsg(&msg);
    }
}

/**
 * @brief Drop a PSP_CD_INFOREQUEST message.
 *
 * Drop the message @a msg of type PSP_CD_INFOREQUEST.
 *
 * Since the requesting process waits for a reaction to its request a
 * corresponding answer is created.
 *
 * @param msg Pointer to the message to drop.
 *
 * @return No return value.
 */
static void drop_INFOREQUEST(DDBufferMsg_t *msg)
{
    DDErrorMsg_t errmsg;

    errmsg.header.type = PSP_CD_ERROR;
    errmsg.header.dest = msg->header.sender;
    errmsg.header.sender = PSC_getMyTID();
    errmsg.header.len = sizeof(errmsg);

    errmsg.error = EHOSTUNREACH;
    errmsg.request = msg->header.type;

    sendMsg(&errmsg);
}

void initInfo(void)
{
    PSID_log(PSID_LOG_VERB, "%s()\n", __func__);

    PSID_registerMsg(PSP_CD_INFOREQUEST, (handlerFunc_t) msg_INFOREQUEST);

    PSID_registerDropper(PSP_CD_INFOREQUEST, drop_INFOREQUEST);
}
