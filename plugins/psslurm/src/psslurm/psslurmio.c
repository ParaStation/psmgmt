/*
 * ParaStation
 *
 * Copyright (C) 2015-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#define _GNU_SOURCE
#include "psslurmio.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "pscio.h"
#include "psenv.h"
#include "pslog.h"
#include "psserial.h"
#include "psstrbuf.h"
#include "selector.h"
#include "pluginconfig.h"
#include "pluginforwarder.h"
#include "pluginmalloc.h"

#include "slurmcommon.h"
#include "psslurmcomm.h"
#include "psslurmconfig.h"
#include "psslurmlog.h"
#include "psslurmproto.h"

#define RING_BUFFER_LEN 1024

#define MAX_LINE_BUF_LENGTH 1024*1024

typedef struct {
    PS_DataBuffer_t out;
    PS_DataBuffer_t err;
} IO_Msg_Buf_t;

typedef struct {
    uint32_t grank;
    uint8_t type;
    char *msg;
    uint32_t msgLen;
} RingMsgBuffer_t;

/** number of sattach connections */
static int sattachCon = 0;

static int sattachSockets[MAX_SATTACH_SOCKETS];

static int sattachCtlSock[MAX_SATTACH_SOCKETS];

static int sattachAddr[MAX_SATTACH_SOCKETS];

static RingMsgBuffer_t ringBuf[RING_BUFFER_LEN];

static uint32_t ringBufLast = 0;

static uint32_t ringBufStart = 0;

const char *IO_strType(int type)
{
    static char buf[128];

    switch (type) {
	case SLURM_IO_STDIN:
	    return "IO_STDIN";
	case SLURM_IO_STDOUT:
	    return "IO_STDOUT";
	case SLURM_IO_STDERR:
	    return "IO_STDERR";
	case SLURM_IO_ALLSTDIN:
	    return "IO_ALLSTDIN";
	case SLURM_IO_CONNECTION_TEST:
	    return "IO_CON_TEST";
	default:
	    snprintf(buf, sizeof(buf), "<unknown: %i>", type);
	    return buf;
    }
}

const char *IO_strOpt(int opt)
{
    static char buf[128];

    switch (opt) {
	case IO_UNDEF:
	    return "IO_UNDEF";
	case IO_SRUN:
	    return "IO_SRUN";
	case IO_SRUN_RANK:
	    return "IO_SRUN_RANK";
	case IO_GLOBAL_FILE:
	    return "IO_GLOBAL_FILE";
	case IO_RANK_FILE:
	    return "IO_RANK_FILE";
	case IO_NODE_FILE:
	    return "IO_NODE_FILE";
	default:
	    snprintf(buf, sizeof(buf), "<unknown: %i>", opt);
	    return buf;
    }
}

void IO_init(void)
{
    uint16_t i;

    for (i=0; i<MAX_SATTACH_SOCKETS; i++) {
	sattachSockets[i] = -1;
	sattachCtlSock[i] = -1;
	sattachAddr[i] = -1;
    }

    for (i=0; i<RING_BUFFER_LEN; i++) {
	ringBuf[i].grank = -1;
	ringBuf[i].type = -1;
	ringBuf[i].msg = NULL;
	ringBuf[i].msgLen = 0;
    }
}

static void forward2Sattach(char *msg, uint32_t msgLen, uint32_t grank,
			    uint8_t type)
{
    int i, ret, error;
    IO_Slurm_Header_t ioh;

    ioh.type = (type == STDOUT) ?  SLURM_IO_STDOUT : SLURM_IO_STDERR;
    ioh.grank = grank;
    ioh.len = msgLen;

    for (i=0; i<MAX_SATTACH_SOCKETS; i++) {
	if (sattachSockets[i] != -1) {
	    ret = srunSendIOEx(sattachSockets[i], &ioh, msg, &error);
	    if (ret <0) {
		if (Selector_isRegistered(sattachSockets[i])) {
		    Selector_remove(sattachSockets[i]);
		}
		close(sattachSockets[i]);
		sattachSockets[i] = -1;
		sattachCtlSock[i] = -1;
		sattachCon--;
	    }
	}
    }
}

