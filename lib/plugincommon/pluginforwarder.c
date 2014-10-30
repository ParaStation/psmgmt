/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
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

#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/signalfd.h>

#include "selector.h"
#include "timer.h"
#include "pscommon.h"
#include "psidutil.h"
#include "psidscripts.h"
#include "plugincomm.h"
#include "pluginhelper.h"
#include "pluginmalloc.h"
#include "pluginlog.h"

#include "pluginforwarder.h"

#define DEFAULT_GRACE_TIME 3

#define TEMP_SOCKET_NAME LOCALSTATEDIR "/run/psmgmt-plugin"

/** pid of the running forwarder child */
static pid_t forwarder_child_pid = -1;

/** session id of the running forwarder child */
static pid_t forwarder_child_sid = -1;

/** unix pipe to recognize signals in sleeping in select() */
static int signalFD;

/** control channel between forwarder and child */
static int controlFDs[2];

static int motherSock = -1;

static Forwarder_Data_t *fwdata = NULL;

static char *jobstring = NULL;

/** flag which will be set to 1 if the first kill phase started */
static bool killAllChildren = 0;

/** flag which indicates of a SIGKILL was already send */
static bool sentHardKill = 0;

/** flag for sigchild */
static bool sigChild = 0;

/** timeout flag set to true if the walltime limit is reached */
static bool job_timeout = 0;

static int connect2Mother(char *listenSocketName)
{
    struct sockaddr_un sa;

    if (!listenSocketName) return 0;

    /* don't use stdin/stdout/stderr */
    while (motherSock < 3) {
	if ((motherSock = socket(PF_UNIX, SOCK_STREAM, 0)) == -1) {
	    fprintf(stderr, "%s: socket() failed :%s\n", __func__,
		    strerror(errno));
	    return 0;
	}
    }

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, listenSocketName, sizeof(sa.sun_path));

    if ((connect(motherSock, (struct sockaddr*) &sa, sizeof(sa))) < 0) {
	fprintf(stderr, "%s: local connection failed\n", __func__);
	return 0;
    }

    //mdbg(PSMOM_LOG_LOCAL, "open local connection to psmom '%i'\n", sock);

    return motherSock;
}

static void killForwarderChild(int signal, char *reason)
{
    int grace;
    char buffer[512];

    if (forwarder_child_sid < 1) {
	fprintf(stderr, "%s: invalid child sid '%i'\n", __func__,
		forwarder_child_sid);
	exit(1);
    }

    grace = fwdata->graceTime ? fwdata->graceTime : DEFAULT_GRACE_TIME;

    if (reason) {
	snprintf(buffer, sizeof(buffer), ", reason: %s", reason);
    } else {
	buffer[0] = '\0';
    }

    fprintf(stderr, "signal '%u' to sid '%i' %sgrace time '%i'"
	    " sec%s\n", signal, forwarder_child_sid,
	    jobstring, grace, buffer);

    if (signal == SIGTERM) {
	/* let children beeing debugged continue */
	kill(forwarder_child_pid, SIGCONT);

	if (!(kill(forwarder_child_pid, signal))) {
	    killAllChildren = 1;
	    alarm(grace);
	}
    } else {
	kill(forwarder_child_pid, signal);
    }
}

static int handleMessage(int fd, void *info)
{
    int count;
    int32_t cmd, signal;
    char *ptr, buf[1024];

    if (!(count = doRead(fd, buf, sizeof(buf))) || count < 0) {
	pluginlog("%s: lost connection to mother psid, killing child\n",
		    __func__);
	Selector_remove(fd);
	close(fd);
	motherSock = -1;
	killForwarderChild(SIGKILL, "lost connection to mother");
	return 0;
    }

    ptr = buf;
    getInt32(&ptr, &cmd);

    if (fwdata->hookHandleMsg) {
	if ((fwdata->hookHandleMsg(fwdata, ptr, cmd))) return 0;
    }

    switch(cmd) {
	case CMD_LOCAL_SIGNAL_CHILD:
	    getInt32(&ptr, &signal);
	    killForwarderChild(signal, NULL);
	    break;
	default:
	    pluginlog("%s: invalid cmd '%i' from '%i'\n", __func__, cmd, fd);
    }

    return 0;
}

