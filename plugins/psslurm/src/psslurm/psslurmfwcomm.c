/*
 * ParaStation
 *
 * Copyright (C) 2020-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmlog.h"
#include "psslurmproto.h"

#include "psidcomm.h"
#include "pluginmalloc.h"

#include "psslurmfwcomm.h"

typedef enum {
    CMD_PRINT_CHILD_MSG = 100,
    CMD_ENABLE_SRUN_IO,
    CMD_FW_FINALIZE,
    CMD_REATTACH_TASKS,
    CMD_INFO_TASKS,
    CMD_STEP_TIMEOUT,
    CMD_BROKE_IO_CON,
} PSSLURM_Fw_Cmds_t;

static void handleStepTimeout(Forwarder_Data_t *fwdata)
{
    Step_t *step = fwdata->userData;

    step->timeout = true;
}

static void handleInfoTasks(Forwarder_Data_t *fwdata, char *ptr)
{
    Step_t *step = fwdata->userData;
    PS_Tasks_t *task;
    size_t len;

    task = getDataM(&ptr, &len);
    list_add_tail(&task->next, &step->tasks);

    fdbg(PSSLURM_LOG_PROCESS, "%s child %s rank %i task %u from %u\n",
	 strStepID(step), PSC_printTID(task->childTID), task->childRank,
	 countRegTasks(step->tasks.next),
	 step->globalTaskIdsLen[step->localNodeId]);

    if (step->globalTaskIdsLen[step->localNodeId] ==
	countRegTasks(&step->tasks)) {
	sendTaskPids(step);
    }
}

static void handleFWfinalize(Forwarder_Data_t *fwdata, char *ptr)
{
    Step_t *step = fwdata->userData;
    size_t len;
    PSLog_Msg_t *msg = getDataM(&ptr, &len);
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
    sendMsgToMother(msg);
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

int fwCMD_handleMthrStepMsg(PSLog_Msg_t *msg, Forwarder_Data_t *fwdata)
{
    PSSLURM_Fw_Cmds_t type = (PSSLURM_Fw_Cmds_t)msg->type;

    switch (type) {
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
	    flog("unexpected msg, type %d (PSlog type %s) from TID %s (%s) "
		 "jobid %s\n", type, PSLog_printMsgType(msg->type),
		 PSC_printTID(msg->sender), fwdata->pTitle, fwdata->jobID);
	    return 0;
    }

    return 1;
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

int fwCMD_handleMthrJobMsg(PSLog_Msg_t *msg, Forwarder_Data_t *fwdata)
{
    PSSLURM_Fw_Cmds_t type = (PSSLURM_Fw_Cmds_t)msg->type;

    switch (type) {
	case CMD_PRINT_CHILD_MSG:
	    handlePrintJobMsg(fwdata, msg->buf);
	    break;
	default:
	    flog("unexpected msg, type %d (PSlog type %s) from TID %s (%s) "
		 "jobid %s\n", type, PSLog_printMsgType(msg->type),
		 PSC_printTID(msg->sender), fwdata->pTitle, fwdata->jobID);
	    return 0;
    }

    return 1;
}

static void handleBrokeIOcon(PSLog_Msg_t *msg)
{
    DDBufferMsg_t *bMsg = (DDBufferMsg_t *)msg;
    size_t used = offsetof(PSLog_Msg_t, buf) - offsetof(DDBufferMsg_t, buf);
    uint32_t jobID, stepID;

    PSP_getMsgBuf(bMsg, &used, "jobID", &jobID, sizeof(jobID));
    PSP_getMsgBuf(bMsg, &used, "stepID", &stepID, sizeof(stepID));

    /* step might already be deleted */
    Step_t *step = findStepByStepId(jobID, stepID);
    if (!step) return;

    if (step->ioCon == IO_CON_NORM) step->ioCon = IO_CON_ERROR;
}

int fwCMD_handleFwStepMsg(PSLog_Msg_t *msg, Forwarder_Data_t *fwdata)
{
    PSSLURM_Fw_Cmds_t type = (PSSLURM_Fw_Cmds_t)msg->type;
    switch (type) {
    case CMD_BROKE_IO_CON:
	handleBrokeIOcon(msg);
	break;
    default:
	mdbg(PSSLURM_LOG_IO_VERB, "%s: Unhandled type %d\n", __func__, type);
	return 0;
    }

    return 1;
}

void fwCMD_stepTimeout(Forwarder_Data_t *fwdata)
{
    PSLog_Msg_t msg = {
	.header = {
	    .type = PSP_CC_MSG,
	    .dest = fwdata ? fwdata->tid : -1,
	    .sender = PSC_getMyTID(),
	    .len = offsetof(PSLog_Msg_t, buf) },
	.version = PLUGINFW_PROTO_VERSION,
	.type = (PSLog_msg_t)CMD_STEP_TIMEOUT,
	.sender = -1};

    /* might happen that forwarder is already gone */
    if (!fwdata) return;

    sendMsg(&msg);
}

