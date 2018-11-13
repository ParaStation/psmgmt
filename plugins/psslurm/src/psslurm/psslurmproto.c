/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
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

#include "pshostlist.h"
#include "psserial.h"
#include "env.h"
#include "selector.h"

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

#include "psslurmproto.h"

#undef DEBUG_MSG_HEADER

/** Slurm protocol version */
uint32_t slurmProto;

/** Slurm protocol version string */
char *slurmProtoStr = NULL;

/** Flag to measure Slurm RPC execution times */
bool measureRPC = false;

typedef struct {
    uint32_t jobid;
    uint32_t stepid;
    bool timeout;
    uid_t uid;
} Kill_Info_t;

char *uid2String(uid_t uid)
{
    struct passwd *pwd = NULL;

    if (!uid) return ustrdup("root");

    while (!pwd) {
	errno = 0;
	pwd = getpwuid(uid);
	if (!pwd) {
	    if (errno == EINTR) continue;
	    mwarn(errno, "%s: getpwuid for %i failed\n", __func__, uid);
	    break;
	}
    }
    if (!pwd) return ustrdup("nobody");

    return ustrdup(pwd->pw_name);
}

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
    PS_SendDB_t msg = { .bufUsed = 0, .useFrag = false };
    Resp_Ping_t ping;
    uint32_t unused;

    getSysInfo(&ping.cpuload, &ping.freemem, &unused);

    packRespPing(&msg, &ping);
    sMsg->outdata = &msg;
    sendSlurmReply(sMsg, RESPONSE_PING_SLURMD);
}

uint32_t getLocalRankID(uint32_t rank, Step_t *step, uint32_t nodeId)
{
    uint32_t i;

    for (i=0; i<step->globalTaskIdsLen[nodeId]; i++) {
	if (step->globalTaskIds[nodeId][i] == rank) return i;
    }
    return -1;
}

bool getNodesFromSlurmHL(char *slurmHosts, uint32_t *nrOfNodes,
			 PSnodes_ID_t **nodes, uint32_t *localId)
{
    const char delimiters[] =", \n";
    char *next, *saveptr, *myHost;
    char compHL[1024], *hostlist;
    int i = 0;

    *localId = -1;
    if (!(hostlist = expandHostList(slurmHosts, nrOfNodes))||
	!*nrOfNodes) {
	mlog("%s: invalid hostlist '%s'\n", __func__, compHL);
	return false;
    }

    myHost = getConfValueC(&Config, "SLURM_HOSTNAME");

    *nodes = umalloc(sizeof(PSnodes_ID_t *) * *nrOfNodes +
	    sizeof(PSnodes_ID_t) * *nrOfNodes);

    next = strtok_r(hostlist, delimiters, &saveptr);

    while (next) {
	(*nodes)[i] = getNodeIDbySlurmHost(next);
	if ((*nodes)[i] == -1) {
	    mlog("%s: failed resolving hostname %s\n", __func__, next);
	    ufree(hostlist);
	    return false;
	}
	if (!strcmp(next, myHost)) *localId = i;
	i++;
	next = strtok_r(NULL, delimiters, &saveptr);
    }
    ufree(hostlist);
    return true;
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
	    PSIDnodes_setAcctPollI(PSC_getMyID(), freq);
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
	mdbg(PSSLURM_LOG_IO, "%s: stdOut '%s' stdOutRank %i stdOutOpt %i\n",
	     __func__, step->stdOut, step->stdOutRank, step->stdOutOpt);

	mdbg(PSSLURM_LOG_IO, "%s: stdErr '%s' stdErrRank %i stdErrOpt %i\n",
	     __func__, step->stdErr, step->stdErrRank, step->stdErrOpt);

	mdbg(PSSLURM_LOG_IO, "%s: stdIn '%s' stdInRank %i stdInOpt %i\n",
	     __func__, step->stdIn, step->stdInRank, step->stdInOpt);

	mdbg(PSSLURM_LOG_IO, "%s: bufferedIO '%i' labelIO '%i'\n", __func__,
	     step->taskFlags & LAUNCH_BUFFERED_IO,
	     step->taskFlags & LAUNCH_LABEL_IO);
    }

    /* job state */
    mdbg(PSSLURM_LOG_JOB, "%s: step %u:%u in %s\n", __func__,
	 step->jobid, step->stepid, strJobState(step->state));

    /* pinning */
    mdbg(PSSLURM_LOG_PART, "%s: cpuBindType 0x%hx, cpuBind '%s'\n", __func__,
	 step->cpuBindType, step->cpuBind);

    mdbg(PSSLURM_LOG_PART, "%s: memBindType 0x%hx, memBind '%s'\n", __func__,
	 step->memBindType, step->memBind);
}

