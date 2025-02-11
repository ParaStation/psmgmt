/*
 * ParaStation
 *
 * Copyright (C) 2020-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmfwcomm.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "list.h"
#include "pscommon.h"
#include "pscpu.h"
#include "psenv.h"
#include "pslog.h"
#include "pspartition.h"
#include "psserial.h"
#include "psstrv.h"

#include "pluginmalloc.h"
#include "psidcomm.h"
#include "psidpartition.h"
#include "psidspawn.h"
#include "psidtask.h"

#include "psaccounthandles.h"
#include "slurmcommon.h"
#include "slurmerrno.h"
#include "psslurmcomm.h"
#include "psslurmenv.h"
#include "psslurmforwarder.h"
#include "psslurmio.h"
#include "psslurmjobcred.h"
#include "psslurmlog.h"
#include "psslurmpin.h"
#include "psslurmproto.h"
#include "psslurmpscomm.h"

typedef enum {
    CMD_PRINT_CHILD_MSG = PLGN_TYPE_LAST+1,
    CMD_ENABLE_SRUN_IO,
    CMD_FW_FINALIZE,
    CMD_REATTACH_TASKS,
    CMD_INFO_TASKS,
    CMD_STEP_TIMEOUT,
    CMD_BROKE_IO_CON,
    CMD_SETENV,
    CMD_UNSETENV,
    CMD_INIT_COMPLETE,
} PSSLURM_Fw_Cmds_t;

static void handleStepTimeout(DDTypedBufferMsg_t *msg, PS_DataBuffer_t data,
			      void *info)
{
    Forwarder_Data_t *fwdata = info;
    Step_t *step = fwdata->userData;

    step->timeout = true;
}

static void handleInfoTasks(DDTypedBufferMsg_t *msg, PS_DataBuffer_t data,
			    void *info)
{
    Forwarder_Data_t *fwdata = info;
    Step_t *step = fwdata->userData;

    PS_Tasks_t *task = getDataM(data, NULL);
    list_add_tail(&task->next, &step->tasks);

    fdbg(PSSLURM_LOG_PROCESS, "%s child %s job rank %d global rank %d task %u"
	 " from %u\n", Step_strID(step), PSC_printTID(task->childTID),
	 task->jobRank, task->globalRank, countRegTasks(step->tasks.next),
	 step->globalTaskIdsLen[step->localNodeId]);

    if (step->globalTaskIdsLen[step->localNodeId] ==
	countRegTasks(&step->tasks)) {
	/* all local tasks finished spawning */
	sendTaskPids(step);
    }
}

static void handleFWfinalize(DDTypedBufferMsg_t *msg, PS_DataBuffer_t data,
			     void *info)
{
    Forwarder_Data_t *fwdata = info;
    Step_t *step = fwdata->userData;

    uint32_t grank;
    getUint32(data, &grank);
    PSLog_Msg_t *logMsg = getDataM(data, NULL);
    PStask_ID_t sender = logMsg->header.sender;

    fdbg(PSSLURM_LOG_IO, "from %s\n", PSC_printTID(sender));

    if (!(step->taskFlags & LAUNCH_PTY)) {
	/* close stdout/stderr */
	IO_closeChannel(fwdata, grank, STDOUT);
	IO_closeChannel(fwdata, grank, STDERR);
    }

    PS_Tasks_t *task = findTaskByFwd(&step->tasks, sender);
    if (!task) {
	flog("no task for forwarder %s\n", PSC_printTID(sender));
    } else {
	task->exitCode = *(int *)logMsg->buf;
    }

    /* let main psslurm forward FINALIZE to logger */
    sendMsgToMother((DDTypedBufferMsg_t *)logMsg);
    ufree(logMsg);
}

static void handleEnableSrunIO(DDTypedBufferMsg_t *msg, PS_DataBuffer_t data,
			       void *info)
{
    Forwarder_Data_t *fwdata = info;
    Step_t *step = fwdata->userData;

    srunEnableIO(step);
}