static void msg2Buffer(char *msg, uint32_t msgLen, uint32_t grank, uint8_t type)
{
    if (!msgLen) return;

    int stype = (type == STDOUT) ?  SLURM_IO_STDOUT : SLURM_IO_STDERR;
    RingMsgBuffer_t *rBuf = &ringBuf[ringBufLast];
    rBuf->grank = grank;
    rBuf->type = stype;
    if (rBuf->msgLen < msgLen) {
	rBuf->msg = urealloc(rBuf->msg, msgLen);
    }
    rBuf->msgLen = msgLen;
    memcpy(rBuf->msg, msg, msgLen);

    ringBufLast = (ringBufLast + 1 == RING_BUFFER_LEN) ? 0 : ringBufLast + 1;

    if (ringBufLast == ringBufStart) {
	ringBufStart = (ringBufStart + 1 == RING_BUFFER_LEN) ?
						    0 : ringBufStart + 1;
    }
}

static void IO_writeMsg(Forwarder_Data_t *fwdata, char *msg, uint32_t msgLen,
			uint32_t grank, uint8_t type, uint32_t lrank)
{
    Step_t *step = fwdata->userData;
    void *msgPtr = msgLen ? msg : NULL;

    fdbg(PSSLURM_LOG_IO, "msgLen %u grank %u type %s(%u) local rank %u "
	    "sattach %i\n", msgLen, grank, PSLog_printMsgType(type), type,
	    lrank, sattachCon);

    /* discard output from exiting ranks for steps with pty */
    if (step->taskFlags & LAUNCH_PTY && grank > 0) return;

    /* forward the message to all sattach processes */
    if (sattachCon > 0) forward2Sattach(msgPtr, msgLen, grank, type);

    if (type == STDOUT) {
	if (step->stdOutOpt == IO_NODE_FILE) {
	    PSCio_sendP(fwdata->stdOut[1], msgPtr, msgLen);
	} else if (step->stdOutOpt == IO_RANK_FILE) {
	    PSCio_sendP(step->outFDs[lrank], msgPtr, msgLen);
	} else {
	    srunSendIO(SLURM_IO_STDOUT, grank, step, msgPtr, msgLen);
	}
    } else if (type == STDERR) {
	if (step->stdErrOpt == IO_NODE_FILE) {
	    PSCio_sendP(fwdata->stdErr[1], msgPtr, msgLen);
	} else if (step->stdErrOpt == IO_RANK_FILE) {
	    PSCio_sendP(step->errFDs[lrank], msgPtr, msgLen);
	} else if (step->taskFlags & LAUNCH_PTY) {
	    srunSendIO(SLURM_IO_STDOUT, grank, step, msgPtr, msgLen);
	} else {
	    srunSendIO(SLURM_IO_STDERR, grank, step, msgPtr, msgLen);
	}
    }

    /* save message to ring buffer for later connecting sattach processes */
    msg2Buffer(msg, msgLen, grank, type);
}

static int getWidth(int32_t num)
{
    int width = 1;

    while (num /= 10) width++;

    return width;
}

static void writeLabelIOmsg(Forwarder_Data_t *fwdata, char *msg,
			    uint32_t msgLen, uint32_t grank, uint8_t type,
			    uint32_t lrank)

{
    Step_t *step = fwdata->userData;

    if (!(step->taskFlags & LAUNCH_LABEL_IO) || !msgLen ||
	(type == STDOUT &&
	(step->stdOutOpt == IO_SRUN || step->stdOutOpt == IO_SRUN_RANK)) ||
	(type == STDERR &&
	(step->stdOutOpt == IO_SRUN || step->stdOutOpt == IO_SRUN_RANK))) {
	/* no label necessary or srun will add it */
	IO_writeMsg(fwdata, msg, msgLen, grank, type, lrank);
	return;
    }

    /* prefix every new line with grank (IO_RANK_FILE, IO_NODE_FILE) */
    char label[128];
    snprintf(label, sizeof(label), "%0*u: ", getWidth(step->np -1), grank);

    ssize_t left = msgLen;
    char *nl, *ptr = msg;

    while (left > 0 && (nl = memchr(ptr, '\n', left))) {
	uint32_t len = (nl +1) - ptr;
	IO_writeMsg(fwdata, label, strlen(label), grank, type, lrank);
	IO_writeMsg(fwdata, ptr, len, grank, type, lrank);
	left -= len;
	ptr = nl+1;
    }
}

