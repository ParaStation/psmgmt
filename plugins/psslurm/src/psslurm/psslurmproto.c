/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <signal.h>
#include <sys/vfs.h>
#include <malloc.h>
#include <pwd.h>
#include <sys/stat.h>

#include "pscio.h"
#include "pshostlist.h"
#include "psserial.h"
#include "env.h"
#include "selector.h"
#include "timer.h"

#include "psidnodes.h"
#include "psidspawn.h"
#include "psidplugin.h"

#include "pluginmalloc.h"
#include "pluginhelper.h"
#include "pluginconfig.h"

#include "psaccounthandles.h"
#include "peloguehandles.h"
#include "pspamhandles.h"

#include "slurmcommon.h"
#include "slurmnode.h"
#include "psslurmjob.h"
#include "psslurmgres.h"
#include "psslurmlog.h"
#include "psslurmcomm.h"
#include "psslurmauth.h"
#include "psslurmconfig.h"
#include "psslurmio.h"
#include "psslurmforwarder.h"
#include "psslurmpscomm.h"
#include "psslurmenv.h"
#include "psslurmpin.h"
#include "psslurmpelogue.h"
#include "psslurmpack.h"
#include "psslurm.h"
#include "psslurmfwcomm.h"
#include "psslurmnodeinfo.h"

#include "psslurmproto.h"

/** Slurm protocol version */
uint32_t slurmProto;

/** Slurm protocol version string */
char *slurmProtoStr = NULL;

/** Slurm version string */
char *slurmVerStr = NULL;

/** Flag to measure Slurm RPC execution times */
bool measureRPC = false;

/** Flag to request additional info in node registration */
static bool needNodeRegResp = true;

static int confAction = 0;

Ext_Resp_Node_Reg_t *tresDBconfig = NULL;

typedef struct {
    uint32_t jobid;
    uint32_t stepid;
    bool timeout;
    uid_t uid;
} Kill_Info_t;

/* operations of RPC SUSPEND_INT */
enum {
    SUSPEND_JOB,
    RESUME_JOB
};

/**
 * @brief Get CPU load average and free memory
 *
 * @param cpuload One minute CPU load average
 *
 * @param freemem Available memory
 *
 * @param uptime Seconds since boot
 */
static void getSysInfo(uint32_t *cpuload, uint64_t *freemem, uint32_t *uptime)
{
    struct sysinfo info;
    float div;

    if (sysinfo(&info) < 0) {
	*cpuload = *freemem = *uptime = 0;
    } else {
	div = (float)(1 << SI_LOAD_SHIFT);
	*cpuload = (info.loads[0]/div) * 100.0;
	*freemem = (((uint64_t )info.freeram)*info.mem_unit)/(1024*1024);
	*uptime = info.uptime;
    }
}

/**
 * @brief Send a ping response
 *
 * Send a Slurm ping message including the current system
 * load and free memory information.
 *
 * @param The ping request message
 */
static void sendPing(Slurm_Msg_t *sMsg)
{
    Resp_Ping_t ping;
    uint32_t unused;

    getSysInfo(&ping.cpuload, &ping.freemem, &unused);

    PS_SendDB_t *msg = &sMsg->reply;
    packRespPing(msg, &ping);
    sendSlurmReply(sMsg, RESPONSE_PING_SLURMD);
}

uint32_t __getLocalRankID(uint32_t rank, Step_t *step,
			  const char *caller, const int line)
{
    if (rank > 0 && (int32_t)(rank - step->packTaskOffset) < 0) {
	flog("invalid rank %u pack offset %u from %s:%i\n", rank,
	     step->packTaskOffset, caller, line);
	return NO_VAL;
    }

    if (step->localNodeId >= step->nrOfNodes) {
	flog("invalid nodeId %u greater than nrOfNodes %u\n",
	     step->localNodeId, step->nrOfNodes);
	return NO_VAL;
    }

    uint32_t adjRank = (rank > 0) ? rank - step->packTaskOffset : 0;

    for (uint32_t i=0; i<step->globalTaskIdsLen[step->localNodeId]; i++) {
	if (step->globalTaskIds[step->localNodeId][i] == adjRank) return i;
    }
    return NO_VAL;
}

bool writeJobscript(Job_t *job)
{
    FILE *fp;
    char *jobdir, buf[PATH_BUFFER_LEN];
    int written;

    if (!job->jsData) {
	mlog("%s: invalid jobscript data\n", __func__);
	return 0;
    }

    /* set jobscript filename */
    jobdir = getConfValueC(&Config, "DIR_JOB_FILES");
    snprintf(buf, sizeof(buf), "%s/%s", jobdir, strJobID(job->jobid));
    job->jobscript = ustrdup(buf);

    if (!(fp = fopen(job->jobscript, "a"))) {
	mlog("%s: open file '%s' failed\n", __func__, job->jobscript);
	return false;
    }

    while ((written = fprintf(fp, "%s", job->jsData)) !=
	    (int) strlen(job->jsData)) {
	if (errno == EINTR) continue;
	mlog("%s: writing jobscript '%s' failed : %s\n", __func__,
		job->jobscript, strerror(errno));
	return false;
    }
    fclose(fp);
    ufree(job->jsData);
    job->jsData = NULL;

    return true;
}

static int needIOReplace(char *ioString, char symbol)
{
    char *ptr = ioString, *next;
    int needReplace = 0, index = 1;

    while ((next = strchr(ptr, '%'))) {
	while (next[index] >= 48 && next[index] <=57) index++;

	if (next[index] == symbol) {
	    needReplace = 1;
	    break;
	}
	ptr = next+index;
    }
    return needReplace;
}

static void setIOoptions(char *ioString, int *Opt, int32_t *rank)
{
    if (ioString && strlen(ioString) > 0) {
	if ((sscanf(ioString, "%i", rank)) == 1) {
	    *Opt = IO_SRUN_RANK;
	} else if (needIOReplace(ioString, 't')) {
	    *Opt = IO_RANK_FILE;
	} else if (needIOReplace(ioString, 'n') ||
		needIOReplace(ioString, 'N')) {
	    *Opt = IO_NODE_FILE;
	} else {
	    *Opt = IO_GLOBAL_FILE;
	}
    } else {
	*Opt = IO_SRUN;
    }
}

/**
 * @brief Set the accounting frequency and type
 *
 * @param freqString String to extract the frequency from
 *
 * @param accType The accounting type to set
 */
static void setAccOpts(char *freqString, uint16_t *accType)
{
    int freq;
    char *strAcctType;

    if (!(strncmp("task=", freqString, 5))) {
	freq = atoi(freqString+5);

	if (freq >0) {
	    mlog("%s: setting acct freq to %i\n", __func__, freq);
	    psAccountSetPoll(freq);
	}
    }

    if ((strAcctType = getConfValueC(&SlurmConfig, "JobAcctGatherType"))) {
	*accType = (!(strcmp(strAcctType, "jobacct_gather/none"))) ? 0 : 1;
    } else {
	*accType = 0;
    }
}

/**
 * @brief Print various step information
 *
 * @param step The step to print the infos from
 */
static void printLaunchTasksInfos(Step_t *step)
{
    uint32_t i;

    /* env */
    for (i=0; i<step->env.cnt; i++) {
	mdbg(PSSLURM_LOG_ENV, "%s: env%i: '%s'\n", __func__, i,
		step->env.vars[i]);
    }

    /* spank env */
    for (i=0; i<step->spankenv.cnt; i++) {
	mdbg(PSSLURM_LOG_ENV, "%s: spankenv%i: '%s'\n", __func__, i,
		step->spankenv.vars[i]);
    }

    /* set stdout/stderr/stdin options */
    if (!(step->taskFlags & LAUNCH_USER_MANAGED_IO)) {
	fdbg(PSSLURM_LOG_IO, "stdOut '%s' stdOutRank %i stdOutOpt %s\n",
	     step->stdOut, step->stdOutRank, IO_strOpt(step->stdOutOpt));

	fdbg(PSSLURM_LOG_IO, "stdErr '%s' stdErrRank %i stdErrOpt %s\n",
	     step->stdErr, step->stdErrRank, IO_strOpt(step->stdErrOpt));

	fdbg(PSSLURM_LOG_IO, "stdIn '%s' stdInRank %i stdInOpt %s\n",
	     step->stdIn, step->stdInRank, IO_strOpt(step->stdInOpt));

	mdbg(PSSLURM_LOG_IO, "%s: bufferedIO '%i' labelIO '%i'\n", __func__,
	     step->taskFlags & LAUNCH_BUFFERED_IO,
	     step->taskFlags & LAUNCH_LABEL_IO);
    }

    /* job state */
    fdbg(PSSLURM_LOG_JOB, "%s in %s\n", strStepID(step),
	 strJobState(step->state));

    /* pinning */
    fdbg(PSSLURM_LOG_PART, "taskDist 0x%x\n", step->taskDist);
    fdbg(PSSLURM_LOG_PART, "cpuBindType 0x%hx, cpuBind '%s'\n",
	 step->cpuBindType, step->cpuBind);
    fdbg(PSSLURM_LOG_PART, "memBindType 0x%hx, memBind '%s'\n",
	 step->memBindType, step->memBind);
}

static bool extractStepPackInfos(Step_t *step)
{
    uint32_t nrOfNodes, i;

    fdbg(PSSLURM_LOG_PACK, "packNodeOffset %u  packJobid %u packNtasks %u "
	 "packOffset %u packTaskOffset %u packHostlist '%s' packNrOfNodes %u\n",
	 step->packNodeOffset, step->packJobid, step->packNtasks,
	 step->packOffset, step->packTaskOffset, step->packHostlist,
	 step->packNrOfNodes);

    if (!convHLtoPSnodes(step->packHostlist, getNodeIDbySlurmHost,
			 &step->packNodes, &nrOfNodes)) {
	mlog("%s: resolving PS nodeIDs from %s failed\n", __func__,
	     step->packHostlist);
	return false;
    }

    if (step->packNrOfNodes != nrOfNodes) {
	if (step->packNrOfNodes == step->nrOfNodes && !step->packNodeOffset) {
	    /* correct invalid pack host-list */
	    fdbg(PSSLURM_LOG_PACK, "correct pack nodes using %s\n",
		 step->slurmHosts);
	    ufree(step->packNodes);
	    step->packNodes = umalloc(sizeof(*step->packNodes) * step->nrOfNodes);
	    for (i=0; i<step->nrOfNodes; i++) {
		step->packNodes[i] = step->nodes[i];
	    }
	} else {
	    flog("extracting PS nodes from Slurm pack hostlist %s failed "
		 "(%u:%u)\n", step->packHostlist, step->packNrOfNodes,
		 nrOfNodes);
	    return false;
	}
    }

    for (i=0; i<step->packNrOfNodes; i++) {
	mdbg(PSSLURM_LOG_PACK, "%s: packTaskCount[%u]: %u\n", __func__, i,
		step->packTaskCounts[i]);
    }

    /* extract pack size */
    char *sPackSize;
    if (!(sPackSize = envGet(&step->env, "SLURM_PACK_SIZE"))) {
	mlog("%s: missing SLURM_PACK_SIZE environment\n", __func__);
	return false;
    }
    step->packSize = atoi(sPackSize);

    /* extract allocation ID */
    char *sPackID;
    if (!(sPackID = envGet(&step->env, "SLURM_JOB_ID_PACK_GROUP_0"))) {
	mlog("%s: missing SLURM_JOB_ID_PACK_GROUP_0 environment\n", __func__);
	return false;
    }
    step->packAllocID = atoi(sPackID);

    return true;
}

static bool testSlurmVersion(uint32_t pVer, uint32_t cmd)
{
    /* allow ping and node registration RPC to pass since they don't
     * contain any body to extract */
    if (cmd == REQUEST_PING || cmd == REQUEST_NODE_REGISTRATION_STATUS) {
	return true;
    }

    if (pVer < SLURM_MIN_PROTO_VERSION ||
	pVer > SLURM_MAX_PROTO_VERSION) {
	flog("slurm protocol version %u not supported, cmd(%i) %s\n",
	     pVer, cmd, msgType2String(cmd));
	return false;
    }
    return true;
}

/**
 * @brief Handle a job info response from slurmctld
 *
 * @param msg The response message to handle
 *
 * @param info Additional info
 *
 * @return Always return 0
 */
static int handleJobInfoResp(Slurm_Msg_t *msg, void *info)
{
    char **ptr = &msg->ptr;
    uint32_t count;
    time_t last;

    if (msg->head.type == RESPONSE_SLURM_RC) {
	uint32_t rc;
	getUint32(ptr, &rc);
	flog("error rc %s\n", slurmRC2String(rc));
	return 0;
    }

    if (msg->head.type != RESPONSE_JOB_INFO) {
	flog("unexpected message type %s(%i)\n", msgType2String(msg->head.type),
	     msg->head.type);
	return 0;
    }

    /* number of job records */
    getUint32(ptr, &count);
    /* last update */
    getTime(ptr, &last);

    flog("received count: %u time %zu\n", count, last);
    /* skip */
    uint32_t tmp;
    getUint32(ptr, &tmp);
    getUint32(ptr, &tmp);
    getStringM(ptr);
    getUint32(ptr, &tmp);
    getUint32(ptr, &tmp);
    getUint32(ptr, &tmp);

    uint32_t jobid, userid;
    getUint32(ptr, &jobid);
    getUint32(ptr, &userid);

    flog("jobid: %u userid %u\n", jobid, userid);

    return 0;
}

