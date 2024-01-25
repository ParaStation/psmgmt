/*
 * ParaStation
 *
 * Copyright (C) 2020-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmfwcomm.h"

#include <arpa/inet.h>
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
#include "pspluginprotocol.h"
#include "psserial.h"

#include "pluginmalloc.h"
#include "pluginstrv.h"
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

static void handleStepTimeout(Forwarder_Data_t *fwdata)
{
    Step_t *step = fwdata->userData;

    step->timeout = true;
}

static void handleInfoTasks(Forwarder_Data_t *fwdata, PS_DataBuffer_t *data)
{
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

static void handleFWfinalize(Forwarder_Data_t *fwdata, PS_DataBuffer_t *data)
{
    Step_t *step = fwdata->userData;
    uint32_t grank;
    getUint32(data, &grank);
    PSLog_Msg_t *msg = getDataM(data, NULL);
    PStask_ID_t sender = msg->header.sender;

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
	task->exitCode = *(int *) msg->buf;
    }

    /* let main psslurm forward FINALIZE to logger */
    sendMsgToMother((DDTypedBufferMsg_t *)msg);
    ufree(msg);
}

static void handleEnableSrunIO(Forwarder_Data_t *fwdata)
{
    Step_t *step = fwdata->userData;

    srunEnableIO(step);
}

static void handleSattachTasks(Forwarder_Data_t *fwdata, PS_DataBuffer_t *data)
{
    Step_t *step = fwdata->userData;
    uint16_t ioPort, ctlPort;
    uint32_t ioAddr;

    getUint32(data, &ioAddr);
    getUint16(data, &ioPort);
    getUint16(data, &ctlPort);
    char *sig = getStringM(data);

    IO_sattachTasks(step, ioAddr, ioPort, ctlPort, sig);

    ufree(sig);
}

static void handlePrintStepMsg(Forwarder_Data_t *fwdata, PS_DataBuffer_t *data)
{
    uint8_t type;
    uint32_t rank;
    size_t msglen;

    /* read message */
    getUint8(data, &type);
    getUint32(data, &rank);
    char *msg = getDataM(data, &msglen);

    IO_printStepMsg(fwdata, msg, msglen, rank, type);

    ufree(msg);
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

    PS_DataBuffer_t data;
    initPSDataBuffer(&data, msg->buf,
		     msg->header.len - offsetof(DDTypedBufferMsg_t, buf));

    switch ((PSSLURM_Fw_Cmds_t)msg->type) {
    case CMD_PRINT_CHILD_MSG:
	handlePrintStepMsg(fwdata, &data);
	break;
    case CMD_ENABLE_SRUN_IO:
	handleEnableSrunIO(fwdata);
	break;
    case CMD_FW_FINALIZE:
	handleFWfinalize(fwdata, &data);
	break;
    case CMD_REATTACH_TASKS:
	handleSattachTasks(fwdata, &data);
	break;
    case CMD_INFO_TASKS:
	handleInfoTasks(fwdata, &data);
	break;
    case CMD_STEP_TIMEOUT:
	handleStepTimeout(fwdata);
	break;
    default:
	flog("unexpected msg, type %d from TID %s (%s) jobid %s\n", msg->type,
	     PSC_printTID(msg->header.sender), fwdata->pTitle, fwdata->jobID);
	return false;
    }

    return true;
}

static void handlePrintJobMsg(Forwarder_Data_t *fwdata, PS_DataBuffer_t *data)
{
    uint8_t type;
    size_t msglen;

    /* read message */
    getUint8(data, &type);
    char *msg = getDataM(data, &msglen);

    IO_printJobMsg(fwdata, msg, msglen, type);

    ufree(msg);
}