static bool extractStepPackInfos(Step_t *step)
{
    uint32_t nrOfNodes, i, localid;

    mdbg(PSSLURM_LOG_PACK, "%s: packNodeOffset %u  packJobid %u packNtasks %u "
	 "packOffset %u packTaskOffset %u packHostlist '%s' packNrOfNodes %u\n",
	    __func__, step->packNodeOffset, step->packJobid, step->packNtasks,
	    step->packOffset, step->packTaskOffset, step->packHostlist,
	    step->packNrOfNodes);

    if (!getNodesFromSlurmHL(step->packHostlist, &nrOfNodes,
			     &step->packNodes, &localid)) {
	mlog("%s: resolving PS nodeIDs from %s failed\n", __func__,
	     step->packHostlist);
	return false;
    }

    if (step->packNrOfNodes != nrOfNodes) {
	mlog("%s extracting PS nodes from Slurm pack hostlist failed\n",
		__func__);
	return false;
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

static int testSlurmVersion(uint32_t version, uint32_t cmd)
{
    if (cmd == REQUEST_PING) return 1;

    if (version < SLURM_MIN_PROTO_VERSION ||
	version > SLURM_MAX_PROTO_VERSION) {
	mlog("%s: slurm protocol version %u not supported, cmd(%i) %s\n",
	     __func__, version, cmd, msgType2String(cmd));
	return 0;
    }
    return 1;
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
	flog("got error return code %u\n", rc);
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

static void handleLaunchTasks(Slurm_Msg_t *sMsg)
{
    Alloc_t *alloc = NULL;
    Job_t *job = NULL;
    Step_t *step = NULL;
    uint32_t count, i, id;

    if (pluginShutdown) {
	/* don't accept new steps if a shutdown is in progress */
	sendSlurmRC(sMsg, SLURM_ERROR);
	return;
    }

    /* unpack request */
    if (!(unpackReqLaunchTasks(sMsg, &step))) {
	mlog("%s: unpacking launch request (%u) failed\n", __func__,
	     sMsg->head.version);
	sendSlurmRC(sMsg, SLURM_ERROR);
	return;
    }

    /* verify job credential */
    if (!(verifyStepData(step))) {
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

    /* convert slurm hostlist to PSnodes   */
    if (!getNodesFromSlurmHL(step->slurmHosts, &count, &step->nodes,
			     &step->localNodeId)) {
	mlog("%s: resolving PS nodeIDs from %s failed\n", __func__,
	     step->slurmHosts);
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	goto ERROR;
    }

    if (count != step->nrOfNodes) {
	mlog("%s: mismatching number of nodes %u:%u for step %u:%u\n",
	     __func__, count, step->nrOfNodes, step->jobid, step->stepid);
	step->nrOfNodes = count;
    }

    /* pack info */
    if (step->packNrOfNodes != NO_VAL) {
	if (!extractStepPackInfos(step)) {
	    mlog("%s: extracting pack information failed\n", __func__);
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

    mlog("%s: step %u:%u user '%s' np %u nodes '%s' N %u tpp %u pack size %u"
	 " leader %i exe '%s' packJobid %u\n", __func__, step->jobid,
	 step->stepid, step->username, step->np, step->slurmHosts,
	 step->nrOfNodes, step->tpp, step->packSize, step->leader,
	 step->argv[0], step->packJobid == NO_VAL ? 0 : step->packJobid);

    /* add allocation */
    id = step->packJobid != NO_VAL ? step->packJobid : step->jobid;
    if (!(job = findJobById(id))) {
	if (!(alloc = findAlloc(step->jobid))) {
	    alloc = addAlloc(step->jobid, step->packJobid,
			     step->cred->jobHostlist, &step->env, step->uid,
			     step->gid, step->username);

	    /* first received step does *not* have to be step 0 */
	    alloc->state = A_PROLOGUE;
	}
    }

    /* set hardware threads */
    if (!(setHWthreads(step))) {
	mlog("%s: setting hardware threads for step %u:%u failed\n", __func__,
	     step->jobid, step->stepid);
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	goto ERROR;
    }

    /* sanity check nrOfNodes */
    if (step->nrOfNodes > (uint16_t) PSC_getNrOfNodes()) {
	mlog("%s: invalid nrOfNodes %u known Nodes %u for step %u:%u\n",
	     __func__, step->nrOfNodes, PSC_getNrOfNodes(), step->jobid,
	     step->stepid);
	sendSlurmRC(sMsg, ESLURMD_INVALID_JOB_CREDENTIAL);
	goto ERROR;
    }

    if (step->leader) {
	/* mother superior (pack leader) */
	if (step->packJobid != NO_VAL) {
	    /* allocate memory for pack infos from other mother superiors */
	    step->packInfo = ucalloc(sizeof(*step->packInfo) *
				      (step->packSize));
	}

	step->srunControlMsg.sock = sMsg->sock;
	step->srunControlMsg.head.forward = sMsg->head.forward;
	step->srunControlMsg.recvTime = sMsg->recvTime;

	if (!step->stepid && !job) {
	    if (alloc->state == A_PROLOGUE) {
		/* start prologue for steps without job */
		mdbg(PSSLURM_LOG_PELOG, "%s: starting prologue for %u:%u\n",
		     __func__, step->jobid, step->stepid);
		startPElogue(alloc, PELOGUE_PROLOGUE);
	    } else {
		/* job/pspelogue already ran parallel prologue */
		mdbg(PSSLURM_LOG_PELOG, "%s: starting step %u:%u\n",
		     __func__, step->jobid, step->stepid);
		step->state = JOB_PRESTART;
		if (step->packJobid == NO_VAL) {
		    if (!(execUserStep(step))) {
			sendSlurmRC(sMsg, ESLURMD_FORK_FAILED);
			goto ERROR;
		    }
		}
	    }
	    mdbg(PSSLURM_LOG_JOB, "%s: step %u:%u in '%s'\n", __func__,
		    step->jobid, step->stepid, strJobState(step->state));
	} else if (!job && alloc->state == A_PROLOGUE) {
	    /* prologue already running, wait till it is finished */
	    mlog("%s: step %u:%u waiting for prologue\n", __func__,
		 step->jobid, step->stepid);
	} else {
	    /* start mpiexec to spawn the parallel processes,
	     * intercept createPart call to overwrite the nodelist */
	    step->state = JOB_PRESTART;
	    mdbg(PSSLURM_LOG_JOB, "%s: step %u:%u in '%s'\n", __func__,
		    step->jobid, step->stepid, strJobState(step->state));
	    if (step->packJobid == NO_VAL) {
		if (!(execUserStep(step))) {
		    sendSlurmRC(sMsg, ESLURMD_FORK_FAILED);
		    goto ERROR;
		}
	    }
	}
    } else {
	/* sister node (pack follower) */
	if (job || step->stepid || alloc->state == A_PROLOGUE_FINISH) {
	    /* start I/O forwarder */
	    execStepFWIO(step);
	}
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
    if (alloc) deleteAlloc(alloc->id);
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

    if (req->flags & KILL_FULL_JOB) {
	/* send signal to complete job including all steps */
	mlog("%s: sending all processes of job %u signal %u\n", __func__,
	     req->jobid, req->signal);
	signalJobscript(req->jobid, req->signal, sMsg->head.uid);
	signalStepsByJobid(req->jobid, req->signal, sMsg->head.uid);
    } else if (req->flags & KILL_STEPS_ONLY) {
	/* send signal to all steps excluding the jobscript */
	mlog("%s: send steps %u signal %u\n", __func__,
	     req->jobid, req->signal);
	signalStepsByJobid(req->jobid, req->signal, sMsg->head.uid);
    } else {
	int ret = 0;

	if (req->stepid == SLURM_BATCH_SCRIPT) {
	    /* signal jobscript only, not all corresponding steps */
	    ret = signalJobscript(req->jobid, req->signal, sMsg->head.uid);
	} else {
	    /* signal a single step */
	    mlog("%s: sending step %u:%u signal %u\n", __func__, req->jobid,
		 req->stepid, req->signal);
	    Step_t *step = findStepByStepId(req->jobid, req->stepid);
	    if (step) ret = signalStep(step, req->signal, sMsg->head.uid);
	}
	/* we only return an error if we signal a specific jobscript/step */
	if (ret != 1) {
	    sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	    return;
	}
    }
    sendSlurmRC(sMsg, SLURM_SUCCESS);
}

static void handleCheckpointTasks(Slurm_Msg_t *sMsg)
{
    /* need slurm plugin to do the checkpointing */
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sMsg, ESLURM_NOT_SUPPORTED);
}

static void sendReattchReply(Step_t *step, Slurm_Msg_t *sMsg, uint32_t rc)
{
    PS_SendDB_t reply = { .bufUsed = 0, .useFrag = false };
    uint32_t i, numTasks, countPIDS = 0, countPos;

    /* hostname */
    addStringToMsg(getConfValueC(&Config, "SLURM_HOSTNAME"), &reply);
    /* return code */
    addUint32ToMsg(rc, &reply);

    if (rc == SLURM_SUCCESS) {
	list_t *t;
	numTasks = step->globalTaskIdsLen[step->localNodeId];
	/* number of tasks */
	addUint32ToMsg(numTasks, &reply);
	/* gtids */
	addUint32ArrayToMsg(step->globalTaskIds[step->localNodeId],
			    numTasks, &reply);
	/* local pids */
	countPos = reply.bufUsed;
	addUint32ToMsg(0, &reply);

	list_for_each(t, &step->tasks) {
	    PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	    if (task->childRank >= 0) {
		countPIDS++;
		addUint32ToMsg(PSC_getPID(task->childTID), &reply);
	    }
	}
	*(uint32_t *) (reply.buf + countPos) = htonl(countPIDS);

	/* executable names */
	for (i=0; i<numTasks; i++) {
	    addStringToMsg(step->argv[0], &reply);
	}
    } else {
	/* number of tasks */
	addUint32ToMsg(0, &reply);
	/* gtids */
	addUint32ToMsg(0, &reply);
	/* local pids */
	addUint32ToMsg(0, &reply);
	/* no executable names */
    }

    sMsg->outdata = &reply;
    sendSlurmReply(sMsg, RESPONSE_REATTACH_TASKS);
}

static void handleReattachTasks(Slurm_Msg_t *sMsg)
{
    char **ptr = &sMsg->ptr;
    Step_t *step;
    uint32_t i, jobid, stepid, rc = SLURM_SUCCESS;
    uint16_t numIOports, numCtlPorts;
    uint16_t *ioPorts = NULL, *ctlPorts = NULL;
    JobCred_t *cred = NULL;
    LIST_HEAD(gresList);

    getUint32(ptr, &jobid);
    getUint32(ptr, &stepid);

    if (!(step = findStepByStepId(jobid, stepid))) {
	mlog("%s: step %u:%u to reattach not found\n", __func__,
		jobid, stepid);
	rc = ESLURM_INVALID_JOB_ID;
	goto SEND_REPLY;
    }

    /* check permissions */
    if (!(verifyUserId(sMsg->head.uid, step->uid))) {
	mlog("%s: request from invalid user %u step %u:%u\n", __func__,
		sMsg->head.uid, jobid, stepid);
	rc = ESLURM_USER_ID_MISSING;
	goto SEND_REPLY;
    }

    if (!step->fwdata) {
	/* no forwarder to attach to */
	mlog("%s: forwarder for step %u:%u to reattach not found\n", __func__,
		jobid, stepid);
	rc = ESLURM_INVALID_JOB_ID;
	goto SEND_REPLY;
    }

    /* srun control ports */
    getUint16(ptr, &numCtlPorts);
    if (numCtlPorts >0) {
	ctlPorts = umalloc(numCtlPorts * sizeof(uint16_t));
	for (i=0; i<numCtlPorts; i++) {
	    getUint16(ptr, &ctlPorts[i]);
	}
    } else {
	mlog("%s: invalid request, no control ports\n", __func__);
	rc = ESLURM_PORTS_INVALID;
	goto SEND_REPLY;
    }

    /* get I/O ports */
    getUint16(ptr, &numIOports);
    if (numIOports >0) {
	ioPorts = umalloc(numIOports * sizeof(uint16_t));
	for (i=0; i<numIOports; i++) {
	    getUint16(ptr, &ioPorts[i]);
	}
    } else {
	mlog("%s: invalid request, no I/O ports\n", __func__);
	rc = ESLURM_PORTS_INVALID;
	goto SEND_REPLY;
    }

    /* job credential including I/O key */
    cred = extractJobCred(&gresList, sMsg, 0);
    if (!cred) {
	mlog("%s: invalid credential for step %u:%u\n", __func__,
		jobid, stepid);
	rc = ESLURM_INVALID_JOB_CREDENTIAL;
	goto SEND_REPLY;
    }
    freeGresCred(&gresList);

    if (strlen(cred->sig) +1 != SLURM_IO_KEY_SIZE) {
	mlog("%s: invalid I/O key size %zu\n", __func__,
		strlen(cred->sig) +1);
	rc = ESLURM_INVALID_JOB_CREDENTIAL;
	goto SEND_REPLY;
    }

    /* send message to forwarder */
    reattachTasks(step->fwdata, sMsg->head.addr,
		    ioPorts[step->localNodeId % numIOports],
		    ctlPorts[step->localNodeId % numCtlPorts],
		    cred->sig);
SEND_REPLY:

    sendReattchReply(step, sMsg, rc);

    freeJobCred(cred);
    ufree(ioPorts);
    ufree(ctlPorts);
}

static void handleSignalJob(Slurm_Msg_t *sMsg)
{
    char **ptr = &sMsg->ptr;
    Job_t *job;
    Step_t *step;
    uint32_t jobid, siginfo, flag;
    int signal;

    getUint32(ptr, &jobid);
    getUint32(ptr, &siginfo);

    flag = siginfo >> 24;
    signal = siginfo & 0xfff;

    job = findJobById(jobid);
    step = findStepByJobid(jobid);

    if (!job && !step) {
	mlog("%s: job %u to signal not found\n", __func__, jobid);
	sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	return;
    }

    if (job && (flag & KILL_JOB_BATCH)) {
	mlog("%s: send jobscript %u signal %u\n", __func__,
		jobid, signal);
	/* signal only job, not all corresponding steps */
	if (job->state == JOB_RUNNING && job->fwdata) {
	    killChild(PSC_getPID(job->fwdata->tid), signal);
	}
    } else if (job) {
	mlog("%s: send job %u signal %u\n", __func__,
		jobid, signal);
	if (signal == SIGUSR1) job->signaled = true;
	signalJob(job, signal, sMsg->head.uid);
    } else {
	mlog("%s: send steps with jobid %u signal %u\n", __func__,
		jobid, signal);
	signalStepsByJobid(jobid, signal, sMsg->head.uid);
    }

    sendSlurmRC(sMsg, SLURM_SUCCESS);
}

static void handleSuspendInt(Slurm_Msg_t *sMsg)
{
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sMsg, ESLURM_NOT_SUPPORTED);
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

    sendSlurmRC(sMsg, SLURM_SUCCESS);
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
    PS_SendDB_t msg = { .bufUsed = 0, .useFrag = false };

    /* check permissions */
    if (sMsg->head.uid != 0 && sMsg->head.uid != slurmUserID) {
	mlog("%s: request from invalid user %u\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    /* pack dummy data */
    packEnergyData(&msg);

    sMsg->outdata = &msg;
    sendSlurmReply(sMsg, RESPONSE_ACCT_GATHER_UPDATE);
}

static void handleAcctGatherEnergy(Slurm_Msg_t *sMsg)
{
    PS_SendDB_t msg = { .bufUsed = 0, .useFrag = false };

    /* check permissions */
    if (sMsg->head.uid != 0 && sMsg->head.uid != slurmUserID) {
	mlog("%s: request from invalid user %u\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    /* pack dummy data */
    packEnergyData(&msg);

    sMsg->outdata = &msg;
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

    Step_t *step = findStepByTaskPid(pid);
    if (step) {
	PS_SendDB_t msg = { .bufUsed = 0, .useFrag = false };

	addUint32ToMsg(step->jobid, &msg);
	addUint32ToMsg(SLURM_SUCCESS, &msg);
	sMsg->outdata = &msg;

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
	    ufree(bcast->username);
	    bcast->username = ustrdup(alloc->username);
	}
    } else {
	bcast->uid = job->uid;
	bcast->gid = job->gid;
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
    if (!execUserBCast(bcast)) {
	sendSlurmRC(sMsg, ESLURMD_FORK_FAILED);
	goto CLEANUP;
    }
    return;

CLEANUP:
    deleteBCast(bcast);
}

static void addSlurmPids(PStask_ID_t loggerTID, PS_SendDB_t *data)
{
    uint32_t count = 0, i;
    pid_t *pids = NULL;

    /* node_name */
    addStringToMsg(getConfValueC(&Config, "SLURM_HOSTNAME"), data);

    psAccountGetPidsByLogger(loggerTID, &pids, &count);

    addUint32ToMsg(count, data);
    for (i=0; i<count; i++) {
	addUint32ToMsg((uint32_t)pids[i], data);
    }
    ufree(pids);
}

static int addSlurmAccData(uint8_t accType, pid_t childPid,
			   PStask_ID_t loggerTID, PS_SendDB_t *data,
			   PSnodes_ID_t *nodes, uint32_t nrOfNodes)
{
    AccountDataExt_t accData;
    bool res;
    uint64_t avgVsize = 0, avgRss = 0;
    SlurmAccData_t slurmAccData;

    slurmAccData.type = accType;
    slurmAccData.nodes = nodes;
    slurmAccData.nrOfNodes = nrOfNodes;
    slurmAccData.accData = &accData;
    accData.numTasks = 0;

    if (!accType) {
	/* no accounting */
	goto PACK_RESPONSE;
    }

    if (childPid) {
	res = psAccountGetDataByJob(childPid, &accData);
    } else {
	res = psAccountGetDataByLogger(loggerTID, &accData);
    }

    slurmAccData.empty = !res;

    if (!res) {
	/* getting account data failed */
	mlog("%s: getting account data for pid %u logger '%s' failed\n",
	     __func__, childPid, PSC_printTID(loggerTID));
	goto PACK_RESPONSE;
    }

    avgVsize = accData.avgVsizeCount ?
		    accData.avgVsizeTotal / accData.avgVsizeCount : 0;
    avgRss = accData.avgRssCount ?
		    accData.avgRssTotal / accData.avgRssCount : 0;

    mlog("%s: adding account data: maxVsize %zu maxRss %zu pageSize %lu "
	 "u_sec %lu u_usec %lu s_sec %lu s_usec %lu num_tasks %u avgVsize %lu"
	 " avgRss %lu avg cpufreq %.2fG\n", __func__, accData.maxVsize,
	 accData.maxRss, accData.pageSize, accData.rusage.ru_utime.tv_sec,
	 accData.rusage.ru_utime.tv_usec, accData.rusage.ru_stime.tv_sec,
	 accData.rusage.ru_stime.tv_usec, accData.numTasks, avgVsize, avgRss,
	 ((double) accData.cpuFreq / (double) accData.numTasks)
	 / (double) 1048576);

    mdbg(PSSLURM_LOG_ACC, "%s: nodes maxVsize %u maxRss %u maxPages %u "
	 "minCpu %u maxDiskRead %u maxDiskWrite %u\n", __func__,
	 PSC_getID(accData.taskIds[ACCID_MAX_VSIZE]),
	 PSC_getID(accData.taskIds[ACCID_MAX_RSS]),
	 PSC_getID(accData.taskIds[ACCID_MAX_PAGES]),
	 PSC_getID(accData.taskIds[ACCID_MIN_CPU]),
	 PSC_getID(accData.taskIds[ACCID_MAX_DISKREAD]),
	 PSC_getID(accData.taskIds[ACCID_MAX_DISKWRITE]));

    if (accData.avgVsizeCount > 0 &&
	accData.avgVsizeCount != accData.numTasks) {
	mlog("%s: warning: total Vsize is not sum of #tasks values (%lu!=%u)\n",
		__func__, accData.avgVsizeCount, accData.numTasks);
    }

    if (accData.avgRssCount > 0 &&
	    accData.avgRssCount != accData.numTasks) {
	mlog("%s: warning: total RSS is not sum of #tasks values (%lu!=%u)\n",
		__func__, accData.avgRssCount, accData.numTasks);
    }

PACK_RESPONSE:
    packSlurmAccData(data, &slurmAccData);

    return accData.numTasks;
}

static void handleStepStat(Slurm_Msg_t *sMsg)
{
    PS_SendDB_t msg = { .bufUsed = 0, .useFrag = false };
    char **ptr = &sMsg->ptr;
    uint32_t jobid, stepid, numTasks, numTasksUsed;
    Step_t *step;

    getUint32(ptr, &jobid);
    getUint32(ptr, &stepid);

    if (!(step = findStepByStepId(jobid, stepid))) {
	mlog("%s: step %u.%u to signal not found\n", __func__, jobid, stepid);
	sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	return;
    }

    /* check permissions */
    if (!(verifyUserId(sMsg->head.uid, step->uid))) {
	mlog("%s: request from invalid user %u\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    /* add return code */
    addUint32ToMsg(SLURM_SUCCESS, &msg);
    /* add placeholder for num tasks */
    numTasksUsed = msg.bufUsed;
    addUint32ToMsg(SLURM_SUCCESS, &msg);
    /* account data */
    numTasks = addSlurmAccData(step->accType, 0, step->loggerTID, &msg,
				step->nodes, step->nrOfNodes);
    /* correct numTasks */
    *(uint32_t *)(msg.buf + numTasksUsed) = htonl(numTasks);
    /* add step pids */
    addSlurmPids(step->loggerTID, &msg);

    sMsg->outdata = &msg;
    sendSlurmReply(sMsg, RESPONSE_JOB_STEP_STAT);
}

static void handleStepPids(Slurm_Msg_t *sMsg)
{
    PS_SendDB_t msg = { .bufUsed = 0, .useFrag = false };
    char **ptr = &sMsg->ptr;
    Step_t *step;
    uint32_t jobid, stepid;

    getUint32(ptr, &jobid);
    getUint32(ptr, &stepid);

    if (!(step = findStepByStepId(jobid, stepid))) {
	mlog("%s: step %u.%u to signal not found\n", __func__, jobid, stepid);
	sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	return;
    }

    /* check permissions */
    if (!(verifyUserId(sMsg->head.uid, step->uid))) {
	mlog("%s: request from invalid user %u\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    /* add step pids */
    addSlurmPids(step->loggerTID, &msg);

    sMsg->outdata = &msg;
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
    Step_t *step;
    char **ptr = &sMsg->ptr;
    char *msg = NULL;
    uint32_t jobid, stepid;

    getUint32(ptr, &jobid);
    getUint32(ptr, &stepid);

    if (stepid == SLURM_BATCH_SCRIPT) {
	step = findStepByJobid(jobid);
    } else {
	step = findStepByStepId(jobid, stepid);
    }

    if (!step) {
	mlog("%s: step %u.%u to signal not found\n", __func__, jobid, stepid);
	sendSlurmRC(sMsg, ESLURM_INVALID_JOB_ID);
	return;
    }

    /* check permissions */
    if (!(verifyUserId(sMsg->head.uid, step->uid))) {
	mlog("%s: request from invalid user %u\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	return;
    }

    msg = getStringM(ptr);
    /*
    mlog("%s: send message '%s' to step %u:%u\n", __func__, msg, step->jobid,
	    step->stepid);
    */

    printChildMessage(step, "psslurm: ", strlen("psslurm: "), STDERR, 0);
    printChildMessage(step, msg, strlen(msg), STDERR, 0);
    printChildMessage(step, "\n", strlen("\n"), STDERR, 0);

    sendSlurmRC(sMsg, SLURM_SUCCESS);
    ufree(msg);
}

static void handleForwardData(Slurm_Msg_t *sMsg)
{
    mlog("%s: implement me!\n", __func__);
    sendSlurmRC(sMsg, ESLURM_NOT_SUPPORTED);
}

static void handleLaunchProlog(Slurm_Msg_t *sMsg)
{
    uint32_t jobid;
    uid_t uid;
    gid_t gid;
    char *alias, *nodes, *partition;
    char **ptr = &sMsg->ptr;

    getUint32(ptr, &jobid);
    getUint32(ptr, &uid);
    getUint32(ptr, &gid);

    alias = getStringM(ptr);
    nodes = getStringM(ptr);
    partition = getStringM(ptr);

    mlog("%s: start prolog jobid %u uid %u gid %u alias '%s' nodes '%s'"
	 " partition '%s'\n", __func__, jobid, uid, gid, alias, nodes,
	 partition);

    sendSlurmRC(sMsg, SLURM_SUCCESS);

    ufree(nodes);
    ufree(alias);
    ufree(partition);
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
    uint32_t i, tmp;
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

    if (!getNodesFromSlurmHL(job->packHostlist, &job->packNrOfNodes,
			     &job->packNodes, &tmp)) {
	mlog("%s: resolving PS nodeIDs from %s failed\n", __func__,
	     job->packHostlist);
	return false;
    }

    mdbg(PSSLURM_LOG_PACK, "%s: pack hostlist '%s'\n", __func__,
	 job->packHostlist);

    return true;
}

/**
 * @brief Print various job information
 *
 * @param job The job to print the infos from
 */
static void printJobLaunchInfos(Job_t *job)
{
    uint32_t i;

    mdbg(PSSLURM_LOG_JOB, "%s: job %u in '%s'\n", __func__,
	    job->jobid, strJobState(job->state));

    /* log cpu options */
    if (job->cpusPerNode && job->cpuCountReps) {
	for (i=0; i<job->cpuGroupCount; i++) {
	    mdbg(PSSLURM_LOG_PART, "cpusPerNode %u cpuCountReps %u\n",
		 job->cpusPerNode[i], job->cpuCountReps[i]);
	}
    }

    /* job env */
    job->env.size = job->env.cnt;
    for (i=0; i<job->env.cnt; i++) {
	mdbg(PSSLURM_LOG_ENV, "%s: env%i: '%s'\n", __func__, i,
		job->env.vars[i]);
    }

    /* spank env */
    job->spankenv.size = job->spankenv.cnt;
    for (i=0; i<job->spankenv.cnt; i++) {
	mdbg(PSSLURM_LOG_ENV, "%s: spankenv%i: '%s'\n", __func__, i,
		job->spankenv.vars[i]);
    }
}

static void handleBatchJobLaunch(Slurm_Msg_t *sMsg)
{
    Job_t *job;
    Alloc_t *alloc;

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
    if (!getNodesFromSlurmHL(job->slurmHosts, &job->nrOfNodes, &job->nodes,
			     &job->localNodeId)) {
	mlog("%s: resolving PS nodeIDs from %s failed\n", __func__,
	     job->slurmHosts);
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

    job->hostname = ustrdup(getConfValueC(&Config, "SLURM_HOSTNAME"));

    /* write the jobscript */
    if (!(writeJobscript(job))) {
	/* set myself offline and requeue the job */
	setNodeOffline(&job->env, job->jobid, slurmController,
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

    /* add allocation if pspelogue isn't used */
    alloc = addAlloc(job->jobid, job->packSize, job->slurmHosts, &job->env,
		     job->uid, job->gid, job->username);

    /* santy check allocation state */
    if (alloc->state != A_INIT && alloc->state != A_PROLOGUE_FINISH) {
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

    bool ret;
    if (alloc->state == A_INIT) {
	/* start prologue */
	flog("start prologue\n");
	ret = startPElogue(alloc, PELOGUE_PROLOGUE);
    } else {
	/* pspelogue already ran parallel prologue, start job */
	flog("start job\n");
	alloc->state = A_RUNNING;
	ret = execUserJob(job);
    }
    mdbg(PSSLURM_LOG_JOB, "%s: job %u in '%s'\n", __func__,
	 job->jobid, strJobState(job->state));

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
	case A_PROLOGUE:
	    sendSlurmRC(sMsg, SLURM_SUCCESS);
	    flog("waiting for prologue to finish, alloc %u (%i/%i)\n",
		 alloc->id, alloc->terminate, maxTermReq);
	    return;
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
	    }
	    return;
	case A_INIT:
	    /* no processes started, execute epilogue now */
	case A_PROLOGUE_FINISH:
	    /* local epilogue can start now */
	    break;
	default:
	    flog("invalid allocation state %u\n", alloc->state);
	    deleteAlloc(alloc->id);
	    return;
    }

    /* job/steps already finished, start epilogue now */
    sendSlurmRC(sMsg, SLURM_SUCCESS);
    mlog("%s: starting epilogue for allocation %u state %s\n", __func__,
	 alloc->id, strAllocState(alloc->state));
    startPElogue(alloc, PELOGUE_EPILOGUE);
}

static void handleAbortReq(Slurm_Msg_t *sMsg, uint32_t jobid, uint32_t stepid)
{
    Step_t *step;
    Job_t *job;
    Alloc_t *alloc;

    /* send success back to slurmctld */
    sendSlurmRC(sMsg, SLURM_SUCCESS);

    if (stepid != NO_VAL) {
	if (!(step = findStepByStepId(jobid, stepid))) {
	    mlog("%s: step %u:%u not found\n", __func__, jobid, stepid);
	    return;
	}
	signalStep(step, SIGKILL, sMsg->head.uid);
	deleteStep(step->jobid, step->stepid);
	return;
    }

    job = findJobById(jobid);
    alloc = findAlloc(jobid);

    if (job) {
	if (!job->mother) {
	    signalJob(job, SIGKILL, sMsg->head.uid);
	    send_PS_JobExit(job->jobid, SLURM_BATCH_SCRIPT,
		    job->nrOfNodes, job->nodes);
	}
	deleteJob(jobid);
    } else {
	if (isAllocLeader(alloc)) {
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

    if (step->jobid != info->jobid) return false;
    if (info->stepid != NO_VAL && info->stepid != step->stepid) return false;

    if (info->timeout) {
	snprintf(buf, sizeof(buf), "error: *** step %u:%u CANCELLED DUE TO"
		" TIME LIMIT ***\n", step->jobid, step->stepid);
	printChildMessage(step, buf, strlen(buf), STDERR, 0);
	sendStepTimeout(step->fwdata);
	step->timeout = true;
    } else {
	snprintf(buf, sizeof(buf), "error: *** PREEMPTION for step "
		"%u:%u ***\n", step->jobid, step->stepid);
	printChildMessage(step, buf, strlen(buf), STDERR, 0);
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
    if (!alloc->firstKillReq) alloc->firstKillReq = time(NULL);
    alloc->terminate++;
    Job_t *job = findJobById(info->jobid);

    if (info->timeout) {
	if (job) job->timeout = 1;
	/* TODO: redirect to job output */
	mlog("error: *** job %u CANCELLED DUE TO TIME LIMIT ***\n",
	     info->jobid);
    } else {
	/* TODO: redirect to job output */
	mlog("error: *** PREEMPTION for job %u ***\n", info->jobid);
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
    Alloc_t *alloc;
    Req_Terminate_Job_t *req = NULL;
    Kill_Info_t info;

    /* unpack request */
    if (!(unpackReqTerminate(sMsg, &req))) {
	mlog("%s: unpack terminate request failed\n", __func__);
	sendSlurmRC(sMsg, SLURM_ERROR);
	return;
    }
    info.jobid = req->jobid;
    info.stepid = req->stepid;
    info.uid = sMsg->head.uid;

    /* check permissions */
    if (sMsg->head.uid != 0 && sMsg->head.uid != slurmUserID) {
	mlog("%s: request from invalid user %u\n", __func__, sMsg->head.uid);
	sendSlurmRC(sMsg, ESLURM_USER_ID_MISSING);
	goto CLEANUP;
    }

    mlog("%s: jobid %u:%u state %u uid %u type '%s'\n", __func__,
	    req->jobid, req->stepid, req->jobstate, req->uid,
	    msgType2String(sMsg->head.type));

    /* restore account freq */
    PSIDnodes_setAcctPollI(PSC_getMyID(), confAccPollTime);

    /* find the corresponding allocation */
    alloc = findAlloc(req->jobid);

    /* remove all unfinished spawn requests */
    PSIDspawn_cleanupBySpawner(PSC_getMyTID());
    cleanupDelayedSpawns(req->jobid, req->stepid);

    if (!alloc) {
	deleteJob(req->jobid);
	clearStepList(req->jobid);
	mlog("%s: allocation %u:%u not found\n", __func__,
	     req->jobid, req->stepid);
	if (sMsg->head.type == REQUEST_TERMINATE_JOB) {
	    sendSlurmRC(sMsg, ESLURMD_KILL_JOB_ALREADY_COMPLETE);
	} else {
	    sendSlurmRC(sMsg, SLURM_SUCCESS);
	    sendEpilogueComplete(req->jobid, SLURM_SUCCESS);
	}
	goto CLEANUP;
    }

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
	    mlog("%s: unknown terminate request\n", __func__);
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

static void getSlurmMsgHeader(Slurm_Msg_t *sMsg, Msg_Forward_t *fw)
{
    char **ptr = &sMsg->ptr;
    uint16_t i;
    uint32_t tmp;

    getUint16(ptr, &sMsg->head.version);
    getUint16(ptr, &sMsg->head.flags);
    getUint16(ptr, &sMsg->head.index);
    getUint16(ptr, &sMsg->head.type);
    getUint32(ptr, &sMsg->head.bodyLen);

    /* get forwarding info */
    getUint16(ptr, &sMsg->head.forward);
    if (sMsg->head.forward >0) {
	fw->head.nodeList = getStringM(ptr);
	getUint32(ptr, &fw->head.timeout);
	getUint16(ptr, &sMsg->head.treeWidth);
    }
    getUint16(ptr, &sMsg->head.returnList);

    /* addr/port info */
    if (sMsg->source != -1) {
	getUint32(ptr, &sMsg->head.addr);
	getUint16(ptr, &sMsg->head.port);
    } else {
	/* skip empty info */
	getUint32(ptr, &tmp);
	getUint16(ptr, &i);
    }

#if defined (DEBUG_MSG_HEADER)
    mlog("%s: version %u flags %u index %u type %u bodyLen %u forward %u"
	 " treeWidth %u returnList %u\n", __func__, sMsg->head.version,
	 sMsg->head.flags, sMsg->head.index, sMsg->head.type,
	 sMsg->head.bodyLen, sMsg->head.forward, sMsg->head.treeWidth,
	 sMsg->head.returnList);

    if (sMsg->head.forward) {
	mlog("%s: forward to nodeList '%s' timeout %u treeWidth %u\n", __func__,
	     fw->head.nodeList, fw->head.timeout, sMsg->head.treeWidth);
    }
#endif
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
    uint32_t i, nrOfNodes, localId;
    bool verbose = logger_getMask(psslurmlogger) & PSSLURM_LOG_FWD;
    struct timeval time_start, time_now, time_diff;

    /* no forwarding active for this message? */
    if (!sMsg->head.forward) return true;

    /* convert nodelist to PS nodes */
    if (!getNodesFromSlurmHL(fw->head.nodeList, &nrOfNodes, &nodes, &localId)) {
	mlog("%s: resolving PS nodeIDs from %s failed\n", __func__,
	     fw->head.nodeList);
	return false;
    }

    /* save forward information in connection, has to be
       done *before* sending any messages  */
    fw->nodes = nodes;
    fw->nodesCount = nrOfNodes;
    fw->head.forward = sMsg->head.forward;
    fw->head.returnList = sMsg->head.returnList;
    fw->head.fwSize = sMsg->head.forward;
    fw->head.fwdata =
	umalloc(sMsg->head.forward * sizeof(Slurm_Forward_Data_t));

    for (i=0; i<sMsg->head.forward; i++) {
	fw->head.fwdata[i].error = SLURM_COMMUNICATIONS_CONNECTION_ERROR;
	fw->head.fwdata[i].type = RESPONSE_FORWARD_FAILED;
	fw->head.fwdata[i].node = -1;
	fw->head.fwdata[i].body.buf = NULL;
	fw->head.fwdata[i].body.bufUsed = 0;
    }

    if (verbose) {
	gettimeofday(&time_start, NULL);

	mlog("%s: forward type %s count %u nodelist %s timeout %u "
	     "at %.4f\n", __func__, msgType2String(sMsg->head.type),
	     sMsg->head.forward, fw->head.nodeList, fw->head.timeout,
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
    getSlurmMsgHeader(sMsg, fw);

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
	sendSlurmRC(sMsg, SLURM_PROTOCOL_VERSION_ERROR);
	return;
    }

    /* verify munge authentication */
    if (!extractSlurmAuth(sMsg)) {
	sendSlurmRC(sMsg, ESLURM_AUTH_CRED_INVALID);
	return;
    }

    /* let the callback handle the message */
    cb(sMsg, info);
}

static void handleNodeRegStat(Slurm_Msg_t *sMsg)
{
    sendPing(sMsg);
    sendNodeRegStatus();
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

bool initSlurmdProto(void)
{
    char *pver;

    /* Slurm protocol version */
    pver = getConfValueC(&Config, "SLURM_PROTO_VERSION");

    if (!strcmp(pver, "17.11") || !strcmp(pver, "1711")) {
	slurmProto = SLURM_17_11_PROTO_VERSION;
	slurmProtoStr = ustrdup("17.11");
    } else if (!strcmp(pver, "17.02") || !strcmp(pver, "1702")) {
	slurmProto = SLURM_17_02_PROTO_VERSION;
	slurmProtoStr = ustrdup("17.02");
    } else {
	mlog("%s: unsupported Slurm protocol version %s\n", __func__, pver);
	return false;
    }

    registerSlurmdMsg(REQUEST_LAUNCH_PROLOG, handleLaunchProlog);
    registerSlurmdMsg(REQUEST_BATCH_JOB_LAUNCH, handleBatchJobLaunch);
    registerSlurmdMsg(REQUEST_LAUNCH_TASKS, handleLaunchTasks);
    registerSlurmdMsg(REQUEST_SIGNAL_TASKS, handleSignalTasks);
    registerSlurmdMsg(REQUEST_TERMINATE_TASKS, handleSignalTasks);
    registerSlurmdMsg(REQUEST_CHECKPOINT_TASKS, handleCheckpointTasks);
    registerSlurmdMsg(REQUEST_REATTACH_TASKS, handleReattachTasks);
    registerSlurmdMsg(REQUEST_KILL_PREEMPTED, handleTerminateReq);
    registerSlurmdMsg(REQUEST_KILL_TIMELIMIT, handleTerminateReq);
    registerSlurmdMsg(REQUEST_ABORT_JOB, handleTerminateReq);
    registerSlurmdMsg(REQUEST_TERMINATE_JOB, handleTerminateReq);
    registerSlurmdMsg(REQUEST_SUSPEND_INT, handleSuspendInt);
    registerSlurmdMsg(REQUEST_SIGNAL_JOB, handleSignalJob); /* defunct in 17.11 */
    registerSlurmdMsg(REQUEST_COMPLETE_BATCH_SCRIPT, handleInvalid);
    registerSlurmdMsg(REQUEST_UPDATE_JOB_TIME, handleUpdateJobTime);
    registerSlurmdMsg(REQUEST_SHUTDOWN, handleShutdown);
    registerSlurmdMsg(REQUEST_RECONFIGURE, handleReconfigure);
    registerSlurmdMsg(REQUEST_REBOOT_NODES, handleRebootNodes);
    registerSlurmdMsg(REQUEST_NODE_REGISTRATION_STATUS, handleNodeRegStat);
    registerSlurmdMsg(REQUEST_PING, sendPing);
    registerSlurmdMsg(REQUEST_HEALTH_CHECK, handleHealthCheck);
    registerSlurmdMsg(REQUEST_ACCT_GATHER_UPDATE, handleAcctGatherUpdate);
    registerSlurmdMsg(REQUEST_ACCT_GATHER_ENERGY, handleAcctGatherEnergy);
    registerSlurmdMsg(REQUEST_JOB_ID, handleJobId);
    registerSlurmdMsg(REQUEST_FILE_BCAST, handleFileBCast);
    registerSlurmdMsg(REQUEST_STEP_COMPLETE, handleInvalid);
    registerSlurmdMsg(REQUEST_STEP_COMPLETE_AGGR, handleInvalid);
    registerSlurmdMsg(REQUEST_JOB_STEP_STAT, handleStepStat);
    registerSlurmdMsg(REQUEST_JOB_STEP_PIDS, handleStepPids);
    registerSlurmdMsg(REQUEST_DAEMON_STATUS, handleDaemonStatus);
    registerSlurmdMsg(REQUEST_JOB_NOTIFY, handleJobNotify);
    registerSlurmdMsg(REQUEST_FORWARD_DATA, handleForwardData);
    registerSlurmdMsg(REQUEST_NETWORK_CALLERID, handleNetworkCallerID);
    registerSlurmdMsg(MESSAGE_COMPOSITE, handleInvalid);
    registerSlurmdMsg(RESPONSE_MESSAGE_COMPOSITE, handleRespMessageComposite);

    return true;
}

void clearSlurmdProto(void)
{
    clearSlurmdMsg(REQUEST_LAUNCH_PROLOG);
    clearSlurmdMsg(REQUEST_BATCH_JOB_LAUNCH);
    clearSlurmdMsg(REQUEST_LAUNCH_TASKS);
    clearSlurmdMsg(REQUEST_SIGNAL_TASKS);
    clearSlurmdMsg(REQUEST_TERMINATE_TASKS);
    clearSlurmdMsg(REQUEST_CHECKPOINT_TASKS);
    clearSlurmdMsg(REQUEST_REATTACH_TASKS);
    clearSlurmdMsg(REQUEST_KILL_PREEMPTED);
    clearSlurmdMsg(REQUEST_KILL_TIMELIMIT);
    clearSlurmdMsg(REQUEST_ABORT_JOB);
    clearSlurmdMsg(REQUEST_TERMINATE_JOB);
    clearSlurmdMsg(REQUEST_SUSPEND_INT);
    clearSlurmdMsg(REQUEST_SIGNAL_JOB);
    clearSlurmdMsg(REQUEST_COMPLETE_BATCH_SCRIPT);
    clearSlurmdMsg(REQUEST_UPDATE_JOB_TIME);
    clearSlurmdMsg(REQUEST_SHUTDOWN);
    clearSlurmdMsg(REQUEST_RECONFIGURE);
    clearSlurmdMsg(REQUEST_REBOOT_NODES);
    clearSlurmdMsg(REQUEST_NODE_REGISTRATION_STATUS);
    clearSlurmdMsg(REQUEST_PING);
    clearSlurmdMsg(REQUEST_HEALTH_CHECK);
    clearSlurmdMsg(REQUEST_ACCT_GATHER_UPDATE);
    clearSlurmdMsg(REQUEST_ACCT_GATHER_ENERGY);
    clearSlurmdMsg(REQUEST_JOB_ID);
    clearSlurmdMsg(REQUEST_FILE_BCAST);
    clearSlurmdMsg(REQUEST_STEP_COMPLETE);
    clearSlurmdMsg(REQUEST_STEP_COMPLETE_AGGR);
    clearSlurmdMsg(REQUEST_JOB_STEP_STAT);
    clearSlurmdMsg(REQUEST_JOB_STEP_PIDS);
    clearSlurmdMsg(REQUEST_DAEMON_STATUS);
    clearSlurmdMsg(REQUEST_JOB_NOTIFY);
    clearSlurmdMsg(REQUEST_FORWARD_DATA);
    clearSlurmdMsg(REQUEST_NETWORK_CALLERID);
    clearSlurmdMsg(MESSAGE_COMPOSITE);
    clearSlurmdMsg(RESPONSE_MESSAGE_COMPOSITE);

    ufree(slurmProtoStr);
}

void sendNodeRegStatus(void)
{
    PS_SendDB_t msg = { .bufUsed = 0, .useFrag = false };
    struct utsname sys;

    Resp_Node_Reg_Status_t stat;
    memset(&stat, 0, sizeof(stat));

    mlog("%s: host '%s' protoVersion %u\n", __func__,
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

    /* protocol version */
    stat.protoVersion = version;

    /* version string */
    snprintf(stat.verStr, sizeof(stat.verStr), "psslurm-%i-p%s", version,
	     slurmProtoStr);

    packRespNodeRegStatus(&msg, &stat);

    sendSlurmMsg(SLURMCTLD_SOCK, MESSAGE_NODE_REGISTRATION_STATUS, &msg);

    ufree(stat.jobids);
    ufree(stat.stepids);
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
	    ret = sendSlurmMsg(sMsg->sock, type, sMsg->outdata);
	} else {
	    /* we are the root of the forwarding tree, so we save the result
	     * and wait for all other forwarded messages to return */
	    __saveFrwrdMsgRes(sMsg, SLURM_SUCCESS, func, line);
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
    PS_SendDB_t body = { .bufUsed = 0, .useFrag = false };

    addUint32ToMsg(rc, &body);
    sMsg->outdata = &body;
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

void sendStepExit(Step_t *step, uint32_t exit_status)
{
    PS_SendDB_t body = { .bufUsed = 0, .useFrag = false };
    pid_t childPid;

    /* jobid */
    addUint32ToMsg(step->jobid, &body);
    /* stepid */
    addUint32ToMsg(step->stepid, &body);
    /* node range (first, last) */
    addUint32ToMsg(0, &body);
    addUint32ToMsg(step->nrOfNodes -1, &body);
    /* exit status */
    exit_status = (step->timeout) ? 0 : exit_status;
    addUint32ToMsg(exit_status, &body);

    /* account data */
    childPid = (step->fwdata) ? step->fwdata->cPid : 1;
    addSlurmAccData(step->accType, 0, PSC_getTID(-1, childPid),
		    &body, step->nodes, step->nrOfNodes);

    mlog("%s: sending REQUEST_STEP_COMPLETE to slurmctld: exit %u\n", __func__,
	 exit_status);

    sendSlurmMsg(SLURMCTLD_SOCK, REQUEST_STEP_COMPLETE, &body);
}

static void doSendTaskExit(Step_t *step, int exitCode, uint32_t *count,
			   int *ctlPort, int *ctlAddr)
{
    PS_SendDB_t body = { .bufUsed = 0, .useFrag = false };
    list_t *t;
    uint32_t exitCount = 0, exitCount2 = 0;
    int i, sock;

    /* exit status */
    if (step->timeout) {
	addUint32ToMsg(SIGTERM, &body);
    } else {
	addUint32ToMsg(exitCode, &body);
    }

    /* number of processes exited */
    list_for_each(t, &step->tasks) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	if (task->sentExit || task->childRank < 0) continue;
	if (task->exitCode == exitCode) {
	    exitCount++;
	}
    }
    addUint32ToMsg(exitCount, &body);

    /* task ids of processes (array) */
    addUint32ToMsg(exitCount, &body);

    list_for_each(t, &step->tasks) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	if (task->sentExit || task->childRank < 0) continue;
	if (task->exitCode == exitCode) {
	    addUint32ToMsg(task->childRank, &body);
	    task->sentExit = 1;
	    exitCount2++;
	    /*
	    mlog("%s: tasks childRank:%i exit:%i exitCount:%i\n", __func__,
		    task->childRank, task->exitCode, exitCount);
	    */
	}
    }
    *count += exitCount;

    if (exitCount < 1) {
	mlog("%s: failed to find tasks for exitCode %i\n", __func__, exitCode);
	return;
    }

    if (exitCount != exitCount2) {
	mlog("%s: mismatching exit count %i:%i\n", __func__,
	     exitCount, exitCount2);
	return;
    }

    /* job/stepid */
    addUint32ToMsg(step->jobid, &body);
    addUint32ToMsg(step->stepid, &body);

    if (!ctlPort || !ctlAddr) {
	mlog("%s: sending MESSAGE_TASK_EXIT %u:%u exit %i\n", __func__,
	     exitCount, *count,	(step->timeout ? SIGTERM : exitCode));

	srunSendMsg(-1, step, MESSAGE_TASK_EXIT, &body);
    } else {
	for (i=0; i<MAX_SATTACH_SOCKETS; i++) {
	    if (ctlPort[i] != -1) {

		if ((sock = tcpConnectU(ctlAddr[i], ctlPort[i])) <0) {
		    mlog("%s: connection to srun %u:%u failed\n", __func__,
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
	setFDblock(sock, 1);
	if ((sendSlurmMsg(sock, RESPONSE_LAUNCH_TASKS, &body)) < 1) {
	    mlog("%s: send RESPONSE_LAUNCH_TASKS failed step %u:%u\n",
		    __func__, step->jobid, step->stepid);
	}
	close(sock);
    } else {
	mlog("%s: open control connection failed, step %u:%u\n",
		__func__, step->jobid, step->stepid);
    }

    mlog("%s: send RESPONSE_LAUNCH_TASKS step %u:%u pids %u for '%s'\n",
	    __func__, step->jobid, step->stepid, step->globalTaskIdsLen[nodeID],
	    resp.nodeName);
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

    if (countPIDs != countGTIDs || countPIDs != countLocalPIDs
	|| countPIDs != countGTIDs) {
	mlog("%s: mismatching PID %u and GTID %u count\n", __func__,
	     countPIDs, countGTIDs);
	goto CLEANUP;
    }

    packRespLaunchTasks(&body, &resp);

    /* send the message to srun */
    if ((sock = srunOpenControlConnection(step)) != -1) {
	setFDblock(sock, 1);
	if (sendSlurmMsg(sock, RESPONSE_LAUNCH_TASKS, &body) < 1) {
	    mlog("%s: send RESPONSE_LAUNCH_TASKS failed step %u:%u\n",
		 __func__, step->jobid, step->stepid);
	}
	close(sock);
    } else {
	mlog("%s: open control connection failed, step %u:%u\n",
	     __func__, step->jobid, step->stepid);
    }

    mlog("%s: send RESPONSE_LAUNCH_TASKS step %u:%u pids %u\n",
	 __func__, step->jobid, step->stepid, countPIDs);

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
	addSlurmAccData(0, 0, 0, &body, job->nodes, job->nrOfNodes);
    } else {
	addSlurmAccData(job->accType, job->fwdata->cPid, 0, &body, job->nodes,
			job->nrOfNodes);
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

/* vim: set ts=8 sw=4 tw=0 sts=4 noet:*/