static void sendForwarderHello(int32_t forwarderID)
{
    PS_DataBuffer_t data = { .buf = NULL};

    if (motherSock < 0) {
	fprintf(stderr, "%s: no connection to my mother\n", __func__);
	return;
    }

    addInt32ToMsg(CMD_LOCAL_HELLO, &data);
    addInt32ToMsg(forwarderID, &data);
    doWriteP(motherSock, data.buf, data.bufUsed);
    ufree(data.buf);
}

/**
 * @brief Signal handler to break out of Sselect() when a signal arrives.
 *
 * @param fd The file descriptor were to wakup messages was written to.
 *
 * @param info Not used, required by selector facility.
 *
 * @return Always return 1 to stop Sselect().
 */
static int handleSignalFd(int fd, void *info)
{
    sigChild = 1;
    Selector_startOver();
    return 1;
}

/**
 * @brief Handle various signals.
 *
 * @param sig The signal to handle.
 *
 * @return No return value.
 */
static void signalHandler(int sig)
{
    signal(sig, signalHandler);

    switch (sig) {
	case SIGTERM:
	    /* kill the child */
	    killForwarderChild(SIGTERM, "received SIGTERM");
	    break;
	case SIGALRM:
	    /* reset possible alarms */
	    alarm(0);

	    if (killAllChildren) {
		/* second kill phase, do it the hard way now */
		fprintf(stderr, "signal 'SIGKILL' to sid '%i'%s\n",
			forwarder_child_sid, jobstring);
		fwdata->killSession(forwarder_child_sid, SIGKILL);
		killAllChildren = 0;
		sentHardKill = 1;
	    } else {
		/* TODO */
		/* static char errmsg[] = "\r\npsmom: job timeout reached, terminating.\n\n\r";
		if (forwarder_type == FORWARDER_INTER) {
		    writeQsubMessage(errmsg, strlen(errmsg));
		}
		*/
		job_timeout = 1;

		killForwarderChild(SIGTERM, "timeout");
	    }
	    break;
    case SIGPIPE:
	fprintf(stderr, "got sigpipe\n");
	break;
    }
}

static int initForwarder()
{
    sigset_t mask;

    Selector_init(NULL);
    Timer_init(NULL);

    signal(SIGALRM, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGPIPE, signalHandler);

    /* overwrite proc title */
    if (fwdata->pTitle) {
	PSC_setProcTitle((char ** )PSID_argv, PSID_argc, fwdata->pTitle, 0);
    }

    /* Reset connection to syslog */
    closelog();
    openlog(fwdata->syslogID, LOG_PID|LOG_CONS, LOG_DAEMON);

    //forwarder_type = forwarderType;

    if (!(connect2Mother(fwdata->listenSocketName))) {
	fprintf(stderr, "%s: open connection to mother psid failed\n",
		    __func__);
	return 0;
    }
    Selector_register(motherSock, handleMessage, NULL);

    sendForwarderHello(fwdata->forwarderID);

    blockSignal(SIGALRM, 1);

    /* open control fds */
    if ((socketpair(PF_UNIX, SOCK_STREAM, 0, controlFDs)) <0) {
	pluginwarn(errno, "%s: open control socket failed:", __func__);
	return 0;
    }

    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    blockSignal(SIGCHLD, 1);
    if ((signalFD = signalfd(-1, &mask, 0)) == -1) {
	pluginwarn(errno, "%s: signalfd() failed:", __func__);
	return 0;
    }
    Selector_register(signalFD, handleSignalFd, NULL);

    return 1;
}

/**
 * @brief Tell my mother that forking the child failed.
 *
 * @return No return value.
 */
static void sendForkFailed()
{
    PS_DataBuffer_t data = { .buf = NULL};

    if (motherSock < 0) {
	fprintf(stderr, "%s: no connection to my mother\n", __func__);
	return;
    }

    addInt32ToMsg(CMD_LOCAL_FORK_FAILED, &data);
    doWriteP(motherSock, data.buf, data.bufUsed);
    ufree(data.buf);
}

/**
 * @brief Reset changed signal mask and hanlder.
 *
 * @return No return value.
 */