static void handleSattachTasks(DDTypedBufferMsg_t *msg, PS_DataBuffer_t data,
			       void *info)
{
    Forwarder_Data_t *fwdata = info;
    Step_t *step = fwdata->userData;

    uint32_t ioAddr;
    getUint32(data, &ioAddr);
    uint16_t ioPort, ctlPort;
    getUint16(data, &ioPort);
    getUint16(data, &ctlPort);
    char *sig = getStringM(data);

    IO_sattachTasks(step, ioAddr, ioPort, ctlPort, sig);

    ufree(sig);
}

static void handlePrintStepMsg(DDTypedBufferMsg_t *msg, PS_DataBuffer_t data,
			       void *info)
{
    Forwarder_Data_t *fwdata = info;

    /* read message */
    uint8_t type;
    getUint8(data, &type);
    uint32_t rank;
    getUint32(data, &rank);
    size_t msglen;
    char *IOmsg = getDataM(data, &msglen);

    IO_printStepMsg(fwdata, IOmsg, msglen, rank, type);

    ufree(IOmsg);
}

bool fwCMD_handleMthrStepMsg(DDTypedBufferMsg_t *msg, Forwarder_Data_t *fwdata)
{
    switch (msg->header.type) {
    case PSP_PF_MSG:
	break;
    case PSP_CD_SPAWNSUCCESS:
	return true;   // ignore: this might occur in case of PMI[x] spawn
    default:
	return false;  // unexpected: error log created in caller
    }

    switch ((PSSLURM_Fw_Cmds_t)msg->type) {
    case CMD_PRINT_CHILD_MSG:
	recvFragMsgInfo(msg, handlePrintStepMsg, fwdata);
	break;
    case CMD_ENABLE_SRUN_IO:
	recvFragMsgInfo(msg, handleEnableSrunIO, fwdata);
	break;
    case CMD_FW_FINALIZE:
	recvFragMsgInfo(msg, handleFWfinalize, fwdata);
	break;
    case CMD_REATTACH_TASKS:
	recvFragMsgInfo(msg, handleSattachTasks, fwdata);
	break;
    case CMD_INFO_TASKS:
	recvFragMsgInfo(msg, handleInfoTasks, fwdata);
	break;
    case CMD_STEP_TIMEOUT:
	recvFragMsgInfo(msg, handleStepTimeout, fwdata);
	break;
    default:
	flog("unexpected msg, type %d from TID %s (%s) jobid %s\n", msg->type,
	     PSC_printTID(msg->header.sender), fwdata->pTitle, fwdata->jobID);
	return false;
    }

    return true;
}

static void handlePrintJobMsg(DDTypedBufferMsg_t *msg, PS_DataBuffer_t data,
			      void *info)
{
    Forwarder_Data_t *fwdata = info;

    /* read message */
    uint8_t type;
    getUint8(data, &type);
    size_t msglen;
    char *IOmsg = getDataM(data, &msglen);

    IO_printJobMsg(fwdata, IOmsg, msglen, type);

    ufree(IOmsg);
}

bool fwCMD_handleMthrJobMsg(DDTypedBufferMsg_t *msg, Forwarder_Data_t *fwdata)
{
    if (msg->header.type != PSP_PF_MSG) return false;

    switch ((PSSLURM_Fw_Cmds_t)msg->type) {
    case CMD_PRINT_CHILD_MSG:
	recvFragMsgInfo(msg, handlePrintJobMsg, fwdata);
	break;
    default:
	flog("unexpected msg, type %d from TID %s (%s) jobid %s\n", msg->type,
	     PSC_printTID(msg->header.sender), fwdata->pTitle, fwdata->jobID);
	return false;
    }

    return true;
}

static void handleBrokeIOcon(DDTypedBufferMsg_t *msg, PS_DataBuffer_t data)
{
    uint32_t jobID;
    getUint32(data, &jobID);
    uint32_t stepID;
    getUint32(data, &stepID);

    /* step might already be deleted */
    Step_t *step = Step_findByStepId(jobID, stepID);
    if (!step) return;

    if (step->ioCon == IO_CON_NORM) step->ioCon = IO_CON_ERROR;
}