void fwCMD_brokeIOcon(Step_t *step)
{
    PSLog_Msg_t msg = {
	.header = {
	    .type = PSP_CC_MSG,
	    .dest = PSC_getTID(-1,0),
	    .sender = PSC_getMyTID(),
	    .len = offsetof(PSLog_Msg_t, buf) },
	.version = PLUGINFW_PROTO_VERSION,
	.type = (PSLog_msg_t)CMD_BROKE_IO_CON,
	.sender = -1};
    DDBufferMsg_t *bMsg = (DDBufferMsg_t *)&msg;
    uint32_t myJobID = step->jobid, myStepID = step->stepid;

    PSP_putMsgBuf(bMsg, "jobID", &myJobID, sizeof(myJobID));
    PSP_putMsgBuf(bMsg, "stepID", &myStepID, sizeof(myStepID));

    sendMsgToMother(&msg);
}

void fwCMD_enableSrunIO(Step_t *step)
{
    PSLog_Msg_t msg = {
	.header = {
	    .type = PSP_CC_MSG,
	    .dest = step->fwdata ? step->fwdata->tid : -1,
	    .sender = PSC_getMyTID(),
	    .len = offsetof(PSLog_Msg_t, buf) },
	.version = PLUGINFW_PROTO_VERSION,
	.type = (PSLog_msg_t)CMD_ENABLE_SRUN_IO,
	.sender = -1};

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

    PSLog_Msg_t msg = {
	.header = {
	    .type = PSP_CC_MSG,
	    .dest = fwdata->tid,
	    .sender = PSC_getMyTID(),
	    .len = offsetof(PSLog_Msg_t, buf) },
	.version = PLUGINFW_PROTO_VERSION,
	.type = (PSLog_msg_t)CMD_PRINT_CHILD_MSG,
	.sender = -1};
    size_t left = msgLen;
    size_t chunkSize = sizeof(msg.buf) - sizeof(uint8_t) - sizeof(uint32_t);

    if (step) {
	/* reserved for additional rank */
	chunkSize -= sizeof(uint32_t);

	/* connection to srun broke */
	if (step->ioCon == IO_CON_BROKE) return;

	if (step->ioCon == IO_CON_ERROR) {
	    flog("I/O connection for %s is broken\n", strStepID(step));
	    step->ioCon = IO_CON_BROKE;
	}

	/* msg from service rank, make believe it comes from first task */
	if (rank < 0) rank = step->globalTaskIds[step->localNodeId][0];
    }

    do {
	uint32_t chunk = left > chunkSize ? chunkSize : left;
	uint32_t len = htonl(chunk);
	DDBufferMsg_t *bMsg = (DDBufferMsg_t *)&msg;
	msg.header.len = offsetof(PSLog_Msg_t, buf);

	PSP_putMsgBuf(bMsg, "type", &type, sizeof(type));
	if (step) {
	    uint32_t nRank = htonl(rank);
	    PSP_putMsgBuf(bMsg, "rank", &nRank, sizeof(nRank));
	}
	/* Add data chunk including its length mimicking addData */
	PSP_putMsgBuf(bMsg, "len", &len, sizeof(len));
	PSP_putMsgBuf(bMsg, "data", plMsg + msgLen - left, chunk);

	sendMsg(&msg);
	left -= chunk;
    } while (left);
}

void fwCMD_reattachTasks(Forwarder_Data_t *fwdata, uint32_t addr,
			 uint16_t ioPort, uint16_t ctlPort, char *sig)
{
    PSLog_Msg_t msg = {
	.header = {
	    .type = PSP_CC_MSG,
	    .dest = fwdata ? fwdata->tid : -1,
	    .sender = PSC_getMyTID(),
	    .len = offsetof(PSLog_Msg_t, buf) },
	.version = PLUGINFW_PROTO_VERSION,
	.type = (PSLog_msg_t)CMD_REATTACH_TASKS,
	.sender = -1};
    DDBufferMsg_t *bMsg = (DDBufferMsg_t *)&msg;
    uint32_t nAddr = htonl(addr);
    uint16_t nioPort = htons(ioPort), nctlPort = htons(ctlPort);
    uint32_t len = htonl(PSP_strLen(sig));

    /* might happen that forwarder is already gone */
    if (!fwdata) return;

    PSP_putMsgBuf(bMsg, "addr", &nAddr, sizeof(nAddr));
    PSP_putMsgBuf(bMsg, "ioPort", &nioPort, sizeof(nioPort));
    PSP_putMsgBuf(bMsg, "ctlPort", &nctlPort, sizeof(nctlPort));
    /* Add string including its length mimicking addString */
    PSP_putMsgBuf(bMsg, "len", &len, sizeof(len));
    PSP_putMsgBuf(bMsg, "sigStr", sig, PSP_strLen(sig));

    sendMsg(&msg);
}