bool fwCMD_handleMthrJobMsg(DDTypedBufferMsg_t *msg, Forwarder_Data_t *fwdata)
{
    if (msg->header.type != PSP_PF_MSG) return false;

    PS_DataBuffer_t data;
    initPSDataBuffer(&data, msg->buf,
		     msg->header.len - offsetof(DDTypedBufferMsg_t, buf));

    switch ((PSSLURM_Fw_Cmds_t)msg->type) {
    case CMD_PRINT_CHILD_MSG:
	handlePrintJobMsg(fwdata, &data);
	break;
    default:
	flog("unexpected msg, type %d from TID %s (%s) jobid %s\n", msg->type,
	     PSC_printTID(msg->header.sender), fwdata->pTitle, fwdata->jobID);
	return false;
    }

    return true;
}

static void handleBrokeIOcon(DDTypedBufferMsg_t *msg)
{
    size_t used = 0;
    uint32_t jobID;
    PSP_getTypedMsgBuf(msg, &used, "jobID", &jobID, sizeof(jobID));
    uint32_t stepID;
    PSP_getTypedMsgBuf(msg, &used, "stepID", &stepID, sizeof(stepID));

    /* step might already be deleted */
    Step_t *step = Step_findByStepId(jobID, stepID);
    if (!step) return;

    if (step->ioCon == IO_CON_NORM) step->ioCon = IO_CON_ERROR;
}