static void changeEnv(DDTypedBufferMsg_t *msg, PS_DataBuffer_t data)
{
    uint32_t jobid;
    getUint32(data, &jobid);
    uint32_t stepid;
    getUint32(data, &stepid);

    Step_t *step = Step_findByStepId(jobid, stepid);
    if (!step) {
	Step_t s = { .jobid = jobid, .stepid = stepid };
	flog("warning: %s already gone\n", Step_strID(&s));
	return;
    }

    char *var = getStringM(data);

    if ((PSSLURM_Fw_Cmds_t)msg->type == CMD_SETENV) {
	char *val = getStringM(data);
	fdbg(PSSLURM_LOG_SPANK, "setenv %s:%s for %s\n", var, val,
	     Step_strID(step));
	envSet(step->env, var, val);

	ufree(val);
    } else {
	fdbg(PSSLURM_LOG_SPANK, "unsetenv %s for %s\n", var, Step_strID(step));
	envUnset(step->env, var);
    }
    ufree(var);
}

static bool setupPartition(Step_t *step) {
    if (!step) return false;

    Forwarder_Data_t *fwData = step->fwdata;
    if (!fwData) return false;

    PStask_t *fwTask = PStasklist_find(&managedTasks, fwData->tid);
    if (!fwTask) {
	flog("no step forwarder task for %s\n", Step_strID(step));
	return false;
    }

    if (!step->slots) {
	flog("invalid slots in %s\n", Step_strID(step));
	return false;
    }

    /* generate hardware threads array */
    if (!genThreadsArray(&fwTask->partThrds, &fwTask->totalThreads, step)) {
	mwarn(errno, "%s: Could not generate threads array", __func__);
	return false;
    }

    logHWthreads(__func__, fwTask->partThrds, fwTask->totalThreads);

    /* further preparations of the task structure */
    ufree(fwTask->partition);
    fwTask->partition = NULL;
    fwTask->options |= PART_OPT_EXACT;
    fwTask->usedThreads = 0;
    fwTask->activeSlots = 0;
    fwTask->partitionSize = 0;

    fdbg(PSSLURM_LOG_PART, "Created partition for task %s: threads %u"
	 " NODEFIRST %d EXCLUSIVE %d OVERBOOK %d WAIT %d EXACT %d\n",
	 PSC_printTID(fwTask->tid), fwTask->totalThreads,
	 (fwTask->options & PART_OPT_NODEFIRST) ? 1 : 0,
	 (fwTask->options & PART_OPT_EXCLUSIVE) ? 1 : 0,
	 (fwTask->options & PART_OPT_OVERBOOK) ? 1 : 0,
	 (fwTask->options & PART_OPT_WAIT) ? 1 : 0,
	 (fwTask->options & PART_OPT_EXACT) ? 1 : 0);

    /* generate slots in partition from HW threads and register
     * partition to master psid, psilogger and sister steps */
    PSIDpart_register(fwTask, fwTask->partThrds, fwTask->totalThreads);

    return true;
}

static void debugMpiexecStart(strv_t argv, env_t env)
{
    flog("exec:");
    for (char **a = strvGetArray(argv); a && *a; a++) mlog(" %s", *a);
    mlog("\n");

    int cnt = 0;
    for (char **e = envGetArray(env); e && *e; e++) {
	flog("env[%i] '%s'\n", cnt++, *e);
    }
}

/**
 * @brief Start spawner
 *
 * Initiate the startup of a spawner process. For this, first a
 * partition is created and registered to the logger task (and all
 * sister step forwarder tasks). Once the partition is there the
 * actual spawner is started (prepended by its kvsprovider for the PMI
 * case).
 *
 * Reservations will be created on the fly triggered by the spawner
 * similar to the case of standard job startup.
 *
 * @param step Structure containing all information on the step to start
 *
 * @return No return value
 */
