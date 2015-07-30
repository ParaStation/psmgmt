/*
 * ParaStation
 *
 * Copyright (C) 2015 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "psslurmjob.h"
#include "psslurmforwarder.h"
#include "psslurmlog.h"
#include "psslurmconfig.h"
#include "psslurmcomm.h"
#include "psslurmproto.h"
#include "slurmcommon.h"

#include "plugincomm.h"
#include "pluginmalloc.h"
#include "pluginforwarder.h"
#include "pslog.h"
#include "selector.h"

#include "psslurmio.h"

typedef struct {
    PS_DataBuffer_t out;
    PS_DataBuffer_t err;
} IO_Msg_Buf_t;

static void writeIOmsg(char *msg, uint32_t msgLen, uint16_t taskid,
			uint8_t type, Forwarder_Data_t *fwdata, Step_t *step,
			uint32_t lrank)
{
    void *msgPtr;

    msgPtr = msgLen ? msg : NULL;

    mdbg(PSSLURM_LOG_IO, "%s: msgLen '%i' taskid '%i' type '%i'\n", __func__,
	    msgLen, taskid, type);
    /*
    char format[64];
    if (msgLen>0) {
	snprintf(format, sizeof(format), "%s: msg: '%%.%is'\n", __func__,
		    msgLen);
	mdbg(PSSLURM_LOG_IO, format, msg);
    }
    */

    if (type == STDOUT) {
	if (step->stdOutOpt == IO_NODE_FILE) {
	    doWriteP(fwdata->stdOut[1], msgPtr, msgLen);
	} else if (step->stdOutOpt == IO_RANK_FILE) {
	    doWriteP(step->outFDs[lrank], msgPtr, msgLen);
	} else {
	    srunSendIO(SLURM_IO_STDOUT, taskid, step, msgPtr, msgLen);
	}
    } else if (type == STDERR) {
	if (step->stdErrOpt == IO_NODE_FILE) {
	    doWriteP(fwdata->stdErr[1], msgPtr, msgLen);
	} else if (step->stdErrOpt == IO_RANK_FILE) {
	    doWriteP(step->errFDs[lrank], msgPtr, msgLen);
	} else if (step->pty) {
	    srunSendIO(SLURM_IO_STDOUT, taskid, step, msgPtr, msgLen);
	} else {
	    srunSendIO(SLURM_IO_STDERR, taskid, step, msgPtr, msgLen);
	}
    }
}

static int getWidth(int32_t num)
{
    int width = 1;

    while (num /= 10) width++;

    return width;
}

static void writeLabelIOmsg(char *msg, uint32_t msgLen, uint16_t taskid,
			uint8_t type, Forwarder_Data_t *fwdata, Step_t *step,
			uint32_t lrank)

{
    char label[128], format[64];
    char *ptr, *nl;
    uint32_t left, len;

    if (!step->labelIO || !msgLen ||
	(type == STDOUT &&
	(step->stdOutOpt == IO_SRUN || step->stdOutOpt == IO_SRUN_RANK)) ||
	(type == STDERR &&
	(step->stdOutOpt == IO_SRUN || step->stdOutOpt == IO_SRUN_RANK))) {
	writeIOmsg(msg, msgLen, taskid, type, fwdata, step, lrank);
	return;
    }

    /* prefix every new line with taskid */
    snprintf(format, sizeof(format), "%%0%du: ", getWidth(step->np -1));
    snprintf(label, sizeof(label), format, taskid);

    ptr = msg;
    left = msgLen;

    while (ptr && left > 0 && (nl = memchr(ptr, '\n', left))) {
	len = (nl +1) - ptr;
	writeIOmsg(label, strlen(label), taskid, type, fwdata, step, lrank);
	writeIOmsg(ptr, len, taskid, type, fwdata, step, lrank);
	left -= len;
	ptr = nl+1;
    }
}