int requestJobInfo(uint32_t jobid)
{
    uint16_t flags = 16;

    PS_SendDB_t msg = { .bufUsed = 0, .useFrag = false };

    addUint32ToMsg(jobid, &msg);
    addUint16ToMsg(flags, &msg);

    return sendSlurmReq(REQUEST_JOB_INFO_SINGLE, &msg, &handleJobInfoResp, NULL);
}

uint32_t getLocalID(PSnodes_ID_t *nodes, uint32_t nrOfNodes)
{
    uint32_t i, id = NO_VAL;
    PSnodes_ID_t myID = PSC_getMyID();

    for (i=0; i<nrOfNodes; i++) if (nodes[i] == myID) id = i;
    return id;
}

static void handleLaunchTasks(Slurm_Msg_t *sMsg)
{
    Step_t *step = NULL;
    uint32_t count, i;

    if (pluginShutdown) {
	/* don't accept new steps if a shutdown is in progress */
	sendSlurmRC(sMsg, SLURM_ERROR);
	return;
    }

    /* unpack request */
    if (!(unpackReqLaunchTasks(sMsg, &step))) {
	flog("unpacking launch request (%u) failed\n", sMsg->head.version);
	sendSlurmRC(sMsg, SLURM_ERROR);
	return;
    }

    /* verify job credential */
    if (!(verifyStepData(step))) {
	flog("invalid data for %s\n", strStepID(step));
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	goto ERROR;
    }

    step->state = JOB_QUEUED;
    printLaunchTasksInfos(step);

    /* set accounting options */
    setAccOpts(step->acctFreq, &step->accType);

    /* srun addr is always empty, use msg header addr instead */
    step->srun.sin_addr.s_addr = sMsg->head.addr;
    step->srun.sin_port = sMsg->head.port;

    /* env / spank env */
    step->env.size = step->env.cnt;
    step->spankenv.size = step->spankenv.cnt;
    for (i=0; i<step->spankenv.cnt; i++) {
	if (!(strncmp("_SLURM_SPANK_OPTION_x11spank_forward_x",
		step->spankenv.vars[i], 38))) {
	    step->x11forward = 1;
	}
    }

    /* set stdout/stderr/stdin options */
    if (!(step->taskFlags & LAUNCH_USER_MANAGED_IO)) {
	setIOoptions(step->stdOut, &step->stdOutOpt, &step->stdOutRank);
	setIOoptions(step->stdErr, &step->stdErrOpt, &step->stdErrRank);
	setIOoptions(step->stdIn, &step->stdInOpt, &step->stdInRank);
    }

    /* convert slurm hostlist to PSnodes */
    if (!convHLtoPSnodes(step->slurmHosts, getNodeIDbySlurmHost,
			 &step->nodes, &count)) {
	flog("resolving PS nodeIDs from %s failed\n", step->slurmHosts);
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	goto ERROR;
    }

    if (count != step->nrOfNodes) {
	flog("mismatching number of nodes %u vs %u for %s\n",
	     count, step->nrOfNodes, strStepID(step));
	step->nrOfNodes = count;
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	goto ERROR;
    }

    step->localNodeId = getLocalID(step->nodes, step->nrOfNodes);
    if (step->localNodeId == NO_VAL) {
	flog("local node ID %i for %s in %s num nodes %i not found\n",
	     PSC_getMyID(), strStepID(step), step->slurmHosts, step->nrOfNodes);
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	goto ERROR;
    }

    step->nodeinfos = getStepNodeinfoArray(step);
    if (!step->nodeinfos) {
	flog("failed to fill nodeinfos of step %s\n", strStepID(step));
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	goto ERROR;
    }

    /* pack info */
    if (step->packNrOfNodes != NO_VAL) {
	if (!extractStepPackInfos(step)) {
	    flog("extracting pack information failed\n");
	    sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	    goto ERROR;
	}
    }

    /* determine my role in the step */
    if (step->packJobid != NO_VAL) {
	/* step with pack */
	if (step->packNodes[0] == PSC_getMyID()) step->leader = true;
    } else {
	if (step->nodes[0] == PSC_getMyID()) step->leader = true;
    }

    flog("%s user '%s' np %u nodes '%s' N %u tpp %u pack size %u"
	 " leader %i exe '%s' packJobid %u hetComp %u\n", strStepID(step),
	 step->username, step->np, step->slurmHosts, step->nrOfNodes, step->tpp,
	 step->packSize, step->leader, step->argv[0],
	 step->packJobid == NO_VAL ? 0 : step->packJobid,
	 step->stepHetComp == NO_VAL ? 0 : step->stepHetComp);

    /* ensure an allocation exists for the new step */
    if (!findAlloc(step->jobid)) {
	flog("error: no allocation for jobid %u found\n", step->jobid);
	flog("** ensure slurmctld prologue is setup correctly **\n");
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	goto ERROR;
    }

    /* set slots for the step (and number of hardware threads) */
    if (!(setStepSlots(step))) {
	flog("setting hardware threads for %s failed\n", strStepID(step));
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	goto ERROR;
    }

    /* sanity check nrOfNodes */
    if (step->nrOfNodes > (uint16_t) PSC_getNrOfNodes()) {
	flog("invalid nrOfNodes %u known Nodes %u for %s\n",
	     step->nrOfNodes, PSC_getNrOfNodes(), strStepID(step));
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	goto ERROR;
    }

    if (step->leader) {
	/* mother superior (pack leader) */
	if (step->packJobid != NO_VAL) {
	    /* allocate memory for pack infos from other mother superiors */
	    step->packInfo = ucalloc(sizeof(*step->packInfo) *
				    (step->packSize));
	    step->packFollower = ucalloc(sizeof(PSnodes_ID_t) *
					(step->packSize));
	}

	step->srunControlMsg.sock = sMsg->sock;
	step->srunControlMsg.head.forward = sMsg->head.forward;
	step->srunControlMsg.recvTime = sMsg->recvTime;

	/* start mpiexec to spawn the parallel processes,
	 * intercept createPart call to overwrite the nodelist */
	step->state = JOB_PRESTART;
	fdbg(PSSLURM_LOG_JOB, "%s in %s\n", strStepID(step),
	     strJobState(step->state));
	if (step->packJobid == NO_VAL) {
	    if (!(execStepLeader(step))) {
		sendSlurmRC(sMsg, ESLURMD_FORK_FAILED);
		goto ERROR;
	    }
	}
    } else {
	/* sister node (pack follower) */

	/* start I/O forwarder */
	execStepFollower(step);

	if (sMsg->sock != -1) {
	    /* say ok to waiting srun */
	    sendSlurmRC(sMsg, SLURM_SUCCESS);
	}
    }

    if (step->packJobid != NO_VAL && step->nodes[0] == PSC_getMyID()) {
	/* forward hw thread infos to pack leader */
	if (send_PS_PackInfo(step) == -1) {
	    sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	    goto ERROR;
	}
    }

    handleCachedMsg(step);
    releaseDelayedSpawns(step->jobid, step->stepid);
    return;

ERROR:
    if (step) deleteStep(step->jobid, step->stepid);
}

static int doSignalTasks(Req_Signal_Tasks_t *req)
{
    if (req->flags & KILL_FULL_JOB) {
	/* send signal to complete job including all steps */
	mlog("%s: sending all processes of job %u signal %u\n", __func__,
	     req->jobid, req->signal);
	signalJobscript(req->jobid, req->signal, req->uid);
	signalStepsByJobid(req->jobid, req->signal, req->uid);
    } else if (req->flags & KILL_STEPS_ONLY) {
	/* send signal to all steps excluding the jobscript */
	mlog("%s: send steps %u signal %u\n", __func__,
	     req->jobid, req->signal);
	signalStepsByJobid(req->jobid, req->signal, req->uid);
    } else {
	int ret = 0;

	if (req->stepid == SLURM_BATCH_SCRIPT) {
	    /* signal jobscript only, not all corresponding steps */
	    ret = signalJobscript(req->jobid, req->signal, req->uid);
	} else {
	    /* signal a single step */
	    flog("sending step %u:%u signal %u\n", req->jobid,
		 req->stepid, req->signal);
	    Step_t *step = findStepByStepId(req->jobid, req->stepid);
	    if (step) ret = signalStep(step, req->signal, req->uid);
	}

	/* we only return an error if we signal a specific jobscript/step */
	return ret;
    }

    return 1;
}

/**
 * @brief Send a SIGKILL for a request after a grace period
 */
static void sendDelayedKill(int timerId, void *data)
{
    Timer_remove(timerId);

    Req_Signal_Tasks_t *req = data;
    req->signal = SIGKILL;
    doSignalTasks(req);

    ufree(req);
}

/**
 * @brief Send SIGCONT, SIGTERM and SIGKILL to job
 *
 * @param req The request holding all needed information
 */
static void doSendTermKill(Req_Signal_Tasks_t *req)
{
    req->signal = SIGCONT;
    doSignalTasks(req);

    req->signal = SIGTERM;
    doSignalTasks(req);

    Req_Signal_Tasks_t *reqDup = umalloc(sizeof(*req));
    memcpy(reqDup, req, sizeof(*req));

    int grace = getConfValueI(&SlurmConfig, "KillWait");
    struct timeval timeout = {grace, 0};
    Timer_registerEnhanced(&timeout, sendDelayedKill, reqDup);
}

/**
 * @brief Handle a signal tasks request
 *
 * Request to send a signal to selected tasks. Depending on the options
 * decoded in the flags the signal will be send to all tasks of a job/step
 * or to a single jobscript.
 *
 * @param sMsg The message holding the request
 */
static void handleSignalTasks(Slurm_Msg_t *sMsg)
{
    Req_Signal_Tasks_t *req = NULL;

    /* unpack request */
    if (!unpackReqSignalTasks(sMsg, &req)) {
	mlog("%s: unpacking request signal tasks failed\n", __func__);
	sendSlurmRC(sMsg, SLURM_ERROR);
	return;
    }
    req->uid = sMsg->head.uid;

    /* handle magic Slurm signals */
    switch (req->signal) {
	case SIG_TERM_KILL:
	    doSendTermKill(req);
	    sendSlurmRC(sMsg, SLURM_SUCCESS);
	    goto cleanup;
	case SIG_UME:
	case SIG_REQUEUED:
	case SIG_PREEMPTED:
	case SIG_TIME_LIMIT:
	case SIG_ABORT:
	case SIG_NODE_FAIL:
	case SIG_FAILURE:
	    sendSlurmRC(sMsg, SLURM_SUCCESS);
	    flog("implement signal %u\n", req->signal);
	    goto cleanup;
    }

    if (!doSignalTasks(req)) {
	sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	goto cleanup;
    }

    sendSlurmRC(sMsg, SLURM_SUCCESS);

cleanup:
    ufree(req);
}

static void sendReattchReply(Step_t *step, Slurm_Msg_t *sMsg, uint32_t rc)
{
    PS_SendDB_t *reply = &sMsg->reply;
    uint32_t i, numTasks, countPIDS = 0, countPos;

    /* hostname */
    addStringToMsg(getConfValueC(&Config, "SLURM_HOSTNAME"), reply);
    /* return code */
    addUint32ToMsg(rc, reply);

    if (rc == SLURM_SUCCESS) {
	list_t *t;
	numTasks = step->globalTaskIdsLen[step->localNodeId];
	/* number of tasks */
	addUint32ToMsg(numTasks, reply);
	/* gtids */
	addUint32ArrayToMsg(step->globalTaskIds[step->localNodeId],
			    numTasks, reply);
	/* local pids */
	countPos = reply->bufUsed;
	addUint32ToMsg(0, reply);

	list_for_each(t, &step->tasks) {
	    PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	    if (task->childRank >= 0) {
		countPIDS++;
		addUint32ToMsg(PSC_getPID(task->childTID), reply);
	    }
	}
	*(uint32_t *) (reply->buf + countPos) = htonl(countPIDS);

	/* executable names */
	for (i=0; i<numTasks; i++) {
	    addStringToMsg(step->argv[0], reply);
	}
    } else {
	/* number of tasks */
	addUint32ToMsg(0, reply);
	/* gtids */
	addUint32ToMsg(0, reply);
	/* local pids */
	addUint32ToMsg(0, reply);
	/* no executable names */
    }

    sendSlurmReply(sMsg, RESPONSE_REATTACH_TASKS);
}

