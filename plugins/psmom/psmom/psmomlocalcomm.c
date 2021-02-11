/*
 * ParaStation
 *
 * Copyright (C) 2010-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/wait.h>
#include <pwd.h>

#include "pscio.h"
#include "selector.h"
#include "pluginmalloc.h"
#include "pluginpartition.h"

#include "psaccounthandles.h"
#include "pspamhandles.h"

#include "psmom.h"
#include "psmomlog.h"
#include "psmomcomm.h"
#include "psmomconv.h"
#include "psmomforwarder.h"
#include "psmomchild.h"
#include "psmomscript.h"
#include "psmompscomm.h"
#include "psmomcollect.h"
#include "psmomacc.h"
#include "psmominteractive.h"
#include "psmompartition.h"
#include "psmomproto.h"

#include "psmomlocalcomm.h"

#define UNIX_BUFFER_ALLOC 4096 /* the start size for the tcp message buffer */
#define UNIX_BUFFER_GROW 1024  /* min grow size on realloc() message buffer */

/** Master socket (type UNIX) for clients to connect. */
int masterSocket = -1;

static int getChildTypeByForwarder(int type)
{
    switch (type) {
	case FORWARDER_COPY:
	    return PSMOM_CHILD_COPY;
	case FORWARDER_INTER:
	    return PSMOM_CHILD_INTERACTIVE;
	case FORWARDER_JOBSCRIPT:
	    return PSMOM_CHILD_JOBSCRIPT;
    }
    return -1;
}

static int handle_Local_Hello(ComHandle_t *com)
{
    unsigned int type;
    char jobid[JOB_NAME_LEN];
    Child_t *child;
    Job_t * job;

    ReadDigitUI(com, &type);
    ReadString(com, jobid, sizeof(jobid));

    /*
    mlog("%s: hello job '%s' type:%s socket:%i\n", __func__, jobid,
	    fwType2Str(type), com->socket);
    */

    switch (type) {
	case FORWARDER_COPY:
	case FORWARDER_INTER:
	case FORWARDER_JOBSCRIPT:
	    if (!(job = findJobById(jobid))) {
		mlog("%s: jobid '%s' not found\n", __func__, jobid);
		return -1;
	    }

	    mdbg(PSMOM_LOG_LOCAL, "%s: hello from '%s' forwarder for job '%s' "
		"socket: '%i'\n", __func__, fwType2Str(type),
		jobid, com->socket);

	    addJobConn(job, com, JOB_CON_FORWARD);

	    /* if prologue is finished before interactive job forwarders
	     * is ready we need to start the job here */
	    if (type == FORWARDER_INTER && job->prologueExit == 0 &&
		job->prologueTrack == 0 && job->state == JOB_PROLOGUE) {
		startInteractiveJob(job, com);
	    }

	    if (!(child = findChildByJobid(jobid,
		    getChildTypeByForwarder(type)))) {
		mdbg(PSMOM_LOG_WARN, "%s: child '%s' type '%s' not found\n",
		    __func__, jobid, fwType2Str(type));
		return -1;
	    }
	    child->sharedComm = com;

	    break;
	case FORWARDER_PELOGUE:
	    mdbg(PSMOM_LOG_LOCAL, "%s: hello from '%s' forwarder for job '%s' "
		"socket: '%i'\n", __func__, fwType2Str(type),
		jobid, com->socket);

	    if (!com->jobid) com->jobid = ustrdup(jobid);

	    if (!(child = findChildByJobid(jobid, PSMOM_CHILD_PROLOGUE))) {
		child = findChildByJobid(jobid, PSMOM_CHILD_EPILOGUE);
	    }
	    if (!child) {
		/* pelogue forwarder could have exit by now */
		return -1;
	    }
	    child->sharedComm = com;
	    break;
	default:
	    mlog("%s: invalid forwarder type '%i'\n", __func__, type);
	    return -1;
    }
    return 0;
}