static void startSpawner(Step_t *step, int32_t serviceRank)
{
    Forwarder_Data_t *fwData = step->fwdata;

    /* due to lack of real logger register the step forwarder instead */
    psAccountRegisterJob(PSC_getPID(fwData->tid), NULL);

    /* No child started under step-forwarder's control. Start it here */
    // - create a partition and tell the logger (and all sister forwarders)
    if (!setupPartition(step)) shutdownForwarder(fwData);

    // - prepare the task structure like in fwExecStep()
    PStask_t *task = PStask_new();
    task->ptid = fwData->tid;
    task->uid = step->uid;
    task->gid = step->gid;
    char *cols = envGet(step->env, "SLURM_PTY_WIN_COL");
    char *rows = envGet(step->env, "SLURM_PTY_WIN_ROW");
    if (cols && rows) {
	task->winsize = (struct winsize) {
	    .ws_col = atoi(cols),
	    .ws_row =atoi(rows) };
    }
    task->group = TG_KVS;
    task->loggertid = fwData->loggerTid;
    task->rank = serviceRank ? serviceRank : fwData->rank;
    /* service tasks will not be pinned (i.e. pinned to all HW threads) */
    PSCPU_setAll(task->CPUset);
    if (step->cwd) task->workingdir = strdup(step->cwd);

    // - prepare the argument vector according to fwExecStep()
    pmi_type_t pmi_type = getPMIType(step);
    task->argV = buildStartArgv(fwData, pmi_type);

    // - do some environment preparation from fwExecStep()
    Step_t *envStep = Step_new();
    envStep->env = envClone(step->env, NULL);
    envStep->taskFlags = step->taskFlags;
    envStep->memBindType = step->memBindType;

    setStepEnv(envStep);
    removeUserVars(envStep->env, pmi_type);

    task->env = envStep->env;
    envStep->env = NULL;
    Step_delete(envStep);

    if (mset(PSSLURM_LOG_PROCESS)) debugMpiexecStart(task->argV, task->env);

    // - actually start the task
    int ret = PSIDspawn_localTask(task, NULL, NULL);

    /* return launch success to waiting srun since spawner could be spawned */
    if (!ret) {
	flog("launch success for %s to srun sock '%u'\n",
	     Step_strID(step), step->srunControlMsg.sock);
	sendSlurmRC(&step->srunControlMsg, SLURM_SUCCESS);
	step->state = JOB_SPAWNED;
    } else {
	fwarn(ret, "failed to launch spawner for %s", Step_strID(step));
	PStask_delete(task);
	sendSlurmRC(&step->srunControlMsg, ESLURMD_FORK_FAILED);
	shutdownForwarder(fwData);
    }
}


static void handleInitComplete(DDTypedBufferMsg_t *msg, PS_DataBuffer_t data)
{
    uint32_t jobid;
    getUint32(data, &jobid);
    uint32_t stepid;
    getUint32(data, &stepid);
    int32_t serviceRank;
    getInt32(data, &serviceRank);

    Step_t *step = Step_findByStepId(jobid, stepid);
    if (!step) {
	Step_t s = { .jobid = jobid, .stepid = stepid };
	flog("warning: %s already gone\n", Step_strID(&s));
	return;
    }
    releaseDelayedSpawns(step->jobid, step->stepid);
    if (step->spawned) startSpawner(step, serviceRank);
}

bool fwCMD_handleFwStepMsg(DDTypedBufferMsg_t *msg, Forwarder_Data_t *fwdata)
{
    if (msg->header.type == PSP_CC_MSG) {
	PSLog_Msg_t *lmsg = (PSLog_Msg_t *)msg;
	switch (lmsg->type) {
	case FINALIZE:
	case SERV_RNK:
	case STDIN:
	    sendMsg(msg);
	    break;
	default:
	    flog("unhandled Log-type %s\n", PSLog_printMsgType(lmsg->type));
	    return false;
	}
    } else if (msg->header.type == PSP_PF_MSG) {
	switch ((PSSLURM_Fw_Cmds_t)msg->type) {
	case CMD_BROKE_IO_CON:
	    recvFragMsg(msg, handleBrokeIOcon);
	    break;
	case CMD_SETENV:
	case CMD_UNSETENV:
	    recvFragMsg(msg, changeEnv);
	    break;
	case CMD_INIT_COMPLETE:
	    recvFragMsg(msg, handleInitComplete);
	    break;
	default:
	    fdbg(PSSLURM_LOG_IO_VERB, "unhandled type %d\n", msg->type);
	    return false;
	}
    } else return false; // do not handle other types of messages

    return true;
}

