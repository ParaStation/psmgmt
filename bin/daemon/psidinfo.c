/*
 *               ParaStation
 * psidinfo.c
 *
 * Handle info requests to the ParaStation daemon.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidinfo.c,v 1.2 2003/10/23 16:27:35 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidinfo.c,v 1.2 2003/10/23 16:27:35 eicker Exp $";
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

#include "psidinfo.h"

static char errtxt[256]; /**< General string to create error messages */

extern char psid_cvsid[];

void msg_INFOREQUEST(DDTypedBufferMsg_t *inmsg)
{
    int id = PSC_getID(inmsg->header.dest);
    int header = 0;

    snprintf(errtxt, sizeof(errtxt), "%s: type %d for %d from requester %s",
	     __func__, inmsg->type, id, PSC_printTID(inmsg->header.sender));
    PSID_errlog(errtxt, 1);

    if (id!=PSC_getMyID()) {
	DDErrorMsg_t errmsg = (DDErrorMsg_t) {
	    .header = (DDMsg_t) {
		.type = PSP_CD_ERROR,
		.dest = inmsg->header.sender,
		.sender = PSC_getMyTID(),
		.len = sizeof(errmsg) },
	    .request = inmsg->header.type,
	    .error = 0};

	/* request for remote daemon */
	if (PSnodes_isUp(id)) {
	    /*
	     * transfer the request to the remote daemon
	     */
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
	DDTypedBufferMsg_t msg = (DDTypedBufferMsg_t) {
	    .header = (DDMsg_t) {
		.type = PSP_CD_INFORESPONSE,
		.sender = PSC_getMyTID(),
		.dest = inmsg->header.sender,
		.len = sizeof(msg.header) + sizeof(msg.type) },
	    .type = inmsg->type,
	    .buf = { 0 } };
	int err=0;

	switch(inmsg->type){
	case PSP_INFO_TASK:
	    if (PSC_getPID(inmsg->header.dest)) {
		/* request info for a special task */
		PStask_t *task = PStasklist_find(managedTasks,
						 inmsg->header.dest);
		if (task) {
		    Taskinfo_t *taskinfo = (Taskinfo_t *)msg.buf;
		    taskinfo->tid = task->tid;
		    taskinfo->ptid = task->ptid;
		    taskinfo->loggertid = task->loggertid;
		    taskinfo->uid = task->uid;
		    taskinfo->group = task->group;
		    taskinfo->rank = task->rank;
		    taskinfo->connected = (task->fd != -1);

		    msg.header.len += sizeof(Taskinfo_t);
		    sendMsg(&msg);
		    msg.header.len -= sizeof(Taskinfo_t);
		}
	    } else {
		/* request info for all tasks */
		PStask_t *task;
		for (task=managedTasks; task; task=task->next) {
		    Taskinfo_t *taskinfo = (Taskinfo_t *)msg.buf;
		    taskinfo->tid = task->tid;
		    taskinfo->ptid = task->ptid;
		    taskinfo->loggertid = task->loggertid;
		    taskinfo->uid = task->uid;
		    taskinfo->group = task->group;
		    taskinfo->rank = task->rank;
		    taskinfo->connected = (task->fd != -1);

		    msg.header.len += sizeof(Taskinfo_t);
		    sendMsg(&msg);
		    msg.header.len -= sizeof(Taskinfo_t);
		}
	    }

	    /*
	     * send a EndOfList Sign
	     */
	    msg.type = PSP_INFO_TASKEND;
	    break;
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
	    getStateInfoRDP(*(int *) inmsg->buf, msg.buf, sizeof(msg.buf));
	    msg.header.len += strlen(msg.buf)+1;
	    break;
	case PSP_INFO_MCASTSTATUS:
	    getStateInfoMCast(*(int *) inmsg->buf, msg.buf, sizeof(msg.buf));
	    msg.header.len += strlen(msg.buf)+1;
	    break;
	case PSP_INFO_HOSTSTATUS:
	{
	    int i;
	    for (i=0; i<PSC_getNrOfNodes(); i++) {
		msg.buf[i] = PSnodes_isUp(i);
	    }
	    msg.header.len += sizeof(*msg.buf) * PSC_getNrOfNodes();
	    break;
	}
	case PSP_INFO_HOST:
	    *(int *)msg.buf = PSnodes_lookupHost(*(unsigned int *) inmsg->buf);
	    msg.header.len += sizeof(int);
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
	{
	    int i;
	    static NodelistEntry_t *nodelist = NULL;
	    static int nodelistlen = 0;
	    if (nodelistlen < PSC_getNrOfNodes()) {
		nodelist = (NodelistEntry_t *)
		    realloc(nodelist, PSC_getNrOfNodes() * sizeof(*nodelist));
		nodelistlen = PSC_getNrOfNodes();
	    }
	    for (i=0; i<PSC_getNrOfNodes(); i++) {
		MCastConInfo_t info;

		nodelist[i].up = PSnodes_isUp(i);
		nodelist[i].numCPU = PSnodes_getCPUs(i);
		nodelist[i].hwStatus = PSnodes_getHWStatus(i);

		getInfoMCast(i, &info);
		nodelist[i].load[0] = info.load.load[0];
		nodelist[i].load[1] = info.load.load[1];
		nodelist[i].load[2] = info.load.load[2];
		nodelist[i].totalJobs = info.jobs.total;
		nodelist[i].normalJobs = info.jobs.normal;
	    }
	    memcpy(msg.buf, nodelist, PSC_getNrOfNodes() * sizeof(*nodelist));
	    msg.header.len += PSC_getNrOfNodes() * sizeof(*nodelist);
	    break;
	}
	case PSP_INFO_PARTITION:
	{
	    int i, j;
	    static NodelistEntry_t *nodelist = NULL;
	    static int nodelistlen = 0;
	    unsigned int hwType;
	    PStask_t *requester;

	    requester = PStasklist_find(managedTasks, inmsg->header.sender);

	    if (nodelistlen < PSC_getNrOfNodes()) {
		nodelistlen = PSC_getNrOfNodes();
		nodelist = realloc(nodelist, nodelistlen * sizeof(*nodelist));
	    }

	    if (!requester) {
		snprintf(errtxt, sizeof(errtxt), "%s: requester %s not found",
			 __func__, PSC_printTID(inmsg->header.sender));
		PSID_errlog(errtxt, 0);
		err = 1;
		break;
	    }

	    hwType = *(unsigned int *) inmsg->buf;

	    for (i=0, j=0; i<PSC_getNrOfNodes(); i++) {
		MCastConInfo_t info;

		getInfoMCast(i, &info);

		if ((!hwType || PSnodes_getHWStatus(i) & hwType)
		    && (PSnodes_getUser(i) == PSNODES_ANYUSER
			|| PSnodes_getUser(i) == requester->uid
			|| !requester->uid)
		    && (PSnodes_getProcs(i) == PSNODES_ANYPROC
			|| PSnodes_getProcs(i) > info.jobs.normal)
		    && (PSnodes_getGroup(i) == PSNODES_ANYGROUP
			|| PSnodes_getGroup(i) == requester->gid
			|| !requester->gid)
		    && PSnodes_runJobs(i)) {

		    nodelist[j].id = i;
		    nodelist[j].up = PSnodes_isUp(i);
		    nodelist[j].numCPU = PSnodes_getCPUs(i);
		    nodelist[j].hwStatus = PSnodes_getHWStatus(i);

		    nodelist[j].load[0] = info.load.load[0];
		    nodelist[j].load[1] = info.load.load[1];
		    nodelist[j].load[2] = info.load.load[2];
		    nodelist[j].totalJobs = info.jobs.total;
		    nodelist[j].normalJobs = info.jobs.normal;
		    nodelist[j].maxJobs = PSnodes_getProcs(i);

		    j++;
		}
	    }

	    if (j<PSC_getNrOfNodes()) {
		nodelist[j].id = -1;
	    }

	    memcpy(msg.buf, nodelist, PSC_getNrOfNodes() * sizeof(*nodelist));
	    msg.header.len += PSC_getNrOfNodes() * sizeof(*nodelist);
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
			    if (task->options & PART_OPT_OVERBOOK) {
				*(int *)msg.buf =
				    task->partition[rank%task->partitionSize];
			    } else {
				*(int *)msg.buf = -1;
			    }
			} else {
			    *(int *)msg.buf = task->partition[rank];
			}
		    } else {
			*(int *)msg.buf = task->nextRank;
		    }
		    msg.header.len += sizeof(int);
		}
	    } else {
		*(int *)msg.buf = -1;
		msg.header.len += sizeof(int);
	    }
	    break;
	}
	default:
	    msg.type = PSP_INFO_UNKNOWN;
	}
	if (!err) sendMsg(&msg);
    }
}