static void handleReattachTasks(Slurm_Msg_t *sMsg)
{
    uint32_t rc = SLURM_SUCCESS;

    Req_Reattach_Tasks_t *req = NULL;
    if (!unpackReqReattachTasks(sMsg, &req)) {
	flog("unpacking request reattach tasks failed\n");
	rc = ESLURM_INVALID_JOB_ID;
	goto SEND_REPLY;
    }

    Step_t *step = findStepByStepId(req->jobid, req->stepid);
    if (!step) {
	Step_t s = {
	    .jobid = req->jobid,
	    .stepid = req->stepid };
	flog("%s to reattach not found\n", strStepID(&s));
	rc = ESLURM_INVALID_JOB_ID;
	goto SEND_REPLY;
    }

    /* check permissions */
    if (!(verifyUserId(sMsg->head.uid, step->uid))) {
	flog("request from invalid user %u %s\n", sMsg->head.uid,
	     strStepID(step));
	rc = ESLURM_USER_ID_MISSING;
	goto SEND_REPLY;
    }

    if (!step->fwdata) {
	/* no forwarder to attach to */
	flog("forwarder for %s to reattach not found\n", strStepID(step));
	rc = ESLURM_INVALID_JOB_ID;
	goto SEND_REPLY;
    }

    if (req->numCtlPorts < 1) {
	flog("invalid request, no control ports for %s\n", strStepID(step));
	rc = ESLURM_PORTS_INVALID;
	goto SEND_REPLY;
    }

    if (req->numIOports < 1) {
	flog("invalid request, no I/O ports for %s\n", strStepID(step));
	rc = ESLURM_PORTS_INVALID;
	goto SEND_REPLY;
    }

    if (!req->cred) {
	flog("invalid credential for %s\n", strStepID(step));
	rc = ESLURM_INVALID_JOB_CREDENTIAL;
	goto SEND_REPLY;
    }

    if (strlen(req->cred->sig) + 1 != SLURM_IO_KEY_SIZE) {
	flog("invalid I/O key size %zu for %s\n", strlen(req->cred->sig) + 1,
	     strStepID(step));
	rc = ESLURM_INVALID_JOB_CREDENTIAL;
	goto SEND_REPLY;
    }

    /* send message to forwarder */
    fwCMD_reattachTasks(step->fwdata, sMsg->head.addr,
			req->ioPorts[step->localNodeId % req->numIOports],
			req->ctlPorts[step->localNodeId % req->numCtlPorts],
			req->cred->sig);
SEND_REPLY:

    sendReattchReply(step, sMsg, rc);

    if (req) {
	freeJobCred(req->cred);
	ufree(req->ioPorts);
	ufree(req->ctlPorts);
	ufree(req);
    }
}

static void handleSuspendInt(Slurm_Msg_t *sMsg)
{
    Req_Suspend_Int_t *req;

    /* unpack request */
    if (!unpackReqSuspendInt(sMsg, &req)) {
	flog("unpacking request suspend_int failed\n");
	sendSlurmRC(sMsg, SLURM_ERROR);
	return;
    }

    switch (req->op) {
	case SUSPEND_JOB:
	    flog("suspend job %u\n",req->jobid);
	    signalStepsByJobid(req->jobid, SIGSTOP, sMsg->head.uid);
	    break;
	case RESUME_JOB:
	    flog("resume job %u\n",req->jobid);
	    signalStepsByJobid(req->jobid, SIGCONT, sMsg->head.uid);
	    break;
	default:
	    flog("unknown suspend_int operation %u\n", req->op);
	    sendSlurmRC(sMsg, ESLURM_NOT_SUPPORTED);
	    return;
    }

    sendSlurmRC(sMsg, SLURM_SUCCESS);
}