static void handleBufferedMsg(char *msg, uint32_t len, PS_DataBuffer_t *buffer,
				Forwarder_Data_t *fwdata, Step_t *step,
				uint16_t taskid, uint8_t type, uint32_t lrank)
{
    uint32_t nlLen;
    char *nl;

    nl = len ? memrchr(msg, '\n', len) : NULL;

    if (nl || !len) {
	if (buffer->bufUsed) {
	    writeLabelIOmsg(buffer->buf, buffer->bufUsed, taskid, type,
			fwdata, step, lrank);
	    buffer->bufUsed = 0;
	}
	nlLen = nl ? nl - msg +1: len;
	writeLabelIOmsg(msg, nlLen, taskid, type, fwdata, step, lrank);
	if (len - nlLen > 0) {
	    addMemToMsg(msg + nlLen, len - nlLen, buffer);
	}
    } else {
	addMemToMsg(msg, len, buffer);
    }
}

static void handlePrintChildMsg(void *data, char *ptr)
{
    Forwarder_Data_t *fwdata = data;
    Step_t *step = fwdata->userData;
    uint8_t type;
    uint16_t taskid;
    uint32_t len, lrank, i;
    char *msg = NULL;
    static IO_Msg_Buf_t *lineBuf;
    static int32_t myNodeID = -1;
    static int initBuf = 0;

    /* read message */
    getUint8(&ptr, &type);
    getUint16(&ptr, &taskid);
    msg = getDataM((void **)&ptr, &len);

    if (myNodeID < 0) {
	if ((myNodeID = getMyNodeIndex(step->nodes, step->nrOfNodes)) == -1) {
	    mlog("%s: failed to lookup my node\n", __func__);
	    ufree(msg);
	    return;
	}
    }

    /* get local rank from taskid */
    if ((lrank = getLocalRankID(taskid, step, myNodeID)) == (uint32_t )-1) {
	mlog("%s: invalid node rank for taskid '%i' myNodeID '%i'\n",
		__func__, taskid, myNodeID);
	ufree(msg);
	return;
    }

    /* track I/O channels */
    if (!len) {
	if (type == STDOUT && step->outChannels) {
	    if (step->outChannels[lrank] == 0) {
		ufree(msg);
		return;
	    }
	    step->outChannels[lrank] = 0;
	}
	if (type == STDERR && step->errChannels) {
	    if (step->errChannels[lrank] == 0) {
		ufree(msg);
		return;
	    }
	    step->errChannels[lrank] = 0;
	}
    }

    /* handle unbuffered IO */
    if (!step->bufferedIO || step->pty) {
	writeIOmsg(msg, len, taskid, type, fwdata, step, lrank);
	ufree(msg);
	return;
    }

    /* handle buffered IO */
    if (!initBuf) {
	myNodeID = getMyNodeIndex(step->nodes, step->nrOfNodes);

	lineBuf = umalloc(sizeof(IO_Msg_Buf_t) *
				step->globalTaskIdsLen[myNodeID]);
	for (i=0; i<step->globalTaskIdsLen[myNodeID]; i++) {
	    lineBuf[i].out.buf = lineBuf[i].err.buf = NULL;
	    lineBuf[i].out.bufUsed = lineBuf[i].err.bufUsed = 0;
	}
	initBuf = 1;
    }

    if (type == STDOUT) {
	handleBufferedMsg(msg, len, &lineBuf[lrank].out, fwdata, step,
			    taskid, type, lrank);
    } else {
	handleBufferedMsg(msg, len, &lineBuf[lrank].err, fwdata, step,
			    taskid, type, lrank);
    }

    ufree(msg);
}

static void closeIOchannel(Forwarder_Data_t *fwdata, uint16_t taskid,
			    uint8_t type)
{
    PS_DataBuffer_t msg = { .buf = NULL };

    msg.bufUsed = 0;
    addUint8ToMsg(type, &msg);
    addUint16ToMsg(taskid, &msg);
    addDataToMsg(NULL, 0, &msg);
    handlePrintChildMsg(fwdata, msg.buf);

    ufree(msg.buf);
}

void stepFinalize(void *data)
{
    Forwarder_Data_t *fwdata = data;
    Step_t *step = fwdata->userData;
    uint32_t i, myNodeID;

    myNodeID = getMyNodeIndex(step->nodes, step->nrOfNodes);

    /* make sure to close all leftover I/O channels */
    for (i=0; i<step->globalTaskIdsLen[myNodeID]; i++) {
	if (step->outChannels && step->outChannels[i] != 0) {
	    closeIOchannel(fwdata, step->globalTaskIds[myNodeID][i], STDOUT);
	}
	if (step->errChannels && step->errChannels[i] != 0) {
	    closeIOchannel(fwdata, step->globalTaskIds[myNodeID][i], STDERR);
	}
    }
}