static void handleBufferedMsg(Forwarder_Data_t *fwdata, char *msg, uint32_t len,
			      PS_DataBuffer_t *buffer, uint32_t grank,
			      uint8_t type, uint32_t lrank)
{
    char *nl = len ? memrchr(msg, '\n', len) : NULL;
    if (nl || !len || buffer->used + len > MAX_LINE_BUF_LENGTH) {
	/* messages with newline or empty */
	uint32_t nlLen = nl ? nl - msg + 1 : len;

	if (buffer->used) {
	    /* add new data to msg buffer so it can be written in one piece */
	    memToDataBuffer(msg, nlLen, buffer);

	    /* write data saved in the buffer */
	    writeLabelIOmsg(fwdata, buffer->buf, buffer->used, grank,
			    type, lrank);
	    buffer->used = 0;
	} else {
	    /* write data including newline */
	    writeLabelIOmsg(fwdata, msg, nlLen, grank, type, lrank);
	}

	if (len - nlLen > 0) {
	    /* save data after newline to buffer */
	    memToDataBuffer(msg + nlLen, len - nlLen, buffer);
	}
    } else {
	/* save data without newline to buffer */
	memToDataBuffer(msg, len, buffer);
    }
}

void IO_closeChannel(Forwarder_Data_t *fwdata, uint32_t grank, uint8_t type)
{
    IO_printStepMsg(fwdata, NULL, 0, grank, type);
}

void IO_printJobMsg(Forwarder_Data_t *fwdata, char *msg, size_t msgLen,
		    uint8_t type)
{
    Job_t *job = fwdata->userData;

    if (type == STDOUT) {
	/* write to stdout socket */
	PSCio_sendP(job->stdOutFD, msg, msgLen);
	mdbg(PSSLURM_LOG_IO_VERB, "%s: write job %u sock %u stdout msg %s\n",
	     __func__, job->jobid, job->stdOutFD, msg);
    } else if (type == STDERR) {
	/* write to stderr socket */
	PSCio_sendP(job->stdErrFD, msg, msgLen);
	mdbg(PSSLURM_LOG_IO_VERB, "%s: write job %u sock %u stderr msg %s\n",
	     __func__, job->jobid, job->stdErrFD, msg);
    } else {
	flog("unknown type %u for job %u\n", type, job->jobid);
    }
}

void __IO_printStepMsg(Forwarder_Data_t *fwdata, char *msg, size_t msgLen,
		       uint32_t grank, uint8_t type, const char *caller,
		       const int line)
{
    Step_t *step = fwdata->userData;
    static IO_Msg_Buf_t *lineBuf;
    static bool initBuf = false;

    /* get local rank from global rank */
    uint32_t lrank = getLocalRankID(grank, step);
    if (lrank == NO_VAL) {
	flog("error: local rank for global rank %i %s not found,"
	     " caller %s:%i\n", grank, Step_strID(step), caller, line);
	return;
    }

    /* adjust global rank on pack basis,
     * has to be done *after* getLocalRankID()! */
    grank -= step->packTaskOffset;

    /* track I/O channels */
    if (!msgLen) {
	if (type == STDOUT && step->outChannels) {
	    if (!step->outChannels[lrank]) return;
	    step->outChannels[lrank] = 0;
	}
	if (type == STDERR && step->errChannels) {
	    if (!step->errChannels[lrank]) return;
	    step->errChannels[lrank] = 0;
	}
    }

    /* handle unbuffered I/O */
    if (type == STDERR || (!(step->taskFlags & LAUNCH_LABEL_IO)
			   && !(step->taskFlags & LAUNCH_BUFFERED_IO))
	|| step->taskFlags & LAUNCH_PTY) {
	IO_writeMsg(fwdata, msg, msgLen, grank, type, lrank);
	return;
    }

    /* handle buffered I/O */
    if (!initBuf) {
	lineBuf = umalloc(sizeof(IO_Msg_Buf_t)
			  * step->globalTaskIdsLen[step->localNodeId]);
	for (uint32_t i=0; i < step->globalTaskIdsLen[step->localNodeId]; i++) {
	    lineBuf[i].out.buf = lineBuf[i].err.buf = NULL;
	    lineBuf[i].out.used = lineBuf[i].err.used = 0;
	}
	initBuf = true;
    }

    handleBufferedMsg(fwdata, msg, msgLen,
		      (type == STDOUT) ?
		      &lineBuf[lrank].out : &lineBuf[lrank].err,
		      grank, type, lrank);
}