static void resetSignalHandling()
{
    /* restore sighandler */
    signal(SIGALRM, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
    signal(SIGPIPE, SIG_DFL);

    /* unblock blocked signals */
    blockSignal(SIGCHLD, 0);
    blockSignal(SIGALRM, 0);
}

/**
 * @brief Initialize a child process.
 *
 * Initialize a child process and move it to its own session.
 *
 * @return No return value.
 */
static void initChild(int fwpid)
{
    PS_DataBuffer_t data = { .buf = NULL};
    int fd;

    /* needed or ioctl(TIOCSCTTY) will fail! */
    if ((forwarder_child_sid = setsid()) == -1) {
	pluginlog("%s: setsid() failed\n", __func__);
	exit(1);
    }

    /* add header */
    addInt32ToMsg(CMD_LOCAL_CHILD_START, &data);

    /* add pid of the forwarder itself */
    addPidToMsg(fwpid, &data);

    /* add pid of the forwarders child */
    addPidToMsg(getpid(), &data);

    /* add sid of the forwarders child */
    addPidToMsg(forwarder_child_sid, &data);

    doWriteP(motherSock, data.buf, data.bufUsed);
    ufree(data.buf);

    /* close connection to mother */
    close(motherSock);

    /* send sid to forwarder */
    if ((doWriteP(controlFDs[1], &forwarder_child_sid,
	    sizeof(pid_t))) != sizeof(pid_t)) {
	pluginlog("%s: failed writing childs sid\n", __func__);
	exit(1);
    }

    /* close all fd, except for stdout, stderr */
    for (fd=0; fd<getdtablesize(); fd++) {
	if (fd == fwdata->stdIn[0] || fd == fwdata->stdOut[0] ||
	    fd == fwdata->stdErr[0] || fd == fwdata->stdOut[1] ||
	    fd == fwdata->stdErr[1] || fd == fwdata->stdIn[1]) continue;
	if (fd == STDOUT_FILENO || fd == STDERR_FILENO) continue;
	close(fd);
    }

    resetSignalHandling();
}

/**
 * @brief Main loop for all forwarders.
 *
 * @return No return value.
 */
static void forwarderLoop()
{
    /* set timeout */
    if (fwdata->timeoutChild > 0) {
	alarm(fwdata->timeoutChild);
    }

    /* enable signals again */
    blockSignal(SIGALRM, 0);

    while (1) {
	/* check for really short jobs */
	if (sigChild) break;

	if (Sselect(FD_SETSIZE, NULL, NULL, NULL, NULL) < 0) {
	    if (errno != EINTR) {
		fprintf(stderr, "%s: select error : %s\n", __func__,
			strerror(errno));
		killForwarderChild(SIGKILL, "Sselect error");
		break;
	    }
	    if (sigChild) break;
	}
    }
}

/**
 * @brief Make sure all children are dead and request the psmom to close the
 * main connection.
 *
 * @return No return value.
 */
static void forwarderExit()
{
    PS_DataBuffer_t data = { .buf = NULL };

    blockSignal(SIGALRM, 1);
    blockSignal(SIGCHLD, 1);

    /* reset possible alarms */
    alarm(0);

    /* make sure all children are dead */
    fwdata->killSession(forwarder_child_sid, SIGKILL);

    /* request connection close */
    if (motherSock < 0) {
	fprintf(stderr, "%s: communication handle invalid\n", __func__);
    } else {
	addInt32ToMsg(CMD_LOCAL_CLOSE, &data);
	doWriteP(motherSock, data.buf, data.bufUsed);
	ufree(data.buf);
    }
}

static int execForwarder(void *info)
{
    fwdata = info;
    char tmp[200];
    int status = 0, i, fwpid = getpid();
    struct rusage rusage;
    struct timeval mytv={0,100};

    if (fwdata->jobid) {
	snprintf(tmp, sizeof(tmp), "job '%s' ", fwdata->jobid);
	jobstring = ustrdup(tmp);
    } else {
	jobstring = ustrdup("");
    }

    if (!initForwarder()) exit(1);
    if (fwdata->hookForwarderInit) fwdata->hookForwarderInit(fwdata);

    for (i=1; i<=fwdata->childRerun; i++) {

	/* fork child */
	if ((forwarder_child_pid = fork()) < 0) {
	    fprintf(stdout, "%s: unable to fork my child : %s\n", __func__,
		    strerror(errno));
	    sendForkFailed();
	    return -3;
	} else if (forwarder_child_pid == 0) {

	    initChild(fwpid);

	    fwdata->childFunc(fwdata, i);

	    /* never reached */
	    exit(1);
	}

	/* read sid of child */
	if ((doReadP(controlFDs[0], &forwarder_child_sid, sizeof(pid_t))
		    != sizeof(pid_t))) {
	    fprintf(stderr, "%s: reading childs sid failed\n", __func__);
	    kill(SIGKILL, forwarder_child_pid);
	}
	close(controlFDs[0]);
	close(controlFDs[1]);

	fwdata->childPid = forwarder_child_pid;
	if (fwdata->hookForwarderLoop) fwdata->hookForwarderLoop(fwdata);
	forwarderLoop();

	if ((wait4(forwarder_child_pid, &status, 0, &rusage)) == -1) {
	    fprintf(stderr, "%s: waitpid for %d failed\n", __func__,
		forwarder_child_pid);
	    status = 1;
	}

	alarm(0);

	/* make sure we handled all data, after child is gone */
	Sselect(FD_SETSIZE, NULL, NULL, NULL, &mytv);

	/* check for timeout */
	if (job_timeout) {
	    fprintf(stderr, "%s: child timed out (%i sec)\n", __func__,
		fwdata->timeoutChild);
	    status = -4;
	}

	if (status != 0) break;
    }

    /* cleanup */
    forwarderExit();

    return status;
}

static int getScriptCBData(int fd, PSID_scriptCBInfo_t *info, int32_t *exit,
    char *errMsg, size_t errMsgLen, size_t *errLen)
{
    int iofd = -1;

    /* get exit status */
    PSID_readall(fd, exit, sizeof(int32_t));
    Selector_remove(fd);
    close(fd);

    /* get stdout/stderr output / pid of child */
    if (info) {
	if (!info->info) {
	    pluginlog("%s: info missing\n", __func__);
	    return 1;
	}
	if ((iofd = info->iofd)) {
	    if ((*errLen = PSID_readall(iofd, errMsg, errMsgLen)) > 0) {
		//mlog("got error: '%s'\n", errMsg);
	    }
	    errMsg[*errLen] = '\0';
	    close(iofd);
	} else {
	    pluginlog("%s: invalid iofd\n", __func__);
	    errMsg[0] = '\0';
	}
    } else {
	pluginlog("%s: invalid info data\n", __func__);
	return 1;
    }
    return 0;
}

int callbackForwarder(int fd, PSID_scriptCBInfo_t *info)
{
    char errMsg[1024];
    int32_t exit;
    size_t errLen = 0;
    Forwarder_Data_t *data = info->info;

    getScriptCBData(fd, info, &exit, errMsg, sizeof(errMsg), &errLen);

    if (!exit && data->forwarderError == 1) {
	snprintf(errMsg, sizeof(errMsg),
		    "%s", "reading from forwarder failed\n");
	errLen = strlen(errMsg);
	exit = data->forwarderError;
    }

    if (data && data->callback) {
	data->callback(exit, errMsg, errLen, info->info);
    }

    ufree(info);
    return 0;
}

static void closeControlSocket(Forwarder_Data_t *data)
{
    if (data->controlSocket >-1) {
	Selector_remove(data->controlSocket);
	close(data->controlSocket);
	data->controlSocket = -1;
    }
}

static void handle_FW_Hello(Forwarder_Data_t *data, char *ptr)
{
    int32_t id;

    if (!(getInt32(&ptr, &id))) {
	pluginlog("%s: reading forwarder id failed\n", __func__);
	closeControlSocket(data);
	return;
    }

    /* make sure the forwarder is who he claims to be */
    if (data->forwarderID != id) {
	pluginlog("%s: invalid forwarder id '%i'\n", __func__, id);
	closeControlSocket(data);
	return;
    }

    /* remove connect timer */
    if (data->timeoutConnectId > 0) {
	if (Timer_remove(data->timeoutConnectId) == -1) {
	    pluginlog("%s: removing connect timer failed\n", __func__);
	}
	data->timeoutConnectId = -1;
    }
}

static void handle_FW_Close(Forwarder_Data_t *data, char *ptr)
{
    closeControlSocket(data);
}

static void handle_FW_Child_Start(Forwarder_Data_t *data, char *ptr)
{
    pid_t forwarderPid;

    /* pid of the forwarder itself */
    getPid(&ptr, &forwarderPid);

    if (forwarderPid != data->forwarderPid) {
	pluginlog("%s: got invalid forwarder pid '%i:%i'\n", __func__,
	    forwarderPid, data->forwarderPid);
    }

    /* pid of the forwarders child */
    getPid(&ptr, &data->childPid);

    /* sid of the forwarders child */
    getPid(&ptr, &data->childSid);

    if (data->hookChildStart) {
	data->hookChildStart(data, forwarderPid, data->childPid,
				data->childSid);
    }

    // TODO LOG VERBOSE
    /*
    pluginlog("%s: %i : %i : %i\n", __func__,
		data->forwarderPid, data->childPid, data->childSid);
    */
}

static void handle_FW_Fork_Failed(Forwarder_Data_t *data, char *ptr)
{
    closeControlSocket(data);
}

static int handleControlSocket(int fd, void *info)
{
    char *ptr, buf[1024];
    Forwarder_Data_t *data = info;
    int32_t cmd, count;

    count = doRead(fd, buf, sizeof(buf));
    ptr = buf;

    //pluginlog("%s: here: count:%i fd:%i '%s'\n", __func__, count, fd, buf);
    if (count <= 0 || !(getInt32(&ptr, &cmd))) {
	pluginlog("%s: reading from forwarder failed, count:%i :pid=%i\n",
		    __func__, count, getpid());
	data->forwarderError = 1;
	closeControlSocket(data);
	Selector_remove(fd);
	close(fd);
	return 0;
    }

    switch(cmd) {
	case CMD_LOCAL_HELLO:
	    handle_FW_Hello(data, ptr);
	    break;
	case CMD_LOCAL_CLOSE:
	    handle_FW_Close(data, ptr);
	    break;
	case CMD_LOCAL_CHILD_START:
	    handle_FW_Child_Start(data, ptr);
	    break;
	case CMD_LOCAL_FORK_FAILED:
	    handle_FW_Fork_Failed(data, ptr);
	    break;
	default:
	    pluginlog("%s: invalid local cmd '%i' from '%i', closing "
			"connection\n", __func__, cmd, fd);
	    closeControlSocket(data);
    }

    return 0;
}

static void closeListenSocket(Forwarder_Data_t *data)
{
    if (data->listenSocket >-1) {
	Selector_remove(data->listenSocket);
	close(data->listenSocket);
	data->listenSocket = -1;
    }
    unlink(data->listenSocketName);
}

static int handleListenSocket(int fd, void *info)
{
    Forwarder_Data_t *data = info;

    unsigned int clientlen;
    struct sockaddr_in SAddr;

    /* accept new tcp connection */
    clientlen = sizeof(SAddr);

    if ((data->controlSocket = accept(fd, (void *)&SAddr, &clientlen)) == -1) {
	pluginlog("%s error accepting new local tcp connection\n", __func__);
	return -1;
    }
    /*
    mdbg(PSMOM_LOG_LOCAL, "%s: accepting new local client '%i'\n", __func__,
	socket);
    */

    // TODO LOG VERBOSE
    //pluginlog("%s: accepting new local client '%i'\n", __func__, data->controlSocket);
    Selector_register(data->controlSocket, handleControlSocket, data);

    /* remove listening socket */
    closeListenSocket(data);

    return 0;
}

static int openListenSocket(Forwarder_Data_t *data)
{
    struct sockaddr_un sa;
    char *tmpName, buf[50];

    if (data->listenSocketName) {
	pluginlog("%s: listenSocket already in use '%s'.", __func__,
		    data->listenSocketName);
	return -1;
    }

    snprintf(buf, sizeof(buf), "%s-XXXXXX", TEMP_SOCKET_NAME);
    if (!(tmpName = mktemp(buf))) {
	pluginwarn(errno, "%s: mktemp(%s) failed", __func__, buf);
	return -1;
    }
    data->listenSocketName = strdup(buf);

    if ((data->listenSocket = socket(PF_UNIX, SOCK_STREAM, 0)) == -1) {
	pluginwarn(errno, "%s:", __func__);
	return -1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, data->listenSocketName, sizeof(sa.sun_path));

    /*
     * bind the socket to the right address
     */
    unlink(data->listenSocketName);
    if (bind(data->listenSocket, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
	pluginwarn(errno, "%s: bind failed :", __func__);
	close(data->listenSocket);
	data->listenSocket = -1;
	return -1;
    }
    chmod(sa.sun_path, S_IRWXU);

    if (listen(data->listenSocket, 20) < 0) {
	pluginwarn(errno, "Error while trying to listen");
	close(data->listenSocket);
	data->listenSocket = -1;
	return -1;
    }

    /* register the selector */
    Selector_register(data->listenSocket, handleListenSocket, data);

    /*
    plugindbg(PSMOM_LOG_LOCAL, "%s: Local Service Port (%d) created.\n",
	__func__, listenSocket);
    */

    return 1;
}

Forwarder_Data_t *getNewForwarderData()
{
    Forwarder_Data_t *data;

    data = umalloc(sizeof(Forwarder_Data_t));

    data->pTitle = NULL;
    data->syslogID = NULL;
    data->jobid = NULL;
    data->listenSocketName = NULL;
    data->userData = NULL;
    data->killSession = NULL;
    data->callback = NULL;
    data->childFunc = NULL;
    data->hookForwarderLoop = NULL;
    data->hookForwarderInit = NULL;
    data->hookHandleSignal = NULL;
    data->hookHandleMsg = NULL;
    data->hookChildStart = NULL;
    data->logName = "forwarder";

    data->childRerun = 1;
    data->timeoutChild = 0;
    data->timeoutConnect = 10;
    data->timeoutConnectId = -1;
    data->graceTime = 0;
    data->forwarderID = -1;
    data->listenSocket = -1;
    data->forwarderError = 0;
    data->controlSocket = -1;
    data->childPid = 0;
    data->childSid = 0;
    data->forwarderPid = 0;
    data->stdIn[0] = -1;
    data->stdIn[1] = -1;
    data->stdOut[0] = -1;
    data->stdOut[1] = -1;
    data->stdErr[0] = -1;
    data->stdErr[1] = -1;

    return data;
}

void destroyForwarderData(Forwarder_Data_t *data)
{
    ufree(data->pTitle);
    ufree(data->syslogID);
    ufree(data->jobid);
    ufree(data->listenSocketName);
    data->callback = NULL;
    ufree(data);
}

static void handleConnectTimeout(int timerId, void *fwdata)
{
    Forwarder_Data_t *data = fwdata;

    if (data->forwarderPid) {
	pluginlog("%s: forwarder '%i' did not connect back in '%i' seconds\n",
		    __func__, data->forwarderPid, data->timeoutConnect);

	kill(SIGKILL, data->forwarderPid);
	data->timeoutConnectId = -1;
    }
    Timer_remove(timerId);
}

int signalForwarderChild(Forwarder_Data_t *data, int signal)
{
    PS_DataBuffer_t buffer = { .buf = NULL};
    int ret = 0;

    if (data->childSid > 0) {
	data->killSession(data->childSid, signal);
	pluginlog("%s: child session id '%i' signal '%i'\n",
		    __func__, data->childSid, signal);

	if ((signal == SIGTERM || signal == SIGKILL) &&
		data->forwarderPid > 0) {
	    if (signal == SIGKILL) signal = SIGTERM;
	    kill(data->forwarderPid, signal);
	}
	return 1;
    } else if (data->controlSocket > -1) {
	addInt32ToMsg(CMD_LOCAL_SIGNAL_CHILD, &buffer);
	addInt32ToMsg(signal, &buffer);
	ret = doWriteP(data->controlSocket, buffer.buf, buffer.bufUsed);
	ufree(buffer.buf);
	pluginlog("%s: CMD_LOCAL_SIGNAL_CHILD ret: %i\n", __func__, ret);
	return 1;
    } else {
	pluginlog("%s: cannot signal forwarder, missing infos\n", __func__);
    }
    return 0;
}

int startForwarder(Forwarder_Data_t *data)
{
    if (!data->killSession) {
	pluginlog("%s: invalid killSessions() function pointer\n", __func__);
	return 1;
    }

    if (!data->callback) {
	pluginlog("%s: invalid callback() function pointer\n", __func__);
	return 1;
    }

    if (!data->childFunc) {
	pluginlog("%s: invalid childFunc() function pointer\n", __func__);
	return 1;
    }

    if (data->forwarderID == -1) {
	data->forwarderID = (int32_t) getpid() * rand() * time(NULL);
    }

    /* make sure the forwarder connects back to us */
    if (data->timeoutConnect > 0) {
	struct timeval timer = {0, 0};

	timer.tv_sec = data->timeoutConnect;

	if ((data->timeoutConnectId = Timer_registerEnhanced(&timer,
		handleConnectTimeout, data)) == -1) {
	    pluginlog("%s: register child connect timer failed\n", __func__);
	    return 1;
	}
    }

    /* create listen socket */
    if (openListenSocket(data) == -1) return 1;

    /* start the new forwarder */
    if ((data->forwarderPid = PSID_execFunc(execForwarder, NULL,
	    callbackForwarder, data)) == -1) {
	pluginlog("%s: exec forwarder failed\n", __func__);
	closeListenSocket(data);
	return 1;
    }

    return 0;
}