static void handle_Local_Child_Start(ComHandle_t *com)
{
    Job_t *job;
    char jobid[JOB_NAME_LEN];
    pid_t fpid, childpid, childsid;
    int forwarder_type;
    long timeout;
    Child_t *child;

    ReadString(com, jobid, sizeof(jobid));

    /* pid of the forwarder */
    ReadDigitI(com, &fpid);

    /* pid of the forwarders child */
    ReadDigitI(com, &childpid);

    /* sid of the forwaders child */
    ReadDigitI(com, &childsid);

    /* forwarder type */
    ReadDigitI(com, &forwarder_type);

    /* forwarder timeout */
    ReadDigitL(com, &timeout);

    if (!(child = findChild(fpid))) {
	mlog("%s forwarder child for pid '%i' not found\n", __func__, fpid);
    } else {
	child->c_pid = childpid;
	child->c_sid = childsid;
	child->fw_timeout = timeout;

	/* set/remove new walltime timeout */
	setChildTimeout(child, timeout, 1);
    }

    if (forwarder_type != FORWARDER_PELOGUE) {
	if (!(job = findJobById(jobid))) {
	    mlog("%s: job not found for jobid '%s'\n", __func__, jobid);
	} else {
	    job->pid = childpid;
	    job->sid = childsid;

	    /* set batch job to running state */
	    if (forwarder_type == FORWARDER_JOBSCRIPT) {
		job->state = JOB_RUNNING;
		psPamSetState(job->user, job->id, PSPAM_STATE_JOB);
	    }
	}
    }

    /* register jobscript in the accounting plugin */
    if ((forwarder_type == FORWARDER_JOBSCRIPT ||
	forwarder_type == FORWARDER_INTER ) && job) {
	psAccountRegisterJob(childpid, job->id);
    }

    mdbg(PSMOM_LOG_PROCESS, "%s: new child '%i' type '%i' started\n",
	__func__, childpid, forwarder_type);
}

static int handle_Local_Child_Exit(ComHandle_t *com)
{
    Job_t *job;
    Child_t *child;
    int status, forwarder_type;
    char jobid[JOB_NAME_LEN];
    pid_t childpid;
    uint64_t cputime;

    /* read jobid */
    ReadString(com, jobid, sizeof(jobid));

    /* forwarder type */
    ReadDigitI(com, &forwarder_type);

    if (!(child = findChildByJobid(jobid, -1))) {
	mdbg(PSMOM_LOG_WARN, "%s: child '%s' not found\n", __func__,
		jobid);
    } else {
	child->sharedComm = NULL;
    }

    /* read the exit status */
    ReadDigitI(com, &status);

    /* pid of the forwarders child */
    ReadDigitI(com, &childpid);

    /* cputime of child */
    wRead(com, &cputime, sizeof(uint64_t));

    if (!(job = findJobById(jobid))) {
	mlog("%s: job '%s' not found\n", __func__, jobid);
	return 1;
    }

    /* save childs exit status */
    if (forwarder_type == FORWARDER_INTER ||
	forwarder_type == FORWARDER_JOBSCRIPT) {
	job->jobscriptExit = status;
    }

    /* if prologue failed */
    if (job->state == JOB_CANCEL_PROLOGUE) {
	return 0;
    }

    /* save used cpu time from jobscript/interactive bash */
    addJobWaitCpuTime(job, cputime);

    return 0;
}

static void handle_Local_Close(ComHandle_t *com)
{
    mdbg(PSMOM_LOG_LOCAL, "%s: closing local connection '%i'"
	    " on request\n", __func__, com->socket);
    wClose(com);
}

int closeLocalConnetion(int fd)
{
    if (fd == masterSocket) return -1;
    Selector_remove(fd);
    close(fd);
    return 0;
}

static void handle_Local_Fork_Failed(void)
{
    handleFailedSpawn();
}

