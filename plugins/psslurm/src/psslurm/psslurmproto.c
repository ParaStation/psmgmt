/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmproto.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
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
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "list.h"
#include "pscomplist.h"
#include "pscpu.h"
#include "psenv.h"
#include "pslog.h"
#include "psserial.h"
#include "psstrbuf.h"
#include "psstrv.h"
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
#include "psslurmcontainer.h"
#include "psslurmenv.h"
#include "psslurmforwarder.h"
#include "psslurmfwcomm.h"
#include "psslurmio.h"
#include "psslurmjobcred.h"
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

/** list of privileged sstat UIDs */
static uid_t *sstatUIDs = NULL;

/** number of sstat UIDs */
static uint16_t numSstatUIDs = 0;

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
	if (!realUptime) {
	    realUptime = info.uptime - (time(NULL) - start_time);
	}
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
static bool checkPrivMsg(Slurm_Msg_t *sMsg)
{
    if (sMsg->head.uid != 0 && sMsg->head.uid != slurmUserID) {
	flog("request from invalid user %u message %s\n", sMsg->head.uid,
	     msgType2String(sMsg->head.type));
	return false;
    }
    return true;
}

/**
 * @brief Send a ping response
 *
 * Send a Slurm ping message including the current system load and
 * free memory information.
 *
 * @param The ping request message
 *
 * @return Always return SLURM_NO_RC
 */