static void changeEnv(int cmd, PS_DataBuffer_t *data)
{
    uint32_t jobid;
    getUint32(data, &jobid);
    uint32_t stepid;
    getUint32(data, &stepid);

    Step_t *step = Step_findByStepId(jobid, stepid);
    if (!step) {
	flog("warning: step %u:%u already gone\n", jobid, stepid);
	return;
    }

    char *var = getStringM(data);

    if (cmd == CMD_SETENV) {
	char *val = getStringM(data);
	fdbg(PSSLURM_LOG_SPANK, "setenv %s:%s for %u:%u\n", var, val,
	     jobid, stepid);
	envSet(&step->env, var, val);

	ufree(val);
    } else {
	fdbg(PSSLURM_LOG_SPANK, "unsetenv %s for %u:%u\n", var, jobid, stepid);
	envUnset(&step->env, var);
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
    fwTask->activeChild = 0;
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
static void startSpawner(Step_t *step)
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
    char *cols = envGet(&step->env, "SLURM_PTY_WIN_COL");
    char *rows = envGet(&step->env, "SLURM_PTY_WIN_ROW");
    if (cols && rows) {
	task->winsize = (struct winsize) {
	    .ws_col = atoi(cols),
	    .ws_row =atoi(rows) };
    }
    task->group = TG_KVS;
    task->loggertid = fwData->loggerTid;
    task->rank = fwData->rank;
    /* service tasks will not be pinned (i.e. pinned to all HW threads) */
    PSCPU_setAll(task->CPUset);
    if (step->cwd) task->workingdir = strdup(step->cwd);

    // - prepare the argument vector according to fwExecStep()
    pmi_type_t pmi_type = getPMIType(step);
    strv_t argV;
    buildStartArgv(fwData, &argV, pmi_type);
    task->argc = argV.count;
    task->argv = argV.strings;

    // - do some environment preparation from fwExecStep()
    Step_t *envStep = Step_new();
    envClone(&step->env, &envStep->env, NULL);
    envStep->taskFlags = step->taskFlags;
    envStep->memBindType = step->memBindType;

    setStepEnv(envStep);
    removeUserVars(&envStep->env, pmi_type);

    task->environ = envStep->env.vars;
    task->envSize = envSize(&envStep->env);

    envStep->env.vars = NULL;
    envStep->env.cnt = 0;
    Step_delete(envStep);

    // - actually start the task
    int ret = PSIDspawn_localTask(task, NULL, NULL);

    /* return launch success to waiting srun since spawner could be spawned */
    if (!ret) {
	flog("launch success for %s to srun sock '%u'\n",
	     Step_strID(step), step->srunControlMsg.sock);
	sendSlurmRC(&step->srunControlMsg, SLURM_SUCCESS);
	step->state = JOB_SPAWNED;
    }
}


static void handleInitComplete(PS_DataBuffer_t *data)
{
    uint32_t jobid;
    getUint32(data, &jobid);
    uint32_t stepid;
    getUint32(data, &stepid);

    Step_t *step = Step_findByStepId(jobid, stepid);
    if (!step) {
	flog("warning: step %u:%u already gone\n", jobid, stepid);
	return;
    }
    releaseDelayedSpawns(step->jobid, step->stepid);
    if (step->spawned) startSpawner(step);
}

bool fwCMD_handleFwStepMsg(DDTypedBufferMsg_t *msg, Forwarder_Data_t *fwdata)
{
    if (msg->header.type == PSP_CC_MSG) {
	PSLog_Msg_t *lmsg = (PSLog_Msg_t *)msg;
	switch (lmsg->type) {
	case FINALIZE:
	case STDIN:
	    sendMsg(msg);
	    break;
	default:
	    flog("unhandled Log-type %s\n", PSLog_printMsgType(lmsg->type));
	    return false;
	}
    } else if (msg->header.type == PSP_PLUG_PSSLURM) {
	PS_DataBuffer_t data;
	initPSDataBuffer(&data, msg->buf,
			 msg->header.len - offsetof(DDTypedBufferMsg_t, buf));

	switch ((PSSLURM_Fw_Cmds_t)msg->type) {
	case CMD_BROKE_IO_CON:
	    handleBrokeIOcon(msg);
	    break;
	case CMD_SETENV:
	case CMD_UNSETENV:
	    changeEnv((PSSLURM_Fw_Cmds_t)msg->type, &data);
	    break;
	case CMD_INIT_COMPLETE:
	    handleInitComplete(&data);
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
    DDTypedMsg_t msg = {
	.header = {
	    .type = PSP_PF_MSG,
	    .dest = fwdata ? fwdata->tid : -1,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg) },
	.type = CMD_STEP_TIMEOUT };

    /* might happen that forwarder is already gone */
    if (!fwdata) return;

    sendMsg(&msg);
}

void fwCMD_setEnv(Step_t *step, const char *var, const char *val)
{
    if (!step || !var || !val) {
	flog("error: missing parameters\n");
	return;
    }

    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_PLUG_PSSLURM,
	    .dest = PSC_getTID(-1,0),
	    .sender = PSC_getMyTID(),
	    .len = 0, },
	.type = CMD_SETENV };
    uint32_t myJobID = htonl(step->jobid);
    PSP_putTypedMsgBuf(&msg, "jobID", &myJobID, sizeof(myJobID));
    uint32_t myStepID = htonl(step->stepid);
    PSP_putTypedMsgBuf(&msg, "stepID", &myStepID, sizeof(myStepID));

    uint32_t len = htonl(PSP_strLen(var));
    PSP_putTypedMsgBuf(&msg, "len", &len, sizeof(len));
    PSP_putTypedMsgBuf(&msg, "var", var, PSP_strLen(var));

    len = htonl(PSP_strLen(val));
    PSP_putTypedMsgBuf(&msg, "len", &len, sizeof(len));
    PSP_putTypedMsgBuf(&msg, "val", val, PSP_strLen(val));

    sendMsgToMother(&msg);
}

void fwCMD_initComplete(Step_t *step)
{
    if (!step) {
	flog("error: missing step\n");
	return;
    }

    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_PLUG_PSSLURM,
	    .dest = PSC_getTID(-1,0),
	    .sender = PSC_getMyTID(),
	    .len = 0, },
	.type = CMD_INIT_COMPLETE };
    uint32_t myJobID = htonl(step->jobid);
    PSP_putTypedMsgBuf(&msg, "jobID", &myJobID, sizeof(myJobID));
    uint32_t myStepID = htonl(step->stepid);
    PSP_putTypedMsgBuf(&msg, "stepID", &myStepID, sizeof(myStepID));

    sendMsgToMother(&msg);
}

