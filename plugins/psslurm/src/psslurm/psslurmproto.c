/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmproto.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <malloc.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "pscomplist.h"
#include "pscpu.h"
#include "pslog.h"
#include "psserial.h"
#include "timer.h"

#include "psidspawn.h"
#include "psidplugin.h"
#include "psidscripts.h"

#include "pluginconfig.h"
#include "pluginforwarder.h"
#include "pluginhelper.h"
#include "pluginmalloc.h"

#include "psaccounthandles.h"
#include "peloguehandles.h"

#include "slurmcommon.h"
#include "slurmerrno.h"
#include "slurmnode.h"
#include "psslurmalloc.h"
#include "psslurmauth.h"
#include "psslurmbcast.h"
#include "psslurmcomm.h"
#include "psslurmconfig.h"
#include "psslurmenv.h"
#include "psslurmforwarder.h"
#include "psslurmfwcomm.h"
#include "psslurmgres.h"
#include "psslurmio.h"
#include "psslurmlog.h"
#include "psslurmnodeinfo.h"
#include "psslurmpack.h"
#include "psslurmpelogue.h"
#include "psslurmpin.h"
#include "psslurmpscomm.h"
#include "psslurmtasks.h"
#include "psslurm.h"
#include "psslurmaccount.h"

/** Slurm protocol version */
uint32_t slurmProto;

/** Slurm protocol version string */
char *slurmProtoStr = NULL;

/** Slurm version string */
char *slurmVerStr = NULL;

/** Flag to measure Slurm RPC execution times */
bool measureRPC = false;

/** Counter to track how often a Slurm Healthcheck was executed */
uint64_t slurmHCRuns = 0;

/** Flag to request additional info in node registration */
static bool needNodeRegResp = true;

/** PID of a running Slurm health-check script */
static pid_t slurmHCpid = -1;

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
    if (sysinfo(&info) < 0) {
	mwarn(errno, "%s: sysinfo()", __func__);
	*cpuload = *freemem = *uptime = 0;
	return;
    }

    float div = (float) (1 << SI_LOAD_SHIFT);
    *cpuload = (info.loads[0] / div) * 100.0;
    *freemem = (((uint64_t) info.freeram) * info.mem_unit) / (1024*1024);
    *uptime = info.uptime;

    /* pretend the node was booted when psslurm was loaded */
    if (getenv("PSSLURM_FAKE_UPTIME")) {
	static uint32_t realUptime = 0;
	if (!realUptime) realUptime = info.uptime;
	*uptime = info.uptime - realUptime;
    }
}

/**
 * @brief Check if a Slurm message has privileged rights
 *
 * If the user does not have privileged rights, an error messages
 * is logged and the message sender is notified.
 *
 * @param sMsg The message to check
 *
 * @return Returns true on success otherwise false is returned.
 */
bool checkPrivMsg(Slurm_Msg_t *sMsg)
{
    if (sMsg->head.uid != 0 && sMsg->head.uid != slurmUserID) {
	flog("request from invalid user %u message %s\n", sMsg->head.uid,
	     msgType2String(sMsg->head.type));
	sendSlurmRC(sMsg, ESLURM_ACCESS_DENIED);
	return false;
    }
    return true;
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

    for (uint32_t i = 0; i < step->globalTaskIdsLen[step->localNodeId]; i++) {
	if (step->globalTaskIds[step->localNodeId][i] == adjRank) return i;
    }
    return NO_VAL;
}

bool writeJobscript(Job_t *job)
{
    bool ret = false;

    if (!job->jsData) {
	flog("invalid jobscript data for job %u\n", job->jobid);
	return ret;
    }

    /* set jobscript filename */
    char *jobdir = getConfValueC(Config, "DIR_JOB_FILES");
    char buf[PATH_BUFFER_LEN];
    snprintf(buf, sizeof(buf), "%s/%s", jobdir, Job_strID(job->jobid));
    job->jobscript = ustrdup(buf);

    FILE *fp = fopen(job->jobscript, "a");
    if (!fp) {
	mwarn(errno, "%s: open jobscript '%s' for job %u failed", __func__,
	      job->jobscript, job->jobid);
	goto CLEANUP;
    }

    while (fprintf(fp, "%s", job->jsData) != (int)strlen(job->jsData)) {
	if (errno == EINTR) continue;
	mwarn(errno, "%s: writing jobscript '%s' for job %i failed", __func__,
	      job->jobscript, job->jobid);
	goto CLEANUP;
    }

    ret = true;

CLEANUP:
    fclose(fp);
    strShred(job->jsData);
    job->jsData = NULL;

    return ret;
}