static int sendPing(Slurm_Msg_t *sMsg)
{
    Resp_Ping_t ping;
    uint32_t unused;
    getSysInfo(&ping.cpuload, &ping.freemem, &unused);

    PS_SendDB_t *msg = &sMsg->reply;
    packRespPing(msg, &ping);
    sendSlurmReply(sMsg, RESPONSE_PING_SLURMD);
    return SLURM_NO_RC;
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

    char *jobdir = getConfValueC(Config, "DIR_JOB_FILES");
    if (!writeFile(Job_strID(job->jobid), jobdir, job->jsData,
		   strlen(job->jsData))) {
	flog("writing jobscript '%s' for job %u failed", job->jobscript,
	     job->jobid);
    } else {
	ret = true;
	job->jobscript = PSC_concat(jobdir, "/", Job_strID(job->jobid));
    }

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
    int cnt = 0;
    for (char **e = envGetArray(step->env); e && *e; e++, cnt++) {
	fdbg(PSSLURM_LOG_ENV, "env%i: '%s'\n", cnt, *e);
    }

    /* spank env */
    cnt = 0;
    for (char **e = envGetArray(step->spankenv); e && *e; e++, cnt++) {
	fdbg(PSSLURM_LOG_ENV, "spankenv%i: '%s'\n", cnt, *e);
    }

    /* set stdout/stderr/stdin options */
    fdbg(PSSLURM_LOG_IO, "stdOut '%s' stdOutRank %i stdOutOpt %s\n",
	 step->stdOut, step->stdOutRank, IO_strOpt(step->stdOutOpt));

    fdbg(PSSLURM_LOG_IO, "stdErr '%s' stdErrRank %i stdErrOpt %s\n",
	 step->stdErr, step->stdErrRank, IO_strOpt(step->stdErrOpt));

    fdbg(PSSLURM_LOG_IO, "stdIn '%s' stdInRank %i stdInOpt %s\n",
	 step->stdIn, step->stdInRank, IO_strOpt(step->stdInOpt));

    fdbg(PSSLURM_LOG_IO, "bufferedIO '%li' labelIO '%li'\n",
	 step->taskFlags & LAUNCH_BUFFERED_IO,
	 step->taskFlags & LAUNCH_LABEL_IO);

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
    fdbg(PSSLURM_LOG_PACK, "packNodeOffset %u packJobid %u packNtasks %u "
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
	fdbg(PSSLURM_LOG_PACK, "packTaskCount[%u]: %u\n", i,
	     step->packTaskCounts[i]);
    }

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

uint32_t getLocalID(PSnodes_ID_t *nodes, uint32_t nrOfNodes)
{
    PSnodes_ID_t myID = PSC_getMyID();
    for (uint32_t i = 0; i < nrOfNodes; i++) if (nodes[i] == myID) return i;

    return NO_VAL;
}

/**
 * @brief Set node local memory limits for step
 *
 * @param step The step to set the limits for
 */
static void setStepMemLimits(Step_t *step)
{
    if (step->localNodeId == NO_VAL) {
	flog("invalid local node ID for %s\n", Step_strID(step));
	return;
    }

    JobCred_t *cred = step->cred;

    if (step->jobMemLimit == NO_VAL64) {
	uint32_t i = 0, idx = 0;
	while (i < cred->jobMemAllocSize
	       && idx + cred->jobMemAllocRepCount[i] <= step->localNodeId)
	    idx += cred->jobMemAllocRepCount[i++];
	if (i < cred->jobMemAllocSize) {
	    step->jobMemLimit = cred->jobMemAlloc[i];
	}
    }

    if (step->jobMemLimit == NO_VAL64) {
	flog("warning: could not set job memory limit for %s\n",
	     Step_strID(step));
    }

    if (step->stepMemLimit == NO_VAL64) {
	uint32_t i = 0, idx = 0;
	while (i < cred->stepMemAllocSize
	       && idx + cred->stepMemAllocRepCount[i] <= step->localNodeId)
	    idx += cred->stepMemAllocRepCount[i++];
	if (i < cred->stepMemAllocSize) {
	    step->stepMemLimit = cred->stepMemAlloc[i]*1024*1024;
	}
    }

    if (step->stepMemLimit == NO_VAL64) {
	flog("warning: could not set step memory limit for %s\n",
	     Step_strID(step));
    }
}

static int handleLaunchTasks(Slurm_Msg_t *sMsg)
{
    /* don't accept new steps if a shutdown is in progress */
    if (pluginShutdown) return SLURM_ERROR;

    Step_t *step = sMsg->unpData;
    if (!step) {
	flog("unpacking step failed\n");
	return ESLURMD_INVALID_JOB_CREDENTIAL;
    }

    if (Auth_isDeniedUID(step->uid)) {
	flog("denied UID %u to start step %s\n", sMsg->head.uid,
	     Step_strID(step));
	return ESLURM_USER_ID_MISSING;
    }

    /* verify job credential */
    if (!Step_verifyData(step)) {
	flog("invalid data for %s\n", Step_strID(step));
	return ESLURMD_INVALID_JOB_CREDENTIAL;
    }

    step->state = JOB_QUEUED;
    printLaunchTasksInfos(step);

    /* launch step in container */
    if (step->containerBundle && step->containerBundle[0] != '\0') {
	step->ct = Container_new(step->containerBundle, step->jobid,
				 step->stepid, step->username, step->uid,
				 step->gid);
	if (!step->ct) {
	    flog("error: failed to initialize container %s %s\n",
		 step->containerBundle, Step_strID(step));
	    return ESLURM_CONTAINER_NOT_CONFIGURED;
	}
    }

    /* set accounting options */
    setAccOpts(step->acctFreq, &step->accType);

    /* srun addr is always empty, use msg header addr instead */
    step->srun.sin_addr.s_addr = sMsg->head.addr.ip;
    step->srun.sin_port = sMsg->head.addr.port;

    /* env / spank env */
    for (char **e = envGetArray(step->spankenv); e && *e; e++) {
	if (!strncmp("_SLURM_SPANK_OPTION_x11spank_forward_x", *e, 38)) {
	    step->x11forward = true;
	    break;
	}
    }

    /* set stdout/stderr/stdin options */
    setIOoptions(step->stdOut, &step->stdOutOpt, &step->stdOutRank);
    setIOoptions(step->stdErr, &step->stdErrOpt, &step->stdErrRank);
    setIOoptions(step->stdIn, &step->stdInOpt, &step->stdInRank);

    /* convert slurm hostlist to PSnodes */
    uint32_t count;
    if (!convHLtoPSnodes(step->slurmHosts, getNodeIDbySlurmHost,
			 &step->nodes, &count)) {
	flog("resolving PS nodeIDs from %s failed\n", step->slurmHosts);
	return ESLURMD_INVALID_JOB_CREDENTIAL;
    }

    if (count != step->nrOfNodes) {
	flog("mismatching number of nodes %u vs %u for %s\n",
	     count, step->nrOfNodes, Step_strID(step));
	step->nrOfNodes = count;
	return ESLURMD_INVALID_JOB_CREDENTIAL;
    }

    step->localNodeId = getLocalID(step->nodes, step->nrOfNodes);
    if (step->localNodeId == NO_VAL) {
	flog("local node ID %i for %s in %s num nodes %i not found\n",
	     PSC_getMyID(), Step_strID(step), step->slurmHosts, step->nrOfNodes);
	return ESLURMD_INVALID_JOB_CREDENTIAL;
    }

    JobCred_t *cred = step->cred;
    step->credID = getLocalID(cred->jobNodes, cred->jobNumHosts);
    if (step->credID == NO_VAL) {
	flog("credentail node ID %i for %s in %s num nodes %i not found\n",
	     PSC_getMyID(), Step_strID(step), cred->jobHostlist,
	     cred->jobNumHosts);
	return ESLURMD_INVALID_JOB_CREDENTIAL;
    }

    /* set memory limits from credential */
    setStepMemLimits(step);

    step->nodeinfos = getStepNodeinfoArray(step);
    if (!step->nodeinfos) {
	flog("failed to fill nodeinfos of step %s\n", Step_strID(step));
	return ESLURMD_INVALID_JOB_CREDENTIAL;
    }

    /* pack info */
    if (step->packNrOfNodes != NO_VAL) {
	if (!extractStepPackInfos(step)) {
	    flog("extracting pack information failed\n");
	    return ESLURMD_INVALID_JOB_CREDENTIAL;
	}
    }

    /* determine my role in the step */
    if (step->packStepCount > 1) {
	/* step with pack */
	if (step->packNodes[0] == PSC_getMyID()) step->leader = true;
    } else {
	if (step->nodes[0] == PSC_getMyID()) step->leader = true;
    }

    flog("%s user '%s' np %u nodes '%s' N %u tpp %u pack's step count %u"
	 " leader %i exe '%s' packJobid %u hetComp %u\n", Step_strID(step),
	 step->username, step->np, step->slurmHosts, step->nrOfNodes, step->tpp,
	 step->packStepCount, step->leader, strvGet(step->argV, 0),
	 step->packJobid == NO_VAL ? 0 : step->packJobid,
	 step->stepHetComp == NO_VAL ? 0 : step->stepHetComp);

    /* ensure an allocation exists for the new step */
    Alloc_t *alloc = Alloc_find(step->jobid);
    if (!alloc) {
	char buf[128];
	snprintf(buf, sizeof(buf), "error: no allocation for jobid %u found\n"
		 "** ensure slurmctld prologue is setup correctly **\n",
		 step->jobid);
	flog("%s", buf);
	/* special service for admins missing to call pspelogue in prologue */
	fwCMD_printMsg(NULL, step, buf, strlen(buf), STDERR, -1);
	step->termAfterFWmsg = ESLURMD_INVALID_JOB_CREDENTIAL;
    } else {
	/* ensure a proper cleanup is executed on termination */
	alloc->state = A_RUNNING;
    }

    /* set slots for the step (and number of hardware threads) */
    if (!setStepSlots(step)) {
	flog("setting hardware threads for %s failed\n", Step_strID(step));
	char msg[160];
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
	return ESLURMD_INVALID_JOB_CREDENTIAL;
    }

    int ret = SLURM_NO_RC;

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
	if (step->packStepCount == 1) {
	    if (!execStepLeader(step)) return ESLURMD_FORK_FAILED;
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
	    ret = SLURM_SUCCESS;
	}
    }

    if (step->packStepCount > 1 && step->nodes[0] == PSC_getMyID()) {
	/* forward hw thread infos to pack leader */
	/* if this is the pack leader execStepLeader() might be called
	 * in the handler */
	if (send_PS_PackInfo(step) == -1) return ESLURMD_INVALID_JOB_CREDENTIAL;
    }

    /* prevent step from automatic free */
    sMsg->unpData = NULL;

    return ret;
}

static bool doSignalTasks(Req_Signal_Tasks_t *req)
{
    if (req->flags & KILL_FULL_JOB) {
	/* send signal to complete job including all steps */
	flog("sending all processes of job %u signal %u\n",
	     req->jobid, req->signal);
	Job_signalJS(req->jobid, req->signal, req->uid);
	Step_signalByJobid(req->jobid, req->signal, req->uid);
    } else if (req->flags & KILL_STEPS_ONLY) {
	/* send signal to all steps excluding the jobscript */
	flog("send all steps of job %u signal %u\n", req->jobid, req->signal);
	Step_signalByJobid(req->jobid, req->signal, req->uid);
    } else {
	if (req->stepid == SLURM_BATCH_SCRIPT) {
	    /* signal jobscript only, not all corresponding steps */
	    return Job_signalJS(req->jobid, req->signal, req->uid);
	} else {
	    /* signal a single step */
	    Step_t s = { .jobid = req->jobid, .stepid = req->stepid };
	    flog("send %s (stepHetComp %u) signal %u\n", Step_strID(&s),
		 req->stepHetComp, req->signal);
	    Step_t *step = Step_findByStepId(req->jobid, req->stepid);
	    if (step && Step_signal(step, req->signal, req->uid) != -1) {
		return true;
	    }
	}

	/* we only return an error if we signal a specific jobscript/step */
	return false;
    }

    return true;
}

