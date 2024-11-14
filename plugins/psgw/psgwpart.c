/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psgwpart.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "psattribute.h"
#include "pscommon.h"
#include "psdaemonprotocol.h"
#include "pspartition.h"
#include "psserial.h"
#include "timer.h"
#include "pluginconfig.h"
#include "pluginforwarder.h"
#include "pluginmalloc.h"
#include "psidstatus.h"
#include "psidtask.h"

#include "peloguetypes.h"
#include "psaccounthandles.h"

#include "psgwconfig.h"
#include "psgwlog.h"
#include "psgwres.h"

static char msgBuf[1024];

int handleReceivePart(void *data)
{
    PStask_t *task = data;
    if (!task) return 1;

    PSGW_Req_t *req = Request_findByFW(task->tid);
    if (!req) return 1;   // ignore this received partition

    fdbg(PSGW_LOG_PART, "handle psgw partition slots\n");

    if (req->timerPartReq != -1) {
	Timer_remove(req->timerPartReq);
	req->timerPartReq = -1;
    }

    PSnodes_ID_t *nodes = umalloc(sizeof(*nodes) * task->partitionSize);
    for (uint32_t i = 0; i < task->partitionSize; i++) {
	fdbg(PSGW_LOG_PART, "part %i slot %i size %i\n", i,
	     task->partition[i].node, task->partitionSize);
	nodes[i] = task->partition[i].node;
    }

    Request_setNodes(req, nodes, task->partitionSize);

    /* start prologue on psgwd nodes using pelogue */
    if (!startPElogue(req, PELOGUE_PROLOGUE)) {
	flog("starting prologue failed\n");
    } else if (!startPSGWD(req)) { /* start psgw daemon using psexec */
	flog("starting psgwd failed\n");
    }
    return 0;
}

static void partTimeout(int timerId, void *data)
{
    PSGW_Req_t *req = Request_verify(data);

    /* remove the timer */
    Timer_remove(timerId);

    if (!req) {
	flog("no request found for timer %i\n", timerId);
	return;
    }

    req->timerPartReq = -1;

    snprintf(msgBuf, sizeof(msgBuf), "partition request for jobid %s "
	     "timed out\n", req->jobid);
    cancelReq(req, msgBuf);
}

static bool sendPartitionReq(PStask_t *task, PSGW_Req_t *req, int numNodes)
{
    task->request = PSpart_newReq();
    if (!task->request) {
	flog("PSpart_newReq() failed\n");
	return false;
    }
    int tpp = getConfValueI(config, "GATEWAY_TPP");

    task->request->uid = req->uid;
    task->request->gid = req->gid;
    task->request->start = task->started.tv_sec;
    task->request->tpp = tpp; /* set to max cores of gateway nodes to
				 simulate NODEFIRST */
    task->request->tid = task->tid;
    task->request->num = 0;
    task->request->nodes = NULL;
    task->request->size = numNodes;
    task->request->hwType = 0;

    AttrIdx_t gwAttrIdx = Attr_index("gateway");
    if (gwAttrIdx == -1) {
	flog("unknown attribute 'gateway'\n");
	return false;
    }
    task->request->hwType |= 1 << gwAttrIdx;
    task->request->sort = PART_SORT_NONE;

    PSpart_option_t options = 0;
    options |= PART_OPT_NODEFIRST; /* not working anymore */
    task->request->options = options;
    task->request->priority = 0; /* Not used */

    if (!knowMaster()) return true; /* Automatic pull in initPartHandler() */

    PS_SendDB_t msg;
    initFragBuffer(&msg, PSP_DD_CREATEPART, 0);
    setFragDest(&msg, PSC_getTID(getMasterID(), 0));

    addTaskIdToMsg(task->request->tid, &msg);
    if (!PSpart_addToMsg(task->request, &msg)) {
	flog("PSpart_addToMsg() failed\n");
	goto ERROR;
    }
    /* add task->request->nodes stub */
    addDataToMsg(NULL, 0, &msg);

    if (sendFragMsg(&msg) == -1) {
	flog("sendFragMsg() failed\n");
	goto ERROR;
    }

    struct timeval timeout = { 10, 0};

    req->timerPartReq =
	Timer_registerEnhanced(&timeout, partTimeout, req);
    if (req->timerPartReq == -1) {
	flog("register timer for route script failed\n");
    }

    return true;

ERROR:
    if (task->request) {
	PSpart_delReq(task->request);
	task->request = NULL;
    }
    return false;
}

static void fwCallback(int32_t exit_status, Forwarder_Data_t *fw)
{
    PSGW_Req_t *req = Request_verify(fw->userData);

    if (!req) {
	/* may happen on shutdown after epilogue */
	fdbg(PSGW_LOG_DEBUG, "no request for %p\n", fw->userData);
	return;
    }

    req->fwdata = NULL;
    if (exit_status) {
	snprintf(msgBuf, sizeof(msgBuf), "partition forwarder for job %s exit "
		 "with error %i\n", req->jobid, exit_status);
	cancelReq(req, msgBuf);
    }
}

static bool handleMotherMsg(DDTypedBufferMsg_t *msg, Forwarder_Data_t *fw)
{
    PSGW_Req_t *req = Request_verify(fw->userData);
    if (!req) {
	flog("no request for %p\n", fw->userData);
	return false;
    }

    /* failed to get resources from master */
    if (msg->header.type == PSP_CD_PARTITIONRES) {
	int tpp = getConfValueI(config, "GATEWAY_TPP");
	flog("error: no free gateway nodes with %i cores found\n", tpp);
	snprintf(msgBuf, sizeof(msgBuf), "getting gateway resources for job %s "
		 "failed\n", req->jobid);
	flog("%s", msgBuf);

	/* inform user via error file */
	writeErrorFile(req, msgBuf, NULL, true);

	exit(1);
    }

    return false;
}

bool requestGWnodes(PSGW_Req_t *req, int numNodes)
{
    char fname[300];
    Forwarder_Data_t *fwdata = ForwarderData_new();

    snprintf(fname, sizeof(fname), "psgw-fw:%s", req->jobid);
    fwdata->pTitle = ustrdup(fname);
    fwdata->jobID = ustrdup(req->jobid);
    fwdata->userData = req;
    fwdata->graceTime = 30;
    fwdata->killSession = psAccountSignalSession;
    fwdata->callback = fwCallback;
    fwdata->handleMthrMsg = handleMotherMsg;

    if (!startForwarder(fwdata)) {
	mlog("%s: starting forwarder for request '%s' failed\n",
		__func__, req->jobid);
	ForwarderData_delete(fwdata);
	return false;
    }
    req->fwdata = fwdata;

    PStask_t *task = PStasklist_find(&managedTasks, fwdata->tid);
    if (!task) {
	flog("task for forwarder not found\n");
	return false;
    }

    fdbg(PSGW_LOG_PART, "psgw forwarder started, requesting %i gateways "
	  "from master\n", numNodes);

    return sendPartitionReq(task, req, numNodes);
}