static void handle_Local_Request_Account(ComHandle_t *com)
{
    Job_t *job;
    char jobid[JOB_NAME_LEN];

    if (!(ReadString(com, jobid, sizeof(jobid)))) {
	mlog("%s: failed reading jobid\n", __func__);
	WriteDigit(com, 0);
	wDoSend(com);
	return;
    }

    if (!(job = findJobById(jobid))) {
	mlog("%s: job for jobid '%s' not found\n", __func__, jobid);
	WriteDigit(com, 0);
	wDoSend(com);
	return;
    }

    /* get accounting info from the psaccount plugin */
    fetchAccInfo(job);

    WriteDataStruct(com, &job->status);
    wDoSend(com);
}

static int handleLocalConnection(int fd, void *info)
{
    ComHandle_t *com = getComHandle(fd, UNIX_PROTOCOL);
    int ret;
    unsigned int cmd;

    if ((ret = ReadDigitUI(com, &cmd)) < 0) {
	/* connection closed from other side */
	mlog("%s: closing local connection '%i' on reset, ret '%i'\n", __func__,
		com->socket, ret);
	wClose(com);
	return 0;
    }

    switch (cmd) {
	case CMD_LOCAL_HELLO:
	    handle_Local_Hello(com);
	    break;
	case CMD_LOCAL_CLOSE:
	    handle_Local_Close(com);
	    break;
	case CMD_LOCAL_CHILD_START:
	    handle_Local_Child_Start(com);
	    break;
	case CMD_LOCAL_CHILD_EXIT:
	    handle_Local_Child_Exit(com);
	    break;
	case CMD_LOCAL_REQUEST_ACCOUNT:
	    handle_Local_Request_Account(com);
	    break;
	case CMD_LOCAL_FORK_FAILED:
	    handle_Local_Fork_Failed();
	    break;
	default:
	    mlog("%s: invalid local cmd '%i' from '%i', closing connection\n",
		__func__, cmd, fd);
	    wClose(com);
    }
    return 0;
}

ComHandle_t *openLocalConnection()
{
    int sock = 0;
    struct sockaddr_un sa;

    /* don't use stdin/stdout/stderr */
    while (sock < 3) {
	if ((sock = socket(PF_UNIX, SOCK_STREAM, 0)) == -1) {
	    fprintf(stderr, "%s: socket() failed :%s\n", __func__,
		    strerror(errno));
	    return NULL;
	}
    }

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, masterSocketName, sizeof(sa.sun_path));

    if ((connect(sock, (struct sockaddr*) &sa, sizeof(sa))) < 0) {
	fprintf(stderr, "%s: local connection failed\n", __func__);
	return NULL;
    }

    mdbg(PSMOM_LOG_LOCAL, "open local connection to psmom '%i'\n", sock);

    return getComHandle(sock, UNIX_PROTOCOL);
}

static int handleMasterSock(int fd, void *info)
{
    unsigned int clientlen;
    struct sockaddr_in SAddr;
    int socket = -1;

    /* accept new tcp connection */
    clientlen = sizeof(SAddr);

    if ((socket = accept(fd, (void *)&SAddr, &clientlen)) == -1) {
	mlog("%s error accepting new local tcp connection\n", __func__);
	return -1;
    }
    mdbg(PSMOM_LOG_LOCAL, "%s: accepting new local client '%i'\n", __func__,
	socket);

    Selector_register(socket, handleLocalConnection, NULL);

    getComHandle(socket, UNIX_PROTOCOL);
    return 0;
}

void openMasterSock()
{
    struct sockaddr_un sa;

    masterSocket = socket(PF_UNIX, SOCK_STREAM, 0);

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, masterSocketName, sizeof(sa.sun_path));

    /*
     * bind the socket to the right address
     */
    unlink(masterSocketName);
    if (bind(masterSocket, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
	mwarn(errno, "psmom already running?");
    }
    chmod(sa.sun_path, S_IRWXU);

    if (listen(masterSocket, 20) < 0) {
	mwarn(errno, "Error while trying to listen");
    }

    /* register the selector */
    Selector_register(masterSocket, handleMasterSock, NULL);

    mdbg(PSMOM_LOG_LOCAL, "%s: Local Service Port (%d) created.\n",
	__func__, masterSocket);
}