/**
 * @brief Send a SIGKILL for a request after a grace period
 */
static void sendDelayedKill(int timerId, void *data)
{
    Timer_remove(timerId);
    Req_Signal_Tasks_t *req = data;

    Step_t s = {
	.jobid = req->jobid,
	.stepid = req->stepid };
    flog("SIGKILL to %s uid %u\n", Step_strID(&s), req->uid);

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
 * or to a single jobscript. If the sender of the request has the correct
 * permissions is verified in the functions acutally sending the signal
 * (e.g. @ref Step_signal()).
 *
 * @param sMsg The message holding the request
 */
static int handleSignalTasks(Slurm_Msg_t *sMsg)
{
    Req_Signal_Tasks_t *req = sMsg->unpData;
    if (!req) {
	flog("unpacking request signal tasks failed\n");
	return ESLURM_INVALID_JOB_ID;
    }

    Step_t s = {
	.jobid = req->jobid,
	.stepid = req->stepid };
    flog("%s uid %i signal %i flags %i from %s\n", Step_strID(&s), req->uid,
	 req->signal, req->flags, strRemoteAddr(sMsg));

    req->uid = sMsg->head.uid;

    /* handle magic Slurm signals */
    switch (req->signal) {
    case SIG_TERM_KILL:
	doSendTermKill(req);
	return SLURM_SUCCESS;
    case SIG_UME:
    case SIG_REQUEUED:
    case SIG_PREEMPTED:
    case SIG_TIME_LIMIT:
    case SIG_ABORT:
    case SIG_NODE_FAIL:
    case SIG_FAILURE:
	flog("implement signal %u\n", req->signal);
	return SLURM_SUCCESS;
    }

    if (!doSignalTasks(req)) return ESLURM_INVALID_JOB_ID;

    return SLURM_SUCCESS;
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
	if (task->globalRank < 0) continue;
	countPIDS++;
	addUint32ToMsg(PSC_getPID(task->childTID), reply);
    }
    *(uint32_t *) (reply->buf + countPos) = htonl(countPIDS);

    /* executable names */
    for (uint32_t i = 0; i < numTasks; i++) {
	addStringToMsg(strvGet(step->argV, 0), reply);
    }

    sendSlurmReply(sMsg, RESPONSE_REATTACH_TASKS);
}

static int handleReattachTasks(Slurm_Msg_t *sMsg)
{
    Req_Reattach_Tasks_t *req = sMsg->unpData;
    if (!req) {
	flog("unpacking request reattach tasks failed\n");
	sendReattachFail(sMsg, ESLURM_INVALID_JOB_ID);
	return SLURM_NO_RC;
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
    } else if (!req->ioKey) {
	flog("invalid I/O key %s\n", Step_strID(step));
	sendReattachFail(sMsg, ESLURM_INVALID_JOB_CREDENTIAL);
    } else {
	/* send message to forwarder */
	fwCMD_reattachTasks(step->fwdata, sMsg->head.addr.ip,
			    req->ioPorts[step->localNodeId % req->numIOports],
			    req->ctlPorts[step->localNodeId % req->numCtlPorts],
			    req->ioKey);
	sendReattchReply(step, sMsg);
    }

    return SLURM_NO_RC;
}

static int handleSuspendInt(Slurm_Msg_t *sMsg)
{
    Req_Suspend_Int_t *req = sMsg->unpData;
    if (!req) {
	flog("unpacking request suspend int failed\n");
	return ESLURM_NOT_SUPPORTED;
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
	return ESLURM_NOT_SUPPORTED;
    }
    return SLURM_SUCCESS;
}

/**
 * @brief Handle a shutdown request
 *
 * Only psslurm and its dependent plugins will be unloaded.
 * The psid itself will *not* be terminated.
 *
 * @param sMsg The Slurm message to handle
 */
static int handleShutdown(Slurm_Msg_t *sMsg)
{
    /* check permissions */
    if (!checkPrivMsg(sMsg)) return ESLURM_ACCESS_DENIED;

    sendSlurmRC(sMsg, SLURM_SUCCESS);

    mlog("%s: shutdown on request from uid %i\n", __func__, sMsg->head.uid);
    PSIDplugin_finalize("psslurm");

    return SLURM_NO_RC;
}