void IO_finalize(Forwarder_Data_t *fwdata)
{
    Step_t *step = fwdata->userData;
    uint32_t myNodeID = step->localNodeId;

    /* make sure to close all leftover I/O channels */
    for (uint32_t i = 0; i < step->globalTaskIdsLen[myNodeID]; i++) {
	/* use global rank */
	uint32_t grank = step->globalTaskIds[myNodeID][i] +
			 step->packTaskOffset;

	if (step->outChannels && step->outChannels[i] != 0) {
	    IO_closeChannel(fwdata, grank, STDOUT);
	}
	if (step->errChannels && step->errChannels[i] != 0) {
	    IO_closeChannel(fwdata, grank, STDERR);
	}
    }

    /* send task exit to sattach processes */
    sendTaskExit(step, sattachCtlSock, sattachAddr);

    /* close all sattach sockets */
    for (uint32_t i = 0; i < MAX_SATTACH_SOCKETS; i++) {
	if (sattachSockets[i] == -1) continue;

	if (Selector_isRegistered(sattachSockets[i])) {
	    Selector_remove(sattachSockets[i]);
	}
	close(sattachSockets[i]);
	close(sattachCtlSock[i]);
    }

    /* ensure to wait for all answers from srun */
    while (findConnectionByStep(step)) Swait(1);
}

void IO_sattachTasks(Step_t *step, uint32_t ioAddr, uint16_t ioPort,
		     uint16_t ctlPort, char *sig)
{
    int sock = srunOpenIOConnectionEx(step, ioAddr, ioPort, sig);
    if (sock == -1) {
	mlog("%s: I/O connection to srun %u:%u failed\n", __func__,
	     ioAddr, ioPort);
	return;
    }

    mdbg(PSSLURM_LOG_IO, "%s: opened connection to %u:%u ctlPort %u\n",
	 __func__, ioAddr, ioPort, ctlPort);

    int sockIndex = -1;
    for (int i = 0; i < MAX_SATTACH_SOCKETS; i++) {
	if (sattachSockets[i] == -1) {
	    sattachSockets[i] = sock;
	    sattachCtlSock[i] = ctlPort;
	    sattachAddr[i] = ioAddr;
	    sattachCon++;
	    sockIndex = i;
	    break;
	}
    }

    if (sockIndex < 0) {
	mlog("%s: no more free sattach sockets available\n", __func__);
	close(sock);
	return;
    }

    /* send previous buffered output */
    uint32_t index = ringBufStart;
    for (int i = 0; i < RING_BUFFER_LEN; i++) {
	int ret, error;
	RingMsgBuffer_t *rBuf = &ringBuf[index];
	if (!rBuf->msg) break;

	IO_Slurm_Header_t ioh;
	ioh.type = rBuf->type;
	ioh.grank = rBuf->grank;
	ioh.len = rBuf->msgLen;
	ret = srunSendIOEx(sattachSockets[sockIndex], &ioh, rBuf->msg, &error);

	if (ret < 0) {
	    mlog("%s: sending IO failed\n", __func__);
	    close(sattachSockets[sockIndex]);
	    sattachSockets[sockIndex] = -1;
	    sattachCtlSock[sockIndex] = -1;
	    sattachCon--;
	    return;
	}

	index = (index + 1 == RING_BUFFER_LEN) ? 0 : index + 1;
	if (index == ringBufLast) break;
    }

    if (Selector_register(sock, handleSrunIOMsg, step) == -1) {
	flog("Selector_register(%i) srun I/O socket failed\n", sock);
    }
}

static int getAppendFlags(uint8_t appendMode)
{
    int flags = 0;

    if (!appendMode) {
	/* TODO: use default of configuration JobFileAppend */
	flags |= O_CREAT|O_WRONLY|O_TRUNC|O_APPEND;
    } else if (appendMode == OPEN_MODE_APPEND) {
	flags |= O_CREAT|O_WRONLY|O_APPEND;
    } else {
	flags |= O_CREAT|O_WRONLY|O_TRUNC|O_APPEND;
    }

    return flags;
}

