/*
 * ParaStation
 *
 * Copyright (C) 2018-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>
#include <errno.h>
#include <signal.h>

#include "psidtask.h"
#include "psidstatus.h"
#include "psdaemonprotocol.h"
#include "psidcomm.h"
#include "pscommon.h"
#include "psidutil.h"
#include "psidnodes.h"
#include "hardware.h"
#include "timer.h"
#include "psaccounthandles.h"

#include "pluginmalloc.h"

#include "psgwres.h"
#include "psgwlog.h"
#include "psgwconfig.h"

#include "psgwpart.h"

static handlerFunc_t oldProvidePart = NULL;
static handlerFunc_t oldProvidePartSL = NULL;

static char msgBuf[1024];

static void handleProvidePart(DDBufferMsg_t *msg)
{
    PSGW_Req_t *req = Request_findByFW(msg->header.dest);
    if (!req) {
	if (oldProvidePart) oldProvidePart(msg);
	return;
    }

    if (req->timerPartReq != -1) {
	Timer_remove(req->timerPartReq);
	req->timerPartReq = -1;
    }

    fdbg(PSGW_LOG_PART, "handle psgw partition slots\n");

    PStask_t *task = PStasklist_find(&managedTasks, msg->header.dest);
    PSpart_option_t options;
    size_t used = 0;

    if (!task) {
	flog("Task %s not found\n", PSC_printTID(msg->header.dest));
	return;
    }

    PSpart_request_t *partReq = task->request;
    if (!partReq) {
	flog("No partRequest for task %s\n", PSC_printTID(msg->header.dest));
	return;
    }

    /* The size of the partition to be received */
    PSP_getMsgBuf(msg, &used, __func__, "sizeExpected", &partReq->sizeExpected,
		  sizeof(partReq->sizeExpected));

    PSP_getMsgBuf(msg, &used, __func__, "options", &options, sizeof(options));
    if (partReq->options != options) {
	flog("options (%d/%d) have changed for %s\n", partReq->options, options,
	     PSC_printTID(msg->header.dest));
	return;
    }

    partReq->slots = malloc(partReq->sizeExpected * sizeof(*partReq->slots));
    if (!partReq->slots) {
	flog("no memory\n");
	return;
    }

    partReq->sizeGot = 0;
}

static void appendToSlotlist(DDBufferMsg_t *msg, PSpart_request_t *request)
{
    PSpart_slot_t *slots = request->slots + request->sizeGot;
    int16_t n, chunk, nBytes, myBytes = PSCPU_bytesForCPUs(PSCPU_MAX);
    size_t used = 0;

    PSID_log(PSID_LOG_PART, "%s(%s)\n", __func__, PSC_printTID(request->tid));

    PSP_getMsgBuf(msg, &used, __func__, "chunk", &chunk, sizeof(chunk));
    PSP_getMsgBuf(msg, &used, __func__, "nBytes", &nBytes, sizeof(nBytes));

    if (nBytes > myBytes) {
	PSID_log(-1, "%s(%s): too many CPUs: %d > %d\n", __func__,
		 PSC_printTID(request->tid), nBytes*8, myBytes*8);
    }

    for (n = 0; n < chunk; n++) {
	char cpuBuf[nBytes];

	PSP_getMsgBuf(msg, &used, __func__, "node", &slots[n].node,
		      sizeof(slots[n].node));

	PSP_getMsgBuf(msg, &used, __func__, "CPUset", cpuBuf, nBytes);
	PSCPU_clrAll(slots[n].CPUset);
	PSCPU_inject(slots[n].CPUset, cpuBuf, nBytes);
    }
    request->sizeGot += chunk;
}

static int getHWThreads(PSpart_slot_t *slots, uint32_t num,
			PSpart_HWThread_t **threads)
{
    unsigned int s, t = 0, totThreads = 0;
    PSpart_HWThread_t *HWThreads;

    for (s=0; s<num; s++) {
	totThreads += PSCPU_getCPUs(slots[s].CPUset, NULL, PSCPU_MAX);
    }

    if (totThreads < 1) {
	PSID_log(-1, "%s: No HW-threads in slots\n", __func__);
	if (*threads) free(*threads);
	*threads = NULL;

	return 0;
    }

    PSID_log(PSID_LOG_PART, "%s: slots %d threads %d\n", __func__, num,
	     totThreads);
    HWThreads = malloc(totThreads * sizeof(*HWThreads));

    if (!HWThreads) {
	PSID_log(-1, "%s: No memory\n", __func__);
	errno = ENOMEM;
	return -1;
    }

    for (s=0; s<num; s++) {
	unsigned int cpu;
	for (cpu=0; cpu<PSCPU_MAX; cpu++) {
	    if (PSCPU_isSet(slots[s].CPUset, cpu)) {
		HWThreads[t].node = slots[s].node;
		HWThreads[t].id = cpu;
		HWThreads[t].timesUsed = 0;
		t++;
	    }
	}
    }

    if (*threads) free(*threads);
    *threads = HWThreads;

    return totThreads;
}