static void handleUpdateJobTime(Slurm_Msg_t *sMsg)
{
    /* does nothing, since timelimit is not used in slurmd */

    /* check permissions */
    if (sMsg->head.uid != 0 && sMsg->head.uid != slurmUserID) {
	mlog("%s: request from invalid user %u\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    sendSlurmRC(sMsg, SLURM_SUCCESS);
}

/**
 * @brief Handle a shutdown request
 *
 * Only psslurm and its dependent plugins will be unloaded.
 * The psid itself will *not* be terminated.
 *
 * @param sMsg The Slurm message to handle
 */
static void handleShutdown(Slurm_Msg_t *sMsg)
{
    /* check permissions */
    if (sMsg->head.uid != 0 && sMsg->head.uid != slurmUserID) {
	mlog("%s: request from invalid user %u\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }
    sendSlurmRC(sMsg, SLURM_SUCCESS);

    mlog("%s: shutdown on request from uid %i\n", __func__, sMsg->head.uid);
    PSIDplugin_finalize("psslurm");
}

static void handleReconfigure(Slurm_Msg_t *sMsg)
{
    /* check permissions */
    if (sMsg->head.uid != 0 && sMsg->head.uid != slurmUserID) {
	mlog("%s: request from invalid user %u\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    /* activate the new configuration */
    updateSlurmConf(&configHash);

    /* protocol version SLURM_20_02 and above don't expect an answer */
    if (slurmProto <= SLURM_19_05_PROTO_VERSION) {
	sendSlurmRC(sMsg, SLURM_SUCCESS);
    }
}

/**
 * @brief Free a Slurm configuration message
 *
 * @param config The message to free
 */
static void freeSlurmConfigMsg(Config_Msg_t *config)
{
    ufree(config->slurm_conf);
    ufree(config->acct_gather_conf);
    ufree(config->cgroup_conf);
    ufree(config->cgroup_allowed_dev_conf);
    ufree(config->ext_sensor_conf);
    ufree(config->gres_conf);
    ufree(config->knl_cray_conf);
    ufree(config->knl_generic_conf);
    ufree(config->plugstack_conf);
    ufree(config->topology_conf);
    ufree(config->xtra_conf);
    ufree(config->slurmd_spooldir);
    ufree(config);
}

static bool writeFile(const char *name, const char *dir, const char *data)
{
    char path[1024];

    /* skip empty files */
    if (!data || strlen(data) < 1) return true;

    snprintf(path, sizeof(path), "%s/%s", dir, name);

    FILE *fp = fopen(path, "w+");
    if (!fp) {
	mwarn(errno, "%s: open %s failed: ", __func__, path);
	return false;
    }

    if (data) {
	errno = 0;
	fwrite(data, strlen(data), 1, fp);
	if (errno) {
	    mwarn(errno, "%s: writing to %s failed: ", __func__, path);
	    return false;
	}
    }

    fclose(fp);
    return true;
}

/**
 * @brief Write various Slurm configuration files
 *
 * @param config The configuration message holding the payload to write
 *
 * @param confDir The directory to write configuration files to
 *
 * @return Returns true on success or false on error
 */
static bool writeSlurmConfigFiles(Config_Msg_t *config, char *confDir)
{
    if (mkdir(confDir, 0755) == -1) {
	if (errno != EEXIST) {
	    mwarn(errno, "%s: mkdir(%s) failed: ", __func__, confDir);
	    return false;
	}
    }

    /* write various Slurm configuration files */
    if (!writeFile("slurm.conf", confDir, config->slurm_conf)) return false;
    if (!writeFile("acct_gather.conf", confDir, config->acct_gather_conf)) {
	return false;
    }
    if (!writeFile("cgroup.conf", confDir, config->cgroup_conf)) return false;
    if (!writeFile("cgroup_allowd_dev.conf", confDir,
		   config->cgroup_allowed_dev_conf)) {
	return false;
    }
    if (!writeFile("ext_sensor.conf", confDir, config->ext_sensor_conf)) {
	return false;
    }
    if (!writeFile("gres.conf", confDir, config->gres_conf)) return false;
    if (!writeFile("knl_cray.conf", confDir, config->knl_cray_conf)) {
	return false;
    }
    if (!writeFile("knl_generic.conf", confDir, config->knl_generic_conf)) {
	return false;
    }
    if (!writeFile("plugstack.conf", confDir, config->plugstack_conf)) {
	return false;
    }
    if (!writeFile("topology.conf", confDir, config->topology_conf)) {
	return false;
    }

    /* link configuration to Slurm /run-directory so Slurm commands (e.g.
     * scontrol, sbatch) can use them */
    char *runDir = getConfValueC(&Config, "SLURM_RUN_DIR");
    if (mkdir(runDir, 0755) == -1) {
	if (errno != EEXIST) {
	    mwarn(errno, "%s: mkdir(%s) failed: ", __func__, runDir);
	    return false;
	}
    }

    char destLink[1024];
    snprintf(destLink, sizeof(destLink), "%s/conf", runDir);

    unlink(destLink);
    if (symlink(confDir, destLink) == -1) {
	flog("symlink to %s -> %s failed\n", destLink, confDir);
	return false;
    }

    return true;
}

/**
 * @brief Handle a REQUEST_RECONFIGURE_WITH_CONFIG RPC
 *
 * Update all Slurm configuration cache files and activate the new
 * configuration.
 *
 * @param sMsg The message holding the RPC to handle
 */
static void handleConfig(Slurm_Msg_t *sMsg)
{
    Config_Msg_t *config;

    /* check permissions */
    if (sMsg->head.uid != 0 && sMsg->head.uid != slurmUserID) {
	flog("request from invalid user %u\n", sMsg->head.uid);
	return;
    }

    /* unpack request */
    if (!(unpackConfigMsg(sMsg, &config))) {
	flog("unpacking job launch request failed\n");
	return;
    }

    /* update all configuration files */
    char *confDir = getConfValueC(&Config, "SLURM_CONF_DIR");
    bool ret = writeSlurmConfigFiles(config, confDir);
    freeSlurmConfigMsg(config);

    if (!ret) {
	flog("failed to write Slurm configuration files to %s\n", confDir);
	return;
    }

    /* activate the new configuration */
    updateSlurmConf(&configHash);

    /* slurmctld does not expect an answer */
}

static void handleRebootNodes(Slurm_Msg_t *sMsg)
{
    /* check permissions */
    if (sMsg->head.uid != 0 && sMsg->head.uid != slurmUserID) {
	mlog("%s: request from invalid user %u\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    sendSlurmRC(sMsg, ESLURM_NOT_SUPPORTED);
}

static void handleHealthCheck(Slurm_Msg_t *sMsg)
{
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sMsg, ESLURM_NOT_SUPPORTED);
}

static void handleAcctGatherUpdate(Slurm_Msg_t *sMsg)
{
    PS_SendDB_t *msg = &sMsg->reply;

    /* check permissions */
    if (sMsg->head.uid != 0 && sMsg->head.uid != slurmUserID) {
	mlog("%s: request from invalid user %u\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    /* pack dummy data */
    packEnergyData(msg);

    sendSlurmReply(sMsg, RESPONSE_ACCT_GATHER_UPDATE);
}

static void handleAcctGatherEnergy(Slurm_Msg_t *sMsg)
{
    PS_SendDB_t *msg = &sMsg->reply;

    /* check permissions */
    if (sMsg->head.uid != 0 && sMsg->head.uid != slurmUserID) {
	mlog("%s: request from invalid user %u\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    /* pack dummy data */
    packEnergyData(msg);

    sendSlurmReply(sMsg, RESPONSE_ACCT_GATHER_ENERGY);
}

/**
 * @brief Find a step by its task (mpiexec) pid
 *
 * @param sMsg The request message
 */
static void handleJobId(Slurm_Msg_t *sMsg)
{
    char **ptr = &sMsg->ptr;
    uint32_t pid = 0;

    getUint32(ptr, &pid);

    Step_t *step = findStepByPsidTask(pid);
    if (step) {
	PS_SendDB_t *msg = &sMsg->reply;

	addUint32ToMsg(step->jobid, msg);
	addUint32ToMsg(SLURM_SUCCESS, msg);

	sendSlurmReply(sMsg, RESPONSE_JOB_ID);
    } else {
	sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
    }
}

static void handleFileBCast(Slurm_Msg_t *sMsg)
{
    BCast_t *bcast = NULL;
    Job_t *job;
    Alloc_t *alloc;

    /* unpack request */
    if (!unpackReqFileBcast(sMsg, &bcast)) {
	mlog("%s: unpacking request file bcast failed\n", __func__);
	sendSlurmRC(sMsg, SLURM_ERROR);
	return;
    }
    bcast->msg.sock = sMsg->sock;

    /* unpack credential */
    if (!extractBCastCred(sMsg, bcast)) {
	if (!errno) {
	    sendSlurmRC(sMsg, ESLURM_AUTH_CRED_INVALID);
	} else {
	    sendSlurmRC(sMsg, errno);
	}
	mlog("%s: extracting bcast credential failed\n", __func__);
	goto CLEANUP;
    }

    if (bcast->compress) {
	mlog("%s: implement compression for bcast\n", __func__);
	sendSlurmRC(sMsg, SLURM_ERROR);
	goto CLEANUP;
    }

    /* assign to job/allocation */
    if (!(job = findJobById(bcast->jobid))) {
	if (!(alloc = findAlloc(bcast->jobid))) {
	    mlog("%s: job %u not found\n", __func__, bcast->jobid);
	    sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	    goto CLEANUP;
	} else {
	    bcast->uid = alloc->uid;
	    bcast->gid = alloc->gid;
	    bcast->env = &alloc->env;
	    ufree(bcast->username);
	    bcast->username = ustrdup(alloc->username);
	    Step_t *step = findStepByJobid(bcast->jobid);
	    if (step) PSCPU_copy(bcast->hwthreads,
		    step->nodeinfos[step->localNodeId].stepHWthreads);
	}
    } else {
	bcast->uid = job->uid;
	bcast->gid = job->gid;
	bcast->env = &job->env;
	PSCPU_copy(bcast->hwthreads, job->hwthreads);
	ufree(bcast->username);
	bcast->username = ustrdup(job->username);
    }

    mdbg(PSSLURM_LOG_PROTO, "%s: jobid %u blockNum %u lastBlock %u force %u"
	 " modes %u, user '%s' uid %u gid %u fileName '%s' blockLen %u\n",
	 __func__, bcast->jobid, bcast->blockNumber, bcast->lastBlock,
	 bcast->force, bcast->modes, bcast->username, bcast->uid, bcast->gid,
	 bcast->fileName, bcast->blockLen);
    if (logger_getMask(psslurmlogger) & PSSLURM_LOG_PROTO) {
	printBinaryData(bcast->block, bcast->blockLen, "bcast->block");
    }

    if (bcast->blockNumber == 1) {
	mlog("%s: jobid %u file '%s' user '%s'\n", __func__, bcast->jobid,
		bcast->fileName, bcast->username);
    }

    /* start forwarder to write the file */
    if (!execBCast(bcast)) {
	sendSlurmRC(sMsg, ESLURMD_FORK_FAILED);
	goto CLEANUP;
    }
    return;

CLEANUP:
    deleteBCast(bcast);
}

static int addSlurmAccData(SlurmAccData_t *slurmAccData, PS_SendDB_t *data)
{
    bool res;
    AccountDataExt_t *accData = &slurmAccData->psAcct;

    /* no accounting data */
    if (!slurmAccData->type) goto PACK_RESPONSE;

    if (slurmAccData->childPid) {
	res = psAccountGetDataByJob(slurmAccData->childPid, accData);
    } else {
	res = psAccountGetDataByLogger(slurmAccData->loggerTID, accData);
    }

    slurmAccData->empty = !res;

    if (!res) {
	/* getting account data failed */
	flog("getting account data for pid %u logger '%s' failed\n",
	     slurmAccData->childPid, PSC_printTID(slurmAccData->loggerTID));
	goto PACK_RESPONSE;
    }

    uint64_t avgVsize = accData->avgVsizeCount ?
			accData->avgVsizeTotal / accData->avgVsizeCount : 0;
    uint64_t avgRss = accData->avgRssCount ?
		      accData->avgRssTotal / accData->avgRssCount : 0;

    mlog("%s: adding account data: maxVsize %zu maxRss %zu pageSize %lu "
	 "u_sec %lu u_usec %lu s_sec %lu s_usec %lu num_tasks %u avgVsize %lu"
	 " avgRss %lu avg cpufreq %.2fG\n", __func__, accData->maxVsize,
	 accData->maxRss, accData->pageSize, accData->rusage.ru_utime.tv_sec,
	 accData->rusage.ru_utime.tv_usec, accData->rusage.ru_stime.tv_sec,
	 accData->rusage.ru_stime.tv_usec, accData->numTasks, avgVsize, avgRss,
	 ((double) accData->cpuFreq / (double) accData->numTasks)
	 / (double) 1048576);

    mdbg(PSSLURM_LOG_ACC, "%s: nodes maxVsize %u maxRss %u maxPages %u "
	 "minCpu %u maxDiskRead %u maxDiskWrite %u\n", __func__,
	 PSC_getID(accData->taskIds[ACCID_MAX_VSIZE]),
	 PSC_getID(accData->taskIds[ACCID_MAX_RSS]),
	 PSC_getID(accData->taskIds[ACCID_MAX_PAGES]),
	 PSC_getID(accData->taskIds[ACCID_MIN_CPU]),
	 PSC_getID(accData->taskIds[ACCID_MAX_DISKREAD]),
	 PSC_getID(accData->taskIds[ACCID_MAX_DISKWRITE]));

    if (accData->avgVsizeCount > 0 &&
	accData->avgVsizeCount != accData->numTasks) {
	mlog("%s: warning: total Vsize is not sum of #tasks values (%lu!=%u)\n",
		__func__, accData->avgVsizeCount, accData->numTasks);
    }

    if (accData->avgRssCount > 0 &&
	    accData->avgRssCount != accData->numTasks) {
	mlog("%s: warning: total RSS is not sum of #tasks values (%lu!=%u)\n",
		__func__, accData->avgRssCount, accData->numTasks);
    }

PACK_RESPONSE:
    packSlurmAccData(data, slurmAccData);

    return accData->numTasks;
}

static void handleStepStat(Slurm_Msg_t *sMsg)
{
    char **ptr = &sMsg->ptr;

    Slurm_Step_Head_t head;
    unpackStepHead(ptr, &head, sMsg->head.version);

    Step_t *step = findStepByStepId(head.jobid, head.stepid);
    if (!step) {
	Step_t s = {
	    .jobid = head.jobid,
	    .stepid = head.stepid };
	flog("%s to signal not found\n", strStepID(&s));
	sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	return;
    }

    /* check permissions */
    if (!(verifyUserId(sMsg->head.uid, step->uid))) {
	mlog("%s: request from invalid user %u\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    PS_SendDB_t *msg = &sMsg->reply;

    /* add return code */
    addUint32ToMsg(SLURM_SUCCESS, msg);
    /* add placeholder for number of tasks */
    uint32_t numTasksUsed = msg->bufUsed;
    addUint32ToMsg(SLURM_SUCCESS, msg);
    /* account data */
    SlurmAccData_t slurmAccData = {
	.psAcct = { .numTasks = 0 },
	.type = step->accType,
	.nodes = step->nodes,
	.nrOfNodes = step->nrOfNodes,
	.loggerTID = step->loggerTID,
	.tasks = &step->tasks,
	.remoteTasks = &step->remoteTasks,
	.childPid = 0 };
    uint32_t numTasks = addSlurmAccData(&slurmAccData, msg);
    /* correct number of tasks */
    *(uint32_t *)(msg->buf + numTasksUsed) = htonl(numTasks);

    /* add step PIDs */
    Slurm_PIDs_t sPID;
    sPID.hostname = getConfValueC(&Config, "SLURM_HOSTNAME");
    psAccountGetPidsByLogger(step->loggerTID, &sPID.pid, &sPID.count);
    packSlurmPIDs(msg, &sPID);

    sendSlurmReply(sMsg, RESPONSE_JOB_STEP_STAT);
}

static void handleStepPids(Slurm_Msg_t *sMsg)
{
    char **ptr = &sMsg->ptr;

    Slurm_Step_Head_t head;
    unpackStepHead(ptr, &head, sMsg->head.version);

    Step_t *step = findStepByStepId(head.jobid, head.stepid);
    if (!step) {
	Step_t s = {
	    .jobid = head.jobid,
	    .stepid = head.stepid };
	flog("%s to signal not found\n", strStepID(&s));
	sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	return;
    }

    /* check permissions */
    if (!(verifyUserId(sMsg->head.uid, step->uid))) {
	mlog("%s: request from invalid user %u\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    /* send step PIDs */
    PS_SendDB_t *msg = &sMsg->reply;
    Slurm_PIDs_t sPID;

    sPID.hostname = getConfValueC(&Config, "SLURM_HOSTNAME");
    psAccountGetPidsByLogger(step->loggerTID, &sPID.pid, &sPID.count);
    packSlurmPIDs(msg, &sPID);

    sendSlurmReply(sMsg, RESPONSE_JOB_STEP_PIDS);
}

static uint32_t getNodeMem(void)
{
    long pages, pageSize;

    if ((pages = sysconf(_SC_PHYS_PAGES))< 0) {
	return 1;
    }
    if ((pageSize = sysconf(_SC_PAGE_SIZE)) < 0) {
	return 1;
    }

    return (uint32_t)((float) pages * (pageSize / 1024 * 1024));
}

static uint32_t getTmpDisk(void)
{
    struct statfs sbuf;
    float pageSize;
    char tmpDef[] = "/tmp";
    char *fs;
    static int report = 1;

    if ((pageSize = sysconf(_SC_PAGE_SIZE)) < 0) {
	mwarn(errno, "%s: getting _SC_PAGE_SIZE failed: ", __func__);
	return 1;
    }

    if (!(fs = getConfValueC(&SlurmConfig, "TmpFS"))) {
	fs = tmpDef;
    }

    if ((statfs(fs, &sbuf)) == -1) {
	if (report) {
	    mwarn(errno, "%s: statfs(%s) failed: ", __func__, fs);
	    report = 0;
	}
	return 1;
    }
    return (uint32_t)((long)sbuf.f_blocks * (pageSize / 1048576.0));
}

/**
 * @brief Handle a daemon status request
 *
 * This RCP is requested from the "scontrol show slurmd"
 * command.
 *
 * @param sMsg The request to handle
 */
static void handleDaemonStatus(Slurm_Msg_t *sMsg)
{
    Resp_Daemon_Status_t stat;
    PS_SendDB_t msg = { .bufUsed = 0, .useFrag = false };

    /* start time */
    stat.startTime = start_time;
    /* last slurmctld msg */
    stat.now = time(NULL);
    /* debug */
    stat.debug = getConfValueI(&Config, "DEBUG_MASK");
    /* cpus */
    stat.cpus = getConfValueI(&Config, "SLURM_CPUS");
    /* boards */
    stat.boards = getConfValueI(&Config, "SLURM_BOARDS");
    /* sockets */
    stat.sockets = getConfValueI(&Config, "SLURM_SOCKETS");
    /* cores */
    stat.coresPerSocket = getConfValueI(&Config, "SLURM_CORES_PER_SOCKET");
    /* threads */
    stat.threadsPerCore = getConfValueI(&Config, "SLURM_THREADS_PER_CORE");
    /* real mem */
    stat.realMem = getNodeMem();
    /* tmp disk */
    stat.tmpDisk = getTmpDisk();
    /* pid */
    stat.pid = getpid();
    /* hostname */
    stat.hostname = getConfValueC(&Config, "SLURM_HOSTNAME");
    /* logfile */
    stat.logfile = "syslog";
    /* step list */
    if (!countSteps()) {
	stat.stepList = strdup("NONE");
    } else {
	stat.stepList = getActiveStepList();
    }
    /* version string */
    snprintf(stat.verStr, sizeof(stat.verStr), "psslurm-%i-p%s", version,
	     slurmProtoStr);

    packRespDaemonStatus(&msg, &stat);
    sendSlurmMsg(sMsg->sock, RESPONSE_SLURMD_STATUS, &msg);

    ufree(stat.stepList);
}

static void handleJobNotify(Slurm_Msg_t *sMsg)
{
    char **ptr = &sMsg->ptr;
    uint32_t jobid, stepid;

    getUint32(ptr, &jobid);
    getUint32(ptr, &stepid);

    Job_t *job = findJobById(jobid);
    Step_t *step = findStepByStepId(jobid, stepid);
    char *msg = getStringM(ptr);

    if (!job && !step) {
	flog("job/step %u.%u to notify not found, msg %s\n", jobid,
	     stepid, msg);
	sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	ufree(msg);
	return;
    }

    /* check permissions */
    if (!(verifyUserId(sMsg->head.uid, job ? job->uid : step->uid))) {
	flog("request from invalid user %u for job %u stepid %u\n",
	     sMsg->head.uid, jobid, step->stepid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	ufree(msg);
	return;
    }

    flog("notify jobid %u stepid %u msg %s\n", jobid, stepid, msg);
    if (job) {
	fwCMD_printMsg(job, NULL, "psslurm: ", strlen("psslurm: "), STDERR, 0);
	fwCMD_printMsg(job, NULL, msg, strlen(msg), STDERR, 0);
	fwCMD_printMsg(job, NULL, "\n", strlen("\n"), STDERR, 0);
    } else {
	fwCMD_printMsg(NULL, step, "psslurm: ", strlen("psslurm: "), STDERR, 0);
	fwCMD_printMsg(NULL, step, msg, strlen(msg), STDERR, 0);
	fwCMD_printMsg(NULL, step, "\n", strlen("\n"), STDERR, 0);
    }

    ufree(msg);
    sendSlurmRC(sMsg, SLURM_SUCCESS);
}

static void handleForwardData(Slurm_Msg_t *sMsg)
{
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sMsg, ESLURM_NOT_SUPPORTED);
}

static void handleLaunchProlog(Slurm_Msg_t *sMsg)
{
    flog("unsupported request, please use the slurmctld prologue\n");
    sendSlurmRC(sMsg, ESLURM_NOT_SUPPORTED);
}

/**
 * @brief Extract jobpack information
 *
 * Extract the jobpack size and hostlist from a jobs environment.
 *
 * @param job The job to extract the information from
 */
static bool extractJobPackInfos(Job_t *job)
{
    char *sPackSize;
    uint32_t i;
    size_t nlSize = 0;

    /* extract pack size */
    if (!(sPackSize = envGet(&job->env, "SLURM_PACK_SIZE"))) {
	return true;
    }
    job->packSize = atoi(sPackSize);

    /* extract pack nodes */
    for (i=0; i<job->packSize; i++) {
	char *next, nodeListName[256];

	snprintf(nodeListName, sizeof(nodeListName),
		 "SLURM_JOB_NODELIST_PACK_GROUP_%u", i);

	if (!(next = envGet(&job->env, nodeListName))) {
	    mlog("%s: %s not found in job environment\n", __func__,
		 nodeListName);
	    ufree(job->packHostlist);
	    job->packHostlist = NULL;
	    job->packSize = 0;
	    return false;
	}
	if (nlSize) str2Buf(",", &job->packHostlist, &nlSize);
	str2Buf(next, &job->packHostlist, &nlSize);
    }

    if (!convHLtoPSnodes(job->packHostlist, getNodeIDbySlurmHost,
			 &job->packNodes, &job->packNrOfNodes)) {
	flog("resolving PS nodeIDs from pack host-list %s failed\n",
	     job->packHostlist);
	return false;
    }

    fdbg(PSSLURM_LOG_PACK, "job %u pack nrOfNodes %u hostlist '%s'\n",
	 job->jobid, job->packNrOfNodes, job->packHostlist);

    return true;
}

/**
 * @brief Print various job information
 *
 * @param job The job to print the infos from
 */
static void printJobLaunchInfos(Job_t *job)
{
    fdbg(PSSLURM_LOG_JOB, "job %u in %s\n", job->jobid,strJobState(job->state));

    /* log cpu options */
    if (job->cpusPerNode && job->cpuCountReps) {
	for (uint32_t i = 0; i < job->cpuGroupCount; i++) {
	    mdbg(PSSLURM_LOG_PART, "cpusPerNode %u cpuCountReps %u\n",
		 job->cpusPerNode[i], job->cpuCountReps[i]);
	}
    }

    /* job env */
    job->env.size = job->env.cnt;
    for (uint32_t i = 0; i < job->env.cnt; i++) {
	mdbg(PSSLURM_LOG_ENV, "%s: env%i: '%s'\n", __func__, i,
	     job->env.vars[i]);
    }

    /* spank env */
    job->spankenv.size = job->spankenv.cnt;
    for (uint32_t i = 0; i < job->spankenv.cnt; i++) {
	mdbg(PSSLURM_LOG_ENV, "%s: spankenv%i: '%s'\n", __func__, i,
	     job->spankenv.vars[i]);
    }
}

static void handleBatchJobLaunch(Slurm_Msg_t *sMsg)
{
    Job_t *job;

    if (pluginShutdown) {
	/* don't accept new jobs if a shutdown is in progress */
	sendSlurmRC(sMsg, SLURM_ERROR);
	return;
    }

    /* memory cleanup  */
    malloc_trim(200);

    /* unpack request */
    if (!(unpackReqBatchJobLaunch(sMsg, &job))) {
	mlog("%s: unpacking job launch request failed\n", __func__);
	sendSlurmRC(sMsg, SLURM_ERROR);
	return;
    }

    /* convert slurm hostlist to PSnodes   */
    if (!convHLtoPSnodes(job->slurmHosts, getNodeIDbySlurmHost,
			 &job->nodes, &job->nrOfNodes)) {
	mlog("%s: resolving PS nodeIDs from %s failed\n", __func__,
	     job->slurmHosts);
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	deleteJob(job->jobid);
	return;
    }

    job->localNodeId = getLocalID(job->nodes, job->nrOfNodes);
    if (job->localNodeId == NO_VAL) {
	flog("could not find my local ID for job %u in %s\n",
	     job->jobid, job->slurmHosts);
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	deleteJob(job->jobid);
	return;
    }

    /* verify job credential */
    if (!(verifyJobData(job))) {
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	deleteJob(job->jobid);
	return;
    }
    job->state = JOB_QUEUED;

    printJobLaunchInfos(job);

    /* set accounting options */
    setAccOpts(job->acctFreq, &job->accType);

    if (!(extractJobPackInfos(job))) {
	mlog("%s: invalid job pack information\n", __func__);
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	return;
    }

    /* set mask of hardware threads to use */
    nodeinfo_t *nodeinfo;
    nodeinfo = getNodeinfo(PSC_getMyID(), job, NULL);
    if (!nodeinfo) {
	mlog("%s: could not extract nodeinfo from credentials\n", __func__);
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	return;
    }
    PSCPU_copy(job->hwthreads, nodeinfo->jobHWthreads);
    ufree(nodeinfo);

    job->hostname = ustrdup(getConfValueC(&Config, "SLURM_HOSTNAME"));

    /* write the jobscript */
    if (!(writeJobscript(job))) {
	/* set myself offline and requeue the job */
	setNodeOffline(&job->env, job->jobid,
			getConfValueC(&Config, "SLURM_HOSTNAME"),
			"psslurm: writing jobscript failed");
	/* need to return success to be able to requeue the job */
	sendSlurmRC(sMsg, SLURM_SUCCESS);
	return;
    }

    mlog("%s: job %u user '%s' np %u nodes '%s' N %u tpp %u pack size %u"
	 " script '%s'\n", __func__, job->jobid, job->username, job->np,
	 job->slurmHosts, job->nrOfNodes, job->tpp, job->packSize,
	 job->jobscript);

    /* sanity check nrOfNodes */
    if (job->nrOfNodes > (uint16_t) PSC_getNrOfNodes()) {
	mlog("%s: invalid nrOfNodes %u known Nodes %u\n", __func__,
	     job->nrOfNodes, PSC_getNrOfNodes());
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	return;
    }

    Alloc_t *alloc = findAlloc(job->jobid);
    if (!alloc) {
	flog("error: no allocation for job %u found\n", job->jobid);
	flog("** ensure slurmctld prologue is setup correctly **\n");
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	deleteJob(job->jobid);
	return;
    }

    /* santy check allocation state */
    if (alloc->state != A_PROLOGUE_FINISH) {
	flog("allocation %u in invalid state %s\n", alloc->id,
	     strAllocState(alloc->state));
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	deleteJob(job->jobid);
	return;
    }

    /* forward job info to other nodes in the job */
    send_PS_JobLaunch(job);

    /* setup job environment */
    initJobEnv(job);

    /* pspelogue already ran parallel prologue, start job */
    flog("start job\n");
    alloc->state = A_RUNNING;
    bool ret = execBatchJob(job);
    fdbg(PSSLURM_LOG_JOB, "job %u in %s\n", job->jobid,strJobState(job->state));

    /* return result to slurmctld */
    if (ret) {
	sendSlurmRC(sMsg, SLURM_SUCCESS);
    } else {
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	deleteJob(job->jobid);
    }
}

static void doTerminateAlloc(Slurm_Msg_t *sMsg, Alloc_t *alloc)
{
    int grace = getConfValueI(&SlurmConfig, "KillWait");
    int maxTermReq = getConfValueI(&Config, "MAX_TERM_REQUESTS");
    int signal = SIGTERM, pcount;

    if (!alloc->firstKillReq) alloc->firstKillReq = time(NULL);
    if (time(NULL) - alloc->firstKillReq > grace + 10) {
	/* grace time is over, use SIGKILL from now on */
	mlog("%s: sending SIGKILL to fowarders of allocation %u\n", __func__,
		alloc->id);
	killForwarderByJobid(alloc->id);
	signal = SIGKILL;
    }

    if (maxTermReq > 0 && alloc->terminate++ >= maxTermReq) {
	/* ensure jobs will not get stuck forever */
	flog("force termination of allocation %u in state %s requests %i\n",
	     alloc->id, strAllocState(alloc->state), alloc->terminate);
	sendEpilogueComplete(alloc->id, 0);
	deleteAlloc(alloc->id);
	sendSlurmRC(sMsg, SLURM_SUCCESS);
	return;
    }

    switch (alloc->state) {
	case A_RUNNING:
	    if ((pcount = signalAlloc(alloc->id, signal, sMsg->head.uid)) > 0) {
		sendSlurmRC(sMsg, SLURM_SUCCESS);
		flog("waiting for %i processes to complete, alloc %u (%i/%i)\n",
		     pcount, alloc->id, alloc->terminate, maxTermReq);
		return;
	    }
	    break;
	case A_EPILOGUE:
	    sendSlurmRC(sMsg, SLURM_SUCCESS);
	    flog("waiting for epilogue to finish, alloc %u (%i/%i)\n",
		 alloc->id, alloc->terminate, maxTermReq);
	    return;
	case A_EPILOGUE_FINISH:
	case A_EXIT:
	    if (!isAllocLeader(alloc)) {
		/* epilogue already executed, we are done */
		sendSlurmRC(sMsg, ESLURMD_KILL_JOB_ALREADY_COMPLETE);
		deleteAlloc(alloc->id);
	    } else {
		/* mother superior will wait for other nodes */
		if (!finalizeEpilogue(alloc)) {
		    flog("waiting for %u sister node(s) to complete epilogue "
			 "(%i/%i)\n", alloc->nrOfNodes - alloc->epilogCnt,
			 alloc->terminate, maxTermReq);
		    send_PS_EpilogueStateReq(alloc);
		}
		sendSlurmRC(sMsg, SLURM_SUCCESS);
	    }
	    return;
	case A_PROLOGUE:
	    sendSlurmRC(sMsg, SLURM_SUCCESS);
	    /* wait for running prologue to finish */
	    flog("waiting for prologue to finish, alloc %u (%i/%i)\n",
		 alloc->id, alloc->terminate, maxTermReq);
	    return;
	case A_PROLOGUE_FINISH:
	case A_INIT:
	    /* local epilogue can start now */
	    break;
	default:
	    flog("invalid allocation state %u\n", alloc->state);
	    deleteAlloc(alloc->id);
	    sendSlurmRC(sMsg, SLURM_SUCCESS);
	    return;
    }

    /* job/steps already finished, start epilogue now */
    sendSlurmRC(sMsg, SLURM_SUCCESS);
    mlog("%s: starting epilogue for allocation %u state %s\n", __func__,
	 alloc->id, strAllocState(alloc->state));
    startEpilogue(alloc);
}

static void handleAbortReq(Slurm_Msg_t *sMsg, uint32_t jobid, uint32_t stepid)
{
    /* send success back to slurmctld */
    sendSlurmRC(sMsg, SLURM_SUCCESS);

    if (stepid != NO_VAL) {
	Step_t *step = findStepByStepId(jobid, stepid);
	if (!step) {
	    Step_t s = {
		.jobid = jobid,
		.stepid = stepid };
	    flog("%s not found\n", strStepID(&s));
	    return;
	}
	signalStep(step, SIGKILL, sMsg->head.uid);
	deleteStep(step->jobid, step->stepid);
	return;
    }

    Job_t *job = findJobById(jobid);

    if (job) {
	if (!job->mother) {
	    signalJob(job, SIGKILL, sMsg->head.uid);
	    send_PS_JobExit(job->jobid, SLURM_BATCH_SCRIPT,
		    job->nrOfNodes, job->nodes);
	}
	deleteJob(jobid);
    } else {
	Alloc_t *alloc = findAlloc(jobid);
	if (alloc && isAllocLeader(alloc)) {
	    signalStepsByJobid(alloc->id, SIGKILL, sMsg->head.uid);
	    send_PS_JobExit(alloc->id, SLURM_BATCH_SCRIPT,
		    alloc->nrOfNodes, alloc->nodes);
	}
	deleteAlloc(jobid);
    }
}

/**
 * @brief Visitor to kill selected steps
 *
 * @param step The next step to visit
 *
 * @param nfo Kill information structure to define which steps
 * should be could
 *
 * @return bool Always return false to walk through the complete
 * step list
 */
static bool killSelectedSteps(Step_t *step, const void *killInfo)
{
    const Kill_Info_t *info = killInfo;
    char buf[512];

    if (!step->fwdata || step->jobid != info->jobid) return false;
    if (info->stepid != NO_VAL && info->stepid != step->stepid) return false;

    if (info->timeout) {
	if (!step->localNodeId) {
	    snprintf(buf, sizeof(buf), "error: *** %s CANCELLED DUE TO"
		" TIME LIMIT ***\n", strStepID(step));
	    fwCMD_printMsg(NULL, step, buf, strlen(buf), STDERR, 0);
	}
	fwCMD_stepTimeout(step->fwdata);
	step->timeout = true;
    } else {
	if (!step->localNodeId) {
	    snprintf(buf, sizeof(buf), "error: *** PREEMPTION for %s ***\n",
		     strStepID(step));
	    fwCMD_printMsg(NULL, step, buf, strlen(buf), STDERR, 0);
	}
    }

    if (step->stepid != NO_VAL) signalStep(step, SIGTERM, info->uid);

    return false;
}

static void handleKillReq(Slurm_Msg_t *sMsg, Alloc_t *alloc, Kill_Info_t *info)
{
    traverseSteps(killSelectedSteps, info);

    /* if we only kill one selected step, we are done */
    if (info->stepid != NO_VAL) {
	sendSlurmRC(sMsg, SLURM_SUCCESS);
	return;
    }

    /* kill request for complete allocation */
    alloc->terminate++;
    if (!alloc->firstKillReq) {
	alloc->firstKillReq = time(NULL);

	Job_t *job = findJobById(info->jobid);
	if (job) {
	    char buf[512];
	    if (info->timeout) {
		job->timeout = 1;
		snprintf(buf, sizeof(buf), "error: *** job %u CANCELLED DUE "
			 "TO TIME LIMIT ***\n", info->jobid);
		fwCMD_printMsg(job, NULL, buf, strlen(buf), STDERR, 0);
	    } else {
		snprintf(buf, sizeof(buf), "error: *** PREEMPTION for "
			 "job %u ***\n", info->jobid);
		fwCMD_printMsg(job, NULL, buf, strlen(buf), STDERR, 0);
	    }
	}
    }

    if (alloc->state == A_RUNNING || alloc->state == A_PROLOGUE_FINISH) {
	int pcount = signalAlloc(alloc->id, SIGTERM, sMsg->head.uid);
	if (pcount > 0) {
	    flog("waiting for %i processes to complete\n", pcount);
	    sendSlurmRC(sMsg, SLURM_SUCCESS);
	    return;
	} else {
	    doTerminateAlloc(sMsg, alloc);
	}
    }
}

static void handleTerminateReq(Slurm_Msg_t *sMsg)
{
    /* unpack request */
    Req_Terminate_Job_t *req = NULL;
    if (!unpackReqTerminate(sMsg, &req)) {
	flog("unpacking terminate request failed\n");
	sendSlurmRC(sMsg, SLURM_ERROR);
	return;
    }

    Step_t s = {
	.jobid = req->jobid,
	.stepid = req->stepid };

    /* check permissions */
    if (sMsg->head.uid != 0 && sMsg->head.uid != slurmUserID) {
	flog("request from invalid user %u for %s\n", sMsg->head.uid,
	     strStepID(&s));
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	goto CLEANUP;
    }

    flog("%s Slurm-state %u uid %u type %s\n", strStepID(&s),
	 req->jobstate, sMsg->head.uid, msgType2String(sMsg->head.type));

    /* restore account freq */
    psAccountSetPoll(confAccPollTime);

    /* remove all unfinished spawn requests */
    PSIDspawn_cleanupBySpawner(PSC_getMyTID());
    cleanupDelayedSpawns(req->jobid, req->stepid);

    /* find the corresponding allocation */
    Alloc_t *alloc = findAlloc(req->jobid);

    if (!alloc) {
	deleteJob(req->jobid);
	clearStepList(req->jobid);
	flog("allocation %s not found\n", strStepID(&s));
	if (sMsg->head.type == REQUEST_TERMINATE_JOB) {
	    sendSlurmRC(sMsg, ESLURMD_KILL_JOB_ALREADY_COMPLETE);
	} else {
	    sendSlurmRC(sMsg, SLURM_SUCCESS);
	    sendEpilogueComplete(req->jobid, SLURM_SUCCESS);
	}
	goto CLEANUP;
    }

    Kill_Info_t info = {
	.jobid = req->jobid,
	.stepid = req->stepid,
	.uid = sMsg->head.uid };

    switch (sMsg->head.type) {
	case REQUEST_KILL_PREEMPTED:
	    info.timeout = 0;
	    handleKillReq(sMsg, alloc, &info);
	    break;
	case REQUEST_KILL_TIMELIMIT:
	    info.timeout = 1;
	    handleKillReq(sMsg, alloc, &info);
	    break;
	case REQUEST_ABORT_JOB:
	    handleAbortReq(sMsg, req->jobid, req->stepid);
	    break;
	case REQUEST_TERMINATE_JOB:
	    doTerminateAlloc(sMsg, alloc);
	    break;
	default:
	    sendSlurmRC(sMsg, ESLURMD_KILL_JOB_ALREADY_COMPLETE);
	    flog("unknown terminate request for %s\n", strStepID(&s));
    }

CLEANUP:
    envDestroy(&req->spankEnv);
    ufree(req->nodes);
    ufree(req);
}

static void handleNetworkCallerID(Slurm_Msg_t *sMsg)
{
    /* IN: */
    /* ip source */
    /* ip dest */
    /* port source */
    /* port dest */
    /* af */

    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sMsg, ESLURM_NOT_SUPPORTED);

    /* OUT: */
    /* job id */
    /* return code */
    /* node name */
}

static void handleRespMessageComposite(Slurm_Msg_t *sMsg)
{
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sMsg, ESLURM_NOT_SUPPORTED);
}

/**
 * @brief Handle response to node registration request
 *
 * Currently used to receive TRes accounting fields from slurmctld.
 *
 * @param sMsg The Slurm message to handle
 */
static void handleRespNodeReg(Slurm_Msg_t *sMsg)
{
    /* don't request the additional info again */
    needNodeRegResp = false;

    if (!unpackExtRespNodeReg(sMsg, &tresDBconfig)) {
	flog("unpack slurmctld node registration response failed\n");
	return;
    }

    for (uint32_t i=0; i<tresDBconfig->count; i++) {
	fdbg(PSSLURM_LOG_ACC, "alloc %zu count %lu id %u name %s type: %s\n",
	     tresDBconfig->entry[i].allocSec, tresDBconfig->entry[i].count,
	     tresDBconfig->entry[i].id, tresDBconfig->entry[i].name,
	     tresDBconfig->entry[i].type);
    }
}

/**
 * @brief Forward a Slurm message using RDP
 *
 * @param sMsg The Slurm message to forward
 *
 * @param fw The forward header of the connection
 *
 * @return Returns true on success otherwise false
 */
static bool slurmTreeForward(Slurm_Msg_t *sMsg, Msg_Forward_t *fw)
{
    PSnodes_ID_t *nodes = NULL;
    uint32_t i, nrOfNodes;
    bool verbose = logger_getMask(psslurmlogger) & PSSLURM_LOG_FWD;
    struct timeval time_start, time_now, time_diff;

    /* no forwarding active for this message? */
    if (!sMsg->head.forward) return true;

    /* convert nodelist to PS nodes */
    if (!convHLtoPSnodes(fw->head.fwNodeList, getNodeIDbySlurmHost,
			 &nodes, &nrOfNodes)) {
	mlog("%s: resolving PS nodeIDs from %s failed\n", __func__,
	     fw->head.fwNodeList);
	return false;
    }

    /* save forward information in connection, has to be
       done *before* sending any messages  */
    fw->nodes = nodes;
    fw->nodesCount = nrOfNodes;
    fw->head.forward = sMsg->head.forward;
    fw->head.returnList = sMsg->head.returnList;
    fw->head.fwResSize = sMsg->head.forward;
    fw->head.fwRes =
	umalloc(sMsg->head.forward * sizeof(Slurm_Forward_Res_t));

    for (i=0; i<sMsg->head.forward; i++) {
	fw->head.fwRes[i].error = SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	fw->head.fwRes[i].type = RESPONSE_FORWARD_FAILED;
	fw->head.fwRes[i].node = -1;
	fw->head.fwRes[i].body.buf = NULL;
	fw->head.fwRes[i].body.used = 0;
    }

    if (verbose) {
	gettimeofday(&time_start, NULL);

	mlog("%s: forward type %s count %u nodelist %s timeout %u "
	     "at %.4f\n", __func__, msgType2String(sMsg->head.type),
	     sMsg->head.forward, fw->head.fwNodeList, fw->head.fwTimeout,
	     time_start.tv_sec + 1e-6 * time_start.tv_usec);
    }

    /* use RDP to send the message to other nodes */
    int ret = forwardSlurmMsg(sMsg, nrOfNodes, nodes);

    if (verbose) {
	gettimeofday(&time_now, NULL);
	timersub(&time_now, &time_start, &time_diff);
	mlog("%s: forward type %s of size %u took %.4f seconds\n", __func__,
	     msgType2String(sMsg->head.type), ret,
	     time_diff.tv_sec + 1e-6 * time_diff.tv_usec);

    }

    return true;
}

void processSlurmMsg(Slurm_Msg_t *sMsg, Msg_Forward_t *fw, Connection_CB_t *cb,
		     void *info)
{
    /* extract Slurm message header */
    if (!unpackSlurmHeader(&sMsg->ptr, &sMsg->head, fw)) {
	sendSlurmRC(sMsg, SLURM_ERROR);
	return;
    }

    /* forward the message using RDP */
    if (fw && !slurmTreeForward(sMsg, fw)) {
	sendSlurmRC(sMsg, SLURM_ERROR);
	return;
    }

    mdbg(PSSLURM_LOG_PROTO, "%s: msg(%i): %s, version %u addr %u.%u.%u.%u"
	    " port %u\n", __func__, sMsg->head.type,
	    msgType2String(sMsg->head.type), sMsg->head.version,
	    (sMsg->head.addr & 0x000000ff),
	    (sMsg->head.addr & 0x0000ff00) >> 8,
	    (sMsg->head.addr & 0x00ff0000) >> 16,
	    (sMsg->head.addr & 0xff000000) >> 24,
	    sMsg->head.port);

    /* verify protocol version */
    if (!testSlurmVersion(sMsg->head.version, sMsg->head.type)) {
	/* try to update the incorrect node protocol in slurmctld */
	static bool nodeReq = false;
	if (!nodeReq) {
	    sendNodeRegStatus(false);
	    nodeReq = true;
	}
	sendSlurmRC(sMsg, SLURM_PROTOCOL_VERSION_ERROR);
	return;
    }

    /* verify munge authentication */
    if (!extractSlurmAuth(sMsg)) {
	flog("extracting slurm auth for msg(%i): %s, version %u failed,"
	     " message dropped\n", sMsg->head.type,
	     msgType2String(sMsg->head.type), sMsg->head.version);
	sendSlurmRC(sMsg, ESLURM_AUTH_CRED_INVALID);
	return;
    }

    /* let the callback handle the message */
    cb(sMsg, info);
}

static void handleNodeRegStat(Slurm_Msg_t *sMsg)
{
    sendPing(sMsg);
    sendNodeRegStatus(false);
}

static void handleInvalid(Slurm_Msg_t *sMsg)
{
    mlog("%s: got invalid %s (%i) request\n", __func__,
	 msgType2String(sMsg->head.type), sMsg->head.type);

    switch (sMsg->head.type) {
	case REQUEST_COMPLETE_BATCH_SCRIPT:
	case REQUEST_STEP_COMPLETE:
	case REQUEST_STEP_COMPLETE_AGGR:
	    sendSlurmRC(sMsg, SLURM_ERROR);
	    break;
	case MESSAGE_COMPOSITE:
	    sendSlurmRC(sMsg, ESLURM_NOT_SUPPORTED);
	    break;
    }
}

typedef struct {
    list_t next;
    int msgType;
    slurmdHandlerFunc_t handler;
} msgHandler_t;

static LIST_HEAD(msgList);

int handleSlurmdMsg(Slurm_Msg_t *sMsg, void *info)
{
    struct timeval time_start, time_now, time_diff;

    list_t *h;
    list_for_each (h, &msgList) {
	msgHandler_t *msgHandler = list_entry(h, msgHandler_t, next);

	if (msgHandler->msgType == sMsg->head.type) {
	    bool measure = measureRPC;
	    if (measure) {
		gettimeofday(&time_start, NULL);
		mlog("%s: exec RPC %s at %.4f\n", __func__,
		     msgType2String(msgHandler->msgType),
		     time_start.tv_sec + 1e-6 * time_start.tv_usec);
	    }

	    if (msgHandler->handler) msgHandler->handler(sMsg);

	    if (measure) {
		gettimeofday(&time_now, NULL);
		timersub(&time_now, &time_start, &time_diff);
		mlog("%s: exec RPC %s took %.4f seconds\n", __func__,
		     msgType2String(msgHandler->msgType),
		     time_diff.tv_sec + 1e-6 * time_diff.tv_usec);
	    }
	    return 0;
	}
    }

    sendSlurmRC(sMsg, SLURM_ERROR);
    return 0;
}

slurmdHandlerFunc_t registerSlurmdMsg(int msgType, slurmdHandlerFunc_t handler)
{
    msgHandler_t *newHandler;
    list_t *h;

    list_for_each(h, &msgList) {
	msgHandler_t *msgHandler = list_entry(h, msgHandler_t, next);

	if (msgHandler->msgType == msgType) {
	    /* found old handler */
	    slurmdHandlerFunc_t oldHandler = msgHandler->handler;
	    msgHandler->handler = handler;
	    return oldHandler;
	}
    }

    newHandler = umalloc(sizeof(*newHandler));

    newHandler->msgType = msgType;
    newHandler->handler = handler;

    list_add_tail(&newHandler->next, &msgList);

    return NULL;
}

slurmdHandlerFunc_t clearSlurmdMsg(int msgType)
{
    list_t *h;
    list_for_each (h, &msgList) {
	msgHandler_t *msgHandler = list_entry(h, msgHandler_t, next);

	if (msgHandler->msgType == msgType) {
	    /* found handler */
	    slurmdHandlerFunc_t handler = msgHandler->handler;
	    list_del(&msgHandler->next);
	    ufree(msgHandler);
	    return handler;
	}
    }

    return NULL;
}

/**
 * @brief Automatically detect the installed Slurm version
 */
static const char *autoDetectSlurmVer(void)
{
    const char *sinfo = getConfValueC(&Config, "SINFO_BINARY");
    char *line = NULL;
    static char autoVer[32];
    size_t len = 0;
    ssize_t read;
    bool ret = false;

    FILE *fp = fopen(sinfo, "r");
    if (!fp) {
	const char *pver = getConfValueC(&Config, "SLURM_PROTO_VERSION");
	if (!strcmp(pver, "auto")) {
	    mwarn(errno, "sinfo binary %s not found:", sinfo);
	    flog("please set SINFO_BINARY to the correct path of sinfo\n");
	}
	return NULL;
    }

    while ((read = getdelim(&line, &len, '\0', fp)) != -1) {
	if (!strncmp(line, "SLURM_VERSION_STRING", 20)) {
	    if (sscanf(line, "SLURM_VERSION_STRING \"%31s\"", autoVer) == 1) {
		char *quote = strchr(autoVer, '"');
		if (quote) quote[0] = '\0';
		ret = true;
		break;
	    } else {
		flog("invalid slurm version string: %s\n", line);
	    }
	}
    }

    free(line);
    fclose(fp);

    return (ret ? autoVer : NULL);
}

bool initSlurmdProto(void)
{
    const char *pver = getConfValueC(&Config, "SLURM_PROTO_VERSION");
    const char *autoVer = autoDetectSlurmVer();

    if (!strcmp(pver, "auto")) {
	if (!autoVer) {
	    flog("Automatic detection of the Slurm protocol failed, "
		 "consider setting SLURM_PROTO_VERSION in psslurm.conf\n");
	    return false;
	}
	slurmVerStr = ustrdup(autoVer);
	pver = autoVer;
    }

    if (!strncmp(pver, "20.11", 5) || !strncmp(pver, "2011", 4)) {
	slurmProto = SLURM_20_11_PROTO_VERSION;
	slurmProtoStr = ustrdup("20.11");
    } else if (!strncmp(pver, "20.02", 5) || !strncmp(pver, "2002", 4)) {
	slurmProto = SLURM_20_02_PROTO_VERSION;
	slurmProtoStr = ustrdup("20.02");
    } else if (!strncmp(pver, "19.05", 5) || !strncmp(pver, "1905", 4)) {
	slurmProto = SLURM_19_05_PROTO_VERSION;
	slurmProtoStr = ustrdup("19.05");
    } else {
	mlog("%s: unsupported Slurm protocol version %s\n", __func__, pver);
	return false;
    }

    if (!slurmVerStr) {
	char buf[64];
	snprintf(buf, sizeof(buf), "%s.0-0", slurmProtoStr);
	slurmVerStr = ustrdup(buf);
    }

    registerSlurmdMsg(REQUEST_LAUNCH_PROLOG, handleLaunchProlog);
    registerSlurmdMsg(REQUEST_BATCH_JOB_LAUNCH, handleBatchJobLaunch);
    registerSlurmdMsg(REQUEST_LAUNCH_TASKS, handleLaunchTasks);
    registerSlurmdMsg(REQUEST_SIGNAL_TASKS, handleSignalTasks);
    registerSlurmdMsg(REQUEST_TERMINATE_TASKS, handleSignalTasks);
    registerSlurmdMsg(REQUEST_REATTACH_TASKS, handleReattachTasks);
    registerSlurmdMsg(REQUEST_KILL_PREEMPTED, handleTerminateReq);
    registerSlurmdMsg(REQUEST_KILL_TIMELIMIT, handleTerminateReq);
    registerSlurmdMsg(REQUEST_ABORT_JOB, handleTerminateReq);
    registerSlurmdMsg(REQUEST_TERMINATE_JOB, handleTerminateReq);
    registerSlurmdMsg(REQUEST_SUSPEND_INT, handleSuspendInt);
    registerSlurmdMsg(REQUEST_COMPLETE_BATCH_SCRIPT, handleInvalid); /* removed in 20.11 */
    registerSlurmdMsg(REQUEST_UPDATE_JOB_TIME, handleUpdateJobTime); /* removed in 20.11 */
    registerSlurmdMsg(REQUEST_SHUTDOWN, handleShutdown);
    registerSlurmdMsg(REQUEST_RECONFIGURE, handleReconfigure);
    registerSlurmdMsg(REQUEST_RECONFIGURE_WITH_CONFIG, handleConfig);
    registerSlurmdMsg(REQUEST_REBOOT_NODES, handleRebootNodes);
    registerSlurmdMsg(REQUEST_NODE_REGISTRATION_STATUS, handleNodeRegStat);
    registerSlurmdMsg(REQUEST_PING, sendPing);
    registerSlurmdMsg(REQUEST_HEALTH_CHECK, handleHealthCheck);
    registerSlurmdMsg(REQUEST_ACCT_GATHER_UPDATE, handleAcctGatherUpdate);
    registerSlurmdMsg(REQUEST_ACCT_GATHER_ENERGY, handleAcctGatherEnergy);
    registerSlurmdMsg(REQUEST_JOB_ID, handleJobId);
    registerSlurmdMsg(REQUEST_FILE_BCAST, handleFileBCast);
    registerSlurmdMsg(REQUEST_STEP_COMPLETE, handleInvalid);
    registerSlurmdMsg(REQUEST_STEP_COMPLETE_AGGR, handleInvalid); /* removed in 20.11 */
    registerSlurmdMsg(REQUEST_JOB_STEP_STAT, handleStepStat);
    registerSlurmdMsg(REQUEST_JOB_STEP_PIDS, handleStepPids);
    registerSlurmdMsg(REQUEST_DAEMON_STATUS, handleDaemonStatus);
    registerSlurmdMsg(REQUEST_JOB_NOTIFY, handleJobNotify);
    registerSlurmdMsg(REQUEST_FORWARD_DATA, handleForwardData);
    registerSlurmdMsg(REQUEST_NETWORK_CALLERID, handleNetworkCallerID);
    registerSlurmdMsg(MESSAGE_COMPOSITE, handleInvalid); /* removed in 20.11 */
    registerSlurmdMsg(RESPONSE_MESSAGE_COMPOSITE, handleRespMessageComposite); /* removed in 20.11 */
    registerSlurmdMsg(RESPONSE_NODE_REGISTRATION, handleRespNodeReg);

    return true;
}

void clearSlurmdProto(void)
{
    list_t *h, *tmp;
    list_for_each_safe (h, tmp, &msgList) {
	msgHandler_t *msgHandler = list_entry(h, msgHandler_t, next);
	list_del(&msgHandler->next);
	ufree(msgHandler);
    }

    ufree(slurmProtoStr);
    ufree(slurmVerStr);

    if (tresDBconfig) {
	for (uint32_t i=0; i<tresDBconfig->count; i++) {
	    ufree(tresDBconfig->entry[i].name);
	    ufree(tresDBconfig->entry[i].type);
	}

	ufree(tresDBconfig->entry);
	ufree(tresDBconfig->nodeName);
	ufree(tresDBconfig);
	tresDBconfig = NULL;
    }
}

void sendNodeRegStatus(bool startup)
{
    PS_SendDB_t msg = { .bufUsed = 0, .useFrag = false };
    struct utsname sys;

    Resp_Node_Reg_Status_t stat;
    memset(&stat, 0, sizeof(stat));

    flog("answer ping request, host %s protoVersion %u\n",
	 getConfValueC(&Config, "SLURM_HOSTNAME"), slurmProto);

    /* current time */
    stat.now = time(NULL);
    /* start time */
    stat.startTime = start_time;
    /* status */
    stat.status = SLURM_SUCCESS;
    /* node_name */
    stat.nodeName = getConfValueC(&Config, "SLURM_HOSTNAME");
    /* architecture */
    uname(&sys);
    stat.arch = sys.machine;
    /* os */
    stat.sysname = sys.sysname;
    /* cpus */
    stat.cpus = getConfValueI(&Config, "SLURM_CPUS");
    /* boards */
    stat.boards = getConfValueI(&Config, "SLURM_BOARDS");
    /* sockets */
    stat.sockets = getConfValueI(&Config, "SLURM_SOCKETS");
    /* cores */
    stat.coresPerSocket = getConfValueI(&Config, "SLURM_CORES_PER_SOCKET");
    /* threads */
    stat.threadsPerCore = getConfValueI(&Config, "SLURM_THREADS_PER_CORE");
    /* real mem */
    stat.realMem = getNodeMem();
    /* tmp disk */
    stat.tmpDisk = getTmpDisk();
    /* sysinfo (uptime, cpu load average, free mem) */
    getSysInfo(&stat.cpuload, &stat.freemem, &stat.uptime);
    /* hash value of the SLURM config file */
    if (getConfValueI(&Config, "DISABLE_CONFIG_HASH") == 1) {
	stat.config = NO_VAL;
    } else {
	stat.config = configHash;
    }

    /* job id infos (count, array (jobid/stepid) */
    getJobInfos(&stat.jobInfoCount, &stat.jobids, &stat.stepids);
    getStepInfos(&stat.jobInfoCount, &stat.jobids, &stat.stepids);
    stat.stepHetComp = umalloc(sizeof(*stat.stepHetComp) * stat.jobInfoCount);
    for (uint32_t i=0; i<stat.jobInfoCount; i++) {
	stat.stepHetComp[i] = NO_VAL;
    }

    /* protocol version */
    stat.protoVersion = version;

    /* version string */
    snprintf(stat.verStr, sizeof(stat.verStr), "psslurm-%i-p%s", version,
	     slurmProtoStr);

    /* flags */
    if (needNodeRegResp) stat.flags |= SLURMD_REG_FLAG_RESP;
    if (startup) stat.flags |= SLURMD_REG_FLAG_STARTUP;

    /* fill energy data */
    psAccountGetEnergy(&stat.eData);

    /* dynamic node feature */
    stat.dynamic = false;
    stat.dynamicFeat = NULL;

    packRespNodeRegStatus(&msg, &stat);

    sendSlurmMsg(SLURMCTLD_SOCK, MESSAGE_NODE_REGISTRATION_STATUS, &msg);

    ufree(stat.jobids);
    ufree(stat.stepids);
    ufree(stat.stepHetComp);
}

int __sendSlurmReply(Slurm_Msg_t *sMsg, slurm_msg_type_t type,
		     const char *func, const int line)
{
    int ret = 1;

    /* save the new message type */
    sMsg->head.type = type;

    if (sMsg->source == -1) {
	if (!sMsg->head.forward) {
	    /* no forwarding active for this message, just send the answer */
	    ret = sendSlurmMsg(sMsg->sock, type, &sMsg->reply);
	} else {
	    /* we are the root of the forwarding tree, so we save the result
	     * and wait for all other forwarded messages to return */
	    __handleFrwrdMsgReply(sMsg, SLURM_SUCCESS, func, line);
	}
    } else {
	/* forwarded message from other psid,
	 * send result back upward the tree */
	ret = send_PS_ForwardRes(sMsg);
    }

    return ret;
}

int __sendSlurmRC(Slurm_Msg_t *sMsg, uint32_t rc, const char *func,
		    const int line)
{
    PS_SendDB_t *body = &sMsg->reply;

    addUint32ToMsg(rc, body);

    int ret = __sendSlurmReply(sMsg, RESPONSE_SLURM_RC, func, line);

    if (!sMsg->head.forward) freeSlurmMsg(sMsg);

    if (ret < 1) {
	mlog("%s: sending rc %u for %s:%u failed\n", __func__, rc, func, line);
    }

    return ret;
}

int getSlurmNodeID(PSnodes_ID_t psNodeID, PSnodes_ID_t *nodes,
		    uint32_t nrOfNodes)
{
    uint32_t i;

    for (i=0; i<nrOfNodes; i++) {
	if (nodes[i] == psNodeID) return i;
    }
    return -1;
}

void sendStepExit(Step_t *step, uint32_t exitStatus)
{
    PS_SendDB_t body = { .bufUsed = 0, .useFrag = false };
    Req_Step_Comp_t req = {
	.jobid = step->jobid,
	.stepid = step->stepid,
	.stepHetComp = step->stepHetComp,
	.firstNode = 0,
	.lastNode = step->nrOfNodes -1,
	.exitStatus = exitStatus,
    };

    packReqStepComplete(&body, &req);

    /* add account data to request */
    pid_t childPid = (step->fwdata) ? step->fwdata->cPid : 1;
    step->accType = (step->leader) ? step->accType : 0;

    SlurmAccData_t slurmAccData = {
	.psAcct = { .numTasks = 0 },
	.type = step->accType,
	.nodes = step->nodes,
	.nrOfNodes = step->nrOfNodes,
	.loggerTID = PSC_getTID(-1, childPid),
	.tasks = &step->tasks,
	.remoteTasks = &step->remoteTasks,
	.childPid = 0 };
    addSlurmAccData(&slurmAccData, &body);

    flog("REQUEST_STEP_COMPLETE for %s to slurmctld: exit %u\n",
	 strStepID(step), exitStatus);

    sendSlurmMsg(SLURMCTLD_SOCK, REQUEST_STEP_COMPLETE, &body);
}

/**
 * @brief Pack the given data and send a MESSAGE_TASK_EXIT RPC
 *
 * @param step The step to send the message for
 *
 * @param exitCode Send all Slurm ranks exited with that code
 *
 * @param count Add the number of ranks processed
 *
 * @param ctlPort sattach control ports to send the message to
 * or NULL to send it to the main srun process
 *
 * @param ctlAddr sattach control addresses to send the message to or
 * NULL to send it to the main srun process
 */
static void doSendTaskExit(Step_t *step, int exitCode, uint32_t *count,
			   int *ctlPort, int *ctlAddr)
{
    Msg_Task_Exit_t msg = { .exitCount = 0 };

    /* exit status */
    msg.exitStatus = step->timeout ? SIGTERM : exitCode;

    /* calculate the number of processes exited with specific exit status */
    list_t *t;
    list_for_each(t, &step->tasks) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	if (task->sentExit || task->childRank < 0) continue;
	if (task->exitCode == exitCode) {
	    msg.exitCount++;
	}
    }

    *count += msg.exitCount;
    if (msg.exitCount < 1) {
	flog("failed to find tasks for exitCode %i\n", exitCode);
	return;
    }
    msg.taskRanks = umalloc(sizeof(*msg.taskRanks) * msg.exitCount);

    /* add all ranks with the specific exit status */
    uint32_t exitCount2 = 0;
    list_for_each(t, &step->tasks) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	if (task->sentExit || task->childRank < 0) continue;
	if (task->exitCode == exitCode) {
	    msg.taskRanks[exitCount2++] = task->childRank;
	    task->sentExit = 1;
	    /*
	    mlog("%s: tasks childRank:%i exit:%i exitCount:%i\n", __func__,
		    task->childRank, task->exitCode, msg.exitCount);
	    */
	}
    }

    if (msg.exitCount != exitCount2) {
	flog("mismatching exit count %i:%i\n", msg.exitCount, exitCount2);
	ufree(msg.taskRanks);
	return;
    }

    /* job/stepid */
    msg.jobid = step->jobid;
    msg.stepid = step->stepid;
    msg.stepHetComp = NO_VAL;

    PS_SendDB_t body = { .bufUsed = 0, .useFrag = false };
    packMsgTaskExit(&body, &msg);
    ufree(msg.taskRanks);

    if (!ctlPort || !ctlAddr) {
	/* send to step associated srun process */
	flog("MESSAGE_TASK_EXIT %u of %u tasks, exit %i to srun\n",
	     msg.exitCount, *count, (step->timeout ? SIGTERM : exitCode));

	srunSendMsg(-1, step, MESSAGE_TASK_EXIT, &body);
    } else {
	/* send to all sattach processes */
	for (int i=0; i<MAX_SATTACH_SOCKETS; i++) {
	    if (ctlPort[i] != -1) {
		int sock = tcpConnectU(ctlAddr[i], ctlPort[i]);
		if (sock <0) {
		    flog("connection to srun %u:%u failed\n",
			 ctlAddr[i], ctlPort[i]);
		} else {
		    srunSendMsg(sock, step, MESSAGE_TASK_EXIT, &body);
		}
	    }
	}
    }
}

/**
 * @brief Add missing tasks
 *
 * Add missing tasks which should be spawned to the task
 * list of a step. This ensures that srun will get an exit
 * notification for all tasks of the step, even if the spawn
 * request from mpiexec never arrived. This might happen e.g.
 * if the executable does not exist.
 *
 * @param step The step to add the tasks for
 */
static void addMissingTasks(Step_t *step)
{
    uint32_t i;
    int32_t rank;

    for (i=0; i<step->globalTaskIdsLen[step->localNodeId]; i++) {
	rank = step->globalTaskIds[step->localNodeId][i];
	if (!findTaskByRank(&step->tasks, rank)) {
	    PS_Tasks_t *task = addTask(&step->tasks, -1, -1, NULL, TG_ANY, rank);
	    task->exitCode = -1;
	}
    }
}

void sendTaskExit(Step_t *step, int *ctlPort, int *ctlAddr)
{
    uint32_t count = 0, taskCount = 0;
    list_t *t;
    int exitCode;

    addMissingTasks(step);

    taskCount = countRegTasks(&step->tasks);

    if (taskCount != step->globalTaskIdsLen[step->localNodeId]) {
	mlog("%s: still missing tasks: %u of %u\n", __func__, taskCount,
	    step->globalTaskIdsLen[step->localNodeId]);
    }

    while (count < taskCount) {
	exitCode = -100;

	list_for_each(t, &step->tasks) {
	    PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	    if (task->childRank < 0) continue;
	    if (!task->sentExit) {
		exitCode = task->exitCode;
		break;
	    }
	}

	if (exitCode == -100) {
	    if (count != taskCount) {
		mlog("%s: failed to find next exit code, count %u"
		     " taskCount %u\n", __func__, count, taskCount);
	    }
	    return;
	}

	doSendTaskExit(step, exitCode, &count, ctlPort, ctlAddr);
    }
}

/**
 * @brief Send a single Task Launch Failed Response
 *
 * Send a Task Launch Failed message to srun. srun will
 * wait until it receives a launch result for every process
 * in the step. It is possible to send results for processes
 * not spawned on the local node.
 *
 * @param step The step which failed to launch
 *
 * @param nodeID The step local nodeID to send the message for
 *
 * @param error Error code
 */
static void doSendLaunchTasksFailed(Step_t *step, uint32_t nodeID,
				    uint32_t error)
{
    Resp_Launch_Tasks_t resp = { .jobid = step->jobid, .stepid = step->stepid };
    PS_SendDB_t body = { .bufUsed = 0, .useFrag = false };

    /* return code */
    resp.returnCode = error;
    /* hostname */
    resp.nodeName = getSlurmHostbyNodeID(step->nodes[nodeID]);
    /* count of PIDs */
    resp.countPIDs = step->globalTaskIdsLen[nodeID];
    /* local PIDs */
    resp.countLocalPIDs = step->globalTaskIdsLen[nodeID];
    resp.localPIDs = step->globalTaskIds[nodeID];
    /* global task IDs */
    resp.countGlobalTIDs = step->globalTaskIdsLen[nodeID];
    resp.globalTIDs = step->globalTaskIds[nodeID];

    packRespLaunchTasks(&body, &resp);

    /* send the message to srun */
    int sock = srunOpenControlConnection(step);
    if (sock != -1) {
	PSCio_setFDblock(sock, true);
	if ((sendSlurmMsg(sock, RESPONSE_LAUNCH_TASKS, &body)) < 1) {
	    flog("send RESPONSE_LAUNCH_TASKS failed %s\n", strStepID(step));
	}
	close(sock);
    } else {
	flog("open control connection failed, %s\n", strStepID(step));
    }

    flog("send RESPONSE_LAUNCH_TASKS %s pids %u for '%s'\n",
	 strStepID(step), step->globalTaskIdsLen[nodeID], resp.nodeName);
}

void sendLaunchTasksFailed(Step_t *step, uint32_t nodeID, uint32_t error)
{
    if (nodeID == (uint32_t) ALL_NODES) {
	uint32_t i;
	for (i=0; i<step->nrOfNodes; i++) {
	    doSendLaunchTasksFailed(step, i, error);
	}
    } else {
	doSendLaunchTasksFailed(step, nodeID, error);
    }
}

void sendTaskPids(Step_t *step)
{
    PS_SendDB_t body = { .bufUsed = 0, .useFrag = false };
    uint32_t countPIDs = 0, countLocalPIDs = 0, countGTIDs = 0;
    int sock = -1;
    list_t *t;
    Resp_Launch_Tasks_t resp;

    resp.jobid = step->jobid;
    resp.stepid = step->stepid;
    resp.stepHetComp = step->stepHetComp;
    resp.returnCode = SLURM_SUCCESS;
    resp.nodeName = getConfValueC(&Config, "SLURM_HOSTNAME");

    /* count of PIDs */
    list_for_each(t, &step->tasks) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	if (task->childRank <0) continue;
	countPIDs++;
    }
    resp.countPIDs = countPIDs;

    /* local PIDs */
    resp.localPIDs = umalloc(sizeof(uint32_t) * countPIDs);

    list_for_each(t, &step->tasks) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	if (task->childRank <0) continue;
	if (countLocalPIDs >=countPIDs) break;
	resp.localPIDs[countLocalPIDs++] = PSC_getPID(task->childTID);
    }
    resp.countLocalPIDs = countLocalPIDs;

    /* global task IDs */
    resp.globalTIDs = umalloc(sizeof(uint32_t) * countPIDs);

    list_for_each(t, &step->tasks) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	if (task->childRank <0) continue;
	if (countGTIDs >=countPIDs) break;
	resp.globalTIDs[countGTIDs++] = task->childRank;
    }
    resp.countGlobalTIDs = countGTIDs;

    if (countPIDs != countGTIDs || countPIDs != countLocalPIDs) {
	mlog("%s: mismatching PID %u and GTID %u count\n", __func__,
	     countPIDs, countGTIDs);
	goto CLEANUP;
    }

    packRespLaunchTasks(&body, &resp);

    /* send the message to srun */
    if ((sock = srunOpenControlConnection(step)) != -1) {
	PSCio_setFDblock(sock, true);
	if (sendSlurmMsg(sock, RESPONSE_LAUNCH_TASKS, &body) < 1) {
	    flog("send RESPONSE_LAUNCH_TASKS failed %s\n", strStepID(step));
	}
	close(sock);
    } else {
	flog("open control connection failed, %s\n", strStepID(step));
    }

    flog("send RESPONSE_LAUNCH_TASKS %s pids %u\n", strStepID(step), countPIDs);

CLEANUP:
    ufree(resp.localPIDs);
    ufree(resp.globalTIDs);
}

void sendJobExit(Job_t *job, uint32_t exit_status)
{
    PS_SendDB_t body = { .bufUsed = 0, .useFrag = false };

    if (job->signaled) exit_status = 0;
    if (job->timeout) exit_status = SIGTERM;

    mlog("%s: REQUEST_COMPLETE_BATCH_SCRIPT: jobid %u exit %u\n",
	 __func__, job->jobid, exit_status);

    /* batch job */
    if (!job->fwdata) {
	/* No data available */
	SlurmAccData_t slurmAccData = {
	    .psAcct = { .numTasks = 0 },
	    .type = 0,
	    .nodes = job->nodes,
	    .nrOfNodes = job->nrOfNodes,
	    .loggerTID = 0,
	    .tasks = NULL,
	    .remoteTasks = NULL,
	    .childPid = 0 };
	addSlurmAccData(&slurmAccData, &body);
    } else {
	SlurmAccData_t slurmAccData = {
	    .psAcct = { .numTasks = 0 },
	    .type = job->accType,
	    .nodes = job->nodes,
	    .nrOfNodes = job->nrOfNodes,
	    .loggerTID = 0,
	    .tasks = &job->tasks,
	    .remoteTasks = NULL,
	    .childPid = job->fwdata->cPid };
	addSlurmAccData(&slurmAccData, &body);
    }
    /* jobid */
    addUint32ToMsg(job->jobid, &body);
    /* jobscript exit code */
    addUint32ToMsg(exit_status, &body);
    /* slurm return code, other than 0 the node goes offline */
    addUint32ToMsg(0, &body);
    /* uid of job */
    addUint32ToMsg((uint32_t) job->uid, &body);
    /* mother superior hostname */
    addStringToMsg(job->hostname, &body);

    sendSlurmMsg(SLURMCTLD_SOCK, REQUEST_COMPLETE_BATCH_SCRIPT, &body);
}

void sendEpilogueComplete(uint32_t jobid, uint32_t rc)
{
    PS_SendDB_t body = { .bufUsed = 0, .useFrag = false };

    mdbg(PSSLURM_LOG_PELOG, "%s: allocation %u to slurmctld\n",
	 __func__, jobid);

    /* jobid */
    addUint32ToMsg(jobid, &body);
    /* return code */
    addUint32ToMsg(rc, &body);
    /* node_name */
    addStringToMsg(getConfValueC(&Config, "SLURM_HOSTNAME"), &body);

    sendSlurmMsg(SLURMCTLD_SOCK, MESSAGE_EPILOG_COMPLETE, &body);
}

void sendDrainNode(const char *nodeList, const char *reason)
{
    PS_SendDB_t msg = { .bufUsed = 0, .useFrag = false };

    Req_Update_Node_t req;
    memset(&req, 0, sizeof(req));

    req.weight = NO_VAL;
    req.nodeList = nodeList;
    req.reason = reason;
    req.reasonUID = getuid();
    req.nodeState = NODE_STATE_DRAIN;

    packUpdateNode(&msg, &req);
    sendSlurmMsg(SLURMCTLD_SOCK, REQUEST_UPDATE_NODE, &msg);
}

void activateConfigCache(char *confDir)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", confDir, "slurm.conf");
    addConfigEntry(&Config, "SLURM_CONF", path);

    snprintf(path, sizeof(path), "%s/%s", confDir, "gres.conf");
    addConfigEntry(&Config, "SLURM_GRES_CONF", path);

    snprintf(path, sizeof(path), "%s/%s", confDir, "plugstack.conf");
    addConfigEntry(&Config, "SLURM_SPANK_CONF", path);
}

/**
 * @brief Handle a Slurm configuration response
 *
 * @param sMsg The Slurm message to handle
 *
 * @param info The action to be taken if the configuration
 * was successful fetched
 *
 * @return Always returns 0
 */
static int handleSlurmConf(Slurm_Msg_t *sMsg, void *info)
{
    uint32_t rc;
    int *action = info;

    switch (sMsg->head.type) {
	case RESPONSE_SLURM_RC:
	    /* return code */
	    getUint32(&sMsg->ptr, &rc);
	    flog("configuration request error: reply %s rc %s sock %i\n",
		 msgType2String(sMsg->head.type), slurmRC2String(rc),
		 sMsg->sock);
	    return 0;
	case RESPONSE_CONFIG:
	    break;
	default:
	    flog("unexpected message type %i\n", sMsg->head.type);
	    return 0;
    }

    /* unpack config response */
    Config_Msg_t *config;
    if (!(unpackConfigMsg(sMsg, &config))) {
	flog("unpacking config response failed\n");
	return 0;
    }

    flog("successfully unpacked config msg\n");

    char *confDir = getConfValueC(&Config, "SLURM_CONF_DIR");
    bool ret = writeSlurmConfigFiles(config, confDir);
    freeSlurmConfigMsg(config);

    if (!ret) {
	flog("failed to write Slurm configuration files to %s\n", confDir);
	return 0;
    }

    /* update configuration file defaults */
    activateConfigCache(confDir);

    switch (*action) {
	case CONF_ACT_STARTUP:
	    /* parse updated configuration files */
	    if (!parseSlurmConfigFiles(&configHash)) {
		flog("fatal: failed to parse configuration\n");
		return 0;
	    }

	    /* finalize the startup of psslurm */
	    finalizeInit();
	    break;
	case CONF_ACT_RELOAD:
	    flog("ignoring configuration reload request\n");
	    break;
	case CONF_ACT_NONE:
	    break;
    }

    return 0;
}

bool sendConfigReq(const char *server, const int action)
{
    PS_SendDB_t body = { .bufUsed = 0, .useFrag = false };
    char *confServer = ustrdup(server);
    char *confPort = strchr(confServer, ':');
    confAction = action;

    if (confPort) {
	confPort[0] = '\0';
	confPort++;
    } else {
	confPort = PSSLURM_SLURMCTLD_PORT;
    }

    /* open connection to slurmcltd */
    int sock = tcpConnect(confServer, confPort);
    if (sock > -1) {
	fdbg(PSSLURM_LOG_IO | PSSLURM_LOG_IO_VERB,
	     "connected to %s socket %i\n", confServer, sock);
    }

    if (sock < 0) {
	flog("open connection to %s:%s failed\n", confServer, confPort);
	goto ERROR;
    }

    if (!registerSlurmSocket(sock, handleSlurmConf, &confAction)) {
	flog("register Slurm socket %i failed\n", sock);
	goto ERROR;
    }

    /* send configuration request message to slurmctld */
    addUint32ToMsg(CONFIG_REQUEST_SLURMD, &body);
    if (sendSlurmMsg(sock, REQUEST_CONFIG, &body) == -1) {
	flog("sending config request message failed\n");
	goto ERROR;
    }

    ufree(confServer);
    return true;

ERROR:
    ufree(confServer);
    return false;
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet:*/