/**
 * @brief Replace various symbols in a path
 *
 * Supported symbols:
 *
 * %A     Job array's master job allocation number
 * %a     Job array ID (index) number
 * %J     jobid.stepid of the running job. (e.g. "128.0")
 * %j     jobid of the running job
 * %s     stepid of the running job
 * %N     short hostname. This will create a separate IO file per node.
 * %n     Node identifier relative to current job (e.g. "0" is the first node
 *	    of the running job) This will create a separate IO file per node.
 * %t     task identifier (rank) relative to current job. This will
 *	    create a separate IO file per task.
 * %u     User name
 * %x     Job name
 *
 * @param jobid Unique job identifier
 *
 * @param stepid Unique step identifier
 *
 * @param nodeid Job local node ID
 *
 * @param username Username of step owner
 *
 * @param arrayJobId Unique array job identifier
 *
 * @parm arrayTaskId Unique array task identifier
 *
 * @param rank The rank of the process
 *
 * @param path The string (path) holding the symbols to replace
 *
 * @param Returns a string holding the result or NULL on error. The caller
 * is responsible to free the allocated memory after use.
*/
static char *replaceSymbols(uint32_t jobid, uint32_t stepid, char *hostname,
			    int nodeid, char *username, uint32_t arrayJobId,
			    uint32_t arrayTaskId, int rank, char *path,
			    char *jobname)
{
    strbuf_t buf = strbufNew(path);

    char *ptr = path;
    char *next = strchr(ptr, '%');
    if (!next) return strbufSteal(buf);

    strbufClear(buf);
    while (next) {
	char tmp[1024], symLen[64], symLen2[256];
	char *symNum = next + 1;
	char *symbol = symNum;

	strbufAddNum(buf, ptr, next - ptr);

	/* zero padding */
	snprintf(symLen, sizeof(symLen), "%%u");
	while (symNum[0] >= 48 && symNum[0] <=57) symNum++;
	size_t symNumLen = symNum - symbol;
	if (symNumLen > 0 && symNumLen <= sizeof(symLen) - 3) {
	    strcpy(symLen, "%0");
	    strncat(symLen, symbol, symNumLen);
	    strcat(symLen, "u");
	    symbol += symNumLen;
	}

	switch (symbol[0]) {
	case 'A':
	    snprintf(tmp, sizeof(tmp), symLen, arrayJobId);
	    strbufAdd(buf, tmp);
	    break;
	case 'a':
	    snprintf(tmp, sizeof(tmp), symLen, arrayTaskId);
	    strbufAdd(buf, tmp);
	    break;
	case 'J':
	    snprintf(symLen2, sizeof(symLen2), "%s.%s", symLen, symLen);
	    snprintf(tmp, sizeof(tmp), symLen2, jobid, stepid);
	    strbufAdd(buf, tmp);
	    break;
	case 'j':
	    snprintf(tmp, sizeof(tmp), symLen, jobid);
	    strbufAdd(buf, tmp);
	    break;
	case 's':
	    snprintf(tmp, sizeof(tmp), symLen, stepid);
	    strbufAdd(buf, tmp);
	    break;
	case 'N':
	    strbufAdd(buf, hostname);
	    break;
	case 'n':
	    snprintf(tmp, sizeof(tmp), symLen, nodeid);
	    strbufAdd(buf, tmp);
	    break;
	case 't':
	    snprintf(tmp, sizeof(tmp), symLen, rank);
	    strbufAdd(buf, tmp);
	    break;
	case 'u':
	    strbufAdd(buf, username);
	    break;
	case 'x':
	    if (jobname) {
		strbufAdd(buf, jobname);
		break;
	    }
	    __attribute__((fallthrough));
	default:
	    strbufAddNum(buf, next, 2 + symNumLen);
	}

	ptr = next + 2 + symNumLen;
	next = strchr(ptr, '%');
    }
    strbufAdd(buf, ptr);
    fdbg(PSSLURM_LOG_IO, "orig '%s' result: '%s'\n", path, strbufStr(buf));

    return strbufSteal(buf);
}

char *IO_replaceStepSymbols(Step_t *step, int rank, char *path)
{
    uint32_t arrayJobId = 0, arrayTaskId = 0;

    char *hostname = getConfValueC(Config, "SLURM_HOSTNAME");
    Job_t *job = Job_findById(step->jobid);
    if (job) {
	arrayJobId = job->arrayJobId;
	arrayTaskId = job->arrayTaskId;
    }

    char *jobname = envGet(step->env, "SLURM_JOB_NAME");

    return replaceSymbols(step->jobid, step->stepid, hostname,
			  step->localNodeId, step->username, arrayJobId,
			  arrayTaskId, rank, path, jobname);
}

char *IO_replaceJobSymbols(Job_t *job, char *path)
{
    char *jobname = envGet(job->env, "SLURM_JOB_NAME");

    return replaceSymbols(job->jobid, SLURM_BATCH_SCRIPT, job->hostname,
			  0, job->username, job->arrayJobId, job->arrayTaskId,
			  0, path, jobname);
}

static char *addCwd(char *cwd, char *path)
{
    if (path[0] == '/' || path[0] == '.') return path;

    strbuf_t buf = strbufNew(NULL);
    strbufAdd(buf, cwd);
    strbufAdd(buf, "/");
    strbufAdd(buf, path);
    ufree(path);

    return strbufSteal(buf);
}