static void handleProvidePartSL(DDBufferMsg_t *msg)
{

    PSGW_Req_t *req = Request_findByFW(msg->header.dest);
    if (!req) {
	if (oldProvidePartSL) oldProvidePartSL(msg);
	return;
    }

    fdbg(PSGW_LOG_PART, "handle psgw partition slots\n");

    PStask_t *task = PStasklist_find(&managedTasks, msg->header.dest);
    PSpart_request_t *partReq;

    if (!task) {
	flog("Task %s not found\n", PSC_printTID(msg->header.dest));
	return;
    }

    partReq = task->request;
    if (!partReq) {
	flog("No partRequest for task %s\n", PSC_printTID(msg->header.dest));
	return;
    }

    if (!partReq->slots) {
	flog("No slotlist created for task %s\n",
	     PSC_printTID(msg->header.dest));
	return;
    }

    appendToSlotlist(msg, partReq);

    if (partReq->sizeGot == partReq->sizeExpected) {
	/* partition complete, now delete the corresponding partRequest */
	int thrds;

	task->partitionSize = task->request->sizeExpected;
	task->partition = task->request->slots;
	task->request->slots = NULL;
	thrds = getHWThreads(task->partition, task->partitionSize,
			     &task->partThrds);
	if (thrds < 0) return;
	task->totalThreads = thrds;
	task->usedThreads = 0;
	task->activeChild = 0;
	task->options = task->request->options;

	PSpart_delReq(task->request);
	task->request = NULL;

	uint32_t i;
	PSnodes_ID_t *nodes = umalloc(sizeof(*nodes) * task->partitionSize);

	for (i=0; i<task->partitionSize; i++) {
	    fdbg(PSGW_LOG_PART, "part %i slot %i size %i\n", i,
		  task->partition[i].node, task->partitionSize);
	    nodes[i] = task->partition[i].node;
	}

	Request_setNodes(req, nodes, task->partitionSize);

	/* start prologue on psgwd nodes using pelogue */
	if (!startPElogue(req, PELOGUE_PROLOGUE)) {
	    flog("starting prologue failed\n");
	    return;
	}

	/* start psgw daemon using psexec */
	if (!startPSGWD(req)) {
	    flog("starting psgwd failed\n");
	    return;
	}
    }
}

void regPartMsg(void)
{
    oldProvidePart = PSID_registerMsg(PSP_DD_PROVIDEPART, handleProvidePart);
    oldProvidePartSL = PSID_registerMsg(PSP_DD_PROVIDEPARTSL,
					handleProvidePartSL);
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
    int tpp = getConfValueI(&config, "GATEWAY_TPP");

    task->request->uid = req->uid;
    task->request->gid = req->gid;
    task->request->start = task->started.tv_sec;
    task->request->tpp = tpp; /* set to max cores of gateway nodes to
				 simulate NODEFIRST */
    task->request->tid = task->tid;
    task->request->num = 0;
    task->request->nodes = NULL;
    task->request->numGot = 0;

    task->request->size = numNodes;
    task->request->hwType = 0;

    int hwIdx = HW_index("gateway");
    if (hwIdx == -1) {
	flog("invalid hardware index for gateway\n");
	return false;
    }
    task->request->hwType |= 1 << hwIdx;
    task->request->sort = PART_SORT_NONE;

    PSpart_option_t options = 0;
    options |= PART_OPT_NODEFIRST; /* not working anymore */
    task->request->options = options;
    task->request->priority = 0; /* Not used */

    if (!knowMaster()) return true; /* Automatic pull in initPartHandler() */

    DDBufferMsg_t msg = (DDBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_DD_GETPART,
	    .dest = PSC_getTID(getMasterID(), 0),
	    .sender = task->tid,
	    .len = sizeof(msg.header) },
	.buf = { 0 } };

    PSpart_encodeReq(&msg, task->request);

    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	mwarn(errno, "%s: sendMsg()", __func__);
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

static int fwCallback(int32_t exit_status, Forwarder_Data_t *fw)
{
    PSGW_Req_t *req = Request_verify(fw->userData);

    if (!req) {
	/* may happen on shutdown after epilogue */
	fdbg(PSGW_LOG_DEBUG, "no request for %p\n", fw->userData);
	return 0;
    }

    req->fwdata = NULL;
    if (exit_status) {
	snprintf(msgBuf, sizeof(msgBuf), "partition forwarder for job %s exit "
		 "with error %i\n", req->jobid, exit_status);
	cancelReq(req, msgBuf);
    }

    return 0;
}

int handleMotherMsg(PSLog_Msg_t *msg, Forwarder_Data_t *fw)
{
    PSGW_Req_t *req = Request_verify(fw->userData);

    if (!req) {
	flog("no request for %p\n", fw->userData);
	return 0;
    }

    /* failed to get resources from master */
    if (msg->header.type == PSP_CD_PARTITIONRES) {
	snprintf(msgBuf, sizeof(msgBuf), "getting resources for job %s "
		 "failed\n", req->jobid);
	flog("%s", msgBuf);

	/* inform user via error file */
	writeErrorFile(req, msgBuf, NULL, true);

	exit(1);
    }

    return 0;
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