void fwCMD_stepTimeout(Forwarder_Data_t *fwdata)
{
    /* might happen that forwarder is already gone */
    if (!fwdata) return;

    PS_SendDB_t data;
    initFragBuffer(&data, PSP_PF_MSG, CMD_STEP_TIMEOUT);
    setFragDest(&data, fwdata->tid);

    sendFragMsg(&data);
}

void fwCMD_setEnv(Step_t *step, const char *var, const char *val)
{
    if (!step || !var || !val) {
	flog("error: missing parameters\n");
	return;
    }

    PS_SendDB_t data;
    initFragBuffer(&data, PSP_PF_MSG, CMD_SETENV);
    setFragDest(&data, PSC_getTID(-1,0));

    addUint32ToMsg(step->jobid, &data);
    addUint32ToMsg(step->stepid, &data);
    addStringToMsg(var, &data);
    addStringToMsg(val, &data);

    sendFragMsg(&data);
}

void fwCMD_initComplete(Step_t *step, int32_t serviceRank)
{
    if (!step) {
	flog("error: missing step\n");
	return;
    }

    PS_SendDB_t data;
    initFragBuffer(&data, PSP_PF_MSG, CMD_INIT_COMPLETE);
    setFragDest(&data, PSC_getTID(-1,0));

    addUint32ToMsg(step->jobid, &data);
    addUint32ToMsg(step->stepid, &data);
    addInt32ToMsg(serviceRank, &data);

    sendFragMsg(&data);
}

void fwCMD_unsetEnv(Step_t *step, const char *var)
{
    if (!step || !var) {
	flog("error: missing parameters\n");
	return;
    }

    PS_SendDB_t data;
    initFragBuffer(&data, PSP_PF_MSG, CMD_UNSETENV);
    setFragDest(&data, PSC_getTID(-1,0));

    addUint32ToMsg(step->jobid, &data);
    addUint32ToMsg(step->stepid, &data);
    addStringToMsg(var, &data);

    sendFragMsg(&data);
}

void fwCMD_brokeIOcon(Step_t *step)
{
    PS_SendDB_t data;
    initFragBuffer(&data, PSP_PF_MSG, CMD_BROKE_IO_CON);
    setFragDest(&data, PSC_getTID(-1,0));

    addUint32ToMsg(step->jobid, &data);
    addUint32ToMsg(step->stepid, &data);

    sendFragMsg(&data);
}

void fwCMD_enableSrunIO(Step_t *step)
{
    /* might happen that forwarder is already gone */
    if (!step->fwdata) return;

    fdbg(PSSLURM_LOG_IO, "to %s\n", PSC_printTID(step->fwdata->tid));

    PS_SendDB_t data;
    initFragBuffer(&data, PSP_PF_MSG, CMD_ENABLE_SRUN_IO);
    setFragDest(&data, step->fwdata->tid);

    sendFragMsg(&data);
}

void clearFwMsgQueue(list_t *queue)
{
    if (!queue) return;
    list_t *t, *tmp;
    list_for_each_safe(t, tmp, queue) {
	FwUserMsgBuf_t *buf = list_entry(t, FwUserMsgBuf_t, next);
	ufree(buf->msg);
	list_del(&buf->next);
	ufree(buf);
    }
}

void queueFwMsg(list_t *queue, char *msg, uint32_t msgLen,
		uint8_t type, int32_t rank)
{
    if (!queue || !msg) {
	flog("invalid queue or message\n");
	return;
    }

    FwUserMsgBuf_t *buf = umalloc(sizeof *buf);
    buf->msg = ustrdup(msg);
    buf->msgLen = msgLen;
    buf->type = type;
    buf->rank = rank;

    list_add_tail(&buf->next, queue);
}