void IO_redirectJob(Forwarder_Data_t *fwdata, Job_t *job)
{
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    close(STDIN_FILENO);

    /* stdout */
    if (dup2(fwdata->stdOut[1], STDOUT_FILENO) == -1) {
	mwarn(errno, "%s: dup2(%i)", __func__, fwdata->stdOut[1]);
	exit(1);
    }
    close(fwdata->stdOut[0]);

    /* stderr */
    if (dup2(fwdata->stdErr[1], STDERR_FILENO) == -1) {
	mwarn(errno, "%s: dup2(%i)", __func__, fwdata->stdErr[1]);
	exit(1);
    }
    close(fwdata->stdErr[0]);

    /* stdin */
    int fd = open(job->stdIn, O_RDONLY);
    if (fd == -1) {
	mwarn(errno, "%s: open stdin '%s' failed", __func__, job->stdIn);
	exit(1);
    }
    if (dup2(fd, STDIN_FILENO) == -1) {
	mwarn(errno, "%s: dup2(%i) '%s' failed", __func__, fd, job->stdIn);
	exit(1);
    }
}

int IO_redirectRank(Step_t *step, int rank)
{
    int fd;

    /* redirect stdin */
    if (step->stdInOpt == IO_RANK_FILE) {
	char *ptr = IO_replaceStepSymbols(step, rank, step->stdIn);
	char *inFile = addCwd(step->cwd, ptr);

	fd = open(inFile, O_RDONLY);
	if (fd == -1) {
	    mwarn(errno, "%s: open stdin '%s' failed", __func__, inFile);
	    return 0;
	}
	close(STDIN_FILENO);
	if (dup2(fd, STDIN_FILENO) == -1) {
	    mwarn(errno, "%s: stdin dup2(%u) failed", __func__, fd);
	    return 0;
	}
    }

    if (step->taskFlags & LAUNCH_PTY && rank >0) {
	close(STDIN_FILENO);
	fd = open("/dev/null", O_RDONLY);
	dup2(fd, STDIN_FILENO);
    }

    return 1;
}

int IO_openJobPipes(Forwarder_Data_t *fwdata)
{
    Job_t *job = fwdata->userData;

    /* stdout */
    if (pipe(fwdata->stdOut) == -1) {
	fwarn(errno, "open stdout pipe for job %u failed", job->jobid);
	return 0;
    }
    fdbg(PSSLURM_LOG_IO, "stdout pipe %i:%i for job %u\n", fwdata->stdOut[0],
	 fwdata->stdOut[1], job->jobid);

    /* stderr */
    if (pipe(fwdata->stdErr) == -1) {
	fwarn(errno, "create stderr pipe for job %u failed", job->jobid);
	return 0;
    }
    fdbg(PSSLURM_LOG_IO, "stderr pipe %i:%i for job %u\n", fwdata->stdErr[0],
	 fwdata->stdErr[1], job->jobid);

    return 1;
}

void IO_openStepPipes(Forwarder_Data_t *fwdata, Step_t *step)
{
    /* stdout */
    if (step->stdOutOpt != IO_NODE_FILE && step->stdOutOpt != IO_GLOBAL_FILE) {
	if (pipe(fwdata->stdOut) == -1) {
	    fwarn(errno, "create stdout pipe failed");
	    return;
	}
	fdbg(PSSLURM_LOG_IO, "stdout pipe %i:%i\n",
	     fwdata->stdOut[0], fwdata->stdOut[1]);
    }

    /* stderr */
    if (step->stdErrOpt != IO_NODE_FILE && step->stdErrOpt != IO_GLOBAL_FILE) {
	if (pipe(fwdata->stdErr) == -1) {
	    fwarn(errno, "create stderr pipe failed");
	    return;
	}
	fdbg(PSSLURM_LOG_IO, "stderr pipe %i:%i\n",
		fwdata->stdErr[0], fwdata->stdErr[1]);
    }

    /* stdin */
    if (!(step->stdInRank == -1 && step->stdIn && strlen(step->stdIn) > 0)) {
	if (pipe(fwdata->stdIn) == -1) {
	    fwarn(errno, "create stdin pipe failed");
	    return;
	}
	fdbg(PSSLURM_LOG_IO, "stdin pipe %i:%i\n",
	     fwdata->stdIn[0], fwdata->stdIn[1]);
    }
}

