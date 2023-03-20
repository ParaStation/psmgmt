/*
 * ParaStation
 *
 * Copyright (C) 2020-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmfwcomm.h"

#include <arpa/inet.h>
#include <stddef.h>

#include "list.h"
#include "pscommon.h"
#include "pslog.h"
#include "pspluginprotocol.h"
#include "psserial.h"

#include "pluginmalloc.h"
#include "psidcomm.h"

#include "slurmcommon.h"
#include "psslurmcomm.h"
#include "psslurmio.h"
#include "psslurmlog.h"
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

static void handleInfoTasks(Forwarder_Data_t *fwdata, char *ptr)
{
    Step_t *step = fwdata->userData;

    PS_Tasks_t *task = getDataM(&ptr, NULL);
    list_add_tail(&task->next, &step->tasks);

    fdbg(PSSLURM_LOG_PROCESS, "%s child %s rank %i task %u from %u\n",
	 Step_strID(step), PSC_printTID(task->childTID), task->childRank,
	 countRegTasks(step->tasks.next),
	 step->globalTaskIdsLen[step->localNodeId]);

    if (step->globalTaskIdsLen[step->localNodeId] ==
	countRegTasks(&step->tasks)) {
	/* all local tasks finished spawning */
	sendTaskPids(step);
    }
}