void fwCMD_unsetEnv(Step_t *step, const char *var)
{
    if (!step || !var) {
	flog("error: missing parameters\n");
	return;
    }

    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_PLUG_PSSLURM,
	    .dest = PSC_getTID(-1,0),
	    .sender = PSC_getMyTID(),
	    .len = 0, },
	.type = CMD_UNSETENV };
    uint32_t myJobID = htonl(step->jobid);
    PSP_putTypedMsgBuf(&msg, "jobID", &myJobID, sizeof(myJobID));
    uint32_t myStepID = htonl(step->stepid);
    PSP_putTypedMsgBuf(&msg, "stepID", &myStepID, sizeof(myStepID));

    uint32_t len = htonl(PSP_strLen(var));
    PSP_putTypedMsgBuf(&msg, "len", &len, sizeof(len));
    PSP_putTypedMsgBuf(&msg, "var", var, PSP_strLen(var));

    sendMsgToMother(&msg);
}

void fwCMD_brokeIOcon(Step_t *step)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_PF_MSG,
	    .dest = PSC_getTID(-1,0),
	    .sender = PSC_getMyTID(),
	    .len = 0, },
	.type = CMD_BROKE_IO_CON };
    uint32_t myJobID = step->jobid;
    PSP_putTypedMsgBuf(&msg, "jobID", &myJobID, sizeof(myJobID));
    uint32_t myStepID = step->stepid;
    PSP_putTypedMsgBuf(&msg, "stepID", &myStepID, sizeof(myStepID));

    sendMsgToMother(&msg);
}

void fwCMD_enableSrunIO(Step_t *step)
{
    DDTypedMsg_t msg = {
	.header = {
	    .type = PSP_PF_MSG,
	    .dest = step->fwdata ? step->fwdata->tid : -1,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg) },
	.type = CMD_ENABLE_SRUN_IO };

    /* might happen that forwarder is already gone */
    if (!step->fwdata) return;
    fdbg(PSSLURM_LOG_IO, "to %s\n", PSC_printTID(step->fwdata->tid));
    sendMsg(&msg);
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

    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_PF_MSG,
	    .dest = fwdata->tid,
	    .sender = PSC_getMyTID(),
	    .len = 0, },
	.type = CMD_PRINT_CHILD_MSG };
    size_t left = msgLen;
    size_t chunkSize = sizeof(msg.buf) - sizeof(uint8_t) - sizeof(uint32_t);

    if (step) {
	/* reserved for additional rank */
	chunkSize -= sizeof(uint32_t);

	/* connection to srun broke */
	if (step->ioCon == IO_CON_BROKE) return -1;

	if (step->ioCon == IO_CON_ERROR) {
	    flog("I/O connection for %s is broken\n", Step_strID(step));
	    step->ioCon = IO_CON_BROKE;
	}
    }

    do {
	uint32_t chunk = left > chunkSize ? chunkSize : left;
	uint32_t len = htonl(chunk);
	msg.header.len = 0;
	PSP_putTypedMsgBuf(&msg, "type", &type, sizeof(type));
	if (step) {
	    uint32_t nRank = htonl(rank);
	    PSP_putTypedMsgBuf(&msg, "rank", &nRank, sizeof(nRank));
	}
	/* Add data chunk including its length mimicking addData */
	PSP_putTypedMsgBuf(&msg, "len", &len, sizeof(len));
	PSP_putTypedMsgBuf(&msg, "data", plMsg + msgLen - left, chunk);

	sendMsg(&msg);
	left -= chunk;
    } while (left);

    return 0;
}

void fwCMD_reattachTasks(Forwarder_Data_t *fwdata, uint32_t addr,
			 uint16_t ioPort, uint16_t ctlPort, char *sig)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_PF_MSG,
	    .dest = fwdata ? fwdata->tid : -1,
	    .sender = PSC_getMyTID(),
	    .len = 0 },
	.type = CMD_REATTACH_TASKS };

    /* might happen that forwarder is already gone */
    if (!fwdata) return;

    uint32_t nAddr = htonl(addr);
    PSP_putTypedMsgBuf(&msg, "addr", &nAddr, sizeof(nAddr));
    uint16_t nioPort = htons(ioPort);
    PSP_putTypedMsgBuf(&msg, "ioPort", &nioPort, sizeof(nioPort));
    uint16_t nctlPort = htons(ctlPort);
    PSP_putTypedMsgBuf(&msg, "ctlPort", &nctlPort, sizeof(nctlPort));
    /* Add string including its length mimicking addString */
    uint32_t len = htonl(PSP_strLen(sig));
    PSP_putTypedMsgBuf(&msg, "len", &len, sizeof(len));
    PSP_putTypedMsgBuf(&msg, "sigStr", sig, PSP_strLen(sig));

    sendMsg(&msg);
}