int IO_forwardJobData(int sock, void *data)
{
    static char buf[1024];
    Forwarder_Data_t *fwdata = data;
    Job_t *job = fwdata->userData;

    /* read from child */
    ssize_t size = PSCio_recvBuf(sock, buf, sizeof(buf));
    if (size <= 0) {
	Selector_remove(sock);
	mdbg(PSSLURM_LOG_IO, "%s: job %u close std[out|err] sock %i\n",
	     __func__, job->jobid, sock);
	close(sock);
	return 0;
    }

    if (sock == fwdata->stdOut[0]) {
	/* write to stdout socket */
	PSCio_sendP(job->stdOutFD, buf, size);
	mdbg(PSSLURM_LOG_IO_VERB, "%s: write job %u sock %u stdout msg %s\n",
	     __func__, job->jobid, sock, buf);
    } else if (sock == fwdata->stdErr[0]) {
	/* write to stderr socket */
	PSCio_sendP(job->stdErrFD, buf, size);
	mdbg(PSSLURM_LOG_IO_VERB, "%s: write job %u sock %u stderr msg %s\n",
	     __func__, job->jobid, sock, buf);
    } else {
	flog("unknown socket %i for job %u\n", sock, job->jobid);
    }

    return 0;
}

void IO_initJobFilenames(Forwarder_Data_t *fwdata)
{
    Job_t *job = fwdata->userData;

    /* stdin */
    char *inFile;
    if (!strlen(job->stdIn)) {
	inFile = ustrdup("/dev/null");
    } else {
	inFile = addCwd(job->cwd, IO_replaceJobSymbols(job, job->stdIn));
    }
    ufree(job->stdIn);
    job->stdIn = inFile;

    /* stdout */
    char *outFile;
    if (strlen(job->stdOut)) {
	outFile = addCwd(job->cwd, IO_replaceJobSymbols(job, job->stdOut));
    } else {
	char *defOutName = (job->arrayTaskId != NO_VAL) ?
			    "slurm-%A_%a.out" : "slurm-%j.out";
	outFile = addCwd(job->cwd, IO_replaceJobSymbols(job, defOutName));
    }
    ufree(job->stdOut);
    job->stdOut = outFile;

    /* stderr */
    if (strlen(job->stdErr)) {
	char *errFile = addCwd(job->cwd, IO_replaceJobSymbols(job,job->stdErr));
	ufree(job->stdErr);
	job->stdErr = errFile;
     }
}

void IO_openJobIOfiles(Forwarder_Data_t *fwdata)
{
    Job_t *job = fwdata->userData;
    int flags = getAppendFlags(job->appendMode);

    /* redirect stdout */
    fdbg(PSSLURM_LOG_IO, "job %u stdout file %s\n", job->jobid, job->stdOut);
    job->stdOutFD = open(job->stdOut, flags, 0666);
    if (job->stdOutFD == -1) {
	fwarn(errno, "open stdout '%s' failed", job->stdOut);
	exit(1);
    }

    Selector_register(fwdata->stdOut[0], IO_forwardJobData, fwdata);
    PSCio_setFDCloExec(fwdata->stdOut[0], true);
    close(fwdata->stdOut[1]);

    /* redirect stderr */
    if (strlen(job->stdErr)) {
	fdbg(PSSLURM_LOG_IO, "job %u stderr file %s\n", job->jobid, job->stdErr);
	job->stdErrFD = open(job->stdErr, flags, 0666);
	if (job->stdErrFD == -1) {
	    fwarn(errno, "open stderr '%s' failed for job %u",
		  job->stdErr, job->jobid);
	    exit(1);
	}
    } else {
	job->stdErrFD = job->stdOutFD;
    }

    Selector_register(fwdata->stdErr[0], IO_forwardJobData, fwdata);
    PSCio_setFDCloExec(fwdata->stdErr[0], true);
    close(fwdata->stdErr[1]);
}