static void handleFWfinalize(Forwarder_Data_t *fwdata, char *ptr)
{
    Step_t *step = fwdata->userData;
    PSLog_Msg_t *msg = getDataM(&ptr, NULL);
    PStask_ID_t sender = msg->header.sender;

    mdbg(PSSLURM_LOG_IO, "%s from %s\n", __func__, PSC_printTID(sender));

    if (!(step->taskFlags & LAUNCH_PTY)) {
	/* close stdout/stderr */
	uint32_t grank = msg->sender;
	IO_closeChannel(fwdata, grank, STDOUT);
	IO_closeChannel(fwdata, grank, STDERR);
    }

    PS_Tasks_t *task = findTaskByFwd(&step->tasks, sender);
    if (!task) {
	mlog("%s: no task for forwarder %s\n", __func__, PSC_printTID(sender));
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

static void handleSattachTasks(Forwarder_Data_t *fwdata, char *ptr)
{
    Step_t *step = fwdata->userData;
    uint16_t ioPort, ctlPort;
    uint32_t ioAddr;

    getUint32(&ptr, &ioAddr);
    getUint16(&ptr, &ioPort);
    getUint16(&ptr, &ctlPort);
    char *sig = getStringM(&ptr);

    IO_sattachTasks(step, ioAddr, ioPort, ctlPort, sig);

    ufree(sig);
}

static void handlePrintStepMsg(Forwarder_Data_t *fwdata, char *ptr)
{
    uint8_t type;
    uint32_t rank;
    size_t msglen;

    /* read message */
    getUint8(&ptr, &type);
    getUint32(&ptr, &rank);
    char *msg = getDataM(&ptr, &msglen);

    IO_printStepMsg(fwdata, msg, msglen, rank, type);

    ufree(msg);
}

bool fwCMD_handleMthrStepMsg(DDTypedBufferMsg_t *msg, Forwarder_Data_t *fwdata)
{
    if (msg->header.type != PSP_PF_MSG) return false;

    switch ((PSSLURM_Fw_Cmds_t)msg->type) {
    case CMD_PRINT_CHILD_MSG:
	handlePrintStepMsg(fwdata, msg->buf);
	break;
    case CMD_ENABLE_SRUN_IO:
	handleEnableSrunIO(fwdata);
	break;
    case CMD_FW_FINALIZE:
	handleFWfinalize(fwdata, msg->buf);
	break;
    case CMD_REATTACH_TASKS:
	handleSattachTasks(fwdata, msg->buf);
	break;
    case CMD_INFO_TASKS:
	handleInfoTasks(fwdata, msg->buf);
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

static void handlePrintJobMsg(Forwarder_Data_t *fwdata, char *ptr)
{
    uint8_t type;
    size_t msglen;

    /* read message */
    getUint8(&ptr, &type);
    char *msg = getDataM(&ptr, &msglen);

    IO_printJobMsg(fwdata, msg, msglen, type);

    ufree(msg);
}

bool fwCMD_handleMthrJobMsg(DDTypedBufferMsg_t *msg, Forwarder_Data_t *fwdata)
{
    if (msg->header.type != PSP_PF_MSG) return false;

    switch ((PSSLURM_Fw_Cmds_t)msg->type) {
    case CMD_PRINT_CHILD_MSG:
	handlePrintJobMsg(fwdata, msg->buf);
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

static void changeEnv(int cmd, DDTypedBufferMsg_t *msg)
{
    char *ptr = msg->buf;

    uint32_t jobid;
    getUint32(&ptr, &jobid);
    uint32_t stepid;
    getUint32(&ptr, &stepid);

    Step_t *step = Step_findByStepId(jobid, stepid);
    if (!step) {
	flog("warning: step %u:%u already gone\n", jobid, stepid);
	return;
    }

    char *var = getStringM(&ptr);

    if (cmd == CMD_SETENV) {
	char *val = getStringM(&ptr);
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

static void handleInitComplete(DDTypedBufferMsg_t *msg)
{
    char *ptr = msg->buf;

    uint32_t jobid;
    getUint32(&ptr, &jobid);
    uint32_t stepid;
    getUint32(&ptr, &stepid);

    Step_t *step = Step_findByStepId(jobid, stepid);
    if (!step) {
	flog("warning: step %u:%u already gone\n", jobid, stepid);
	return;
    }

    handleCachedMsg(step);
    releaseDelayedSpawns(step->jobid, step->stepid);
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
	    mdbg(-1, "%s: Unhandled Log-type %s\n", __func__,
		 PSLog_printMsgType(lmsg->type));
	    return false;
	}
    } else if (msg->header.type == PSP_PLUG_PSSLURM) {
	switch ((PSSLURM_Fw_Cmds_t)msg->type) {
	case CMD_BROKE_IO_CON:
	    handleBrokeIOcon(msg);
	    break;
	case CMD_SETENV:
	case CMD_UNSETENV:
	    changeEnv((PSSLURM_Fw_Cmds_t)msg->type, msg);
	    break;
	case CMD_INIT_COMPLETE:
	    handleInitComplete(msg);
	    break;
	default:
	    mdbg(PSSLURM_LOG_IO_VERB, "%s: Unhandled type %d\n", __func__,
		 msg->type);
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
    mdbg(PSSLURM_LOG_IO, "%s: to %s\n", __func__,
	 PSC_printTID(step->fwdata->tid));
    sendMsg(&msg);
}

void fwCMD_printMsg(Job_t *job, Step_t *step, char *plMsg, uint32_t msgLen,
		    uint8_t type, int32_t rank)
{
    Forwarder_Data_t *fwdata = job ? job->fwdata : step ? step->fwdata : NULL;

    if (job && step) {
	flog("error: job and step are mutually exclusive\n");
	return;
    }

    /* might happen that forwarder is already gone */
    if (!fwdata) return;

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
	if (step->ioCon == IO_CON_BROKE) return;

	if (step->ioCon == IO_CON_ERROR) {
	    flog("I/O connection for %s is broken\n", Step_strID(step));
	    step->ioCon = IO_CON_BROKE;
	}

	/* msg from service rank, make believe it comes from first task */
	if (rank < 0) rank = step->globalTaskIds[step->localNodeId][0];
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

void fwCMD_finalize(Forwarder_Data_t *fwdata, PSLog_Msg_t *plMsg)
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
    /* Add data including its length mimicking addData */
    uint32_t len = htonl(plMsg->header.len);
    PSP_putTypedMsgBuf(&msg, "len", &len, sizeof(len));
    PSP_putTypedMsgBuf(&msg, "plMsg", plMsg, plMsg->header.len);

    if (msg.header.dest == -1) mlog("%s unknown destination for %s\n", __func__,
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