static void handleEnableSrunIO(void *data, char *ptr)
{
    Forwarder_Data_t *fwdata = data;
    Step_t *step = fwdata->userData;
    uint16_t i, taskCount;
    int32_t tid, rank;

    getUint16(&ptr, &taskCount);

    for (i=0; i<taskCount; i++) {
	getInt32(&ptr, &rank);
	getInt32(&ptr, &tid);
	addTask(&step->tasks.list, -1, tid, NULL, 0, rank);
    }

    srunEnableIO(step);
}

static void handleFWfinalize(void *data, char *ptr)
{
    Forwarder_Data_t *fwdata = data;
    PSLog_Msg_t *msg;
    uint32_t len;

    msg = getDataM((void **)&ptr, &len);

    /* close stdout/stderr */
    closeIOchannel(fwdata, msg->sender, STDOUT);
    closeIOchannel(fwdata, msg->sender, STDERR);

    /* let main psslurm forward FINALIZE to logger */
    forwardMsgtoMother((DDMsg_t *)msg);
}

int stepForwarderMsg(void *data, char *ptr, int32_t cmd)
{
    switch (cmd) {
	case CMD_PRINT_CHILD_MSG:
	    handlePrintChildMsg(data, ptr);
	    return 1;
	case CMD_ENABLE_SRUN_IO:
	    handleEnableSrunIO(data, ptr);
	    return 1;
	case CMD_FW_FINALIZE:
	    handleFWfinalize(data, ptr);
	    return 1;
    }

    return 0;
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

char *replaceStepSymbols(Step_t *step, int rank, char *path)
{
    char *hostname;
    Job_t *job;
    uint32_t arrayJobId = 0, arrayTaskId = 0;

    hostname = getConfValueC(&Config, "SLURM_HOSTNAME");
    if ((job = findJobById(step->jobid))) {
	arrayJobId = job->arrayJobId;
	arrayTaskId = job->arrayTaskId;
    }

    return replaceSymbols(step->jobid, step->stepid, hostname,
			    getStepLocalNodeID(step), step->username,
			    arrayJobId, arrayTaskId, rank, path);
}

char *replaceJobSymbols(Job_t *job, char *path)
{
    return replaceSymbols(job->jobid, SLURM_BATCH_SCRIPT, job->hostname,
			    0, job->username, job->arrayJobId, job->arrayTaskId,
			    0, path);
}

/*
 * supported symbols
 *
 * %A     Job array's master job allocation number.
 * %a     Job array ID (index) number.
 * %J     jobid.stepid of the running job. (e.g. "128.0")
 * %j     jobid of the running job.
 * %s     stepid of the running job.
 * %N     short hostname. This will create a separate IO file per node.
 * %n     Node identifier relative to current job (e.g. "0" is the first node
 *	    of the running job) This will create a separate IO file per node.
 * %t     task identifier (rank) relative to current job. This will
 *	    create a separate IO file per task.
 * %u     User name.
*/
char *replaceSymbols(uint32_t jobid, uint32_t stepid, char *hostname,
			int nodeid, char *username, uint32_t arrayJobId,
			uint32_t arrayTaskId, int rank, char *path)
{
    char *next, *ptr, *symbol, *symNum, *buf = NULL;
    char tmp[1024], symLen[64], symLen2[256];
    size_t symNumLen, len, bufSize = 0;
    int saved = 0;

    ptr = path;
    if (!(next = strchr(ptr, '%'))) {
	return ustrdup(path);
    }

    while (next) {
	symbol = symNum = next+1;
	len = next - ptr;
	strn2Buf(ptr, len, &buf, &bufSize);

	/* zero padding */
	symNumLen = 0;
	snprintf(symLen, sizeof(symLen), "%%u");
	while (symNum[0] >= 48 && symNum[0] <=57) symNum++;
	if ((symNumLen = symNum - symbol) >0) {
	    if (symNumLen <= sizeof(symLen) -3) {
		strcpy(symLen, "%0");
		strncat(symLen, symbol, symNumLen);
		strcat(symLen, "u");
		symbol += symNumLen;
	    }
	}

	switch (symbol[0]) {
	    case 'A':
		snprintf(tmp, sizeof(tmp), symLen, arrayJobId);
		str2Buf(tmp, &buf, &bufSize);
		saved = 1;
		break;
	    case 'a':
		snprintf(tmp, sizeof(tmp), symLen, arrayTaskId);
		str2Buf(tmp, &buf, &bufSize);
		saved = 1;
		break;
	    case 'J':
		snprintf(symLen2, sizeof(symLen2), "%s.%s", symLen, symLen);
		snprintf(tmp, sizeof(tmp), symLen2, jobid, stepid);
		str2Buf(tmp, &buf, &bufSize);
		saved = 1;
		break;
	    case 'j':
		snprintf(tmp, sizeof(tmp), symLen, jobid);
		str2Buf(tmp, &buf, &bufSize);
		saved = 1;
		break;
	    case 's':
		snprintf(tmp, sizeof(tmp), symLen, stepid);
		str2Buf(tmp, &buf, &bufSize);
		saved = 1;
		break;
	    case 'N':
		str2Buf(hostname, &buf, &bufSize);
		saved = 1;
		break;
	    case 'n':
		snprintf(tmp, sizeof(tmp), symLen, nodeid);
		str2Buf(tmp, &buf, &bufSize);
		saved = 1;
		break;
	    case 't':
		snprintf(tmp, sizeof(tmp), symLen, rank);
		str2Buf(tmp, &buf, &bufSize);
		saved = 1;
		break;
	    case 'u':
		str2Buf(username, &buf, &bufSize);
		saved = 1;
		break;
	}

	if (!saved) {
	    strn2Buf(next, 2 + symNumLen, &buf, &bufSize);
	}

	saved = 0;
	ptr = next + 2 + symNumLen;
	next = strchr(ptr, '%');
    }
    str2Buf(ptr, &buf, &bufSize);
    mdbg(PSSLURM_LOG_IO, "%s: orig '%s' result: '%s'\n", __func__, path, buf);

    return buf;
}

static char *addCwd(char *cwd, char *path)
{
    char *buf = NULL;
    size_t bufSize = 0;

    if (path[0] == '/' || path[0] == '.') {
	return path;
    }

    str2Buf(cwd, &buf, &bufSize);
    str2Buf("/", &buf, &bufSize);
    str2Buf(path, &buf, &bufSize);
    ufree(path);

    return buf;
}

void redirectJobOutput(Job_t *job)
{
    char *outFile, *errFile, *inFile, *defOutName;
    int fd, flags = 0;

    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    close(STDIN_FILENO);

    flags = getAppendFlags(job->appendMode);

    if (job->arrayTaskId != NO_VAL) {
	defOutName = "slurm-%A_%a.out";
    } else {
	defOutName = "slurm-%j.out";
    }

    /* stdout */
    if (!(strlen(job->stdOut))) {
	outFile = addCwd(job->cwd, replaceJobSymbols(job, defOutName));
    } else {
	outFile = addCwd(job->cwd, replaceJobSymbols(job, job->stdOut));
    }

    if ((fd = open(outFile, flags, 0666)) == -1) {
	mwarn(errno, "%s: open stdout '%s' failed :", __func__, outFile);
	exit(1);
    }
    if ((dup2(fd, STDOUT_FILENO)) == -1) {
	mwarn(errno, "%s: dup2(%i) '%s' failed :", __func__, fd, outFile);
	exit(1);
    }

    /* stderr */
    if (!(strlen(job->stdErr))) {
	errFile = addCwd(job->cwd, replaceJobSymbols(job, defOutName));
    } else {
	errFile = addCwd(job->cwd, replaceJobSymbols(job, job->stdErr));
    }

    if (strlen(job->stdErr)) {
	if ((fd = open(errFile, flags, 0666)) == -1) {
	    mwarn(errno, "%s: open stderr '%s' failed :", __func__, errFile);
	    exit(1);
	}
    }
    if ((dup2(fd, STDERR_FILENO)) == -1) {
	mwarn(errno, "%s: dup2(%i) '%s' failed :", __func__, fd, errFile);
	exit(1);
    }

    ufree(errFile);
    ufree(outFile);

    /* stdin */
    if (!(strlen(job->stdIn))) {
	inFile = ustrdup("/dev/null");
    } else {
	inFile = addCwd(job->cwd, replaceJobSymbols(job, job->stdIn));
    }
    if ((fd = open(inFile, O_RDONLY)) == -1) {
	mwarn(errno, "%s: open stdin '%s' failed :", __func__, inFile);
	exit(1);
    }
    if ((dup2(fd, STDIN_FILENO)) == -1) {
	mwarn(errno, "%s: dup2(%i) '%s' failed :", __func__, fd, inFile);
	exit(1);
    }
    ufree(inFile);
}

void redirectIORank(Step_t *step, int rank)
{
    char *ptr, *inFile;
    int fd;

    /* redirect stdin */
    if (step->stdInOpt == IO_RANK_FILE) {
	ptr = replaceStepSymbols(step, rank, step->stdIn);
	inFile = addCwd(step->cwd, ptr);

	if ((fd = open(inFile, O_RDONLY)) == -1) {
	    mwarn(errno, "%s: open stdin '%s' failed: ", __func__, inFile);
	    exit(1);
	}
	close(STDIN_FILENO);
	if ((dup2(fd, STDIN_FILENO)) == -1) {
	    mwarn(errno, "%s: stdin dup2(%u) failed: ", __func__, fd);
	    exit(1);
	}
    }
}

void redirectStepIO(Forwarder_Data_t *fwdata, Step_t *step)
{
    char *outFile = NULL, *errFile = NULL, *inFile;
    int flags = 0;
    int32_t myNodeID = -1;
    uint32_t i;

    if ((myNodeID = getMyNodeIndex(step->nodes, step->nrOfNodes)) == -1) {
	mlog("%s: getting my node ID failed\n", __func__);
    }

    flags = getAppendFlags(step->appendMode);

    if (setgid(step->gid) == -1) {
	mwarn(errno, "%s: setgid(%i) failed: ", __func__, step->gid);
    }

    /* need to create pipes as user, or the permission to /dev/stdX
     *  will be denied */
    if (seteuid(step->uid) == -1) {
	mwarn(errno, "%s: seteuid(%i) failed: ", __func__, step->uid);
	return;
    }

    /* stdout */
    if (step->stdOutOpt == IO_NODE_FILE || step->stdOutOpt == IO_GLOBAL_FILE) {
	outFile = addCwd(step->cwd, replaceStepSymbols(step, 0, step->stdOut));

	fwdata->stdOut[0] = -1;
	if ((fwdata->stdOut[1] = open(outFile, flags, 0666)) == -1) {
	    mwarn(errno, "%s: open stdout '%s' failed :", __func__, outFile);
	}
	mdbg(PSSLURM_LOG_IO, "%s: opt '%u' outfile: '%s' fd '%i'\n", __func__,
		step->stdOutOpt, outFile, fwdata->stdOut[1]);

    } else if (step->stdOutOpt == IO_RANK_FILE) {
	/* open files for all ranks */
	step->outFDs = umalloc(step->globalTaskIdsLen[myNodeID] * sizeof(int));

	for (i=0; i<step->globalTaskIdsLen[myNodeID]; i++) {
	    outFile = addCwd(step->cwd, replaceStepSymbols(step,
				step->globalTaskIds[myNodeID][i],
				step->stdOut));

	    if ((step->outFDs[i] = open(outFile, flags, 0666)) == -1) {
		mwarn(errno, "%s: open stdout '%s' failed :",
			__func__, outFile);
	    }
	    mdbg(PSSLURM_LOG_IO, "%s: outfile: '%s' fd '%i'\n", __func__,
		    outFile, fwdata->stdOut[1]);
	}

	if ((pipe(fwdata->stdOut)) == -1) {
	    mlog("%s: create stdout pipe failed\n", __func__);
	    return;
	}

    } else {
	if ((pipe(fwdata->stdOut)) == -1) {
	    mlog("%s: create stdout pipe failed\n", __func__);
	    return;
	}
	mdbg(PSSLURM_LOG_IO, "%s: stdout pipe '%i:%i'\n", __func__,
		fwdata->stdOut[0], fwdata->stdOut[1]);
    }

    /* stderr */
    if (step->stdErrOpt == IO_NODE_FILE || step->stdErrOpt == IO_GLOBAL_FILE) {
	errFile = addCwd(step->cwd, replaceStepSymbols(step, 0, step->stdErr));

	fwdata->stdErr[0] = -1;
	if (outFile && !(strcmp(outFile, errFile))) {
	    fwdata->stdErr[1] = fwdata->stdOut[1];
	} else {
	    if ((fwdata->stdErr[1] = open(errFile, flags, 0666)) == -1) {
		mwarn(errno, "%s: open stderr '%s' failed :",
			__func__, errFile);
	    }
	}
	mdbg(PSSLURM_LOG_IO, "%s: errfile: '%s' fd '%i'\n", __func__, errFile,
		fwdata->stdErr[1]);

    } else if (step->stdErrOpt == IO_RANK_FILE) {
	/* open files for all ranks */
	step->errFDs = umalloc(step->globalTaskIdsLen[myNodeID] * sizeof(int));

	for (i=0; i<step->globalTaskIdsLen[myNodeID]; i++) {
	    errFile = addCwd(step->cwd, replaceStepSymbols(step,
				step->globalTaskIds[myNodeID][i],
				step->stdErr));

	    if ((step->errFDs[i] = open(errFile, flags, 0666)) == -1) {
		mwarn(errno, "%s: open stderr '%s' failed :",
			__func__, errFile);
	    }
	    mdbg(PSSLURM_LOG_IO, "%s: errfile: '%s' fd '%i'\n", __func__,
		    errFile, fwdata->stdErr[1]);
	}
	if ((pipe(fwdata->stdErr)) == -1) {
	    mlog("%s: create stderr pipe failed\n", __func__);
	    return;
	}

    } else {
	if ((pipe(fwdata->stdErr)) == -1) {
	    mlog("%s: create stderr pipe failed\n", __func__);
	    return;
	}
	mdbg(PSSLURM_LOG_IO, "%s: stderr pipe '%i:%i'\n", __func__,
		fwdata->stdErr[0], fwdata->stdErr[1]);
    }

    /* stdin */
    if (step->stdInRank == -1 && step->stdIn && strlen(step->stdIn) > 0) {
	inFile = addCwd(step->cwd, replaceStepSymbols(step, 0, step->stdIn));

	fwdata->stdIn[1] = -1;
	if ((fwdata->stdIn[0] = open(inFile, O_RDONLY)) == -1) {
	    mwarn(errno, "%s: open stdin '%s' failed :",
		    __func__, inFile);
	}
	mdbg(PSSLURM_LOG_IO, "%s: infile: '%s' fd '%i'\n", __func__, inFile,
		fwdata->stdIn[0]);
    } else {
	if ((pipe(fwdata->stdIn)) == -1) {
	    mlog("%s: create stdin pipe failed\n", __func__);
	    return;
	}
	mdbg(PSSLURM_LOG_IO, "%s: stdin pipe '%i:%i'\n", __func__,
		fwdata->stdIn[0], fwdata->stdIn[1]);
    }

    if (seteuid(0) == -1) {
	mwarn(errno, "%s: seteuid(0) failed: ", __func__);
    };
    if (setgid(0) == -1) {
	mwarn(errno, "%s: setgid(0) failed: ", __func__);
    };
}

void redirectStepIO2(Forwarder_Data_t *fwdata, Step_t *step)
{
    char *outFile = NULL, *errFile = NULL, *inFile;
    int flags = 0;
    int32_t myNodeID = -1;
    uint32_t i;

    if ((myNodeID = getMyNodeIndex(step->nodes, step->nrOfNodes)) == -1) {
	mlog("%s: getting my node ID failed\n", __func__);
    }

    flags = getAppendFlags(step->appendMode);

    if (setgid(step->gid) == -1) {
	mwarn(errno, "%s: setgid(%i) failed: ", __func__, step->gid);
    }

    /* need to create pipes as user, or the permission to /dev/stdX
     *  will be denied */
    if (seteuid(step->uid) == -1) {
	mwarn(errno, "%s: seteuid(%i) failed: ", __func__, step->uid);
	return;
    }

    /* stdout */
    if (step->stdOutOpt == IO_NODE_FILE) {
	outFile = addCwd(step->cwd, replaceStepSymbols(step, 0, step->stdOut));

	fwdata->stdOut[0] = -1;
	if ((fwdata->stdOut[1] = open(outFile, flags, 0666)) == -1) {
	    mwarn(errno, "%s: open stdout '%s' failed :", __func__, outFile);
	}
	mdbg(PSSLURM_LOG_IO, "%s: outfile: '%s' fd '%i'\n", __func__, outFile,
		fwdata->stdOut[1]);
    } else if (step->stdOutOpt == IO_RANK_FILE) {
	/* open files for all ranks */
	step->outFDs = umalloc(step->globalTaskIdsLen[myNodeID] * sizeof(int));

	for (i=0; i<step->globalTaskIdsLen[myNodeID]; i++) {
	    outFile = addCwd(step->cwd, replaceStepSymbols(step,
				step->globalTaskIds[myNodeID][i],
				step->stdOut));

	    if ((step->outFDs[i] = open(outFile, flags, 0666)) == -1) {
		mwarn(errno, "%s: open stdout '%s' failed :",
			__func__, outFile);
	    }
	    mdbg(PSSLURM_LOG_IO, "%s: outfile: '%s' fd '%i'\n", __func__,
		    outFile, fwdata->stdOut[1]);
	}
    }

    /* stderr */
    if (step->stdErrOpt == IO_NODE_FILE) {
	errFile = addCwd(step->cwd, replaceStepSymbols(step, 0, step->stdErr));

	fwdata->stdErr[0] = -1;
	if (outFile && !(strcmp(outFile, errFile))) {
	    fwdata->stdErr[1] = fwdata->stdOut[1];
	} else {
	    if ((fwdata->stdErr[1] = open(errFile, flags, 0666)) == -1) {
		mwarn(errno, "%s: open stderr '%s' failed :",
			__func__, errFile);
	    }
	}
	mdbg(PSSLURM_LOG_IO, "%s: errfile: '%s' fd '%i'\n", __func__, errFile,
		fwdata->stdErr[1]);
    } else if (step->stdErrOpt == IO_RANK_FILE) {
	/* open files for all ranks */
	step->errFDs = umalloc(step->globalTaskIdsLen[myNodeID] * sizeof(int));

	for (i=0; i<step->globalTaskIdsLen[myNodeID]; i++) {
	    errFile = addCwd(step->cwd, replaceStepSymbols(step,
				step->globalTaskIds[myNodeID][i],
				step->stdErr));

	    if ((step->errFDs[i] = open(errFile, flags, 0666)) == -1) {
		mwarn(errno, "%s: open stderr '%s' failed :",
			__func__, errFile);
	    }
	    mdbg(PSSLURM_LOG_IO, "%s: errfile: '%s' fd '%i'\n", __func__,
		    errFile, fwdata->stdErr[1]);
	}
    }

    /* stdin */
    if (step->stdInRank == -1 && step->stdIn && strlen(step->stdIn) > 0) {
	inFile = addCwd(step->cwd, replaceStepSymbols(step, 0, step->stdIn));

	fwdata->stdIn[1] = -1;
	if ((fwdata->stdIn[0] = open(inFile, O_RDONLY)) == -1) {
	    mwarn(errno, "%s: open stdin '%s' failed :",
		    __func__, inFile);
	}
	mdbg(PSSLURM_LOG_IO, "%s: infile: '%s' fd '%i'\n", __func__, inFile,
		fwdata->stdIn[0]);
    }

    if (seteuid(0) == -1) {
	mwarn(errno, "%s: seteuid(0) failed: ", __func__);
    };
    if (setgid(0) == -1) {
	mwarn(errno, "%s: setgid(0) failed: ", __func__);
    };
}

void sendEnableSrunIO(Step_t *step)
{
    PS_DataBuffer_t data = { .buf = NULL };
    uint16_t count = 0;
    struct list_head *pos;
    PS_Tasks_t *tasks;

    /* can happen, if forwarder is already gone */
    if (!step->fwdata) return;

    count = countTasks(&step->tasks.list);

    addInt32ToMsg(CMD_ENABLE_SRUN_IO, &data);
    addUint16ToMsg(count, &data);

    list_for_each(pos, &step->tasks.list) {
	if (!(tasks = list_entry(pos, PS_Tasks_t, list))) break;
	addInt32ToMsg(tasks->childRank, &data);
	addInt32ToMsg(tasks->forwarderTID, &data);
    }

    mdbg(PSSLURM_LOG_IO, "%s: to controlSocket: %u\n", __func__,
	    step->fwdata->controlSocket);
    sendFWMsg(step->fwdata->controlSocket, &data);
    ufree(data.buf);
}

void printChildMessage(Forwarder_Data_t *fwdata, char *msg, uint32_t msgLen,
			uint8_t type, int32_t taskid)
{
    PS_DataBuffer_t data = { .buf = NULL };

    /* can happen, if forwarder is already gone */
    if (!fwdata) return;

    /* if msg from service rank, let it seem like it comes from task 0 */
    if (taskid < 0) taskid = 0;

    addInt32ToMsg(CMD_PRINT_CHILD_MSG, &data);
    addUint8ToMsg(type, &data);
    addUint16ToMsg(taskid, &data);
    addDataToMsg(msg, msgLen, &data);

    sendFWMsg(fwdata->controlSocket, &data);
    ufree(data.buf);
}

void sendFinMessage(Forwarder_Data_t *fwdata, PSLog_Msg_t *msg)
{
    PS_DataBuffer_t data = { .buf = NULL };

    /* can happen, if forwarder is already gone */
    if (!fwdata) return;

    addInt32ToMsg(CMD_FW_FINALIZE, &data);
    addDataToMsg(msg, msg->header.len, &data);

    sendFWMsg(fwdata->controlSocket, &data);
    ufree(data.buf);
}

int handleUserOE(int sock, void *data)
{
    static char buf[1024];
    Forwarder_Data_t *fwdata = data;
    Step_t *step = fwdata->userData;
    int32_t size, ret;
    uint16_t type;

    if (step->pty) {
	type = (sock == fwdata->stdOut[1]) ? SLURM_IO_STDOUT : SLURM_IO_STDERR;
    } else {
	type = (sock == fwdata->stdOut[0]) ? SLURM_IO_STDOUT : SLURM_IO_STDERR;
    }

    if ((size = doRead(sock, buf, sizeof(buf) - 1)) <= 0) {
	Selector_remove(sock);
	close(sock);
    }

    mdbg(PSSLURM_LOG_IO, "%s: sock '%i' forward '%s' size '%u'\n", __func__,
	    sock, type == SLURM_IO_STDOUT ? "stdout" : "stderr", size);

    /* eof to srun */
    if (size <0) size = 0;
    if (size >0) buf[size] = '\0';

    /* disable for now with new I/O architecture */
    return 0;

    /* forward data to srun, size of 0 means EOF for stream */
    if ((ret = srunSendIO(type, 0, step, buf, size)) != (size + 10)) {
	if (!step->labelIO) {
	    mwarn(errno, "%s: sending IO failed: size:%i ret:%i error:%i ",
		    __func__, (size +10), ret, errno);
	}
    }

    return 0;
}

int setFilePermissions(Job_t *job)
{
    if (!job->jobscript) return 1;

    if ((chown(job->jobscript, job->uid, job->gid)) == -1) {
	mlog("%s: chown(%i:%i) '%s' failed : %s\n", __func__,
		job->uid, job->gid, job->jobscript,
		strerror(errno));
	return 1;
    }

    if ((chmod(job->jobscript, 0700)) == -1) {
	mlog("%s: chmod 0700 on '%s' failed : %s\n", __func__,
		job->jobscript, strerror(errno));
	return 1;
    }

    return 0;
}