void fwCMD_finalize(Forwarder_Data_t *fwdata, PSLog_Msg_t *plMsg, int32_t rank)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_PF_MSG,
	    .dest = fwdata ? fwdata->tid : -1,
	    .sender = PSC_getMyTID(),
	    .len = 0, },
	.type = CMD_FW_FINALIZE };

    /* might happen that forwarder is already gone */
    if (!fwdata) return;

    /* Shall be okay since PSLog_Msg_t always fits DDTypedBufferMsg_t buf */
    uint32_t nRank = htonl(rank);
    PSP_putTypedMsgBuf(&msg, "rank", &nRank, sizeof(nRank));
    /* Add data including its length mimicking addData */
    uint32_t len = htonl(plMsg->header.len);
    PSP_putTypedMsgBuf(&msg, "len", &len, sizeof(len));
    PSP_putTypedMsgBuf(&msg, "plMsg", plMsg, plMsg->header.len);

    if (msg.header.dest == -1) flog("unknown destination for %s\n",
				    PSC_printTID(plMsg->header.sender));
    sendMsg(&msg);
}

void fwCMD_taskInfo(Forwarder_Data_t *fwdata, PS_Tasks_t *task)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_PF_MSG,
	    .dest = fwdata ? fwdata->tid : -1,
	    .sender = PSC_getMyTID(),
	    .len = 0 },
	.type = CMD_INFO_TASKS };

    /* might happen that forwarder is already gone */
    if (!fwdata) return;

    /* Add data including its length mimicking addData */
    uint32_t len = htonl(sizeof(*task));
    PSP_putTypedMsgBuf(&msg, "len", &len, sizeof(len));
    PSP_putTypedMsgBuf(&msg, "task", task, sizeof(*task));

    sendMsg(&msg);
}

void fwCMD_msgSrunProxy(Step_t *step, PSLog_Msg_t *lmsg, int32_t senderRank)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_PF_MSG,
	    .dest = step->fwdata ? step->fwdata->tid : -1,
	    .sender = lmsg->header.sender,
	    .len = 0 },
	.type = CMD_PRINT_CHILD_MSG };

    /* might happen if forwarder is already gone */
    if (!step->fwdata) return;

    /* connection to srun broke */
    if (step->ioCon == IO_CON_BROKE) return;

    if (step->ioCon == IO_CON_ERROR) {
	flog("I/O connection for %s is broken\n", Step_strID(step));
	step->ioCon = IO_CON_BROKE;
    }

    /* if msg from service rank, let it seem like it comes from first task */
    if (senderRank < 0) senderRank = step->globalTaskIds[step->localNodeId][0];

    /* no chunks required since sizeof(lmsg->buf) << msg.buf */
    uint8_t type = lmsg->type;
    PSP_putTypedMsgBuf(&msg, "type", &type, sizeof(type));
    uint32_t nRank = htonl(senderRank);
    PSP_putTypedMsgBuf(&msg, "rank", &nRank, sizeof(nRank));
    /* Add data chunk including its length mimicking addData */
    size_t msgLen = lmsg->header.len - offsetof(PSLog_Msg_t, buf);
    uint32_t len = htonl(msgLen);
    PSP_putTypedMsgBuf(&msg, "len", &len, sizeof(len));
    PSP_putTypedMsgBuf(&msg, "data", lmsg->buf, msgLen);

    sendMsg(&msg);
}
