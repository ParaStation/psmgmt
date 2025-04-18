/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "list.h"
#include "psattribute.h"
#include "pscommon.h"
#include "psprotocol.h"
#include "pspartition.h"
#include "psreservation.h"
#include "psstrv.h"

#include "mcast.h"
#include "rdp.h"

#include "psidcomm.h"
#include "psidenv.h"
#include "psidhook.h"
#include "psidhw.h"
#include "psidnodes.h"
#include "psidpartition.h"
#include "psidplugin.h"
#include "psidstatus.h"
#include "psidtask.h"
#include "psidutil.h"

#include "psidinfo.h"

/**
 * @brief Handle a PSP_CD_INFOREQUEST message
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
 * @param inmsg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_INFOREQUEST(DDTypedBufferMsg_t *inmsg)
{
    int destID = PSC_getID(inmsg->header.dest);
    char funcStr[80];

    PSID_fdbg(PSID_LOG_INFO, "type %s for %d from requester %s\n",
	      PSP_printInfo(inmsg->type), destID,
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
	return true;
    }

    /* a request for my own Information*/
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_INFORESPONSE,
	    .sender = PSC_getMyTID(),
	    .dest = inmsg->header.sender,
	    .len = offsetof(DDTypedBufferMsg_t, type) + sizeof(msg.type) },
	.type = inmsg->type,
	.buf = { 0 } };
    int err=0;

    switch((PSP_Info_t) inmsg->type) {
    case PSP_INFO_COUNTHEADER:
    case PSP_INFO_COUNTSTATUS:
	PSID_sendCounter(inmsg);
	return true;
    case PSP_INFO_RDPSTATUS:
	getStateInfoRDP(*(PSnodes_ID_t *) inmsg->buf, msg.buf, sizeof(msg.buf));
	msg.header.len += strlen(msg.buf)+1;
	break;
    case PSP_INFO_RDPCONNSTATUS:
	getConnInfoRDP(*(PSnodes_ID_t *) inmsg->buf, msg.buf, sizeof(msg.buf));
	msg.header.len += strlen(msg.buf)+1;
	break;
    case PSP_INFO_MCASTSTATUS:
	getStateInfoMCast(*(PSnodes_ID_t *) inmsg->buf,
			  msg.buf, sizeof(msg.buf));
	msg.header.len += strlen(msg.buf)+1;
	break;
    case PSP_INFO_HOST:
	;
	in_addr_t inAddr = *(in_addr_t *) inmsg->buf;
	PSnodes_ID_t nodeID = PSIDnodes_lookupHost(inAddr);
	if (!PSC_validNode(nodeID)) {
	    struct sockaddr_in sin = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = inAddr,
	    };
	    int ret = PSIDhook_call(PSIDHOOK_SENDER_UNKNOWN, &sin);
	    /* resolving might have been successful */
	    if (ret != PSIDHOOK_NOFUNC) nodeID = PSIDnodes_lookupHost(inAddr);
	    /* connect to new node (might be required soon anyhow) */
	    if (PSC_validNode(nodeID)) send_DAEMONCONNECT(nodeID);
	}
	*(PSnodes_ID_t *)msg.buf = nodeID;
	msg.header.len += sizeof(nodeID);
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
    case PSP_INFO_HWNUM:
	*(int *)msg.buf = Attr_num();
	msg.header.len += sizeof(int);
	break;
    case PSP_INFO_HWINDEX:
	*(int *)msg.buf = Attr_index(inmsg->buf);
	msg.header.len += sizeof(int);
	break;
    case PSP_INFO_HWNAME:
    {
	char *name = Attr_name(*(int *) inmsg->buf);
	if (name) {
	    strncpy(msg.buf, name, sizeof(msg.buf));
	    msg.buf[sizeof(msg.buf)-1] = '\0';

	    msg.header.len += strlen(msg.buf) + 1;
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
	    PSID_log("%s: task %s not found\n",
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
	    return true;
	} /* else fallthrough */
    case PSP_INFO_LIST_HOSTSTATUS:
    case PSP_INFO_LIST_VIRTCPUS:
    case PSP_INFO_LIST_PHYSCPUS:
    case PSP_INFO_LIST_HWSTATUS:
	if (PSC_getID(inmsg->header.sender) == PSC_getMyID()) {
	    PStask_t *requester = PStasklist_find(&managedTasks,
						  inmsg->header.sender);
	    if (!requester) {
		PSID_log("%s: requester %s not found\n",
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
	for (PSnodes_ID_t node = 0; node < PSC_getNrOfNodes(); node++) {
	    switch (inmsg->type) {
	    case PSP_INFO_LIST_HOSTSTATUS:
		((char *)msg.buf)[idx] = PSIDnodes_isUp(node);
		size = sizeof(char);
		break;
	    case PSP_INFO_LIST_VIRTCPUS:
		((uint16_t *)msg.buf)[idx] = PSIDnodes_getNumThrds(node);
		size = sizeof(uint16_t);
		break;
	    case PSP_INFO_LIST_PHYSCPUS:
		((uint16_t *)msg.buf)[idx] = PSIDnodes_getNumCores(node);
		size = sizeof(uint16_t);
		break;
	    case PSP_INFO_LIST_HWSTATUS:
		((uint32_t *)msg.buf)[idx] = PSIDnodes_getHWStatus(node);
		size = sizeof(uint32_t);
		break;
	    case PSP_INFO_LIST_LOAD:
		status = getStatusInfo(node);
		float loadStat[3] = {status.load.load[0],
				     status.load.load[1],
				     status.load.load[2]};
		size = sizeof(loadStat);
		memcpy(msg.buf + size * idx, loadStat, size);
		break;
	    case PSP_INFO_LIST_MEMORY:
		memory = getMemoryInfo(node);
		((uint64_t *)msg.buf)[2*idx+0] = memory.total;
		((uint64_t *)msg.buf)[2*idx+1] = memory.free;
		size = 2*sizeof(uint64_t);
		break;
	    case PSP_INFO_LIST_ALLJOBS:
		status = getStatusInfo(node);
		((uint16_t *)msg.buf)[idx] = status.tasks.total;
		size = sizeof(uint16_t);
		break;
	    case PSP_INFO_LIST_NORMJOBS:
		status = getStatusInfo(node);
		((uint16_t *)msg.buf)[idx] = status.tasks.normal;
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
    case PSP_INFO_LIST_PARTITION:
    {
	PStask_ID_t target = PSC_getPID(inmsg->header.dest) ?
	    inmsg->header.dest : inmsg->header.sender;
	PStask_t *task = PStasklist_find(&managedTasks, target);

	if (!task) {
	    PSID_log("%s: task %s not found\n",
		     funcStr, PSC_printTID(target));
	    err = 1;
	    break;
	}

	if (task->ptid && !task->partition) {
	    PSID_dbg(PSID_LOG_INFO, "%s: forward to parent %s\n", funcStr,
		     PSC_printTID(task->ptid));
	    msg.header.type = inmsg->header.type;
	    msg.header.sender = inmsg->header.sender;
	    msg.header.dest = task->ptid;
	    msg_INFOREQUEST(&msg);
	    return true;
	} else {
	    const size_t chunkSize = 1024;
	    unsigned int idx = 0;
	    for (unsigned int n = 0; n < task->partitionSize; n++) {
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
    case PSP_INFO_LIST_RESNODES:
    {
	PStask_ID_t target = PSC_getPID(inmsg->header.dest) ?
	    inmsg->header.dest : inmsg->header.sender;
	PStask_t *task = PStasklist_find(&managedTasks, target);
	if (!task) {
	    PSID_log("%s: task %s not found\n", funcStr, PSC_printTID(target));
	    msg.type = PSP_INFO_LIST_END;
	    break;
	}

	if (task->ptid && list_empty(&task->reservations)) {
	    PSID_dbg(PSID_LOG_INFO, "%s: forward to parent %s\n", funcStr,
		     PSC_printTID(task->ptid));
	    msg.header.type = inmsg->header.type;
	    msg.header.sender = inmsg->header.sender;
	    msg.header.dest = task->ptid;
	    memcpy(msg.buf, inmsg->buf, inmsg->header.len - DDTypedBufMsgOffset);
	    msg.header.len = inmsg->header.len;
	    msg_INFOREQUEST(&msg);
	    return true;
	} else {
	    PSrsrvtn_ID_t resID;
	    size_t used = 0;

	    PSP_getTypedMsgBuf(inmsg, &used, "resID", &resID,sizeof(resID));

	    PSIDpart_sendResNodes(resID, task, &msg);
	    msg.type = PSP_INFO_LIST_END;
	}
	break;
    }
    case PSP_INFO_CMDLINE:
    {
	PStask_t *task = PStasklist_find(&managedTasks, inmsg->header.dest);
	if (task) {
	    for (char **a = strvGetArray(task->argV); a && *a; a++) {
		snprintf(&msg.buf[strlen(msg.buf)],
			 sizeof(msg.buf)-strlen(msg.buf), "%s ", *a);
	    }
	    /* Cut the trailing space */
	    if (strlen(msg.buf)) msg.buf[strlen(msg.buf) - 1]='\0';
	}
	msg.header.len += strlen(msg.buf) + 1;
	break;
    }
    case PSP_INFO_RPMREV:
	snprintf(msg.buf, sizeof(msg.buf), "%s", PSC_getVersionStr());
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
	    return true;
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
    return true;
}

/**
 * @brief Drop a PSP_CD_INFOREQUEST message
 *
 * Drop the message @a msg of type PSP_CD_INFOREQUEST.
 *
 * Since the requesting process waits for a reaction to its request a
 * corresponding answer is created.
 *
 * @param msg Pointer to message to drop
 *
 * @return Always return true
 */
static bool drop_INFOREQUEST(DDBufferMsg_t *msg)
{
    DDErrorMsg_t errmsg = {
	.header = {
	    .type = PSP_CD_ERROR,
	    .dest = msg->header.sender,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(errmsg) },
	.error = EHOSTUNREACH,
	.request = msg->header.type };
    sendMsg(&errmsg);
    return true;
}

void initInfo(void)
{
    PSID_fdbg(PSID_LOG_VERB, "\n");

    PSID_registerMsg(PSP_CD_INFOREQUEST, (handlerFunc_t) msg_INFOREQUEST);

    PSID_registerDropper(PSP_CD_INFOREQUEST, drop_INFOREQUEST);
}