void fwCMD_finalize(Forwarder_Data_t *fwdata, PSLog_Msg_t *plMsg)
{
    PSLog_Msg_t msg = {
	.header = {
	    .type = PSP_CC_MSG,
	    .dest = fwdata ? fwdata->tid : -1,
	    .sender = PSC_getMyTID(),
	    .len = offsetof(PSLog_Msg_t, buf) },
	.version = PLUGINFW_PROTO_VERSION,
	.type = (PSLog_msg_t)CMD_FW_FINALIZE,
	.sender = -1};
    DDBufferMsg_t *bMsg = (DDBufferMsg_t *)&msg;
    uint32_t len = htonl(plMsg->header.len);

    /* might happen that forwarder is already gone */
    if (!fwdata) return;

    /* This shall be okay since FINALIZE messages are << PSLog_Msg_t */
    /* Add data including its length mimicking addData */
    PSP_putMsgBuf(bMsg, "len", &len, sizeof(len));
    PSP_putMsgBuf(bMsg, "plMsg", plMsg, plMsg->header.len);

    if (msg.header.dest == -1) mlog("%s unknown destination for %s\n", __func__,
				    PSC_printTID(plMsg->header.sender));
    sendMsg(&msg);
}

void fwCMD_taskInfo(Forwarder_Data_t *fwdata, PS_Tasks_t *task)
{
    PSLog_Msg_t msg = {
	.header = {
	    .type = PSP_CC_MSG,
	    .dest = fwdata ? fwdata->tid : -1,
	    .sender = PSC_getMyTID(),
	    .len = offsetof(PSLog_Msg_t, buf) },
	.version = PLUGINFW_PROTO_VERSION,
	.type = (PSLog_msg_t)CMD_INFO_TASKS,
	.sender = -1};
    DDBufferMsg_t *bMsg = (DDBufferMsg_t *)&msg;
    uint32_t len = htonl(sizeof(*task));

    /* might happen that forwarder is already gone */
    if (!fwdata) return;

    /* Add data including its length mimicking addData */
    PSP_putMsgBuf(bMsg, "len", &len, sizeof(len));
    PSP_putMsgBuf(bMsg, "task", task, sizeof(*task));

    sendMsg(&msg);
}

void fwCMD_msgSrunProxy(Step_t *step, PSLog_Msg_t *lmsg, int32_t senderRank)
{
    PSLog_Msg_t msg = {
	.header = {
	    .type = PSP_CC_MSG,
	    .dest = step->fwdata ? step->fwdata->tid : -1,
	    .sender = lmsg->header.sender,
	    .len = offsetof(PSLog_Msg_t, buf) },
	.version = PLUGINFW_PROTO_VERSION,
	.type = (PSLog_msg_t)CMD_PRINT_CHILD_MSG,
	.sender = -1};
    const size_t chunkSize = sizeof(msg.buf) - sizeof(uint8_t)
	- sizeof(uint32_t) - sizeof(uint32_t);
    char *buf = lmsg->buf;
    size_t msgLen = lmsg->header.len - offsetof(PSLog_Msg_t, buf);
    size_t left = msgLen;

    /* might happen if forwarder is already gone */
    if (!step->fwdata) return;

    /* connection to srun broke */
    if (step->ioCon == IO_CON_BROKE) return;

    if (step->ioCon == IO_CON_ERROR) {
	flog("I/O connection for %s is broken\n", strStepID(step));
	step->ioCon = IO_CON_BROKE;
    }

    /* if msg from service rank, let it seem like it comes from first task */
    if (senderRank < 0) senderRank = step->globalTaskIds[step->localNodeId][0];

    do {
	uint32_t chunk = left > chunkSize ? chunkSize : left;
	uint8_t type = lmsg->type;
	uint32_t nRank = htonl(senderRank);
	uint32_t len = htonl(chunk);
	DDBufferMsg_t *bMsg = (DDBufferMsg_t *)&msg;
	msg.header.len = offsetof(PSLog_Msg_t, buf);

	PSP_putMsgBuf(bMsg, "type", &type, sizeof(type));
	PSP_putMsgBuf(bMsg, "rank", &nRank, sizeof(nRank));
	/* Add data chunk including its length mimicking addData */
	PSP_putMsgBuf(bMsg, "len", &len, sizeof(len));
	PSP_putMsgBuf(bMsg, "data", buf + msgLen - left, chunk);

	sendMsg(&msg);
	left -= chunk;
    } while (left);
}