int fwCMD_printMsg(Job_t *job, Step_t *step, char *plMsg, uint32_t msgLen,
		   uint8_t type, int32_t rank)
{
    if (job && step) {
	flog("error: job and step are mutually exclusive\n");
	return -1;
    }

    if (!job && !step) {
	flog("error: no job or step given\n");
	return -1;
    }

    /* msg from service rank, make believe it comes from first task */
    if (step && rank < 0) rank = step->globalTaskIds[step->localNodeId][0];

    Forwarder_Data_t *fwdata = job ? job->fwdata : step->fwdata;
    if (!fwdata) {
	/* might happen that forwarder is already gone or has not been
	 * started yet */
	list_t *queue = job ? &job->fwMsgQueue : &step->fwMsgQueue;
	queueFwMsg(queue, plMsg, msgLen, type, rank);
	return 1;
    }

    PS_SendDB_t data;
    initFragBuffer(&data, PSP_PF_MSG, CMD_PRINT_CHILD_MSG);
    setFragDest(&data, fwdata->tid);

    addUint8ToMsg(type, &data);

    if (step) {
	/* connection to srun broke */
	if (step->ioCon == IO_CON_ERROR) {
	    flog("I/O connection for %s is broken\n", Step_strID(step));
	    step->ioCon = IO_CON_BROKE;
	}
	if (step->ioCon == IO_CON_BROKE) return -1;

	addUint32ToMsg(rank, &data);
    }
    addDataToMsg(plMsg, msgLen, &data);

    sendFragMsg(&data);

    return 0;
}

void fwCMD_reattachTasks(Forwarder_Data_t *fwdata, uint32_t addr,
			 uint16_t ioPort, uint16_t ctlPort, char *sig)
{
    /* might happen that forwarder is already gone */
    if (!fwdata) return;

    PS_SendDB_t data;
    initFragBuffer(&data, PSP_PF_MSG, CMD_REATTACH_TASKS);
    setFragDest(&data, fwdata->tid);

    addUint32ToMsg(addr, &data);
    addUint16ToMsg(ioPort, &data);
    addUint16ToMsg(ctlPort, &data);
    addStringToMsg(sig, &data);

    sendFragMsg(&data);
}

void fwCMD_finalize(Forwarder_Data_t *fwdata, PSLog_Msg_t *plMsg, int32_t rank)
{
    /* might happen that forwarder is already gone */
    if (!fwdata) return;

    if (fwdata->tid == -1) flog("unknown destination for %s\n",
				PSC_printTID(plMsg->header.sender));

    PS_SendDB_t data;
    initFragBuffer(&data, PSP_PF_MSG, CMD_FW_FINALIZE);
    setFragDest(&data, fwdata->tid);

    addUint32ToMsg(rank, &data);
    addDataToMsg(plMsg, plMsg->header.len, &data);

    sendFragMsg(&data);
}

void fwCMD_taskInfo(Forwarder_Data_t *fwdata, PS_Tasks_t *task)
{
    /* might happen that forwarder is already gone */
    if (!fwdata) return;

    PS_SendDB_t data;
    initFragBuffer(&data, PSP_PF_MSG, CMD_INFO_TASKS);
    setFragDest(&data, fwdata->tid);

    addDataToMsg(task, sizeof(*task), &data);

    sendFragMsg(&data);
}

void fwCMD_msgSrunProxy(Step_t *step, PSLog_Msg_t *lmsg, int32_t senderRank)
{
    /* might happen if forwarder is already gone */
    if (!step->fwdata) return;

    /* connection to srun broke */
    if (step->ioCon == IO_CON_ERROR) {
	flog("I/O connection for %s is broken\n", Step_strID(step));
	step->ioCon = IO_CON_BROKE;
    }
    if (step->ioCon == IO_CON_BROKE) return;

    PS_SendDB_t data;
    initFragBuffer(&data, PSP_PF_MSG, CMD_PRINT_CHILD_MSG);
    setFragDest(&data, step->fwdata->tid);

    addUint8ToMsg(lmsg->type, &data);

    /* if msg from service rank, let it seem like it comes from first task */
    if (senderRank < 0) senderRank = step->globalTaskIds[step->localNodeId][0];
    addUint32ToMsg(senderRank, &data);

    size_t msgLen = lmsg->header.len - PSLog_headerSize;
    addDataToMsg(lmsg->buf, msgLen, &data);

    sendFragMsg(&data);
}