void IO_redirectStep(Forwarder_Data_t *fwdata, Step_t *step)
{
    char *outFile = NULL, *errFile = NULL, *inFile;
    int32_t myNodeID = step->localNodeId;
    int flags = getAppendFlags(step->appendMode);

    /* stdout */
    if (step->stdOutOpt == IO_NODE_FILE ||
	(!myNodeID && step->stdOutOpt == IO_GLOBAL_FILE)) {
	outFile = addCwd(step->cwd,
			 IO_replaceStepSymbols(step, 0, step->stdOut));

	fwdata->stdOut[0] = -1;
	fwdata->stdOut[1] = open(outFile, flags, 0666);
	if (fwdata->stdOut[1] == -1) {
	    mwarn(errno, "%s: open stdout '%s' failed", __func__, outFile);
	}
	mdbg(PSSLURM_LOG_IO, "%s: opt %u outfile: '%s' fd %i\n", __func__,
	     step->stdOutOpt, outFile, fwdata->stdOut[1]);

    } else if (step->stdOutOpt == IO_RANK_FILE) {
	/* open separate files for all ranks */
	step->outFDs = umalloc(step->globalTaskIdsLen[myNodeID] * sizeof(int));

	for (uint32_t i = 0; i < step->globalTaskIdsLen[myNodeID]; i++) {
	    outFile = addCwd(step->cwd, IO_replaceStepSymbols(step,
			     step->globalTaskIds[myNodeID][i],
			     step->stdOut));

	    step->outFDs[i] = open(outFile, flags, 0666);
	    if (step->outFDs[i] == -1) {
		mwarn(errno, "%s: open stdout '%s' failed", __func__, outFile);
	    }
	    mdbg(PSSLURM_LOG_IO, "%s: outfile: '%s' fd %i\n", __func__,
		 outFile, fwdata->stdOut[1]);
	}
    }

    /* stderr */
    if (step->stdErrOpt == IO_NODE_FILE ||
	(!myNodeID && step->stdErrOpt == IO_GLOBAL_FILE)) {
	errFile = addCwd(step->cwd,
			 IO_replaceStepSymbols(step, 0, step->stdErr));

	fwdata->stdErr[0] = -1;
	if (outFile && !strcmp(outFile, errFile)) {
	    fwdata->stdErr[1] = fwdata->stdOut[1];
	} else {
	    fwdata->stdErr[1] = open(errFile, flags, 0666);
	    if (fwdata->stdErr[1] == -1) {
		mwarn(errno, "%s: open stderr '%s' failed", __func__, errFile);
	    }
	}
	mdbg(PSSLURM_LOG_IO, "%s: errfile: '%s' fd %i\n", __func__, errFile,
	     fwdata->stdErr[1]);

    } else if (step->stdErrOpt == IO_RANK_FILE) {
	/* open separate files for all ranks */
	step->errFDs = umalloc(step->globalTaskIdsLen[myNodeID] * sizeof(int));

	for (uint32_t i = 0; i < step->globalTaskIdsLen[myNodeID]; i++) {
	    errFile = addCwd(step->cwd, IO_replaceStepSymbols(step,
			     step->globalTaskIds[myNodeID][i],
			     step->stdErr));

	    step->errFDs[i] = open(errFile, flags, 0666);
	    if (step->errFDs[i] == -1) {
		mwarn(errno, "%s: open stderr '%s' failed", __func__, errFile);
	    }
	    mdbg(PSSLURM_LOG_IO, "%s: errfile: '%s' fd %i\n", __func__,
		 errFile, fwdata->stdErr[1]);
	}
    }

    /* stdin */
    if (step->stdInRank == -1 && step->stdIn && strlen(step->stdIn) > 0) {
	inFile = addCwd(step->cwd, IO_replaceStepSymbols(step, 0, step->stdIn));

	fwdata->stdIn[1] = -1;
	fwdata->stdIn[0] = open(inFile, O_RDONLY);
	if (fwdata->stdIn[0] == -1) {
	    mwarn(errno, "%s: open stdin '%s' failed", __func__, inFile);
	}
	mdbg(PSSLURM_LOG_IO, "%s: infile: '%s' fd %i\n", __func__, inFile,
	     fwdata->stdIn[0]);
    }
}

int handleUserOE(int sock, void *data)
{
    Forwarder_Data_t *fwdata = data;
    Step_t *step = fwdata->userData;
    uint16_t type;

    if (step->taskFlags & LAUNCH_PTY) {
	type = (sock == fwdata->stdOut[1]) ? SLURM_IO_STDOUT : SLURM_IO_STDERR;
    } else {
	type = (sock == fwdata->stdOut[0]) ? SLURM_IO_STDOUT : SLURM_IO_STDERR;
    }

    static char buf[1024];
    ssize_t size = PSCio_recvBufS(sock, buf, sizeof(buf) - 1);
    if (size <= 0) {
	fdbg(PSSLURM_LOG_IO, "close sock %i ret %li\n", sock, size);
	Selector_remove(sock);
	close(sock);
    }

    fdbg(PSSLURM_LOG_IO, "sock %i forward %s size %zi\n", sock,
	 type == SLURM_IO_STDOUT ? "stdout" : "stderr", size);

    /* EOF to srun */
    if (size < 0) size = 0;
    if (size > 0) buf[size] = '\0';

    /* disable for now with new I/O architecture */
    return 0;

    /* forward data to srun, size of 0 means EOF for stream */
    int32_t ret = srunSendIO(type, 0, step, buf, size);
    if (ret != (size + 10) && !(step->taskFlags & LAUNCH_LABEL_IO)) {
	mwarn(errno, "%s: sending IO failed: size:%zi ret:%i error: %i",
	      __func__, (size + 10), ret, errno);
    }

    return 0;
}