void closeMasterSock()
{
    if (masterSocket == -1) {
	mlog("%s: master socket already closed\n", __func__);
	return;
    }

    if (Selector_isRegistered(masterSocket)) Selector_remove(masterSocket);
    close(masterSocket);
    unlink(masterSocketName);

    masterSocket = -1;

    mdbg(PSMOM_LOG_LOCAL, "Local Service Port (%d) closed.\n", masterSocket);
}

ssize_t localRead(int sock, char *buffer, ssize_t len, const char *caller)
{
    ssize_t read;
    int read_errno;

    read = recv(sock, buffer, len, 0);
    read_errno = errno;

    /* no data received from client */
    if (!read) {
	mlog("%s(%s): no data on socket '%i'\n", __func__, caller, sock);
	closeLocalConnetion(sock);
	errno = read_errno;
	return -1;
    }

    /* socket error occured */
    if (read < 0) {
	if (errno == EINTR) {
	    return localRead(sock, buffer, len, caller);
	}
	mlog("%s(%s): error on unix socket '%i' occured : %s\n", __func__,
		caller, sock, strerror(errno));
	mwarn(errno, "%s(%s): local read on socket:%i failed ", __func__,
		caller, sock);
	closeLocalConnetion(sock);
	errno = read_errno;
	return -1;
    }

    return read;
}

int localWrite(int sock, void *msg, size_t len, const char *caller)
{
    ComHandle_t *com;
    size_t newlen;

    if (sock < 0) {
	mlog("%s(%s): invalid socket '%i'\n", __func__, caller, sock);
	return -1;
    }

    if ((com = findComHandle(sock, UNIX_PROTOCOL)) == NULL) {
	mlog("%s(%s): msg handle not found for socket '%i'\n", __func__, caller,
		sock);
	return -1;
    }

    if (!com->outBuf) {
	if (len < UNIX_BUFFER_ALLOC) {
	    com->outBuf = umalloc(UNIX_BUFFER_ALLOC);
	    com->bufSize = UNIX_BUFFER_ALLOC;
	} else {
	    com->outBuf = umalloc(len +1);
	    com->bufSize = len +1;
	}
	memcpy(com->outBuf, msg, len);
	com->outBuf[len] = '\0';
	com->dataSize = len;
    } else {
	newlen = com->dataSize + len +1;

	if (newlen > com->bufSize) {
	    com->outBuf = urealloc(com->outBuf, newlen + UNIX_BUFFER_GROW);
	    com->bufSize = newlen + UNIX_BUFFER_GROW;
	}

	memmove(com->outBuf + com->dataSize, msg, len);
	com->outBuf[newlen] = '\0';
	com->dataSize += len;
    }
    return len;
}

int localDoSend(int sock, const char *caller)
{
    if (sock < 0) {
	mlog("%s(%s): invalid socket %i\n", __func__, caller, sock);
	return -1;
    }

    ComHandle_t *com = findComHandle(sock, UNIX_PROTOCOL);
    if (!com) {
	mlog("%s(%s): no com handle for socket %i\n", __func__, caller, sock);
	return -1;
    }

    if (!com->outBuf || !com->dataSize) {
	mlog("%s(%s): empty buffer for socket %i\n", __func__, caller, sock);
	return -1;
    }

    mdbg(PSMOM_LOG_LOCAL, "%s(%s): socket %i len %zu msg '%s'\n", __func__,
	 caller, sock, com->dataSize, com->outBuf);

    size_t sent;
    ssize_t ret = PSCio_sendPProg(sock, com->outBuf, com->dataSize, &sent);
    if (ret < 0) mwarn(errno, "%s: on socket %i", __func__, sock);
    if (sent != com->dataSize) {
	mlog("%s(%s): not all data could be sent, written %zu len %zu\n",
		__func__, caller, sent, com->dataSize);
    }

    if (com->outBuf) {
	ufree(com->outBuf);
	com->outBuf = NULL;
	com->bufSize = 0;
	com->dataSize = 0;
    }

    return ret;
}