static int handleReconfigure(Slurm_Msg_t *sMsg)
{
    /* check permissions */
    if (!checkPrivMsg(sMsg)) return ESLURM_ACCESS_DENIED;

    /* activate the new configuration */
    updateSlurmConf();

    /* send new configuration hash to slurmctld */
    sendNodeRegStatus(false);

    /* no response is expected */
    return SLURM_NO_RC;
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

    /* new Slurm configuration format since Slurm 21.08 */
    for (uint32_t i = 0; i < config->numFiles; i++) {
	Config_File_t *file = &config->files[i];

	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/%s", confDir, file->name);

	if (!file->create) {
	    /* file should be deleted if possible */
	    unlink(path);
	    continue;
	}

	/* skip empty files */
	if (!file->data || strlen(file->data) < 1) continue;

	if (!writeFile(file->name, confDir, file->data, strlen(file->data))) {
	    return false;
	}
	mode_t mode = file->executable ? 0755 : 0644;
	if (chmod(path, mode ) == -1) {
	    fwarn(errno, "chmod(%s, %o)", path, mode);
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
static int handleConfig(Slurm_Msg_t *sMsg)
{
    Config_Msg_t *config = sMsg->unpData;
    if (!config) {
	flog("unpacking new configuration failed\n");
	return SLURM_NO_RC;
    }

    /* check permissions */
    if (!checkPrivMsg(sMsg)) return SLURM_NO_RC;

    /* update all configuration files */
    char *confDir = getConfValueC(Config, "SLURM_CONF_CACHE");
    flog("updating Slurm configuration files in %s\n", confDir);

    bool ret = writeSlurmConfigFiles(config, confDir);

    if (!ret) {
	flog("failed to write Slurm configuration files to %s\n", confDir);
	return SLURM_NO_RC;
    }

    /* activate the new configuration */
    updateSlurmConf();

    /* send new configuration hash to slurmctld */
    sendNodeRegStatus(false);

    /* slurmctld does not require an answer for this RPC and silently ignores
     * any responses */
    return SLURM_NO_RC;
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
 * @param info Extra information pointing to the command string (to be
 * free()ed)
 *
 * @return No return value
 */
static void cbRebootProgram(int exit, bool tmdOut, int iofd, void *info)
{
    char errMsg[1024];
    size_t errLen = 0;
    getScriptCBdata(iofd, errMsg, sizeof(errMsg), &errLen);

    char *cmdStr = info;
    flog("'%s' returned exit status %i\n", cmdStr ? cmdStr : "reboot program",
	 exit);

    if (errMsg[0] != '\0') flog("reboot error message: %s\n", errMsg);

    free(cmdStr);

    char *shutdown = getConfValueC(Config, "SLURMD_SHUTDOWN_ON_REBOOT");
    if (shutdown) {
	flog("unloading psslurm after reboot program\n");
	PSIDplugin_finalize("psslurm");
    }
}

static int handleRebootNodes(Slurm_Msg_t *sMsg)
{
    Req_Reboot_Nodes_t *req = sMsg->unpData;
    if (!req) {
	flog("unpacking node reboot request failed\n");
	return SLURM_NO_RC;
    }

    char *prog = getConfValueC(SlurmConfig, "RebootProgram");
    if (!prog) {
	flog("error: RebootProgram is not set in slurm.conf\n");
    } else if (checkPrivMsg(sMsg)) {
	/* try to execute reboot program */
	strbuf_t cmd = strbufNew(trim(prog));

	if (access(strbufStr(cmd), R_OK | X_OK) < 0) {
	    flog("invalid permissions for reboot program %s\n", strbufStr(cmd));
	    strbufDestroy(cmd);
	} else {
	    if (req->features && req->features[0]) {
		strbufAdd(cmd, " ");
		strbufAdd(cmd, req->features);
	    }
	    char *cmdStr = strbufSteal(cmd);
	    flog("calling reboot program '%s'\n", cmdStr);
	    PSID_execScript(cmdStr, NULL, &cbRebootProgram, NULL, cmdStr);
	}
    }

    /* slurmctld does not require an answer for this RPC and silently ignores
     * any responses */
    return SLURM_NO_RC;
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
static int handleHealthCheck(Slurm_Msg_t *sMsg)
{
    /* check permissions */
    if (!checkPrivMsg(sMsg)) return ESLURM_ACCESS_DENIED;

    runHealthCheck();
    return SLURM_SUCCESS;
}

static int handleAcctGatherUpdate(Slurm_Msg_t *sMsg)
{
    PS_SendDB_t *msg = &sMsg->reply;

    /* check permissions */
    if (!checkPrivMsg(sMsg)) return ESLURM_ACCESS_DENIED;

    /* pack energy data */
    psAccountInfo_t info;
    psAccountGetLocalInfo(&info);

    /* we need at least 1 sensor to prevent segfaults in slurmctld */
    packEnergySensor(msg, &info.energy);

    sendSlurmReply(sMsg, RESPONSE_ACCT_GATHER_UPDATE);
    return SLURM_NO_RC;
}

static int handleAcctGatherEnergy(Slurm_Msg_t *sMsg)
{
    PS_SendDB_t *msg = &sMsg->reply;

    /* check permissions */
    if (!checkPrivMsg(sMsg)) return ESLURM_ACCESS_DENIED;

    /* pack energy data */
    psAccountInfo_t info;
    psAccountGetLocalInfo(&info);

    /* we need at least 1 sensor to prevent segfaults in slurmctld */
    packEnergySensor(msg, &info.energy);

    sendSlurmReply(sMsg, RESPONSE_ACCT_GATHER_ENERGY);
    return SLURM_NO_RC;
}

/**
 * @brief Find a step by its task (mpiexec) pid
 *
 * @param sMsg The request message
 */
static int handleJobId(Slurm_Msg_t *sMsg)
{
    Req_Job_ID_t *req = sMsg->unpData;
    if (!req) {
	flog("unpacking job ID request failed\n");
	return ESLURM_INVALID_JOB_ID;
    }

    Step_t *step = Step_findByPsidTask(req->pid);
    if (!step) return ESLURM_INVALID_JOB_ID;

    PS_SendDB_t *msg = &sMsg->reply;

    addUint32ToMsg(step->jobid, msg);
    addUint32ToMsg(SLURM_SUCCESS, msg);

    sendSlurmReply(sMsg, RESPONSE_JOB_ID);
    return SLURM_NO_RC;
}

static int handleFileBCast(Slurm_Msg_t *sMsg)
{
    BCast_t *bcast = sMsg->unpData;
    if (!bcast) {
	flog("unpacking file bcast request failed\n");
	return ESLURM_INVALID_JOB_ID;
    }
    bcast->msg.sock = sMsg->sock;
    /* set correct uid for response message */
    bcast->msg.head.uid = sMsg->head.uid;

    /* unpack credential */
    if (!BCast_extractCred(sMsg, bcast)) {
	mlog("%s: extracting bcast credential failed\n", __func__);
	if (!errno) {
	    return ESLURM_AUTH_CRED_INVALID;
	} else {
	    return errno;
	}
    }

    if (bcast->compress) {
	flog("implement compression for bcast\n");
	return SLURM_ERROR;
    }

    /* assign to job/allocation */
    Job_t *job = Job_findById(bcast->jobid);
    if (!job) {
	Alloc_t *alloc = Alloc_find(bcast->jobid);
	if (!alloc) {
	    flog("allocation %u not found\n", bcast->jobid);
	    return ESLURM_INVALID_JOB_ID;
	}
	bcast->uid = alloc->uid;
	bcast->gid = alloc->gid;
	bcast->env = alloc->env;
	ufree(bcast->username);
	bcast->username = ustrdup(alloc->username);
	Step_t *step = Step_findByJobid(bcast->jobid);
	if (step) PSCPU_copy(bcast->hwthreads,
			     step->nodeinfos[step->localNodeId].stepHWthreads);
    } else {
	bcast->uid = job->uid;
	bcast->gid = job->gid;
	bcast->env = job->env;
	PSCPU_copy(bcast->hwthreads, job->hwthreads);
	ufree(bcast->username);
	bcast->username = ustrdup(job->username);
    }

    if (Auth_isDeniedUID(bcast->uid)) {
	flog("denied UID %u to start bcast\n", sMsg->head.uid);
	return ESLURM_USER_ID_MISSING;
    }

    fdbg(PSSLURM_LOG_PROTO, "jobid %u blockNum %u lastBlock %u force %u"
	 " modes %u, user '%s' uid %u gid %u fileName '%s' blockLen %u\n",
	 bcast->jobid, bcast->blockNumber,
	 (bcast->flags & BCAST_LAST_BLOCK) ? 1 : 0,
	 (bcast->flags & BCAST_FORCE) ? 1 : 0,
	 bcast->modes, bcast->username, bcast->uid, bcast->gid,
	 bcast->fileName, bcast->blockLen);
    if (mset(PSSLURM_LOG_PROTO)) {
	printBinaryData(bcast->block, bcast->blockLen, "bcast->block");
    }

    if (bcast->blockNumber == 1) {
	mlog("%s: jobid %u file '%s' user '%s'\n", __func__, bcast->jobid,
		bcast->fileName, bcast->username);
    }

    /* start forwarder to write the file */
    if (!execBCast(bcast)) return ESLURMD_FORK_FAILED;

    /* prevent bcast from automatic free */
    sMsg->unpData = NULL;

    return SLURM_NO_RC;
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

static int handleStepStat(Slurm_Msg_t *sMsg)
{
    Slurm_Step_Head_t *head = sMsg->unpData;
    if (!head) {
	flog("unpacking step stat request failed\n");
	return ESLURM_INVALID_JOB_ID;
    }

    Step_t *step = Step_findByStepId(head->jobid, head->stepid);
    if (!step) {
	Step_t s = {
	    .jobid = head->jobid,
	    .stepid = head->stepid };
	flog("%s to signal not found\n", Step_strID(&s));
	return ESLURM_INVALID_JOB_ID;
    }

    /* check permissions */
    if (!verifyUserId(sMsg->head.uid, step->uid)) {
	bool verified = false;
	for (uint16_t i=0; i< numSstatUIDs; i++) {
	    if (sstatUIDs[i] == sMsg->head.uid) {
		verified = true;
		break;
	    }
	}
	if (!verified) {
	    flog("request from invalid user %u\n", sMsg->head.uid);
	    return ESLURM_USER_ID_MISSING;
	}
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
	.packTaskOffset = step->packTaskOffset,
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
    return SLURM_NO_RC;
}

static int handleStepPids(Slurm_Msg_t *sMsg)
{
    Slurm_Step_Head_t *head = sMsg->unpData;
    if (!head) {
	flog("unpacking step pids request failed\n");
	return ESLURM_INVALID_JOB_ID;
    }

    Step_t *step = Step_findByStepId(head->jobid, head->stepid);
    if (!step) {
	Step_t s = {
	    .jobid = head->jobid,
	    .stepid = head->stepid };
	flog("%s to signal not found\n", Step_strID(&s));
	return ESLURM_INVALID_JOB_ID;
    }

    /* check permissions */
    if (!verifyUserId(sMsg->head.uid, step->uid)) {
	flog("request from invalid user %u\n", sMsg->head.uid);
	return ESLURM_USER_ID_MISSING;
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
    return SLURM_NO_RC;
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
static int handleDaemonStatus(Slurm_Msg_t *sMsg)
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
    return SLURM_NO_RC;
}

static int handleJobNotify(Slurm_Msg_t *sMsg)
{
    Req_Job_Notify_t *req = sMsg->unpData;
    if (!req) {
	flog("unpacking job notify request failed\n");
	return ESLURM_INVALID_JOB_ID;
    }

    Job_t *job = Job_findById(req->jobid);
    Step_t *step = Step_findByStepId(req->jobid, req->stepid);

    if (!job && !step) {
	flog("job/step %u.%u to notify not found, msg %s\n", req->jobid,
	     req->stepid, req->msg);
	return ESLURM_INVALID_JOB_ID;
    }

    /* check permissions */
    if (!verifyUserId(sMsg->head.uid, job ? job->uid : step->uid)) {
	flog("request from invalid user %u for job %u stepid %u\n",
	     sMsg->head.uid, req->jobid, req->stepid);
	return ESLURM_USER_ID_MISSING;
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

    return SLURM_SUCCESS;
}

static int handleForwardData(Slurm_Msg_t *sMsg)
{
    /* check permissions */
    if (!checkPrivMsg(sMsg)) return ESLURM_ACCESS_DENIED;

    flog("implement me!\n");
    return ESLURM_NOT_SUPPORTED;
}

static void sendJobKill(Req_Info_t *req, uint16_t signal)
{
    Req_Job_Kill_t kill = {
	.sluid = req->sluid,
	.jobid = req->jobid,
	.stepid = req->stepid,
	.stepHetComp = req->stepHetComp,
	.signal = signal,
	.flags = 0,
	.sibling = NULL
    };

    /* send request to slurmctld */
    Req_Info_t *reqInfo = ucalloc(sizeof(*reqInfo));
    reqInfo->type = REQUEST_KILL_JOB;
    reqInfo->jobid = req->jobid;

    sendSlurmctldReq(reqInfo, &kill);
}

static int handleRespJobRequeue(Slurm_Msg_t *sMsg, void *info)
{
    Req_Info_t *req = info;
    uint32_t rc;
    getUint32(sMsg->data, &rc);

    if (rc == ESLURM_DISABLED || rc == ESLURM_BATCH_ONLY) {
	flog("cancel job %u\n", req->jobid);
	sendJobKill(req, SIGKILL);
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

static int handleLaunchProlog(Slurm_Msg_t *sMsg)
{
    Req_Launch_Prolog_t *req = sMsg->unpData;
    if (!req) {
	flog("unpacking launch prolog request failed\n");
	return ESLURM_INVALID_JOB_ID;
    }

    /* check permissions */
    if (!checkPrivMsg(sMsg)) return ESLURM_ACCESS_DENIED;

    fdbg(PSSLURM_LOG_PELOG, "jobid %u het-jobid %u uid %u gid %u alias %s"
	 " nodes=%s stdErr='%s' stdOut='%s' work-dir=%s user=%s\n",
	 req->jobid, req->hetJobid, req->uid, req->gid, req->aliasList,
	 req->nodes, req->stdErr, req->stdOut, req->workDir, req->userName);

    fdbg(PSSLURM_LOG_DEBUG, "x11 %u allocHost='%s' allocPort %u cookie='%s'"
	 "target=%s targetPort %u\n", req->x11, req->x11AllocHost,
	 req->x11AllocPort, req->x11MagicCookie, req->x11Target,
	 req->x11TargetPort);

    /* spank env */
    int cnt = 0;
    for (char **e = envGetArray(req->spankEnv); e && *e; e++, cnt++) {
	fdbg(PSSLURM_LOG_PELOG, "spankEnv%i: '%s'\n", cnt, *e);
    }

    /* add an allocation if slurmctld prologue did not */
    Alloc_t *alloc = Alloc_find(req->jobid);
    if (!alloc && req->hetJobid != 0 && req->hetJobid != NO_VAL) {
	alloc = Alloc_findByPackID(req->hetJobid);
    }
    if (!alloc) {
	alloc = Alloc_add(req->jobid, req->hetJobid, req->nodes, req->spankEnv,
			 req->uid, req->gid, req->userName);
    } else {
	envAppend(alloc->env, req->spankEnv, envFilterFunc);
    }

    /* move job credential and GRes to allocation */
    alloc->cred = req->cred;
    req->cred = NULL;
    list_splice(&req->gresList, &alloc->gresList);
    INIT_LIST_HEAD(&req->gresList);

    /* mark allocation as running or it might get deleted
     * by @ref cleanupStaleAllocs() */
    alloc->state = A_RUNNING;

    /* set mask of hardware threads to use */
    nodeinfo_t *nodeinfo = getNodeinfo(PSC_getMyID(), alloc->cred, alloc->id);
    if (!nodeinfo) {
	flog("could not extract nodeinfo from credentials for alloc %u\n",
	     alloc->id);
	sendPrologComplete(req->jobid, SLURM_ERROR);
	return SLURM_SUCCESS;
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
    return SLURM_SUCCESS;
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
    int cnt = 0;
    for (char **e = envGetArray(job->env); e && *e; e++, cnt++) {
	fdbg(PSSLURM_LOG_ENV, "env%i: '%s'\n", cnt, *e);
    }

    /* spank env */
    cnt = 0;
    for (char **e = envGetArray(job->spankenv); e && *e; e++, cnt++) {
	fdbg(PSSLURM_LOG_ENV, "spankenv%i: '%s'\n", cnt, *e);
    }
}

static int handleBatchJobLaunch(Slurm_Msg_t *sMsg)
{
    Job_t *job = sMsg->unpData;
    if (!job) {
	flog("unpacking batch job launch request failed\n");
	return ESLURMD_INVALID_JOB_CREDENTIAL;
    }

    /* don't accept new jobs if a shutdown is in progress */
    if (pluginShutdown) return SLURM_ERROR;

    if (Auth_isDeniedUID(job->uid)) {
	flog("denied UID %u to start job %u\n", sMsg->head.uid, job->jobid);
	return ESLURM_USER_ID_MISSING;
    }

    /* memory cleanup  */
    malloc_trim(200);

    /* convert slurm hostlist to PSnodes   */
    if (!convHLtoPSnodes(job->slurmHosts, getNodeIDbySlurmHost,
			 &job->nodes, &job->nrOfNodes)) {
	flog("resolving PS nodeIDs from %s for job %u failed\n",
	     job->slurmHosts, job->jobid);
	return ESLURMD_INVALID_JOB_CREDENTIAL;
    }

    job->localNodeId = getLocalID(job->nodes, job->nrOfNodes);
    if (job->localNodeId == NO_VAL) {
	flog("could not find my local ID for job %u in %s\n",
	     job->jobid, job->slurmHosts);
	return ESLURMD_INVALID_JOB_CREDENTIAL;
    }

    /* verify job credential */
    if (!Job_verifyData(job)) return ESLURMD_INVALID_JOB_CREDENTIAL;
    job->state = JOB_QUEUED;

    printJobLaunchInfos(job);

    /* set accounting options */
    setAccOpts(job->acctFreq, &job->accType);

    /* set mask of hardware threads to use */
    nodeinfo_t *nodeinfo = getNodeinfo(PSC_getMyID(), job->cred, job->jobid);
    if (!nodeinfo) {
	flog("could not extract nodeinfo from credentials for job %u\n",
	     job->jobid);
	return ESLURMD_INVALID_JOB_CREDENTIAL;
    }
    PSCPU_copy(job->hwthreads, nodeinfo->jobHWthreads);
    ufree(nodeinfo);

    job->hostname = ustrdup(getConfValueC(Config, "SLURM_HOSTNAME"));

    /* write the jobscript */
    if (!writeJobscript(job)) {
	/* set myself offline and requeue the job */
	setNodeOffline(job->env, job->jobid,
		       getConfValueC(Config, "SLURM_HOSTNAME"),
			"psslurm: writing jobscript failed");
	/* need to return success to be able to requeue the job */
	return SLURM_SUCCESS;
    }

    flog("job %u user '%s' np %u nodes '%s' N %u tpp %u script '%s'\n",
	 job->jobid, job->username, job->np, job->slurmHosts, job->nrOfNodes,
	 job->tpp, job->jobscript);

    /* sanity check nrOfNodes */
    if (job->nrOfNodes > (uint16_t) PSC_getNrOfNodes()) {
	mlog("%s: invalid nrOfNodes %u known Nodes %u\n", __func__,
	     job->nrOfNodes, PSC_getNrOfNodes());
	return ESLURMD_INVALID_JOB_CREDENTIAL;
    }

    /* forward job info to other nodes in the job */
    send_PS_JobLaunch(job);

    /* setup job environment */
    initJobEnv(job);

    /* container */
    if (job->containerBundle && job->containerBundle[0] != '\0') {
	job->ct = Container_new(job->containerBundle, job->jobid,
				 SLURM_BATCH_SCRIPT, job->username,
				 job->uid, job->gid);
	if (!job->ct) {
	    flog("error: failed to initialize container %s job %u\n",
		 job->containerBundle, job->jobid);
	    return ESLURM_CONTAINER_NOT_CONFIGURED;
	}
    }

    Alloc_t *alloc = Alloc_find(job->jobid);
    bool ret = false;
    char *prologue = getConfValueC(SlurmConfig, "Prolog");
    /* force slurmctld prologue for now */
    if (!prologue || prologue[0] == '\0' || true) {
	/* no slurmd prologue configured,
	 * pspelogue should have added an allocation */
	if (!alloc) {
	    flog("error: no allocation for job %u found\n", job->jobid);
	    flog("** ensure slurmctld prologue is setup correctly **\n");
	    return ESLURMD_INVALID_JOB_CREDENTIAL;
	}

	/* sanity check allocation state */
	if (alloc->state != A_PROLOGUE_FINISH &&
	    alloc->state != A_RUNNING) {
	    flog("allocation %u in invalid state %s\n", alloc->id,
		 Alloc_strState(alloc->state));
	    return ESLURMD_INVALID_JOB_CREDENTIAL;
	}

	/* pspelogue already ran parallel prologue, start job */
	alloc->state = A_RUNNING;
	ret = execBatchJob(job);
	fdbg(PSSLURM_LOG_JOB, "job %u in %s\n", job->jobid,
	     Job_strState(job->state));
    } else {
	/* slurmd prologue is configured */
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
	/* prevent job from automatic free */
	sMsg->unpData = NULL;

	return SLURM_SUCCESS;
    }

    return ESLURMD_INVALID_JOB_CREDENTIAL;
}

static void doTerminateAlloc(Slurm_Msg_t *sMsg, Alloc_t *alloc)
{
    int grace = getConfValueI(SlurmConfig, "KillWait");
    int maxTermReq = getConfValueI(Config, "MAX_TERM_REQUESTS");
    int signal = SIGTERM, pcount;

    if (!alloc->firstKillReq) alloc->firstKillReq = time(NULL);
    if (time(NULL) - alloc->firstKillReq > grace + 10) {
	/* grace time is over, use SIGKILL from now on */
	flog("sending SIGKILL to fowarders of allocation %u\n", alloc->id);
	Job_killForwarder(alloc->id);
	signal = SIGKILL;
    }

    if (maxTermReq > 0 && alloc->terminate++ >= maxTermReq) {
	/* ensure jobs will not get stuck forever */
	flog("force termination of allocation %u in state %s requests %i\n",
	     alloc->id, Alloc_strState(alloc->state), alloc->terminate);
	uint32_t allocID = alloc->id;
	Alloc_delete(alloc);
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
	    Alloc_delete(alloc);
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
	Alloc_delete(alloc);
	sendSlurmRC(sMsg, SLURM_SUCCESS);
	return;
    }

    /* job/steps already finished, start epilogue now */
    sendSlurmRC(sMsg, SLURM_SUCCESS);
    flog("starting epilogue for allocation %u state %s\n", alloc->id,
	 Alloc_strState(alloc->state));
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
	if (alloc) {
	    Alloc_delete(alloc);
	} else {
	    /* just in case there are trailing steps or bcasts @todo required? */
	    Step_destroyByJobid(jobid);
	    BCast_clearByJobid(jobid);
	}
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
static int handleTerminateReq(Slurm_Msg_t *sMsg)
{
    Req_Terminate_Job_t *req = sMsg->unpData;
    if (!req) {
	flog("unpacking terminate request failed\n");
	return ESLURM_USER_ID_MISSING;
    }

    Step_t s = {
	.jobid = req->jobid,
	.stepid = req->stepid };

    /* check permissions */
    if (!checkPrivMsg(sMsg)) return ESLURM_ACCESS_DENIED;

    flog("%s Slurm-state %u uid %u type %s from %s\n", Step_strID(&s),
	 req->jobstate, sMsg->head.uid, msgType2String(sMsg->head.type),
	 strRemoteAddr(sMsg));

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
	    return ESLURMD_KILL_JOB_ALREADY_COMPLETE;
	} else {
	    sendEpilogueComplete(req->jobid, SLURM_SUCCESS);
	    return SLURM_SUCCESS;
	}
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
	flog("unknown terminate request for %s\n", Step_strID(&s));
	return ESLURMD_KILL_JOB_ALREADY_COMPLETE;
    }

    return SLURM_NO_RC;
}

static int handleNetworkCallerID(Slurm_Msg_t *sMsg)
{
    /* check permissions */
    if (!checkPrivMsg(sMsg)) return ESLURM_ACCESS_DENIED;

    /* IN: */
    /* ip source */
    /* ip dest */
    /* port source */
    /* port dest */
    /* af */

    flog("implement me!\n");
    return ESLURM_NOT_SUPPORTED;

    /* OUT: */
    /* job id */
    /* return code */
    /* node name */
}

/**
 * @brief Handle response to node registration request
 *
 * Currently used to receive TRes accounting fields from slurmctld
 *
 * @param sMsg The Slurm message to handle
 */
static int handleRespNodeReg(Slurm_Msg_t *sMsg)
{
    /* check permissions */
    if (!checkPrivMsg(sMsg)) return SLURM_NO_RC;

    /* don't request the additional info again */
    needNodeRegResp = false;

    tresDBconfig = sMsg->unpData;
    if (!tresDBconfig) {
	flog("unpacking response node reg failed\n");
	return SLURM_NO_RC;
    }

    /* don't free the data after this function */
    sMsg->unpData = NULL;

    for (uint32_t i = 0; i < tresDBconfig->count; i++) {
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
    return SLURM_NO_RC;
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
    struct timeval time_start, time_now, time_diff;

    /* no forwarding active for this message? */
    if (!sMsg->head.forward) return true;

    /* convert nodelist to PS nodes */
    if (!convHLtoPSnodes(fw->head.fwNodeList, getNodeIDbySlurmHost,
			 &nodes, &nrOfNodes)) {
	flog("resolving PS nodeIDs from %s failed\n", fw->head.fwNodeList);
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
	fw->head.fwRes[i].body = PSdbNew(NULL, 0);
    }

    bool verbose = mset(PSSLURM_LOG_FWD);
    if (verbose) {
	gettimeofday(&time_start, NULL);

	flog("forward type %s count %u nodelist %s timeout %u at %.4f\n",
	     msgType2String(sMsg->head.type), sMsg->head.forward,
	     fw->head.fwNodeList, fw->head.fwTimeout,
	     time_start.tv_sec + 1e-6 * time_start.tv_usec);
    }

    /* use RDP to send the message to other nodes */
    int ret = forwardSlurmMsg(sMsg, nrOfNodes, nodes);

    if (verbose) {
	gettimeofday(&time_now, NULL);
	timersub(&time_now, &time_start, &time_diff);
	flog("forward type %s of size %u took %.4f seconds\n",
	     msgType2String(sMsg->head.type), ret,
	     time_diff.tv_sec + 1e-6 * time_diff.tv_usec);

    }

    return true;
}

void processSlurmMsg(Slurm_Msg_t *sMsg, Msg_Forward_t *fw, Connection_CB_t *cb,
		     void *info)
{
    /* extract Slurm message header */
    if (!unpackSlurmHeader(sMsg, fw)) {
	sendSlurmRC(sMsg, SLURM_ERROR);
	return;
    }

    /* forward the message using RDP */
    if (fw && !slurmTreeForward(sMsg, fw)) {
	sendSlurmRC(sMsg, SLURM_ERROR);
	return;
    }

    fdbg(PSSLURM_LOG_PROTO, "msg(%i): %s, version %u from %s\n",
	    sMsg->head.type, msgType2String(sMsg->head.type),
	    sMsg->head.version, strRemoteAddr(sMsg));

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
	flog("extracting slurm auth for msg(%i): %s, version %u from %s failed,"
	     " message dropped\n", sMsg->head.type,
	     msgType2String(sMsg->head.type), sMsg->head.version,
	     strRemoteAddr(sMsg));
	sendSlurmRC(sMsg, ESLURM_AUTH_CRED_INVALID);
	return;
    }

    /* let the callback handle the message */
    cb(sMsg, info);
}

static int handleNodeRegStat(Slurm_Msg_t *sMsg)
{
    sendPing(sMsg);
    sendNodeRegStatus(false);
    return SLURM_NO_RC;
}

static int handleInvalid(Slurm_Msg_t *sMsg)
{
    flog("got invalid %s (%i) request\n", msgType2String(sMsg->head.type),
	 sMsg->head.type);

    if (sMsg->head.type == MESSAGE_COMPOSITE) return ESLURM_NOT_SUPPORTED;

    return SLURM_ERROR;
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
	if (msgHandler->msgType != sMsg->head.type) continue;

	bool measure = measureRPC;
	if (measure) {
	    gettimeofday(&time_start, NULL);
	    flog("exec RPC %s at %.4f\n", msgType2String(msgHandler->msgType),
		 time_start.tv_sec + 1e-6 * time_start.tv_usec);
	}

	if (!msgHandler->handler) {
	    flog("error: no handler for %s\n", msgType2String(sMsg->head.type));
	    return 0;
	}

	/* unpack request */
	if (!unpackSlurmMsg(sMsg)) {
	    flog("unpacking message %s (%u) failed\n",
		 msgType2String(sMsg->head.type), sMsg->head.version);
	    sendSlurmRC(sMsg, SLURM_COMMUNICATIONS_RECEIVE_ERROR);
	    return 0;
	}

	/* execute the RPC using the message handler */
	int ret = msgHandler->handler(sMsg);

	/* ensure message type is not overwritten by handler */
	sMsg->head.type = msgHandler->msgType;

	/* free unpacked data */
	freeUnpackMsgData(sMsg);

	/* send optional Slurm response */
	if (ret != SLURM_NO_RC) sendSlurmRC(sMsg, ret);

	if (measure) {
	    gettimeofday(&time_now, NULL);
	    timersub(&time_now, &time_start, &time_diff);
	    flog("exec RPC %s took %.4f seconds\n",
		 msgType2String(msgHandler->msgType),
		 time_diff.tv_sec + 1e-6 * time_diff.tv_usec);
	}
	return 0;
    }

    flog("error: got unregistred RPC %s (%u)\n",
	 msgType2String(sMsg->head.type), sMsg->head.version);

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
	    flog("If a configless setup is in use ensure SLURM_CONF_SERVER "
		 "is correct in psslurm.conf\n");
	    return false;
	}
	slurmVerStr = ustrdup(autoVer);
	pver = autoVer;
    }

    if (!strncmp(pver, "24.11", 5) || !strncmp(pver, "2411", 4)) {
	slurmProto = SLURM_24_11_PROTO_VERSION;
	slurmProtoStr = ustrdup("24.11");
    } else if (!strncmp(pver, "24.05", 5) || !strncmp(pver, "2405", 4)) {
	slurmProto = SLURM_24_05_PROTO_VERSION;
	slurmProtoStr = ustrdup("24.05");
    } else if (!strncmp(pver, "23.11", 5) || !strncmp(pver, "2311", 4)) {
	slurmProto = SLURM_23_11_PROTO_VERSION;
	slurmProtoStr = ustrdup("23.11");
    } else if (!strncmp(pver, "23.02", 5) || !strncmp(pver, "2302", 4)) {
	slurmProto = SLURM_23_02_PROTO_VERSION;
	slurmProtoStr = ustrdup("23.02");
    } else if (!strncmp(pver, "22.05", 5) || !strncmp(pver, "2205", 4)) {
	slurmProto = SLURM_22_05_PROTO_VERSION;
	slurmProtoStr = ustrdup("22.05");
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

    /* initialize privileged sstat users */
    const char *sstatUsers = getConfValueC(Config, "SSTAT_USERS");
    if (!sstatUsers || sstatUsers[0] == '\0') return true;

    if (!arrayFromUserList(sstatUsers, &sstatUIDs, &numSstatUIDs)) {
	flog("initialize of privileged sstat users failed\n");
	return false;
    }

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
    ufree(sstatUIDs);

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

    /* job and step infos (count, array (sluid/jobid/stepid/stepHetComp) */
    Job_getInfos(&stat);
    Step_getInfos(&stat);

    /* protocol version */
    stat.protoVersion = version;

    /* version string */
    snprintf(stat.verStr, sizeof(stat.verStr), "psslurm-%s-p%s",
	     PSC_getVersionStr(), slurmProtoStr);

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
    ufree(stat.infos);
    ufree(stat.dynamicConf);
    ufree(stat.dynamicFeat);
    /* currently unused */
    ufree(stat.extra);
    ufree(stat.cloudID);
    ufree(stat.cloudType);
}

int __sendSlurmReply(Slurm_Msg_t *sMsg, slurm_msg_type_t type,
		     const char *func, const int line)
{
    int ret = 1;

    /* save the new message type */
    uint16_t origType = sMsg->head.type;
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
    /* restore original message type to e.g. properly cleanup the unpData */
    sMsg->head.type = origType;

    return ret;
}

int __sendSlurmRC(Slurm_Msg_t *sMsg, uint32_t rc, const char *func,
		    const int line)
{
    PS_SendDB_t *body = &sMsg->reply;

    addUint32ToMsg(rc, body);

    int ret = __sendSlurmReply(sMsg, RESPONSE_SLURM_RC, func, line);

    if (!sMsg->head.forward) clearSlurmMsg(sMsg);
    if (ret < 1) flog("sending rc %u for %s:%u failed\n", rc, func, line);

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
	.packTaskOffset = step->packTaskOffset,
	.iBase = &step->acctBase };
    addSlurmAccData(&slurmAccData);

    Req_Step_Comp_t comp = {
	.sluid = step->sluid,
	.jobid = step->jobid,
	.stepid = step->stepid,
	.stepHetComp = step->stepHetComp,
	.firstNode = 0,
	.lastNode = step->nrOfNodes -1,
	.exitStatus = exitStatus,
	.sAccData = &slurmAccData };

    Req_Info_t *req = ucalloc(sizeof(*req));
    req->type = REQUEST_STEP_COMPLETE;
    req->sluid = step->sluid;
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
    uint32_t exitCount = 0;
    list_for_each(t, &step->tasks) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	if (task->sentExit || task->jobRank < 0) continue;
	if (task->exitCode != exitCode) continue;
	msg.taskRanks[exitCount++] = task->jobRank;
	task->sentExit = true;
    }

    if (msg.exitCount != exitCount) {
	flog("mismatching exit count %i:%i\n", msg.exitCount, exitCount);
	ufree(msg.taskRanks);
	return;
    }

    /* job/stepid */
    msg.sluid = step->sluid;
    msg.jobid = step->jobid;
    msg.stepid = step->stepid;
    msg.stepHetComp = step->stepHetComp;

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
	    /* no handle on global rank, thus globalRank = rank */
	    PS_Tasks_t *task = addTask(&step->tasks, NULL, -1, rank, rank);
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
    Resp_Launch_Tasks_t resp = { .sluid = step->sluid, .jobid = step->jobid,
				 .stepid = step->stepid };
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
	getUint32(sMsg->data, &rc);
	if (rc == ESLURM_CONFIGLESS_DISABLED) {
	    flog("error: configless is disabled in slurm.conf "
		 "(set SlurmctldParameters = enable_configless)\n");
	} else {
	    flog("configuration request error: reply %s rc %s sock %i\n",
		 msgType2String(sMsg->head.type), slurmRC2String(rc),
		 sMsg->sock);
	}
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

    /* free unpacked data (config) */
    freeUnpackMsgData(sMsg);

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
	} else if (!accomplishInit()) {
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