static bool needIOReplace(char *ioString, char symbol)
{
    char *ptr = ioString, *next;

    while ((next = strchr(ptr, '%'))) {
	int index = 1;
	while (isdigit(next[index])) index++;

	if (next[index] == symbol) return true;
	ptr = next + index;
    }
    return false;
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

    if (!strncmp("task=", freqString, 5)) {
	freq = atoi(freqString+5);

	if (freq > 0) {
	    mlog("%s: setting acct freq to %i\n", __func__, freq);
	    psAccountSetPoll(PSACCOUNT_OPT_MAIN, freq);
	}
    }

    char *strAcctType = getConfValueC(SlurmConfig, "JobAcctGatherType");
    if (strAcctType) {
	*accType = !strcasecmp(strAcctType, "jobacct_gather/none") ? 0 : 1;
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
    /* env */
    for (uint32_t i = 0; i < step->env.cnt; i++) {
	mdbg(PSSLURM_LOG_ENV, "%s: env%i: '%s'\n", __func__, i,
		step->env.vars[i]);
    }

    /* spank env */
    for (uint32_t i = 0; i < step->spankenv.cnt; i++) {
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
    fdbg(PSSLURM_LOG_JOB, "%s in %s\n", Step_strID(step),
	 Job_strState(step->state));

    /* pinning */
    fdbg(PSSLURM_LOG_PART, "taskDist 0x%x\n", step->taskDist);
    fdbg(PSSLURM_LOG_PART, "cpuBindType 0x%hx, cpuBind '%s'\n",
	 step->cpuBindType, step->cpuBind);
    fdbg(PSSLURM_LOG_PART, "memBindType 0x%hx, memBind '%s'\n",
	 step->memBindType, step->memBind);
}

static bool extractStepPackInfos(Step_t *step)
{
    fdbg(PSSLURM_LOG_PACK, "packNodeOffset %u  packJobid %u packNtasks %u "
	 "packOffset %u packTaskOffset %u packHostlist '%s' packNrOfNodes %u\n",
	 step->packNodeOffset, step->packJobid, step->packNtasks,
	 step->packOffset, step->packTaskOffset, step->packHostlist,
	 step->packNrOfNodes);

    uint32_t nrOfNodes;
    if (!convHLtoPSnodes(step->packHostlist, getNodeIDbySlurmHost,
			 &step->packNodes, &nrOfNodes)) {
	flog("resolving PS nodeIDs from %s failed\n", step->packHostlist);
	return false;
    }

    if (step->packNrOfNodes != nrOfNodes) {
	if ((step->packNrOfNodes == step->nrOfNodes && !step->packNodeOffset) ||
	    step->stepid == SLURM_INTERACTIVE_STEP) {
	    /* correct invalid pack host-list */
	    fdbg(PSSLURM_LOG_PACK, "correct pack nodes using %s\n",
		 step->slurmHosts);
	    ufree(step->packNodes);
	    step->packNodes = umalloc(sizeof(*step->packNodes) * step->nrOfNodes);
	    for (uint32_t i = 0; i < step->nrOfNodes; i++) {
		step->packNodes[i] = step->nodes[i];
	    }
	} else {
	    flog("extracting PS nodes from Slurm pack hostlist %s failed "
		 "(%u:%u)\n", step->packHostlist, step->packNrOfNodes,
		 nrOfNodes);
	    return false;
	}
    }

    for (uint32_t i = 0; i < step->packNrOfNodes; i++) {
	mdbg(PSSLURM_LOG_PACK, "%s: packTaskCount[%u]: %u\n", __func__, i,
		step->packTaskCounts[i]);
    }

    /* extract pack size */
    if (step->stepid == SLURM_INTERACTIVE_STEP) {
	/* overwrite pack size and tasks for interactive steps */
	step->packSize = 1;
	step->packNtasks = 1;
    } else {
	char *sPackSize = envGet(&step->env, "SLURM_PACK_SIZE");
	if (!sPackSize) {
	    mlog("%s: missing SLURM_PACK_SIZE environment\n", __func__);
	    return false;
	}
	step->packSize = atoi(sPackSize);
    }

    /* extract allocation ID */
    char *sPackID = envGet(&step->env, "SLURM_JOB_ID_PACK_GROUP_0");
    if (!sPackID) {
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

void freeRespJobInfo(Resp_Job_Info_t *resp)
{
    if (!resp) return;

    for (uint32_t i = 0; i < resp->numJobs; i++) {
	Slurm_Job_Rec_t *rec = &resp->jobs[i];

	ufree(rec->arrayTaskStr);
	ufree(rec->hetJobIDset);
	ufree(rec->container);
	ufree(rec->cluster);
	ufree(rec->nodes);
	ufree(rec->schedNodes);
	ufree(rec->partition);
	ufree(rec->account);
	ufree(rec->adminComment);
	ufree(rec->network);
	ufree(rec->comment);
	ufree(rec->batchFeat);
	ufree(rec->batchHost);
	ufree(rec->burstBuffer);
	ufree(rec->burstBufferState);
	ufree(rec->systemComment);
	ufree(rec->qos);
	ufree(rec->licenses);
	ufree(rec->stateDesc);
	ufree(rec->resvName);
	ufree(rec->mcsLabel);
	ufree(rec->containerID);
	ufree(rec->failedNode);
	ufree(rec->extra);
    }
    ufree(resp->jobs);
    ufree(resp);
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
static int handleJobInfoResp(Slurm_Msg_t *sMsg, void *info)
{
    Req_Info_t *req = info;

    if (!unpackSlurmMsg(sMsg)) {
	flog("unpacking message %s (%u) for jobid %u failed\n",
	     msgType2String(sMsg->head.type), sMsg->head.version, req->jobid);
	return 0;
    }

    Resp_Job_Info_t *resp = sMsg->unpData;
    if (!resp) {
	flog("invalid response data\n");
	return 0;
    }

    flog("received %u jobs, update %zu\n", resp->numJobs, resp->lastUpdate);

    for (uint32_t i = 0; i < resp->numJobs; i++) {
	Slurm_Job_Rec_t *rec = &(resp->jobs)[i];

	if (req->jobid != rec->jobid) {
	    flog("warning: got non requested job %u, requested job %u\n",
		 rec->jobid, req->jobid);
	}

	flog("jobid %u userid %u state %x state_reason %u exit code %u\n",
	     rec->jobid, rec->userID, rec->jobState & JOB_STATE_BASE,
	     rec->stateReason, rec->exitCode);
    }

    freeRespJobInfo(resp);

    return 0;
}

int requestJobInfo(uint32_t jobid, Connection_CB_t *cb)
{
    Req_Job_Info_Single_t jobInfo = { .jobid = jobid, .flags = 0 };

    jobInfo.flags |= JOB_SHOW_ALL;
    jobInfo.flags |= JOB_SHOW_DETAIL;

    Req_Info_t *req = ucalloc(sizeof(*req));
    req->type = REQUEST_JOB_INFO_SINGLE;
    req->jobid = jobid;
    req->cb = cb ? cb : &handleJobInfoResp;

    flog("job %u\n", jobid);
    return sendSlurmctldReq(req, &jobInfo);
}

uint32_t getLocalID(PSnodes_ID_t *nodes, uint32_t nrOfNodes)
{
    PSnodes_ID_t myID = PSC_getMyID();
    for (uint32_t i = 0; i < nrOfNodes; i++) if (nodes[i] == myID) return i;

    return NO_VAL;
}

static void handleLaunchTasks(Slurm_Msg_t *sMsg)
{
    if (pluginShutdown) {
	/* don't accept new steps if a shutdown is in progress */
	sendSlurmRC(sMsg, SLURM_ERROR);
	return;
    }

    Step_t *step = sMsg->unpData;
    if (!step) {
	flog("unpacking step failed\n");
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	return;
    }

    if (Auth_isDeniedUID(step->uid)) {
	flog("denied UID %u to start step %s\n", sMsg->head.uid,
	     Step_strID(step));
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	goto ERROR;
    }

    /* verify job credential */
    if (!Step_verifyData(step)) {
	flog("invalid data for %s\n", Step_strID(step));
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
    for (uint32_t i = 0; i < step->spankenv.cnt; i++) {
	if (!strncmp("_SLURM_SPANK_OPTION_x11spank_forward_x",
		     step->spankenv.vars[i], 38)) {
	    step->x11forward = true;
	}
    }

    /* set stdout/stderr/stdin options */
    if (!(step->taskFlags & LAUNCH_USER_MANAGED_IO)) {
	setIOoptions(step->stdOut, &step->stdOutOpt, &step->stdOutRank);
	setIOoptions(step->stdErr, &step->stdErrOpt, &step->stdErrRank);
	setIOoptions(step->stdIn, &step->stdInOpt, &step->stdInRank);
    }

    /* convert slurm hostlist to PSnodes */
    uint32_t count;
    if (!convHLtoPSnodes(step->slurmHosts, getNodeIDbySlurmHost,
			 &step->nodes, &count)) {
	flog("resolving PS nodeIDs from %s failed\n", step->slurmHosts);
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	goto ERROR;
    }

    if (count != step->nrOfNodes) {
	flog("mismatching number of nodes %u vs %u for %s\n",
	     count, step->nrOfNodes, Step_strID(step));
	step->nrOfNodes = count;
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	goto ERROR;
    }

    step->localNodeId = getLocalID(step->nodes, step->nrOfNodes);
    if (step->localNodeId == NO_VAL) {
	flog("local node ID %i for %s in %s num nodes %i not found\n",
	     PSC_getMyID(), Step_strID(step), step->slurmHosts, step->nrOfNodes);
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	goto ERROR;
    }

    step->nodeinfos = getStepNodeinfoArray(step);
    if (!step->nodeinfos) {
	flog("failed to fill nodeinfos of step %s\n", Step_strID(step));
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
	 " leader %i exe '%s' packJobid %u hetComp %u\n", Step_strID(step),
	 step->username, step->np, step->slurmHosts, step->nrOfNodes, step->tpp,
	 step->packSize, step->leader, step->argv[0],
	 step->packJobid == NO_VAL ? 0 : step->packJobid,
	 step->stepHetComp == NO_VAL ? 0 : step->stepHetComp);

    /* ensure an allocation exists for the new step */
    Alloc_t *alloc = Alloc_find(step->jobid);
    if (!alloc) {
	flog("error: no allocation for jobid %u found\n", step->jobid);
	flog("** ensure slurmctld prologue is setup correctly **\n");
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	goto ERROR;
    }
    alloc->verified = true;
    /* ensure a proper cleanup is executed on termination */
    alloc->state = A_RUNNING;

    /* set slots for the step (and number of hardware threads) */
    if (!setStepSlots(step)) {
	flog("setting hardware threads for %s failed\n", Step_strID(step));
	char msg[40];
	snprintf(msg, sizeof(msg), "Fatal error in pinning for %s",
		 Step_strID(step));
	fwCMD_printMsg(NULL, step, msg, strlen(msg), STDERR, -1);
	/* exit but not before printing messages to the user */
	step->termAfterFWmsg = ESLURMD_INVALID_JOB_CREDENTIAL;
    }

    /* sanity check nrOfNodes */
    if (step->nrOfNodes > (uint16_t) PSC_getNrOfNodes()) {
	flog("invalid nrOfNodes %u known Nodes %u for %s\n",
	     step->nrOfNodes, PSC_getNrOfNodes(), Step_strID(step));
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	goto ERROR;
    }

    if (step->leader) {
	/* mother superior (pack leader) */

	step->srunControlMsg.sock = sMsg->sock;
	step->srunControlMsg.head.forward = sMsg->head.forward;
	step->srunControlMsg.recvTime = sMsg->recvTime;
	step->srunControlMsg.head.uid = step->uid;

	/* start mpiexec to spawn the parallel processes,
	 * intercept createPart call to overwrite the nodelist */
	step->state = JOB_PRESTART;
	fdbg(PSSLURM_LOG_JOB, "%s in %s\n", Step_strID(step),
	     Job_strState(step->state));
	/* non pack jobs can be started right away.
	 * However for pack jobs the pack leader has to wait for
	 * the pack follower to send hw threads */
	if (step->packJobid == NO_VAL) {
	    if (!execStepLeader(step)) {
		sendSlurmRC(sMsg, ESLURMD_FORK_FAILED);
		goto ERROR;
	    }
	} else {
	    /* Check for cached hw threads */
	    handleCachedMsg(step);
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
	/* if this is the pack leader execStepLeader() might be called
	 * in the handler */
	if (send_PS_PackInfo(step) == -1) {
	    sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	    goto ERROR;
	}
    }

    return;

ERROR:
    if (step) Step_destroy(step);
}

static int doSignalTasks(Req_Signal_Tasks_t *req)
{
    if (req->flags & KILL_FULL_JOB) {
	/* send signal to complete job including all steps */
	mlog("%s: sending all processes of job %u signal %u\n", __func__,
	     req->jobid, req->signal);
	Job_signalJS(req->jobid, req->signal, req->uid);
	Step_signalByJobid(req->jobid, req->signal, req->uid);
    } else if (req->flags & KILL_STEPS_ONLY) {
	/* send signal to all steps excluding the jobscript */
	flog("send steps %u:%u (stepHetComp %u) signal %u\n",
	     req->jobid, req->stepid, req->stepHetComp, req->signal);
	Step_signalByJobid(req->jobid, req->signal, req->uid);
    } else {
	if (req->stepid == SLURM_BATCH_SCRIPT) {
	    /* signal jobscript only, not all corresponding steps */
	    return Job_signalJS(req->jobid, req->signal, req->uid);
	} else {
	    /* signal a single step */
	    flog("send step %u:%u (stepHetComp %u) signal %u\n",
		 req->jobid, req->stepid, req->stepHetComp, req->signal);
	    Step_t *step = Step_findByStepId(req->jobid, req->stepid);
	    if (step) return Step_signal(step, req->signal, req->uid);
	}

	/* we only return an error if we signal a specific jobscript/step */
	return 0;
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

    int grace = getConfValueI(SlurmConfig, "KillWait");
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
    Req_Signal_Tasks_t *req = sMsg->unpData;
    if (!req) {
	flog("unpacking request signal tasks failed\n");
	sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
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

static void sendReattachFail(Slurm_Msg_t *sMsg, uint32_t rc)
{
    PS_SendDB_t *reply = &sMsg->reply;

    /* hostname */
    addStringToMsg(getConfValueC(Config, "SLURM_HOSTNAME"), reply);
    /* return code */
    addUint32ToMsg(rc, reply);
    /* number of tasks */
    addUint32ToMsg(0, reply);
    /* gtids */
    addUint32ToMsg(0, reply);
    /* local pids */
    addUint32ToMsg(0, reply);
    /* no executable names */

    sendSlurmReply(sMsg, RESPONSE_REATTACH_TASKS);
}

static void sendReattchReply(Step_t *step, Slurm_Msg_t *sMsg)
{
    PS_SendDB_t *reply = &sMsg->reply;

    /* hostname */
    addStringToMsg(getConfValueC(Config, "SLURM_HOSTNAME"), reply);
    /* return code */
    addUint32ToMsg(SLURM_SUCCESS, reply);
    /* number of tasks */
    uint32_t numTasks = step->globalTaskIdsLen[step->localNodeId];
    addUint32ToMsg(numTasks, reply);
    /* gtids */
    addUint32ArrayToMsg(step->globalTaskIds[step->localNodeId],
			numTasks, reply);
    /* local pids */
    uint32_t countPos = reply->bufUsed;
    addUint32ToMsg(0, reply); /* placeholder -- to be filled later */

    uint32_t countPIDS = 0;
    list_t *t;
    list_for_each(t, &step->tasks) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	if (task->globalRank >= 0) {
	    countPIDS++;
	    addUint32ToMsg(PSC_getPID(task->childTID), reply);
	}
    }
    *(uint32_t *) (reply->buf + countPos) = htonl(countPIDS);

    /* executable names */
    for (uint32_t i = 0; i < numTasks; i++) {
	addStringToMsg(step->argv[0], reply);
    }

    sendSlurmReply(sMsg, RESPONSE_REATTACH_TASKS);
}

static void handleReattachTasks(Slurm_Msg_t *sMsg)
{
    Req_Reattach_Tasks_t *req = sMsg->unpData;
    if (!req) {
	flog("unpacking request reattach tasks failed\n");
	sendReattachFail(sMsg, ESLURM_INVALID_JOB_ID);
	return;
    }

    Step_t *step = Step_findByStepId(req->jobid, req->stepid);
    if (!step) {
	Step_t s = {
	    .jobid = req->jobid,
	    .stepid = req->stepid };
	flog("%s to reattach not found\n", Step_strID(&s));
	sendReattachFail(sMsg, ESLURM_INVALID_JOB_ID);
	/* check permissions */
    } else if (!verifyUserId(sMsg->head.uid, step->uid)) {
	flog("request from invalid user %u %s\n", sMsg->head.uid,
	     Step_strID(step));
	sendReattachFail(sMsg, ESLURM_USER_ID_MISSING);
    } else if (!step->fwdata) {
	/* no forwarder to attach to */
	flog("forwarder for %s to reattach not found\n", Step_strID(step));
	sendReattachFail(sMsg, ESLURM_INVALID_JOB_ID);
    } else if (req->numCtlPorts < 1) {
	flog("invalid request, no control ports for %s\n", Step_strID(step));
	sendReattachFail(sMsg, ESLURM_PORTS_INVALID);
    } else if (req->numIOports < 1) {
	flog("invalid request, no I/O ports for %s\n", Step_strID(step));
	sendReattachFail(sMsg, ESLURM_PORTS_INVALID);
    } else if (!req->cred) {
	flog("invalid credential for %s\n", Step_strID(step));
	sendReattachFail(sMsg, ESLURM_INVALID_JOB_CREDENTIAL);
    } else if (strlen(req->cred->sig) + 1 < SLURM_IO_KEY_SIZE) {
	flog("invalid I/O key size %zu for %s\n", strlen(req->cred->sig) + 1,
	     Step_strID(step));
	sendReattachFail(sMsg, ESLURM_INVALID_JOB_CREDENTIAL);
    } else {
	/* send message to forwarder */
	fwCMD_reattachTasks(step->fwdata, sMsg->head.addr,
			    req->ioPorts[step->localNodeId % req->numIOports],
			    req->ctlPorts[step->localNodeId % req->numCtlPorts],
			    req->cred->sig);
	sendReattchReply(step, sMsg);
    }

    if (req) {
	freeJobCred(req->cred);
	ufree(req->ioPorts);
	ufree(req->ctlPorts);
	ufree(req);
    }
}

static void handleSuspendInt(Slurm_Msg_t *sMsg)
{
    Req_Suspend_Int_t *req = sMsg->unpData;
    if (!req) {
	flog("unpacking request suspend int failed\n");
	sendSlurmRC(sMsg, ESLURM_NOT_SUPPORTED);
	return;
    }

    switch (req->op) {
	case SUSPEND_JOB:
	    flog("suspend job %u\n",req->jobid);
	    Step_signalByJobid(req->jobid, SIGSTOP, sMsg->head.uid);
	    break;
	case RESUME_JOB:
	    flog("resume job %u\n",req->jobid);
	    Step_signalByJobid(req->jobid, SIGCONT, sMsg->head.uid);
	    break;
	default:
	    flog("unknown suspend_int operation %u\n", req->op);
	    sendSlurmRC(sMsg, ESLURM_NOT_SUPPORTED);
	    ufree(req);
	    return;
    }

    ufree(req);
    sendSlurmRC(sMsg, SLURM_SUCCESS);
}

static void handleUpdateJobTime(Slurm_Msg_t *sMsg)
{
    /* does nothing, since timelimit is not used in slurmd */

    if (!checkPrivMsg(sMsg)) return;

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
    if (!checkPrivMsg(sMsg)) return;

    sendSlurmRC(sMsg, SLURM_SUCCESS);

    mlog("%s: shutdown on request from uid %i\n", __func__, sMsg->head.uid);
    PSIDplugin_finalize("psslurm");
}

static void handleReconfigure(Slurm_Msg_t *sMsg)
{
    /* check permissions */
    if (!checkPrivMsg(sMsg)) return;

    /* activate the new configuration */
    updateSlurmConf();

    /* send new configuration hash to slurmctld */
    sendNodeRegStatus(false);

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
    /* new configuration message since 21.08 */
    for (uint32_t i = 0; i < config->numFiles; i++) {
	ufree(config->files[i].name);
	ufree(config->files[i].data);
    }
    ufree(config->files);

    /* old configuration message */
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
	mwarn(errno, "%s: fopen(%s)", __func__, path);
	return false;
    }

    errno = 0;
    fwrite(data, strlen(data), 1, fp);
    if (errno) {
	mwarn(errno, "%s: fwrite() to '%s'", __func__, path);
	return false;
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

    if (config->numFiles) {
	/* new Slurm configuration format since Slurm 21.08 */
	for (uint32_t i = 0; i < config->numFiles; i++) {
	    Config_File_t *file = &config->files[i];

	    if (!file->create) {
		/* file should be deleted if possbile */
		char path[1024];

		snprintf(path, sizeof(path), "%s/%s", confDir, file->name);
		unlink(path);
		continue;
	    }

	    if (!writeFile(file->name, confDir, file->data)) {
		return false;
	    }
	}
    } else {
	/* old format to write various Slurm configuration files */
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
    }

    /* link configuration to Slurm /run-directory so Slurm commands (e.g.
     * scontrol, sbatch) can use them */
    char *runDir = getConfValueC(Config, "SLURM_RUN_DIR");
    if (mkdir(runDir, 0755) == -1) {
	if (errno != EEXIST) {
	    mwarn(errno, "%s: mkdir(%s)", __func__, runDir);
	    return false;
	}
    }

    char destLink[1024];
    snprintf(destLink, sizeof(destLink), "%s/conf", runDir);

    unlink(destLink);
    if (symlink(confDir, destLink) == -1) {
	mwarn(errno, "%s: symlink(%s, %s)", __func__, confDir, destLink);
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
    Config_Msg_t *config = sMsg->unpData;
    if (!config) {
	flog("unpacking new configuration failed\n");
	return;
    }

    /* check permissions */
    if (!checkPrivMsg(sMsg)) {
	freeSlurmConfigMsg(config);
	return;
    }

    /* update all configuration files */
    char *confDir = getConfValueC(Config, "SLURM_CONF_CACHE");
    flog("updating Slurm configuration files in %s\n", confDir);

    bool ret = writeSlurmConfigFiles(config, confDir);
    freeSlurmConfigMsg(config);

    if (!ret) {
	flog("failed to write Slurm configuration files to %s\n", confDir);
	return;
    }

    /* activate the new configuration */
    updateSlurmConf();

    /* send new configuration hash to slurmctld */
    sendNodeRegStatus(false);

    /* slurmctld does not require an answer for this RPC and silently ignores
     * any responses */
}

/**
 * @brief Callback of the reboot program
 *
 * @param exit Reboot program's exit-status
 *
 * @param tmdOut Ignored flag of timeout
 *
 * @param iofd File descriptor providing reboot program's output
 *
 * @param info Extra information pointing to @ref StrBuffer_t
 *
 * @return No return value
 */
static void cbRebootProgram(int exit, bool tmdOut, int iofd, void *info)
{
    char errMsg[1024];
    size_t errLen = 0;
    getScriptCBdata(iofd, errMsg, sizeof(errMsg), &errLen);

    StrBuffer_t *cmdline = info;
    flog("'%s' returned exit status %i\n",
	 (cmdline ? cmdline->buf : "reboot program"), exit);

    if (errMsg[0] != '\0') flog("reboot error message: %s\n", errMsg);

    if (cmdline) freeStrBuf(cmdline);

    char *shutdown = getConfValueC(Config, "SLURMD_SHUTDOWN_ON_REBOOT");
    if (shutdown) {
	flog("unloading psslurm after reboot program\n");
	PSIDplugin_finalize("psslurm");
    }
}

static void freeReqRebootNodes(Req_Reboot_Nodes_t *req)
{
    ufree(req->nodeList);
    ufree(req->reason);
    ufree(req->features);
    ufree(req);
}

static void handleRebootNodes(Slurm_Msg_t *sMsg)
{
    Req_Reboot_Nodes_t *req = sMsg->unpData;
    if (!req) {
	flog("unpacking node reboot request failed\n");
	return;
    }

    char *prog = getConfValueC(SlurmConfig, "RebootProgram");
    if (!prog) {
	flog("error: RebootProgram is not set in slurm.conf\n");

    } else if (checkPrivMsg(sMsg)) {
	/* try to execute reboot program */
	StrBuffer_t cmdline = { .buf = NULL };
	addStrBuf(trim(prog), &cmdline);

	if (access(cmdline.buf, R_OK | X_OK) < 0) {
	    flog("invalid permissions for reboot program %s\n", cmdline.buf);
	} else {
	    if (req->features && req->features[0]) {
		addStrBuf(" ", &cmdline);
		addStrBuf(req->features, &cmdline);
	    }
	    flog("calling reboot program '%s'\n", cmdline.buf);
	    PSID_execScript(cmdline.buf, NULL, &cbRebootProgram, NULL,&cmdline);
	}
	freeStrBuf(&cmdline);
    }

    freeReqRebootNodes(req);

    /* slurmctld does not require an answer for this RPC and silently ignores
     * any responses */
}

uint64_t getSlurmHCRuns(void)
{
    return slurmHCRuns;
}

/**
 * @brief Callback of a health-check script
 *
 * @param exit health-check script's exit-status
 *
 * @param tmdOut flag of timeout
 *
 * @param iofd File descriptor providing health-check script's output
 *
 * @param info No extra info
 */
static void cbHealthcheck(int exit, bool tmdOut, int iofd, void *info)
{
    char errMsg[1024];
    size_t errLen = 0;
    getScriptCBdata(iofd, errMsg, sizeof(errMsg), &errLen);
    slurmHCpid = -1;

    if (errMsg[0] != '\0') flog("health-check script message: %s\n", errMsg);

    char *script = getConfValueC(SlurmConfig, "HealthCheckProgram");
    if (tmdOut) {
	int seconds = getConfValueU(Config, "SLURM_HC_TIMEOUT");
	flog("'%s' was terminated because it exceeded the time-limit"
	     " of %u seconds\n", script, seconds);
    } else if (exit != 0) {
	flog("'%s' returned exit status %i\n", script, exit);
    } else {
	if (isInit) return;
	/* initialize Slurm options and register node to slurmctld */
	if (initSlurmOpt()) {
	    isInit = true;

	    mlog("(%i) successfully started, protocol '%s (%i)'\n", version,
		 slurmProtoStr, slurmProto);
	    return;
	} else {
	    /* psslurm failed initialize, unload */
	    flog("initialize Slurm communication failed\n");
	}
    }

    flog("fatal: healthcheck failed, psslurm will unload itself\n");
    PSIDplugin_finalize("psslurm");
}

static void prepHCenv(void *info)
{
    setenv("SLURMD_NODENAME", getConfValueC(Config, "SLURM_HOSTNAME"), 1);
}

bool stopHealthCheck(int signal)
{
    static pid_t lastpid = -1;

    if (slurmHCpid != -1) {
	if (lastpid != slurmHCpid) {
	    flog("stopping Slurm health-check script %u\n", slurmHCpid);
	    killChild(-slurmHCpid, signal, 0);
	}
	lastpid = slurmHCpid;
	return false;
    }
    return true;
}

bool runHealthCheck(void)
{
    /* run health-check script */
    char *script = getConfValueC(SlurmConfig, "HealthCheckProgram");
    if (!script) return true;

    if (access(script, R_OK | X_OK) < 0) {
	mwarn(errno, "%s: invalid health-check script %s", __func__, script);
	return false;
    }
    if (slurmHCpid != -1) {
	flog("error: another Slurm health-check script with PID %u is"
		" already running\n", slurmHCpid);
	return false;
    }

    int seconds = getConfValueU(Config, "SLURM_HC_TIMEOUT");
    struct timeval timeout = {seconds, 0};
    slurmHCpid = PSID_execScript(script, &prepHCenv, &cbHealthcheck,
	    &timeout, NULL);
    if (slurmHCpid == -1) {
	flog("error spawning health-check script %s\n", script);
	return false;
    }
    flog("(#%zu) execute health-check script %s pid %u\n", ++slurmHCRuns,
	    script, slurmHCpid);

    return true;
}

/**
 * @brief Handle a REQUEST_HEALTH_CHECK RPC
 *
 * @param sMsg The message holding the RPC to handle
 */
static void handleHealthCheck(Slurm_Msg_t *sMsg)
{
    /* check permissions */
    if (!checkPrivMsg(sMsg)) return;

    sendSlurmRC(sMsg, SLURM_SUCCESS);

    runHealthCheck();
}

static void handleAcctGatherUpdate(Slurm_Msg_t *sMsg)
{
    PS_SendDB_t *msg = &sMsg->reply;

    /* check permissions */
    if (!checkPrivMsg(sMsg)) return;

    /* pack energy data */
    psAccountInfo_t info;
    psAccountGetLocalInfo(&info);

    /* we need at least 1 sensor to prevent segfaults in slurmctld */
    packEnergySensor(msg, &info.energy);

    sendSlurmReply(sMsg, RESPONSE_ACCT_GATHER_UPDATE);
}

static void handleAcctGatherEnergy(Slurm_Msg_t *sMsg)
{
    PS_SendDB_t *msg = &sMsg->reply;

    /* check permissions */
    if (!checkPrivMsg(sMsg)) return;

    /* pack energy data */
    psAccountInfo_t info;
    psAccountGetLocalInfo(&info);

    /* we need at least 1 sensor to prevent segfaults in slurmctld */
    packEnergySensor(msg, &info.energy);

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

    Step_t *step = Step_findByPsidTask(pid);
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
    BCast_t *bcast = sMsg->unpData;
    if (!bcast) {
	flog("unpacking file bcast request failed\n");
	sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	return;
    }
    bcast->msg.sock = sMsg->sock;
    /* set correct uid for response message */
    bcast->msg.head.uid = sMsg->head.uid;

    /* unpack credential */
    if (!BCast_extractCred(sMsg, bcast)) {
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
    Job_t *job = Job_findById(bcast->jobid);
    if (!job) {
	Alloc_t *alloc = Alloc_find(bcast->jobid);
	if (!alloc) {
	    mlog("%s: job %u not found\n", __func__, bcast->jobid);
	    sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	    goto CLEANUP;
	}
	bcast->uid = alloc->uid;
	bcast->gid = alloc->gid;
	bcast->env = &alloc->env;
	ufree(bcast->username);
	bcast->username = ustrdup(alloc->username);
	Step_t *step = Step_findByJobid(bcast->jobid);
	if (step) PSCPU_copy(bcast->hwthreads,
			     step->nodeinfos[step->localNodeId].stepHWthreads);
    } else {
	bcast->uid = job->uid;
	bcast->gid = job->gid;
	bcast->env = &job->env;
	PSCPU_copy(bcast->hwthreads, job->hwthreads);
	ufree(bcast->username);
	bcast->username = ustrdup(job->username);
    }

    if (Auth_isDeniedUID(bcast->uid)) {
	flog("denied UID %u to start bcast\n", sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	goto CLEANUP;
    }

    fdbg(PSSLURM_LOG_PROTO, "jobid %u blockNum %u lastBlock %u force %u"
	 " modes %u, user '%s' uid %u gid %u fileName '%s' blockLen %u\n",
	 bcast->jobid, bcast->blockNumber,
	 (bcast->flags & BCAST_LAST_BLOCK) ? 1 : 0,
	 (bcast->flags & BCAST_FORCE) ? 1 : 0,
	 bcast->modes, bcast->username, bcast->uid, bcast->gid,
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
    BCast_delete(bcast);
}

static int addSlurmAccData(SlurmAccData_t *slurmAccData)
{
    AccountDataExt_t *accData = &slurmAccData->psAcct;

    /* no accounting data */
    if (!slurmAccData->type) return accData->numTasks;

    bool res;
    if (slurmAccData->childPid) {
	res = psAccountGetDataByJob(slurmAccData->childPid, accData);
    } else {
	res = psAccountGetDataByLogger(slurmAccData->rootTID, accData);
    }

    slurmAccData->empty = !res;

    if (!res) {
	/* getting account data failed */
	flog("getting account data for pid %u / root %s failed\n",
	     slurmAccData->childPid, PSC_printTID(slurmAccData->rootTID));
	return accData->numTasks;
    }

    uint64_t avgVsize = accData->avgVsizeCount ?
			accData->avgVsizeTotal / accData->avgVsizeCount : 0;
    uint64_t avgRss = accData->avgRssCount ?
		      accData->avgRssTotal / accData->avgRssCount : 0;

    flog("adding account data: maxVsize %zu maxRss %zu pageSize %lu "
	 "u_sec %lu u_usec %lu s_sec %lu s_usec %lu num_tasks %u avgVsize %lu"
	 " avgRss %lu avg cpufreq %.2fG\n", accData->maxVsize,
	 accData->maxRss, accData->pageSize, accData->rusage.ru_utime.tv_sec,
	 accData->rusage.ru_utime.tv_usec, accData->rusage.ru_stime.tv_sec,
	 accData->rusage.ru_stime.tv_usec, accData->numTasks, avgVsize, avgRss,
	 ((double) accData->cpuFreq / (double) accData->numTasks)
	 / (double) 1048576);

    fdbg(PSSLURM_LOG_ACC, "nodes maxVsize %u maxRss %u maxPages %u "
	 "minCpu %u maxDiskRead %u maxDiskWrite %u minEnergy %u maxEnergy %u "
	 "minPower %u maxPower %u\n",
	 PSC_getID(accData->taskIds[ACCID_MAX_VSIZE]),
	 PSC_getID(accData->taskIds[ACCID_MAX_RSS]),
	 PSC_getID(accData->taskIds[ACCID_MAX_PAGES]),
	 PSC_getID(accData->taskIds[ACCID_MIN_CPU]),
	 PSC_getID(accData->taskIds[ACCID_MAX_DISKREAD]),
	 PSC_getID(accData->taskIds[ACCID_MAX_DISKWRITE]),
	 PSC_getID(accData->taskIds[ACCID_MIN_ENERGY]),
	 PSC_getID(accData->taskIds[ACCID_MAX_ENERGY]),
	 PSC_getID(accData->taskIds[ACCID_MIN_POWER]),
	 PSC_getID(accData->taskIds[ACCID_MAX_POWER]));

    if (accData->energyMax || accData->powerMax) {
	fdbg(PSSLURM_LOG_ACC, "energyTot %zu energyMin %zu energyMax %zu"
	     " powerAvg %zu powerMin %zu powerMax %zu", accData->energyTot,
	     accData->energyMin, accData->energyMax, accData->powerAvg,
	     accData->powerMin, accData->powerMax);
    }

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

    return accData->numTasks;
}

static void handleStepStat(Slurm_Msg_t *sMsg)
{
    Slurm_Step_Head_t *head = sMsg->unpData;
    if (!head) {
	flog("unpacking step stat request failed\n");
	sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	return;
    }

    Step_t *step = Step_findByStepId(head->jobid, head->stepid);
    if (!step) {
	Step_t s = {
	    .jobid = head->jobid,
	    .stepid = head->stepid };
	flog("%s to signal not found\n", Step_strID(&s));
	sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	ufree(head);
	return;
    }
    ufree(head);

    /* check permissions */
    if (!verifyUserId(sMsg->head.uid, step->uid)) {
	flog("request from invalid user %u\n", sMsg->head.uid);
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
    PStask_ID_t rootTID =
	(step->spawned && step->fwdata) ? step->fwdata->tid : step->loggerTID;
    SlurmAccData_t slurmAccData = {
	.psAcct = { .numTasks = 0 },
	.type = step->accType,
	.nodes = step->nodes,
	.nrOfNodes = step->nrOfNodes,
	.rootTID = rootTID,
	.tasks = &step->tasks,
	.remoteTasks = &step->remoteTasks,
	.childPid = 0,
	.localNodeId = step->localNodeId,
	.iBase = &step->acctBase };
    uint32_t numTasks = addSlurmAccData(&slurmAccData);
    packSlurmAccData(msg, &slurmAccData);
    /* correct number of tasks */
    *(uint32_t *)(msg->buf + numTasksUsed) = htonl(numTasks);

    /* add step PIDs */
    Slurm_PIDs_t sPID;
    sPID.hostname = getConfValueC(Config, "SLURM_HOSTNAME");
    psAccountGetPidsByLogger(rootTID, &sPID.pid, &sPID.count);
    packSlurmPIDs(msg, &sPID);

    sendSlurmReply(sMsg, RESPONSE_JOB_STEP_STAT);
}

static void handleStepPids(Slurm_Msg_t *sMsg)
{
    Slurm_Step_Head_t *head = sMsg->unpData;
    if (!head) {
	flog("unpacking step pids request failed\n");
	sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	return;
    }

    Step_t *step = Step_findByStepId(head->jobid, head->stepid);
    if (!step) {
	Step_t s = {
	    .jobid = head->jobid,
	    .stepid = head->stepid };
	flog("%s to signal not found\n", Step_strID(&s));
	sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	ufree(head);
	return;
    }
    ufree(head);

    /* check permissions */
    if (!verifyUserId(sMsg->head.uid, step->uid)) {
	flog("request from invalid user %u\n", sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    /* send step PIDs */
    PStask_ID_t rootTID =
	(step->spawned && step->fwdata) ? step->fwdata->tid : step->loggerTID;
    PS_SendDB_t *msg = &sMsg->reply;
    Slurm_PIDs_t sPID;

    sPID.hostname = getConfValueC(Config, "SLURM_HOSTNAME");
    psAccountGetPidsByLogger(rootTID, &sPID.pid, &sPID.count);
    packSlurmPIDs(msg, &sPID);

    sendSlurmReply(sMsg, RESPONSE_JOB_STEP_PIDS);
}

uint64_t getNodeMem(void)
{
    long pages = sysconf(_SC_PHYS_PAGES);
    if (pages < 0) {
	mwarn(errno, "%s: sysconf(_SC_PHYS_PAGES)", __func__);
	return 1;
    }

    long pageSize = sysconf(_SC_PAGE_SIZE);
    if (pageSize < 0) {
	mwarn(errno, "%s: sysconf(_SC_PAGE_SIZE)", __func__);
	return 1;
    }

    return (uint64_t)((float) pages * (pageSize / 1048576.0));
}

/**
 * @brief Calculate the size of the temporary filesystem
 * in megabytes
 */
static uint32_t getTmpDisk(void)
{
    float pageSize = sysconf(_SC_PAGE_SIZE);
    if (pageSize < 0) {
	mwarn(errno, "%s: getting _SC_PAGE_SIZE failed: ", __func__);
	return 1;
    }

    char tmpDef[] = "/tmp";
    char *fs = getConfValueC(SlurmConfig, "TmpFS");
    if (!fs) fs = tmpDef;

    struct statfs sbuf;
    if ((statfs(fs, &sbuf)) == -1) {
	static bool report = true;
	if (report) {
	    mwarn(errno, "%s: statfs(%s)", __func__, fs);
	    report = false;
	}
	return 1;
    }
    return (uint32_t)((long) sbuf.f_blocks * (pageSize / 1048576.0));
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
    stat.debug = getConfValueI(Config, "DEBUG_MASK");
    /* cpus */
    stat.cpus = getConfValueI(Config, "SLURM_CPUS");
    /* boards */
    stat.boards = getConfValueI(Config, "SLURM_BOARDS");
    /* sockets */
    stat.sockets = getConfValueI(Config, "SLURM_SOCKETS");
    /* cores */
    stat.coresPerSocket = getConfValueI(Config, "SLURM_CORES_PER_SOCKET");
    /* threads */
    stat.threadsPerCore = getConfValueI(Config, "SLURM_THREADS_PER_CORE");
    /* real mem */
    stat.realMem = getNodeMem();
    /* tmp disk */
    stat.tmpDisk = getTmpDisk();
    /* pid */
    stat.pid = getpid();
    /* hostname */
    stat.hostname = getConfValueC(Config, "SLURM_HOSTNAME");
    /* logfile */
    stat.logfile = "syslog";
    /* step list */
    if (!Step_count()) {
	stat.stepList = strdup("NONE");
    } else {
	stat.stepList = Step_getActiveList();
    }
    /* version string */
    snprintf(stat.verStr, sizeof(stat.verStr), "psslurm-%i-p%s", version,
	     slurmProtoStr);

    packRespDaemonStatus(&msg, &stat);
    sendSlurmMsg(sMsg->sock, RESPONSE_SLURMD_STATUS, &msg, sMsg->head.uid);

    ufree(stat.stepList);
}

static void handleJobNotify(Slurm_Msg_t *sMsg)
{
    Req_Job_Notify_t *req = sMsg->unpData;
    if (!req) {
	flog("unpacking job notify request failed\n");
	sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	return;
    }

    Job_t *job = Job_findById(req->jobid);
    Step_t *step = Step_findByStepId(req->jobid, req->stepid);

    if (!job && !step) {
	flog("job/step %u.%u to notify not found, msg %s\n", req->jobid,
	     req->stepid, req->msg);
	sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	goto CLEANUP;
    }

    /* check permissions */
    if (!verifyUserId(sMsg->head.uid, job ? job->uid : step->uid)) {
	flog("request from invalid user %u for job %u stepid %u\n",
	     sMsg->head.uid, req->jobid, req->stepid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	goto CLEANUP;
    }

    flog("notify jobid %u stepid %u msg %s\n", req->jobid, req->stepid,
	 req->msg);
    if (job) {
	fwCMD_printMsg(job, NULL, "psslurm: ", strlen("psslurm: "), STDERR, 0);
	fwCMD_printMsg(job, NULL, req->msg, strlen(req->msg), STDERR, 0);
	fwCMD_printMsg(job, NULL, "\n", strlen("\n"), STDERR, 0);
    } else {
	fwCMD_printMsg(NULL, step, "psslurm: ", strlen("psslurm: "), STDERR, 0);
	fwCMD_printMsg(NULL, step, req->msg, strlen(req->msg), STDERR, 0);
	fwCMD_printMsg(NULL, step, "\n", strlen("\n"), STDERR, 0);
    }

    sendSlurmRC(sMsg, SLURM_SUCCESS);

CLEANUP:
    if (req) {
	ufree(req->msg);
	ufree(req);
    }
}

static void handleForwardData(Slurm_Msg_t *sMsg)
{
    /* check permissions */
    if (!checkPrivMsg(sMsg)) return;

    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sMsg, ESLURM_NOT_SUPPORTED);
}

static void sendJobKill(uint32_t jobid, uint32_t stepid, uint16_t signal)
{
    Req_Job_Kill_t kill = {
	.jobid = jobid,
	.stepid = stepid,
	.stepHetComp = NO_VAL,
	.signal = signal,
	.flags = 0,
	.sibling = NULL
    };

    /* send request to slurmctld */
    Req_Info_t *req = ucalloc(sizeof(*req));
    req->type = REQUEST_KILL_JOB;
    req->jobid = jobid;

    sendSlurmctldReq(req, &kill);
}

static int handleRespJobRequeue(Slurm_Msg_t *sMsg, void *info)
{
    Req_Info_t *req = info;
    char **ptr = &sMsg->ptr;
    uint32_t rc;
    getUint32(ptr, &rc);

    if (rc == ESLURM_DISABLED || rc == ESLURM_BATCH_ONLY) {
	flog("cancel job %u\n", req->jobid);
	sendJobKill(req->jobid, req->stepid, SIGKILL);
    } else if (rc != SLURM_SUCCESS) {
	flog("error: response %s rc %s sock %i for request %s jobid %u\n",
	     msgType2String(sMsg->head.type), slurmRC2String(rc), sMsg->sock,
	     msgType2String(req->type), req->jobid);
    }

    return 0;
}

void sendJobRequeue(uint32_t jobid)
{
    Req_Job_Requeue_t requeue = { .jobid = jobid };

    /* cancel or requeue the job on error */
    bool nohold = confHasOpt(SlurmConfig, "SchedulerParameters",
			     "nohold_on_prolog_fail");

    requeue.flags = nohold ? SLURM_JOB_PENDING :
		(SLURM_JOB_REQUEUE_HOLD | SLURM_JOB_LAUNCH_FAILED);

    /* send request to slurmctld */
    Req_Info_t *req = ucalloc(sizeof(*req));
    req->type = REQUEST_JOB_REQUEUE;
    req->jobid = jobid;
    req->stepid = NO_VAL; /* kill complete job */
    req->cb = &handleRespJobRequeue;

    flog("%u\n", jobid);

    sendSlurmctldReq(req, &requeue);
}

void sendPrologComplete(uint32_t jobid, uint32_t rc)
{
    Req_Prolog_Comp_t data = { .jobid = jobid, .rc = rc };

    Req_Info_t *req = ucalloc(sizeof(*req));
    req->type = REQUEST_COMPLETE_PROLOG;
    req->jobid = jobid;

    sendSlurmctldReq(req, &data);

    flog("job %u return code %s\n", jobid, slurmRC2String(rc));

    if (rc != SLURM_SUCCESS) {
	flog("prologue failed, requeuing job %u\n", jobid);

	/* psslurm needs to re-queue the job */
	sendJobRequeue(jobid);
     }
}

static void handleLaunchProlog(Slurm_Msg_t *sMsg)
{
    Req_Launch_Prolog_t *req = sMsg->unpData;
    if (!req) {
	flog("unpacking launch prolog request failed\n");
	sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	return;
    }

    /* check permissions */
    if (!checkPrivMsg(sMsg)) goto CLEANUP;

    fdbg(PSSLURM_LOG_PELOG, "jobid %u het-jobid %u uid %u gid %u alias %s"
	 " nodes=%s partition=%s stdErr='%s' stdOut='%s' work-dir=%s user=%s\n",
	 req->jobid, req->hetJobid, req->uid, req->gid, req->aliasList,
	 req->nodes, req->partition, req->stdErr, req->stdOut, req->workDir,
	 req->userName);

    fdbg(PSSLURM_LOG_DEBUG, "x11 %u allocHost='%s' allocPort %u cookie='%s'"
	 "target=%s targetPort %u\n", req->x11, req->x11AllocHost,
	 req->x11AllocPort, req->x11MagicCookie, req->x11Target,
	 req->x11TargetPort);

    /* spank env */
    for (uint32_t i = 0; i < req->spankEnv.cnt; i++) {
	mdbg(PSSLURM_LOG_PELOG, "%s: spankEnv%i: '%s'\n", __func__, i,
	     req->spankEnv.vars[i]);
    }

    /* let the slurmctld know we got the request */
    sendSlurmRC(sMsg, SLURM_SUCCESS);

    /* add an allocation if slurmctld prologue did not */
    Alloc_t *alloc = Alloc_find(req->jobid);
    if (!alloc && req->hetJobid != 0 && req->hetJobid != NO_VAL) {
	alloc = Alloc_findByPackID(req->hetJobid);
    }
    if (!alloc) {
	alloc = Alloc_add(req->jobid, req->hetJobid, req->nodes, &req->spankEnv,
			 req->uid, req->gid, req->userName);
    } else {
	envCat(&alloc->env, &req->spankEnv, envFilter);
    }

    /* save job credential and GRes in allocation */
    alloc->cred = req->cred;
    alloc->gresList = req->gresList;

    /* set mask of hardware threads to use */
    nodeinfo_t *nodeinfo = getNodeinfo(PSC_getMyID(), alloc->cred, alloc->id);
    if (!nodeinfo) {
	flog("could not extract nodeinfo from credentials for alloc %u\n",
	     alloc->id);
	sendPrologComplete(req->jobid, SLURM_ERROR);
	goto CLEANUP;
    }
    PSCPU_copy(alloc->hwthreads, nodeinfo->jobHWthreads);
    ufree(nodeinfo);

    Alloc_initJail(alloc);

    /* currently the use of the slurmd prologue is to gather more information
     * about the allocation and initialize the jail environment.
     *
     * It is not *yet* used to executed the prologue
     * itself.
     */
    /* let the slurmctld know the prologue has finished */
    sendPrologComplete(req->jobid, SLURM_SUCCESS);

CLEANUP:

    /* free request with the exception of the job credential and gres list
     * saved in the allocation */
    ufree(req->aliasList);
    ufree(req->nodes);
    ufree(req->partition);
    ufree(req->stdErr);
    ufree(req->stdOut);
    ufree(req->workDir);
    ufree(req->x11AllocHost);
    ufree(req->x11MagicCookie);
    ufree(req->x11Target);
    ufree(req->userName);
    envDestroy(&req->spankEnv);
    ufree(req);
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
    size_t nlSize = 0;

    /* extract pack size */
    char *sPackSize = envGet(&job->env, "SLURM_PACK_SIZE");
    if (!sPackSize) return true;

    job->packSize = atoi(sPackSize);

    /* extract pack nodes */
    for (uint32_t i = 0; i < job->packSize; i++) {
	char nodeListName[256];

	snprintf(nodeListName, sizeof(nodeListName),
		 "SLURM_JOB_NODELIST_PACK_GROUP_%u", i);

	char *next = envGet(&job->env, nodeListName);
	if (!next) {
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
    fdbg(PSSLURM_LOG_JOB, "job %u in %s\n", job->jobid,Job_strState(job->state));

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
    Job_t *job = sMsg->unpData;
    if (!job) {
	flog("unpacking batch job launch request failed\n");
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	return;
    }

    if (pluginShutdown) {
	/* don't accept new jobs if a shutdown is in progress */
	sendSlurmRC(sMsg, SLURM_ERROR);
	Job_delete(job);
	return;
    }

    if (Auth_isDeniedUID(job->uid)) {
	flog("denied UID %u to start job %u\n", sMsg->head.uid, job->jobid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	Job_delete(job);
	return;
    }

    /* memory cleanup  */
    malloc_trim(200);

    /* convert slurm hostlist to PSnodes   */
    if (!convHLtoPSnodes(job->slurmHosts, getNodeIDbySlurmHost,
			 &job->nodes, &job->nrOfNodes)) {
	flog("resolving PS nodeIDs from %s for job %u failed\n",
	     job->slurmHosts, job->jobid);
	goto ERROR;
    }

    job->localNodeId = getLocalID(job->nodes, job->nrOfNodes);
    if (job->localNodeId == NO_VAL) {
	flog("could not find my local ID for job %u in %s\n",
	     job->jobid, job->slurmHosts);
	goto ERROR;
    }

    /* verify job credential */
    if (!Job_verifyData(job)) goto ERROR;
    job->state = JOB_QUEUED;

    printJobLaunchInfos(job);

    /* set accounting options */
    setAccOpts(job->acctFreq, &job->accType);

    if (!extractJobPackInfos(job)) {
	flog("invalid job pack information for job %u\n", job->jobid);
	goto ERROR;
    }

    /* set mask of hardware threads to use */
    nodeinfo_t *nodeinfo = getNodeinfo(PSC_getMyID(), job->cred, job->jobid);
    if (!nodeinfo) {
	flog("could not extract nodeinfo from credentials for job %u\n",
	     job->jobid);
	goto ERROR;
    }
    PSCPU_copy(job->hwthreads, nodeinfo->jobHWthreads);
    ufree(nodeinfo);

    job->hostname = ustrdup(getConfValueC(Config, "SLURM_HOSTNAME"));

    /* write the jobscript */
    if (!writeJobscript(job)) {
	/* set myself offline and requeue the job */
	setNodeOffline(&job->env, job->jobid,
		       getConfValueC(Config, "SLURM_HOSTNAME"),
			"psslurm: writing jobscript failed");
	/* need to return success to be able to requeue the job */
	sendSlurmRC(sMsg, SLURM_SUCCESS);
	Job_delete(job);
	return;
    }

    flog("job %u user '%s' np %u nodes '%s' N %u tpp %u pack size %u"
	 " script '%s'\n", job->jobid, job->username, job->np, job->slurmHosts,
	 job->nrOfNodes, job->tpp, job->packSize, job->jobscript);

    /* sanity check nrOfNodes */
    if (job->nrOfNodes > (uint16_t) PSC_getNrOfNodes()) {
	mlog("%s: invalid nrOfNodes %u known Nodes %u\n", __func__,
	     job->nrOfNodes, PSC_getNrOfNodes());
	goto ERROR;
    }

    /* forward job info to other nodes in the job */
    send_PS_JobLaunch(job);

    /* setup job environment */
    initJobEnv(job);

    Alloc_t *alloc = Alloc_find(job->jobid);
    bool ret = false;
    char *prologue = getConfValueC(SlurmConfig, "Prolog");
    /* force slurmctld prologue for now */
    if (true || !prologue || prologue[0] == '\0') {
	/* no slurmd prologue configured,
	 * pspelogue should have added an allocation */
	if (!alloc) {
	    flog("error: no allocation for job %u found\n", job->jobid);
	    flog("** ensure slurmctld prologue is setup correctly **\n");
	    goto ERROR;
	}

	/* sanity check allocation state */
	if (alloc->state != A_PROLOGUE_FINISH) {
	    flog("allocation %u in invalid state %s\n", alloc->id,
		 Alloc_strState(alloc->state));
	    goto ERROR;
	}
	alloc->verified = true;

	/* pspelogue already ran parallel prologue, start job */
	alloc->state = A_RUNNING;
	ret = execBatchJob(job);
	fdbg(PSSLURM_LOG_JOB, "job %u in %s\n", job->jobid,
	     Job_strState(job->state));
    } else {
	/* slurmd prologue is configured */
	alloc->verified = true;
	if (alloc && alloc->state == A_PROLOGUE_FINISH) {
	    /* pspelogue already executed the prologue */
	    alloc->state = A_RUNNING;
	    ret = execBatchJob(job);
	    fdbg(PSSLURM_LOG_JOB, "job %u in %s\n", job->jobid,
		 Job_strState(job->state));
	} else {
	    /* slurmctld will start the "slurmd_prolog"
	     * (see @ref handleLaunchProlog) and the batch
	     * job in parallel. psslurm has to wait until the prologue
	     * is completed before the batch job is started */
	    ret = true;
	    flog("job %u is waiting for prologue to complete\n", job->jobid);
	}
    }

    /* return result to slurmctld */
    if (ret) {
	sendSlurmRC(sMsg, SLURM_SUCCESS);
	return;
    }

ERROR:
    sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
    Job_delete(job);
}

static void doTerminateAlloc(Slurm_Msg_t *sMsg, Alloc_t *alloc)
{
    int grace = getConfValueI(SlurmConfig, "KillWait");
    int maxTermReq = getConfValueI(Config, "MAX_TERM_REQUESTS");
    int signal = SIGTERM, pcount;

    if (!alloc->firstKillReq) alloc->firstKillReq = time(NULL);
    if (time(NULL) - alloc->firstKillReq > grace + 10) {
	/* grace time is over, use SIGKILL from now on */
	mlog("%s: sending SIGKILL to fowarders of allocation %u\n", __func__,
		alloc->id);
	Job_killForwarder(alloc->id);
	signal = SIGKILL;
    }

    if (maxTermReq > 0 && alloc->terminate++ >= maxTermReq) {
	/* ensure jobs will not get stuck forever */
	flog("force termination of allocation %u in state %s requests %i\n",
	     alloc->id, Alloc_strState(alloc->state), alloc->terminate);
	uint32_t allocID = alloc->id;
	Alloc_delete(alloc->id);
	sendEpilogueComplete(allocID, SLURM_SUCCESS);
	sendSlurmRC(sMsg, SLURM_SUCCESS);
	return;
    }

    switch (alloc->state) {
	case A_RUNNING:
	    if ((pcount = Alloc_signal(alloc->id, signal, sMsg->head.uid)) > 0) {
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
	    if (!Alloc_isLeader(alloc)) {
		/* epilogue already executed, we are done */
		sendSlurmRC(sMsg, ESLURMD_KILL_JOB_ALREADY_COMPLETE);
		Alloc_delete(alloc->id);
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
	    Alloc_delete(alloc->id);
	    sendSlurmRC(sMsg, SLURM_SUCCESS);
	    return;
    }

    /* job/steps already finished, start epilogue now */
    sendSlurmRC(sMsg, SLURM_SUCCESS);
    mlog("%s: starting epilogue for allocation %u state %s\n", __func__,
	 alloc->id, Alloc_strState(alloc->state));
    startPElogue(alloc, PELOGUE_EPILOGUE);
}

static void handleAbortReq(Slurm_Msg_t *sMsg, uint32_t jobid, uint32_t stepid)
{
    /* send success back to slurmctld */
    sendSlurmRC(sMsg, SLURM_SUCCESS);

    if (stepid != NO_VAL) {
	Step_t *step = Step_findByStepId(jobid, stepid);
	if (!step) {
	    Step_t s = {
		.jobid = jobid,
		.stepid = stepid };
	    flog("%s not found\n", Step_strID(&s));
	    return;
	}
	Step_signal(step, SIGKILL, sMsg->head.uid);
	Step_destroy(step);
	return;
    }

    Job_t *job = Job_findById(jobid);

    if (job) {
	if (!job->mother) {
	    Job_signalTasks(job, SIGKILL, sMsg->head.uid);
	    send_PS_JobExit(job->jobid, SLURM_BATCH_SCRIPT,
		    job->nrOfNodes, job->nodes);
	}
	Job_delete(job);
    } else {
	Alloc_t *alloc = Alloc_find(jobid);
	if (alloc && Alloc_isLeader(alloc)) {
	    Step_signalByJobid(alloc->id, SIGKILL, sMsg->head.uid);
	    send_PS_JobExit(alloc->id, SLURM_BATCH_SCRIPT,
		    alloc->nrOfNodes, alloc->nodes);
	}
	Alloc_delete(jobid);
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
		" TIME LIMIT ***\n", Step_strID(step));
	    fwCMD_printMsg(NULL, step, buf, strlen(buf), STDERR, 0);
	}
	fwCMD_stepTimeout(step->fwdata);
	step->timeout = true;
    } else {
	if (!step->localNodeId) {
	    snprintf(buf, sizeof(buf), "error: *** PREEMPTION for %s ***\n",
		     Step_strID(step));
	    fwCMD_printMsg(NULL, step, buf, strlen(buf), STDERR, 0);
	}
    }

    /* only signal selected steps */
    if (info->stepid != NO_VAL) Step_signal(step, SIGTERM, info->uid);

    return false;
}

static void handleKillReq(Slurm_Msg_t *sMsg, Alloc_t *alloc, Kill_Info_t *info)
{
    /* save step timeout and print warning message for all steps,
     * but only signal selected steps */
    Step_traverse(killSelectedSteps, info);

    /* if we only kill one selected step, we are done */
    if (info->stepid != NO_VAL) {
	Step_t s = {
	    .jobid = info->jobid,
	    .stepid = info->stepid };

	fdbg(PSSLURM_LOG_JOB, "kill request for single step %s\n",
	     Step_strID(&s));
	sendSlurmRC(sMsg, SLURM_SUCCESS);
	return;
    }

    /* kill request for complete allocation */
    alloc->terminate++;
    if (!alloc->firstKillReq) {
	alloc->firstKillReq = time(NULL);

	Job_t *job = Job_findById(info->jobid);
	if (job) {
	    char buf[512];
	    if (info->timeout) {
		job->timeout = true;
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
	int pcount = Alloc_signal(alloc->id, SIGTERM, sMsg->head.uid);
	if (pcount > 0) {
	    flog("waiting for %i processes to complete for allocation %u\n",
		 pcount, alloc->id);
	    sendSlurmRC(sMsg, SLURM_SUCCESS);
	    return;
	} else {
	    doTerminateAlloc(sMsg, alloc);
	}
    }
}

/**
 * @brief Handles RPCs REQUEST_KILL_PREEMPTED, REQUEST_KILL_TIMELIMIT,
 * REQUEST_ABORT_JOB and  REQUEST_TERMINATE_JOB
 *
 * @param sMsg The message containing the RPC to handle
 */
static void handleTerminateReq(Slurm_Msg_t *sMsg)
{
    Req_Terminate_Job_t *req = sMsg->unpData;
    if (!req) {
	flog("unpacking terminate request failed\n");
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    Step_t s = {
	.jobid = req->jobid,
	.stepid = req->stepid };

    /* check permissions */
    if (!checkPrivMsg(sMsg)) goto CLEANUP;

    flog("%s Slurm-state %u uid %u type %s\n", Step_strID(&s),
	 req->jobstate, sMsg->head.uid, msgType2String(sMsg->head.type));

    /* restore account freq */
    psAccountSetPoll(PSACCOUNT_OPT_MAIN, Acc_getPoll());

    /* remove all unfinished spawn requests */
    PSIDspawn_cleanupBySpawner(PSC_getMyTID());
    cleanupDelayedSpawns(req->jobid, req->stepid);

    /* find the corresponding allocation */
    Alloc_t *alloc = Alloc_find(req->jobid);

    if (!alloc) {
	Job_t *job = Job_findById(req->jobid);
	Job_destroy(job);

	Step_destroyByJobid(req->jobid);
	flog("allocation %s not found\n", Step_strID(&s));
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
	    info.timeout = false;
	    handleKillReq(sMsg, alloc, &info);
	    break;
	case REQUEST_KILL_TIMELIMIT:
	    info.timeout = true;
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
	    flog("unknown terminate request for %s\n", Step_strID(&s));
    }

CLEANUP:
    envDestroy(&req->spankEnv);
    freeGresJobAlloc(&req->gresList);
    freeJobCred(req->cred);
    freeGresCred(&req->gresJobList);
    ufree(req->nodes);
    ufree(req->workDir);
    ufree(req->details);
    ufree(req);
}

static void handleNetworkCallerID(Slurm_Msg_t *sMsg)
{
    /* check permissions */
    if (!checkPrivMsg(sMsg)) return;

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
 * Currently used to receive TRes accounting fields from slurmctld
 *
 * @param sMsg The Slurm message to handle
 */
static void handleRespNodeReg(Slurm_Msg_t *sMsg)
{
    /* check permissions */
    if (!checkPrivMsg(sMsg)) return;

    /* don't request the additional info again */
    needNodeRegResp = false;

    tresDBconfig = sMsg->unpData;
    if (!tresDBconfig) {
	flog("unpacking response node reg failed\n");
	return;
    }

    /* don't free the data after this function */
    sMsg->unpData = NULL;

    for (uint32_t i=0; i<tresDBconfig->count; i++) {
	fdbg(PSSLURM_LOG_ACC, "TRes(%u) alloc %zu count %lu id %u name '%s' "
	     "type '%s'\n", i, tresDBconfig->entry[i].allocSec,
	     tresDBconfig->entry[i].count, tresDBconfig->entry[i].id,
	     tresDBconfig->entry[i].name, tresDBconfig->entry[i].type);
    }

    char *val = getConfValueC(SlurmConfig, "AcctGatherInterconnectType");
    if (val && !strcasecmp(val, "acct_gather_interconnect/ofed")) {
	if (TRes_getID("ic", "ofed") == NO_VAL) {
	    flog("warning: missing ic/ofed in AccountingStorageTRES\n");
	    flog("warning: ofed accounting is disabled\n");
	}
    }

    val = getConfValueC(SlurmConfig, "AcctGatherFilesystemType");
    if (val && !strcasecmp(val, "acct_gather_filesystem/lustre")) {
	if (TRes_getID("fs", "lustre") == NO_VAL) {
	    flog("warning: missing fs/lustre in AccountingStorageTRES\n");
	    flog("warning: lustre accounting is disabled\n");
	}
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
    uint32_t nrOfNodes;
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
    fw->head.uid = sMsg->head.uid;

    for (uint32_t i = 0; i < sMsg->head.forward; i++) {
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
    Alloc_verify(true);
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

	    /* unpack request */
	    if (!unpackSlurmMsg(sMsg)) {
		flog("unpacking message %s (%u) failed\n",
		     msgType2String(sMsg->head.type), sMsg->head.version);
		sendSlurmRC(sMsg, SLURM_ERROR);
		return 0;
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
 *
 * Detect the installed Slurm version by calling the binary defined in
 * the SINFO_BINARY configuration variable with option
 * "--version". The output is expected to be of the form "slurm
 * <version>" and is parsed accordingly.
 *
 * This mechanism will only be triggered if the configuation variable
 * SLURM_PROTO_VERSION is set to "auto".
 *
 * @return Return a static string containing the Slurm version number
 * or NULL in case of failure or SLURM_PROTO_VERSION != "auto"
 */
static const char *autoDetectSlurmVer(void)
{
    static char autoVer[32] = { '\0' };

    const char *confVer = getConfValueC(Config, "SLURM_PROTO_VERSION");
    if (strcmp(confVer, "auto")) return NULL;

    const char *sinfo = getConfValueC(Config, "SINFO_BINARY");
    if (!sinfo) {
	flog("no SINFO_BINARY provided\n");
	return NULL;
    }

    struct stat sb;
    if (stat(sinfo, &sb) == -1) {
	mwarn(errno, "%s: stat('%s')", __func__, sinfo);
	return NULL;
    } else if (!(sb.st_mode & S_IXUSR)) {
	flog("'%s' not executable\n", sinfo);
	return NULL;
    }

    char sinfoCmd[128];
    char *server = getConfValueC(Config, "SLURM_CONF_SERVER");
    if (!strcmp(server, "none")) {
	snprintf(sinfoCmd, sizeof(sinfoCmd), "%s --version", sinfo);
    } else {
	snprintf(sinfoCmd, sizeof(sinfoCmd), "SLURM_CONF_SERVER=%s "
		 "%s --version", server, sinfo);
    }
    FILE *fp = popen(sinfoCmd, "r");
    if (!fp) {
	mwarn(errno, "%s: popen('%s')", __func__, sinfoCmd);
	return NULL;
    }

    char *line = NULL;
    size_t len = 0;
    while (getline(&line, &len, fp) != -1) {
	if (strncmp(line, "slurm ", 6)) continue;
	if (sscanf(line, "slurm %31s", autoVer) == 1) {
	    char *dash = strchr(autoVer, '-');
	    if (dash) *dash = '\0';
	    break;
	} else {
	    char *nl = strchr(line, '\n');
	    if (nl) *nl = '\0';
	    flog("invalid slurm version string: '%s'\n", line);
	}
    }
    free(line);
    pclose(fp);

    if (!strlen(autoVer)) {
	flog("no version found\n");
	return NULL;
    }
    return autoVer;
}

bool initSlurmdProto(void)
{
    const char *pver = getConfValueC(Config, "SLURM_PROTO_VERSION");

    if (!strcmp(pver, "auto")) {
	const char *autoVer = autoDetectSlurmVer();
	if (!autoVer) {
	    flog("Automatic detection of the Slurm protocol failed, "
		 "consider setting SLURM_PROTO_VERSION in psslurm.conf\n");
	    return false;
	}
	slurmVerStr = ustrdup(autoVer);
	pver = autoVer;
    }

    // TODO  :  make a function which converts protocol version in int to string
    if (!strncmp(pver, "23.02", 5) || !strncmp(pver, "2302", 4)) {
	slurmProto = SLURM_23_02_PROTO_VERSION;
	slurmProtoStr = ustrdup("23.02");
    } else if (!strncmp(pver, "22.05", 5) || !strncmp(pver, "2205", 4)) {
	slurmProto = SLURM_22_05_PROTO_VERSION;
	slurmProtoStr = ustrdup("22.05");
    } else if (!strncmp(pver, "21.08", 5) || !strncmp(pver, "2108", 4)) {
	slurmProto = SLURM_21_08_PROTO_VERSION;
	slurmProtoStr = ustrdup("21.08");
    } else if (!strncmp(pver, "20.11", 5) || !strncmp(pver, "2011", 4)) {
	slurmProto = SLURM_20_11_PROTO_VERSION;
	slurmProtoStr = ustrdup("20.11");
    } else if (!strncmp(pver, "20.02", 5) || !strncmp(pver, "2002", 4)) {
	slurmProto = SLURM_20_02_PROTO_VERSION;
	slurmProtoStr = ustrdup("20.02");
    } else if (!strncmp(pver, "19.05", 5) || !strncmp(pver, "1905", 4)) {
	slurmProto = SLURM_19_05_PROTO_VERSION;
	slurmProtoStr = ustrdup("19.05");
    } else {
	flog("unsupported Slurm protocol version %s\n", pver);
	return false;
    }

    if (!slurmVerStr) {
	char buf[64];
	snprintf(buf, sizeof(buf), "%s.0-0", slurmProtoStr);
	slurmVerStr = ustrdup(buf);
    }

    /* register privileged RPCs */
    registerSlurmdMsg(REQUEST_LAUNCH_PROLOG, handleLaunchProlog);
    registerSlurmdMsg(REQUEST_KILL_PREEMPTED, handleTerminateReq);
    registerSlurmdMsg(REQUEST_KILL_TIMELIMIT, handleTerminateReq);
    registerSlurmdMsg(REQUEST_ABORT_JOB, handleTerminateReq);
    registerSlurmdMsg(REQUEST_TERMINATE_JOB, handleTerminateReq);
    registerSlurmdMsg(REQUEST_SHUTDOWN, handleShutdown);
    registerSlurmdMsg(REQUEST_RECONFIGURE, handleReconfigure);
    registerSlurmdMsg(REQUEST_RECONFIGURE_WITH_CONFIG, handleConfig);
    registerSlurmdMsg(REQUEST_REBOOT_NODES, handleRebootNodes);
    registerSlurmdMsg(REQUEST_HEALTH_CHECK, handleHealthCheck);
    registerSlurmdMsg(REQUEST_ACCT_GATHER_UPDATE, handleAcctGatherUpdate);
    registerSlurmdMsg(REQUEST_ACCT_GATHER_ENERGY, handleAcctGatherEnergy);
    registerSlurmdMsg(REQUEST_FORWARD_DATA, handleForwardData);
    registerSlurmdMsg(REQUEST_NETWORK_CALLERID, handleNetworkCallerID);
    registerSlurmdMsg(RESPONSE_NODE_REGISTRATION, handleRespNodeReg);

    /* register unprivileged RPCs */
    registerSlurmdMsg(REQUEST_BATCH_JOB_LAUNCH, handleBatchJobLaunch);
    registerSlurmdMsg(REQUEST_LAUNCH_TASKS, handleLaunchTasks);
    registerSlurmdMsg(REQUEST_SIGNAL_TASKS, handleSignalTasks);
    registerSlurmdMsg(REQUEST_TERMINATE_TASKS, handleSignalTasks);
    registerSlurmdMsg(REQUEST_REATTACH_TASKS, handleReattachTasks);
    registerSlurmdMsg(REQUEST_SUSPEND_INT, handleSuspendInt);
    registerSlurmdMsg(REQUEST_NODE_REGISTRATION_STATUS, handleNodeRegStat);
    registerSlurmdMsg(REQUEST_PING, sendPing);
    registerSlurmdMsg(REQUEST_JOB_ID, handleJobId);
    registerSlurmdMsg(REQUEST_FILE_BCAST, handleFileBCast);
    registerSlurmdMsg(REQUEST_JOB_STEP_STAT, handleStepStat);
    registerSlurmdMsg(REQUEST_JOB_STEP_PIDS, handleStepPids);
    registerSlurmdMsg(REQUEST_DAEMON_STATUS, handleDaemonStatus);
    registerSlurmdMsg(REQUEST_STEP_COMPLETE, handleInvalid);
    registerSlurmdMsg(REQUEST_JOB_NOTIFY, handleJobNotify);

    /* removed in 20.11 */
    registerSlurmdMsg(REQUEST_STEP_COMPLETE_AGGR, handleInvalid);
    registerSlurmdMsg(RESPONSE_MESSAGE_COMPOSITE, handleRespMessageComposite);
    registerSlurmdMsg(MESSAGE_COMPOSITE, handleInvalid);
    registerSlurmdMsg(REQUEST_COMPLETE_BATCH_SCRIPT, handleInvalid);
    registerSlurmdMsg(REQUEST_UPDATE_JOB_TIME, handleUpdateJobTime);

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
	for (uint32_t i = 0; i < tresDBconfig->count; i++) {
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
    Resp_Node_Reg_Status_t stat;
    memset(&stat, 0, sizeof(stat));

    flog("answer ping request, host %s protoVersion %u\n",
	 getConfValueC(Config, "SLURM_HOSTNAME"), slurmProto);

    /* current time */
    stat.now = time(NULL);
    /* start time */
    stat.startTime = start_time;
    /* status */
    stat.status = SLURM_SUCCESS;
    /* node_name */
    stat.nodeName = getConfValueC(Config, "SLURM_HOSTNAME");
    /* architecture */
    struct utsname sys;
    uname(&sys);
    stat.arch = sys.machine;
    /* os */
    stat.sysname = sys.sysname;
    /* cpus */
    stat.cpus = getConfValueI(Config, "SLURM_CPUS");
    /* boards */
    stat.boards = getConfValueI(Config, "SLURM_BOARDS");
    /* sockets */
    stat.sockets = getConfValueI(Config, "SLURM_SOCKETS");
    /* cores */
    stat.coresPerSocket = getConfValueI(Config, "SLURM_CORES_PER_SOCKET");
    /* threads */
    stat.threadsPerCore = getConfValueI(Config, "SLURM_THREADS_PER_CORE");
    /* real mem */
    stat.realMem = getNodeMem();
    /* tmp disk */
    stat.tmpDisk = getTmpDisk();
    /* sysinfo (uptime, cpu load average, free mem) */
    getSysInfo(&stat.cpuload, &stat.freemem, &stat.uptime);
    /* hash value of the SLURM config file */
    if (getConfValueI(Config, "DISABLE_CONFIG_HASH") == 1) {
	stat.config = NO_VAL;
    } else {
	stat.config = getSlurmConfHash();
    }

    fdbg(PSSLURM_LOG_DEBUG, "nodeName '%s' arch '%s' sysname '%s' cpus %hu"
	 " boards %hu sockets %hu coresPerSocket %hu threadsPerCore %hu"
	 " realMem %lu tmpDisk %u\n", stat.nodeName, stat.arch, stat.sysname,
	 stat.cpus, stat.boards, stat.sockets, stat.coresPerSocket,
	 stat.threadsPerCore, stat.realMem, stat.tmpDisk);

    /* job id infos (count, array (jobid/stepid) */
    Job_getInfos(&stat.jobInfoCount, &stat.jobids, &stat.stepids);
    Step_getInfos(&stat.jobInfoCount, &stat.jobids, &stat.stepids);
    stat.stepHetComp = umalloc(sizeof(*stat.stepHetComp) * stat.jobInfoCount);
    for (uint32_t i = 0; i < stat.jobInfoCount; i++) {
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
    psAccountInfo_t info;
    psAccountGetLocalInfo(&info);
    memcpy(&stat.eData, &info.energy, sizeof(info.energy));

    /* dynamic node feature */
    stat.dynamic = false;
    stat.dynamicConf = NULL;
    stat.dynamicFeat = NULL;

    /* send request to slurmctld */
    Req_Info_t *req = ucalloc(sizeof(*req));
    req->type = MESSAGE_NODE_REGISTRATION_STATUS;
    req->cb = &handleSlurmdMsg;

    sendSlurmctldReq(req, &stat);

    /* free data */
    ufree(stat.jobids);
    ufree(stat.stepids);
    ufree(stat.stepHetComp);
    ufree(stat.dynamicConf);
    ufree(stat.dynamicFeat);
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
	    ret = sendSlurmMsg(sMsg->sock, type, &sMsg->reply, sMsg->head.uid);
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
    for (uint32_t i = 0; i < nrOfNodes; i++) if (nodes[i] == psNodeID) return i;
    return -1;
}

void sendStepExit(Step_t *step, uint32_t exitStatus)
{
    flog("REQUEST_STEP_COMPLETE for %s to slurmctld: exit %u\n",
	 Step_strID(step), exitStatus);

    /* add account data to request */
    PStask_ID_t rootTID = PSC_getTID(-1, 1); // dummy TID pointing nowhere
    Forwarder_Data_t *fw = step->fwdata;
    if (fw) rootTID = fw->cPid < 1 ? fw->tid : PSC_getTID(-1, fw->cPid);
    step->accType = (step->leader) ? step->accType : 0;

    SlurmAccData_t slurmAccData = {
	.psAcct = { .numTasks = 0 },
	.type = step->accType,
	.nodes = step->nodes,
	.nrOfNodes = step->nrOfNodes,
	.rootTID = rootTID,
	.tasks = &step->tasks,
	.remoteTasks = &step->remoteTasks,
	.childPid = 0,
	.localNodeId = step->localNodeId,
	.iBase = &step->acctBase };
    addSlurmAccData(&slurmAccData);

    Req_Step_Comp_t comp = {
	.jobid = step->jobid,
	.stepid = step->stepid,
	.stepHetComp = step->stepHetComp,
	.firstNode = 0,
	.lastNode = step->nrOfNodes -1,
	.exitStatus = exitStatus,
	.sAccData = &slurmAccData };

    Req_Info_t *req = ucalloc(sizeof(*req));
    req->type = REQUEST_STEP_COMPLETE;
    req->jobid = step->jobid;
    req->stepid = step->stepid;
    req->stepHetComp = step->stepHetComp;

    sendSlurmctldReq(req, &comp);
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
	if (task->sentExit || task->jobRank < 0) continue;
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
	if (task->sentExit || task->jobRank < 0) continue;
	if (task->exitCode == exitCode) {
	    msg.taskRanks[exitCount2++] = task->jobRank;
	    task->sentExit = 1;
	    /*
	    mlog("%s: tasks jobRank:%i exit:%i exitCount:%i\n", __func__,
		    task->jobRank, task->exitCode, msg.exitCount);
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
	for (int i = 0; i < MAX_SATTACH_SOCKETS; i++) {
	    if (ctlPort[i] != -1) {
		int sock = tcpConnectU(ctlAddr[i], ctlPort[i]);
		if (sock < 0) {
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
    for (uint32_t i = 0; i < step->globalTaskIdsLen[step->localNodeId]; i++) {
	int32_t rank = step->globalTaskIds[step->localNodeId][i];
	if (!findTaskByJobRank(&step->tasks, rank)) {
	    PS_Tasks_t *task = addTask(&step->tasks, -1, -1, NULL, TG_ANY, rank,
				       rank /* no handle on global rank */);
	    task->exitCode = -1;
	}
    }
}

void sendTaskExit(Step_t *step, int *ctlPort, int *ctlAddr)
{
    addMissingTasks(step);

    uint32_t taskCount = countRegTasks(&step->tasks);
    if (taskCount != step->globalTaskIdsLen[step->localNodeId]) {
	mlog("%s: still missing tasks: %u of %u\n", __func__, taskCount,
	    step->globalTaskIdsLen[step->localNodeId]);
    }

    uint32_t count = 0;
    while (count < taskCount) {
	int exitCode = -100;
	list_t *t;
	list_for_each(t, &step->tasks) {
	    PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	    if (task->jobRank < 0) continue;
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

    if (srunSendMsg(-1, step, RESPONSE_LAUNCH_TASKS, &body) < 1) {
	flog("send RESPONSE_LAUNCH_TASKS failed %s\n", Step_strID(step));
    }

    flog("send RESPONSE_LAUNCH_TASKS %s pids %u for '%s'\n",
	 Step_strID(step), step->globalTaskIdsLen[nodeID], resp.nodeName);
}

void sendLaunchTasksFailed(Step_t *step, uint32_t nodeID, uint32_t error)
{
    if (nodeID == (uint32_t) ALL_NODES) {
	for (uint32_t i = 0; i < step->nrOfNodes; i++) {
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
    list_t *t;
    Resp_Launch_Tasks_t resp;

    resp.jobid = step->jobid;
    resp.stepid = step->stepid;
    resp.stepHetComp = step->stepHetComp;
    resp.returnCode = SLURM_SUCCESS;
    resp.nodeName = getConfValueC(Config, "SLURM_HOSTNAME");

    /* count of PIDs */
    list_for_each(t, &step->tasks) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	if (task->jobRank < 0) continue;
	countPIDs++;
    }
    resp.countPIDs = countPIDs;

    /* local PIDs */
    resp.localPIDs = umalloc(sizeof(uint32_t) * countPIDs);

    list_for_each(t, &step->tasks) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	if (task->jobRank < 0) continue;
	if (countLocalPIDs >=countPIDs) break;
	resp.localPIDs[countLocalPIDs++] = PSC_getPID(task->childTID);
    }
    resp.countLocalPIDs = countLocalPIDs;

    /* global task IDs */
    resp.globalTIDs = umalloc(sizeof(uint32_t) * countPIDs);

    list_for_each(t, &step->tasks) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	if (task->jobRank < 0) continue;
	if (countGTIDs >=countPIDs) break;
	resp.globalTIDs[countGTIDs++] = task->jobRank;
    }
    resp.countGlobalTIDs = countGTIDs;

    if (countPIDs != countGTIDs || countPIDs != countLocalPIDs) {
	mlog("%s: mismatching PID %u and GTID %u count\n", __func__,
	     countPIDs, countGTIDs);
	goto CLEANUP;
    }

    packRespLaunchTasks(&body, &resp);

    /* send the message to srun */
    if (srunSendMsg(-1, step, RESPONSE_LAUNCH_TASKS, &body) < 1) {
	flog("send RESPONSE_LAUNCH_TASKS failed %s\n", Step_strID(step));
    }

    flog("send RESPONSE_LAUNCH_TASKS %s pids %u\n", Step_strID(step), countPIDs);

CLEANUP:
    ufree(resp.localPIDs);
    ufree(resp.globalTIDs);
}

void sendJobExit(Job_t *job, uint32_t exitStatus)
{
    if (job->signaled) exitStatus = 0;
    if (job->timeout) exitStatus = SIGTERM;

    flog("REQUEST_COMPLETE_BATCH_SCRIPT: jobid %u exit %u\n",
	 job->jobid, exitStatus);

    /* save account data */
    SlurmAccData_t slurmAccData;
    memset(&slurmAccData, 0, sizeof(slurmAccData));

    slurmAccData.nodes = job->nodes;
    slurmAccData.nrOfNodes = job->nrOfNodes;
    slurmAccData.iBase = &job->acctBase;
    slurmAccData.localNodeId = job->localNodeId;

    if (job->fwdata) {
	/* add information from forwarder */
	slurmAccData.type = job->accType;
	slurmAccData.tasks = &job->tasks;
	slurmAccData.childPid = job->fwdata->cPid;
    }
    addSlurmAccData(&slurmAccData);

    /* send request */
    Req_Comp_Batch_Script_t comp = {
	.sAccData = &slurmAccData,
	.jobid = job->jobid,
	.exitStatus = exitStatus,
	.uid = job->uid,
	.hostname = job->hostname,
	.rc = 0,
    };

    Req_Info_t *req = ucalloc(sizeof(*req));
    req->type = REQUEST_COMPLETE_BATCH_SCRIPT;
    req->jobid = job->jobid;

    sendSlurmctldReq(req, &comp);
}

void sendEpilogueComplete(uint32_t jobid, uint32_t rc)
{
    fdbg(PSSLURM_LOG_PELOG, "allocation %u to slurmctld\n", jobid);

    Req_Epilog_Complete_t msg = {
	.jobid = jobid,
	.rc = rc,
	.nodeName = getConfValueC(Config, "SLURM_HOSTNAME")
    };

    Req_Info_t *req = ucalloc(sizeof(*req));
    req->type = MESSAGE_EPILOG_COMPLETE;
    req->jobid = jobid;

    sendSlurmctldReq(req, &msg);
}

void sendDrainNode(const char *nodeList, const char *reason)
{
    Req_Update_Node_t update;
    memset(&update, 0, sizeof(update));

    update.weight = NO_VAL;
    update.nodeList = nodeList;
    update.reason = reason;
    update.reasonUID = getuid();
    update.nodeState = NODE_STATE_DRAIN;

    Req_Info_t *req = ucalloc(sizeof(*req));
    req->type = REQUEST_UPDATE_NODE;

    sendSlurmctldReq(req, &update);
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
    int action = CONF_ACT_NONE;
    if (info) action = *(int *)info;

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
    if (!unpackSlurmMsg(sMsg)) {
	flog("unpacking config response failed\n");
	return 0;
    }
    Config_Msg_t *config = sMsg->unpData;
    if (!config) {
	flog("unpacking new configuration failed\n");
	return 0;
    }

    flog("successfully unpacked config msg\n");

    char *confCache = getConfValueC(Config, "SLURM_CONF_CACHE");
    bool ret = writeSlurmConfigFiles(config, confCache);
    freeSlurmConfigMsg(config);

    if (!ret) {
	flog("failed to write Slurm configuration files to %s\n", confCache);
	return 0;
    }

    /* update configuration file defaults */
    addConfigEntry(Config, "SLURM_CONFIG_DIR", confCache);

    switch (action) {
	case CONF_ACT_STARTUP:
	    /* parse updated configuration files */
	    if (!parseSlurmConfigFiles()) {
		flog("fatal: failed to parse configuration\n");
		PSIDplugin_finalize("psslurm");
	    } else if (!finalizeInit()) {
		/* finalize psslurm's startup failed */
		flog("startup of psslurm failed\n");
		PSIDplugin_finalize("psslurm");
	    }
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

    int *confAction = umalloc(sizeof(*confAction));
    *confAction = action;
    if (!registerSlurmSocket(sock, handleSlurmConf, confAction)) {
	free(confAction);
	flog("register Slurm socket %i failed\n", sock);
	goto ERROR;
    }

    /* send configuration request message to slurmctld */
    addUint32ToMsg(CONFIG_REQUEST_SLURMD, &body);
    if (sendSlurmMsg(sock, REQUEST_CONFIG, &body, RES_UID_ANY) == -1) {
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
