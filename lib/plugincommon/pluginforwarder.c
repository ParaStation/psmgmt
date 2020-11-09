/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>

#include "pscio.h"
#include "pscommon.h"
#include "selector.h"
#include "timer.h"
#include "psserial.h" // @todo remove once doRead was moved
#include "pluginhelper.h"
#include "pluginmalloc.h"
#include "pluginlog.h"

#include "psidutil.h"
#include "psidclient.h"
#include "psidcomm.h"
#include "psidspawn.h"
#include "psidsignal.h"
#include "psidhook.h"

#include "pluginforwarder.h"

/** Default grace time before sending SIGKILL */
#define DEFAULT_GRACE_TIME 3


/* ----- Data handling (used in mother but data passed to forwarder) ----- */

Forwarder_Data_t *ForwarderData_new(void)
{
    Forwarder_Data_t *fw;

    fw = calloc(sizeof(*fw), 1);
    if (fw) {
	fw->childRerun = 1;
	fw->tid = -1;
	fw->exitRcvd = false;
	fw->codeRcvd = false;
	fw->stdIn[0] = -1;
	fw->stdIn[1] = -1;
	fw->stdOut[0] = -1;
	fw->stdOut[1] = -1;
	fw->stdErr[0] = -1;
	fw->stdErr[1] = -1;
	fw->hideFWctrlMsg = true;
	fw->hideCCError = true;
	fw->fwChildOE = false;
	fw->jailChild = false;
    }

    return fw;
}

void ForwarderData_delete(Forwarder_Data_t *fw)
{
    if (!fw) return;

    if (fw->pTitle) ufree(fw->pTitle);
    if (fw->jobID) ufree(fw->jobID);
    free(fw);
}

/* ----------------------- Stuff used in forwarder ----------------------- */

/** Description of the local forwarder */
static Forwarder_Data_t *fwData = NULL;

/** Description of the local task */
static PStask_t *fwTask;

/** Flag to be set to true when first kill phase is started */
static bool killAllChildren = false;

/** Flag indicating if SIGTERM was already send */
static bool sendHardKill = false;

/** Flag indicating SIGCHLD was received */
static bool sigChild = false;

/** Flag indication child's wall-time limit is reached */
static bool jobTimeout = false;

static struct timeval childStart;

int sendMsgToMother(PSLog_Msg_t *msg)
{
    return PSCio_sendP(fwTask->fd, msg, msg->header.len);
}

static void handleGraceTime(Forwarder_Data_t *fw)
{
    int grace = fw->graceTime ? fw->graceTime : DEFAULT_GRACE_TIME;

    killAllChildren = true;
    alarm(grace);
}

static void doKillChild(pid_t pid, int sig, bool session)
{
    if (session) {
	fwData->killSession(pid, sig);
    } else {
	pskill(pid, sig, 0);
    }
}

static void killForwarderChild(Forwarder_Data_t *fw, int sig, char *reason,
			       bool session)
{
    if (!fw) fw = fwData;
    int grace = fw->graceTime ? fw->graceTime : DEFAULT_GRACE_TIME;

    if (fw->cSid < 1 || fw->cPid < 1) return;

    pluginlog("%s: signal %u to sid %i (job %s) grace %i%s%s\n", __func__,
	      sig, fw->cSid, fw->jobID ? fw->jobID : "<?>", grace,
	      reason ? " reason " : "", reason ? reason : "");

    if (fw->cPid <= 0) return;

    if (sig == SIGTERM) {
	/* let children being debugged continue */
	doKillChild(fw->cSid, SIGCONT, session);

	doKillChild(fw->cSid, sig, session);
	killAllChildren = true;
	alarm(grace);
    } else {
	doKillChild(fw->cSid, sig, session);
    }
}

static void handleLocalShutdown(Forwarder_Data_t *fw)
{
    if (fw->cSid < 1) {
	sigChild = true;
	Selector_startOver();
    } else {
	killForwarderChild(fw, SIGTERM, NULL, true);
    }
}

static void handleFinACK(Forwarder_Data_t *fw)
{
    Selector_startOver();
}

/**
 * @brief Receive message from mother daemon
 *
 * Receive a message from the mother daemon connected via the file
 * descriptor @a fd and store it to @a msg. At most @a size bytes are
 * read from the file descriptor and stored to @a msg.
 *
 * @param fd File descriptor to receive from
 *
 * @param msg Buffer to store the message in
 *
 * @param size Maximum length of the message, i.e. the size of @a msg.
 *
 * @return On success, the number of bytes received is returned, or -1 if
 * an error occurred. In the latter case errno will be set appropriately.
 *
 * @see errno(3)
 */
static ssize_t recvMthrMsg(int fd, DDMsg_t *msg, size_t size)
{
    ssize_t n;
    size_t count = 0;

    if (!msg || size < sizeof(*msg)) {
	pluginlog("%s: size %zd too small\n", __func__, size);
	errno = EINVAL;
	return -1;
    }

    msg->len = sizeof(*msg);

    do {
	n = read(fd, &((char*) msg)[count], msg->len-count);
	if (n > 0) {
	    if (count < sizeof(*msg)) {
		/* Just received first chunk of data */
		if (msg->len > size) {
		    /* message will not fit into msg */
		    errno = EMSGSIZE;
		    n = -1;
		    /* @todo we should remove message from fd (with timeout!) */
		    break;
		}
	    }
	    count += n;
	} else if (n < 0 && errno == EINTR) {
	    continue;
	} else if (n < 0 && errno == ECONNRESET) {
	    /* socket is closed unexpectedly */
	    n = 0;
	    break;
	} else break;
    } while (count < msg->len);

    if (count && count == msg->len) {
	return msg->len;
    } else {
	return n;
    }
}

static int handleMthrSock(int fd, void *info)
{
    Forwarder_Data_t *fw = info;
    DDBufferMsg_t msg; /* ensure we'll have enough space */
    PSLog_Msg_t *lmsg = (PSLog_Msg_t *)&msg;
    size_t used = PSLog_headerSize; /* ensure we use the correct offset */
    int32_t signal;

    if (!recvMthrMsg(fd, (DDMsg_t*)&msg, sizeof(msg))) {
	handleLocalShutdown(fw);
	return 0;
    }

    if (fw->hideCCError && msg.header.type == PSP_CC_ERROR) return 0;

    if (!fw->hideFWctrlMsg || msg.header.type != PSP_CC_MSG
	|| lmsg->type > PSLOG_LAST) {
	if (fw->handleMthrMsg && fw->handleMthrMsg(lmsg, fw)) return 0;
    }

    if (msg.header.type == PSP_CC_ERROR) return 0; /* ignore */

    /* messages not handled by hook need to be PSP_CC_MSG */
    if (msg.header.type != PSP_CC_MSG) {
	pluginlog("%s: unexpected message %s from %s (type %d)\n", __func__,
		  PSP_printMsg(msg.header.type),
		  PSC_printTID(msg.header.sender), lmsg->type);
	return -1;
    }

    switch(lmsg->type) {
    case PLGN_SIGNAL_CHLD:
	PSP_getMsgBuf(&msg, &used, "signal", &signal, sizeof(signal));
	killForwarderChild(fw, signal, NULL, true);
	break;
    case PLGN_START_GRACE:
	handleGraceTime(fw);
	break;
    case PLGN_SHUTDOWN:
	handleLocalShutdown(fw);
	break;
    case PLGN_FIN_ACK:
	handleFinACK(fw);
	break;
    default:
	pluginlog("%s: invalid cmd %i from %s\n", __func__, lmsg->type,
		  PSC_printTID(lmsg->header.sender));
    }

    return 0;
}

/**
 * @brief Signal handler for SIGCHLD
 *
 * Handle SIGCHLD signals delivered via a signalfd. @fd is the actual
 * file descriptor used to deliver the actual signal, @a info is
 * ignored and just provided for compatibility with the Selector
 * facility. This function is basically used to escape from Swait()
 * when a signal arrives. For this @ref Selector_startOver() is called
 * within this function.
 *
 * @param fd File descriptor used to deliver the signal
 *
 * @param info Not used, required by selector facility
 *
 * @return Always return 0
 */
static int handleSignalFd(int fd, void *info)
{
    Forwarder_Data_t *fw = info;

    struct signalfd_siginfo sigInfo;
    if (read(fd, &sigInfo, sizeof(sigInfo)) < 0) {
	pluginwarn(errno, "%s: read()", __func__);
    }

    if (sigInfo.ssi_signo == SIGCHLD && (pid_t)sigInfo.ssi_pid == fw->cPid) {
	sigChild = true;
	Selector_startOver();
    }

    return 0;
}

/**
 * @brief Forwarder's signal handler
 *
 * Handle various signals that might be received by a forwarder
 * process. Currently this includes SIGTERM, SIGALRM, and SIGPIPE.
 *
 * @param sig Signal to handle
 *
 * @return No return value
 */
static void signalHandler(int sig)
{
    switch (sig) {
    case SIGTERM:
	if (sendHardKill) return;

	/* kill the child */
	killForwarderChild(fwData, SIGTERM, "received SIGTERM", false);
	break;
    case SIGALRM:
	/* reset possible alarms */
	alarm(0);

	if (killAllChildren && fwData->cSid > 0) {
	    /* second kill phase, do it the hard way now */
	    pluginlog("%s: SIGKILL to sid %i (job %s)\n", __func__,
		      fwData->cSid, fwData->jobID ? fwData->jobID : "<?>");
	    /* warning: don't use killSession() in the signal handler.
	     * Double entry of killSession() can lead to corrupt memory! */
	    pskill(fwData->cPid, SIGKILL, 0);
	    sendHardKill = true;
	} else {
	    jobTimeout = true;
	    killForwarderChild(fwData, SIGTERM, "timeout", false);
	}
	break;
    case SIGPIPE:
	pluginlog("%s: SIGPIPE\n", __func__);
	break;
    }
}

static bool initForwarder(int motherFD, Forwarder_Data_t *fw)
{
    int signalFD;
    sigset_t mask;

    Selector_init(NULL);
    Timer_init(NULL);

    PSC_setSigHandler(SIGALRM, signalHandler);
    PSC_setSigHandler(SIGTERM, signalHandler);
    PSC_setSigHandler(SIGPIPE, signalHandler);

    /* overwrite proc title */
    if (fw->pTitle) {
	PSC_setProcTitle(PSID_argc, PSID_argv, fw->pTitle, 0);
	initPluginLogger(fw->pTitle, NULL);
    } else {
	initPluginLogger("psidfw", NULL);
    }

    /* Reset connection to syslog */
    closelog();
    openlog("psid", LOG_PID|LOG_CONS, LOG_DAEMON);

    Selector_register(motherFD, handleMthrSock, fw);

    PSID_blockSig(1, SIGALRM);

    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    PSID_blockSig(1, SIGCHLD);
    signalFD = signalfd(-1, &mask, 0);
    if (signalFD == -1) {
	pluginwarn(errno, "%s: signalfd()", __func__);
	return false;
    }
    if (Selector_register(signalFD, handleSignalFd, fw)) {
	pluginwarn(errno, "%s: Selector_register(signalFD)", __func__);
	return false;
    }

    return true;
}

/**
 * @brief Initialize a child process.
 *
 * Initialize a child process and move it to its own session. The
 * child's forwarder process waits to received the session ID via the
 * controlling file descriptor @a controlFD. The intended shape of the
 * forwarder process and its child is described within the forwarder
 * structure @a fw.
 *
 * @param controlFD File descriptor to submit the session ID
 *
 * @param fw Forwarder structure describing the forwarder and its child
 *
 * @return No return value.
 */
static void initChild(int controlFD, Forwarder_Data_t *fw)
{
    int written, fd, maxFD = sysconf(_SC_OPEN_MAX);

    /* needed or ioctl(TIOCSCTTY) will fail! */
    fw->cSid = setsid();
    if (fw->cSid == -1) {
	pluginlog("%s: setsid() failed\n", __func__);
	exit(1);
    }

    /* send sid to forwarder */
    written = PSCio_sendF(controlFD, &fw->cSid, sizeof(fw->cSid));
    if (written != sizeof(pid_t)) {
	pluginlog("%s: failed writing child's sid\n", __func__);
	exit(1);
    }

    /* close all fd, except for stdout, stderr */
    for (fd=0; fd<maxFD; fd++) {
	if (fd == fw->stdIn[0] || fd == fw->stdOut[0] ||
	    fd == fw->stdErr[0] || fd == fw->stdOut[1] ||
	    fd == fw->stdErr[1] || fd == fw->stdIn[1]) continue;
	if (fd == STDOUT_FILENO || fd == STDERR_FILENO) continue;
	close(fd);
    }

    if (fw->fwChildOE) {
	/* redirect stdout */
	close(STDOUT_FILENO);
	if ((dup2(fw->stdOut[1], STDOUT_FILENO)) == -1) {
	    pluginwarn(errno, "%s: dup2(%i) failed :", __func__, fw->stdOut[1]);
	    exit(1);
	}
	close(fw->stdOut[0]);
	/* redirect stderr */
	close(STDERR_FILENO);
	if ((dup2(fw->stdErr[1], STDERR_FILENO)) == -1) {
	    pluginwarn(errno, "%s: dup2(%i) failed :", __func__, fw->stdErr[1]);
	    exit(1);
	}
	close(fw->stdErr[0]);
    }

    /* restore signal handler */
    PSC_setSigHandler(SIGALRM, SIG_DFL);
    PSC_setSigHandler(SIGTERM, SIG_DFL);
    PSC_setSigHandler(SIGCHLD, SIG_DFL);
    PSC_setSigHandler(SIGPIPE, SIG_DFL);

    /* unblock blocked signals */
    PSID_blockSig(0, SIGCHLD);
    PSID_blockSig(0, SIGALRM);
}

/**
 * @brief Forwarder's main loop
 *
 * Main loop to be executed for the forwarder described by the
 * structure @a fw.
 *
 * @param fw Forwarder structure to be handled
 *
 * @return No return value.
 */
static void forwarderLoop(Forwarder_Data_t *fw)
{
    /* set timeout */
    if (fw->timeoutChild > 0) alarm(fw->timeoutChild);

    /* enable signals again */
    PSID_blockSig(0, SIGALRM);

    while (!sigChild) {
	if (Swait(-1) < 0  &&  errno != EINTR) {
	    pluginwarn(errno, "%s: Swait()", __func__);
	    killForwarderChild(fw, SIGKILL, "Swait() error", true);
	    break;
	}
    }
}

/**
 * @brief Make sure all children are dead and request the mother to close the
 * main connection.
 *
 * @return No return value.
 */
static void forwarderExit(Forwarder_Data_t *fw)
{
    PSID_blockSig(1, SIGALRM);
    PSID_blockSig(1, SIGTERM);

    /* reset possible alarms */
    alarm(0);

    /* make sure all children are dead */
    if (fw->cSid > 0) fw->killSession(fw->cSid, SIGKILL);
}

static void sendChildInfo(Forwarder_Data_t *fw)
{
    PSLog_Msg_t msg = {
	.header = {
	    .type = PSP_CC_MSG,
	    .dest = PSC_getTID(-1,0),
	    .sender = PSC_getMyTID(),
	    .len = PSLog_headerSize },
	.version = PLUGINFW_PROTO_VERSION,
	.type = PLGN_CHILD,
	.sender = -1};

    PSP_putMsgBuf((DDBufferMsg_t*)&msg, "childPID",
		  &fw->cPid, sizeof(fw->cPid));
    PSP_putMsgBuf((DDBufferMsg_t*)&msg, "childSID",
		  &fw->cSid, sizeof(fw->cSid));

    sendMsgToMother(&msg);
}

static void sendAccInfo(Forwarder_Data_t *fw, int32_t status,
			struct rusage *rusage)
{
    PSLog_Msg_t msg = {
	.header = {
	    .type = PSP_CC_MSG,
	    .dest = PSC_getTID(-1,0),
	    .sender = PSC_getMyTID(),
	    .len = PSLog_headerSize },
	.version = PLUGINFW_PROTO_VERSION,
	.type = PLGN_ACCOUNT,
	.sender = -1};
    PStask_ID_t logger = -1; /* ignored in psaccount */
    int32_t rank = -1 /* ignored in psaccount */, ext = 0;
    uid_t uid = -1; /* ignored in psaccount */
    gid_t gid = -1; /* ignored in psaccount */
    struct timeval now, wTime;
    uint64_t pSize = sysconf(_SC_PAGESIZE) < 0 ? 0 : sysconf(_SC_PAGESIZE);

    DDTypedBufferMsg_t accMsg = {
	.header = {
	    .type = PSP_CD_ACCOUNT,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getTID(-1, 0),
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_ACCOUNT_END };

    PSP_putTypedMsgBuf(&accMsg, "loggerTID", &logger, sizeof(logger));
    PSP_putTypedMsgBuf(&accMsg, "rank", &rank, sizeof(rank));
    PSP_putTypedMsgBuf(&accMsg, "uid", &uid, sizeof(uid));
    PSP_putTypedMsgBuf(&accMsg, "gid", &gid, sizeof(gid));
    PSP_putTypedMsgBuf(&accMsg, "cPID", &fw->cPid, sizeof(fw->cPid));
    PSP_putTypedMsgBuf(&accMsg, "rusage", rusage, sizeof(*rusage));
    PSP_putTypedMsgBuf(&accMsg, "pagesize", &pSize, sizeof(pSize));

    /* wall-time used by child */
    gettimeofday(&now, NULL);
    timersub(&now, &childStart, &wTime);
    PSP_putTypedMsgBuf(&accMsg, "walltime", &wTime, sizeof(wTime));

    PSP_putTypedMsgBuf(&accMsg, "status", &status, sizeof(status));
    PSP_putTypedMsgBuf(&accMsg, "extended flag", &ext, sizeof(ext));

    if (accMsg.header.len > sizeof(msg.buf)) {
	pluginlog("%s: accMsg to large to append\n", __func__);
    } else {
	memcpy(msg.buf, &accMsg, accMsg.header.len);
	msg.header.len += accMsg.header.len;
    }

    sendMsgToMother(&msg);
}

static void sendCodeInfo(int32_t ecode)
{
    PSLog_Msg_t msg = {
	.header = {
	    .type = PSP_CC_MSG,
	    .dest = PSC_getTID(-1,0),
	    .sender = PSC_getMyTID(),
	    .len = PSLog_headerSize },
	.version = PLUGINFW_PROTO_VERSION,
	.type = PLGN_CODE,
	.sender = -1};

    PSP_putMsgBuf((DDBufferMsg_t*)&msg, "exit code", &ecode, sizeof(ecode));

    sendMsgToMother(&msg);
}

static void sendExitInfo(int32_t estatus)
{
    PSLog_Msg_t msg = {
	.header = {
	    .type = PSP_CC_MSG,
	    .dest = PSC_getTID(-1,0),
	    .sender = PSC_getMyTID(),
	    .len = PSLog_headerSize },
	.version = PLUGINFW_PROTO_VERSION,
	.type = PLGN_EXIT,
	.sender = -1};

    PSP_putMsgBuf((DDBufferMsg_t*)&msg, "exit status",
		  &estatus, sizeof(estatus));

    sendMsgToMother(&msg);
}

static void sendFin(void)
{
    PSLog_Msg_t msg = {
	.header = {
	    .type = PSP_CC_MSG,
	    .dest = PSC_getTID(-1,0),
	    .sender = PSC_getMyTID(),
	    .len = PSLog_headerSize },
	.version = PLUGINFW_PROTO_VERSION,
	.type = PLGN_FIN,
	.sender = -1};

    sendMsgToMother(&msg);
}

static int handleChildOE(int fd, void *info)
{
    Forwarder_Data_t *fw = info;
    static char buf[1024];

    /* read from child */
    int32_t size = doRead(fd, buf, sizeof(buf));
    if (size <= 0) {
	Selector_remove(fd);
	close(fd);
	return 0;
    }

    /* send to mother */
    PSLog_Msg_t msg = {
	.header = {
	    .type = PSP_CC_MSG,
	    .dest = PSC_getTID(-1,0),
	    .sender = PSC_getMyTID(),
	    .len = PSLog_headerSize },
	.version = PLUGINFW_PROTO_VERSION,
	.type = (fd == fw->stdOut[0] ? STDOUT : STDERR),
	.sender = -1};

    /* Add data chunk including its length mimicking addString */
    uint32_t len = htonl(size+1);
    PSP_putMsgBuf((DDBufferMsg_t*)&msg, "len", &len, sizeof(len));
    PSP_putMsgBuf((DDBufferMsg_t*)&msg, "data", buf, size);

    sendMsgToMother(&msg);

    return 0;
}

static void monitorOEpipes(Forwarder_Data_t *fw)
{
    if (Selector_register(fw->stdOut[0], handleChildOE, fw) <0) {
	pluginlog("%s: register fd %i failed\n", __func__, fw->stdOut[0]);
    }
    close(fw->stdOut[1]);

    if (Selector_register(fw->stdErr[0], handleChildOE, fw) <0) {
	pluginlog("%s: register fd %i failed\n", __func__, fw->stdErr[0]);
    }
    close(fw->stdErr[1]);
}

static bool openOEpipes(Forwarder_Data_t *fw)
{
    /* stdout */
    if ((pipe(fw->stdOut)) == -1) {
	pluginwarn(errno, "%s: open stdout pipe for job %s failed", __func__,
		   fw->jobID);
	return false;
    }
    /* stderr */
    if ((pipe(fw->stdErr)) == -1) {
	pluginwarn(errno, "%s: create stderr pipe for job %s failed", __func__,
		   fw->jobID);
	return false;
    }
    return true;
}

static void execForwarder(PStask_t *task)
{
    fwTask = task;
    Forwarder_Data_t *fw = task->info;
    fwData = fw;
    int status = 0, i;
    struct rusage rusage;

    /* cleanup daemon memory and reset global facilities */
    PSID_clearMem(false);

    if (!initForwarder(fwTask->fd, fw)) {
	pluginlog("%s: initForwarder failed\n", __func__);
	exit(1);
    }

    if (fw->hookFWInit) {
	int ret = fw->hookFWInit(fw);
	if (ret < 0) {
	    pluginlog("%s: hookFWInit failed with %d\n", __func__, ret);
	    sendCodeInfo(ret);
	    exit(-1);
	}
    }

    if (fw->fwChildOE) {
	if (!openOEpipes(fw)) {
	    pluginlog("%s: initialize child STDOUT/STDERR failed\n", __func__);
	    exit(1);
	}
    }

    /* jail myself and all my children */
    if (fw->jailChild) {
	pid_t mypid = getpid();
	if (PSIDhook_call(PSIDHOOK_JAIL_CHILD, &mypid) < 0) {
	    pluginlog("%s: hook PSIDHOOK_JAIL_CHILD failed\n", __func__);
	    exit(1);
	}
    }

    for (i = 1; i <= fw->childRerun; i++) {
	if (fw->childFunc) {
	    int controlFDs[2];

	    /* open control fds */
	    if (socketpair(PF_UNIX, SOCK_STREAM, 0, controlFDs) < 0) {
		pluginwarn(errno, "%s: socketpair(controlFDs)", __func__);
		break;
	    }

	    /* fork child */
	    fw->cPid = fork();
	    if (fw->cPid  < 0) {
		pluginwarn(errno, "%s: fork()", __func__);
		exit(3);
	    } else if (!fw->cPid) {
		/* newly spawned child */
		close(controlFDs[0]);
		initChild(controlFDs[1], fw);
		fw->childFunc(fw, i);

		/* never reached */
		exit(1);
	    } else {
		/* read sid of child */
		close(controlFDs[1]);
		int read = doRead(controlFDs[0], &fw->cSid, sizeof(pid_t));
		close(controlFDs[0]);
		if (read != sizeof(pid_t)) {
		    pluginlog("%s: reading childs sid failed\n", __func__);
		    pskill(fw->cPid, SIGKILL, 0);
		}
		gettimeofday(&childStart, NULL);

		sendChildInfo(fw);
	    }
	}

	if (fw->fwChildOE) monitorOEpipes(fw);
	if (fw->hookLoop) fw->hookLoop(fw);
	forwarderLoop(fw);

	if (fw->childFunc) {
	    int res = wait4(fw->cPid, &status, 0, &rusage);
	    if (res == -1) {
		pluginwarn(errno, "%s: wait4(%d)", __func__, fw->cPid);
		status = 1;
	    } else if (fw->accounted) {
		sendAccInfo(fw, status, &rusage);
	    }
	    sendExitInfo(status);
	}
	/* check for timeout */
	if (jobTimeout) {
	    pluginlog("%s: child timed out\n", __func__);
	    status = -4;
	}

	if (status) break;
    }

    /* cancel timeout */
    alarm(0);

    sendFin();

    /* make sure handling all data, after child is gone + wait for FinACK */
    Swait(1);

    if (fw->hookFinalize) fw->hookFinalize(fw);

    /* cleanup */
    forwarderExit(fw);

    exit(status);
}

/* ----------------- Functions to be executed in mother ------------------ */

static void sigChldCB(int estatus, PStask_t *task)
{
    Forwarder_Data_t *fw = task->info;

    plugindbg(PLUGIN_LOG_FW, "%s: forwarder", __func__);
    if (fw) plugindbg(PLUGIN_LOG_FW, " %s (jobID %s)", fw->pTitle,
		      fw->jobID ? fw->jobID : "<?>");
    plugindbg(PLUGIN_LOG_FW, " TID %s returns %d\n",
	      PSC_printTID(task->tid), estatus);

    fw->fwExitStatus = estatus;

    if (task->fd == -1) {
	if (fw && fw->callback) fw->callback(fw->fwExitStatus, fw);
	ForwarderData_delete(fw);
	task->info = NULL;
    } else {
	/* sigChldCB() to be removed from task in caller */
	/* wait for connection to close */
    }
}

static void handleChildStart(Forwarder_Data_t *fw, PSLog_Msg_t *msg)
{
    size_t used = PSLog_headerSize - sizeof(msg->header);

    PSP_getMsgBuf((DDBufferMsg_t *)msg, &used, "childPID", &fw->cPid,
		  sizeof(fw->cPid));
    PSP_getMsgBuf((DDBufferMsg_t *)msg, &used, "childSID", &fw->cSid,
		  sizeof(fw->cSid));

    if (fw->hookChild) {
	fw->hookChild(fw, PSC_getPID(msg->header.sender), fw->cPid, fw->cSid);
    }

    plugindbg(PLUGIN_LOG_FW, "%s: fwTID %s childPid %i childSid %i\n", __func__,
	      PSC_printTID(msg->header.sender), fw->cPid, fw->cSid);
}

static void handleChildCode(Forwarder_Data_t *fw, PSLog_Msg_t *msg)
{
    size_t used = PSLog_headerSize - sizeof(msg->header);

    PSP_getMsgBuf((DDBufferMsg_t *)msg, &used, "exit code", &fw->hookExitCode,
		  sizeof(fw->hookExitCode));
    fw->codeRcvd = true;

    plugindbg(PLUGIN_LOG_FW, "%s: ecode %i\n", __func__, fw->hookExitCode);
}

static void handleChildExit(Forwarder_Data_t *fw, PSLog_Msg_t *msg)
{
    size_t used = PSLog_headerSize - sizeof(msg->header);

    PSP_getMsgBuf((DDBufferMsg_t *)msg, &used, "exit status",
		  &fw->chldExitStatus, sizeof(fw->chldExitStatus));
    fw->exitRcvd = true;

    plugindbg(PLUGIN_LOG_FW, "%s: estatus %i\n", __func__, fw->chldExitStatus);
}

static void handleChildFin(PStask_ID_t sender)
{
    PSLog_Msg_t msg = {
	.header = {
	    .type = PSP_CC_MSG,
	    .dest = sender,
	    .sender = PSC_getMyTID(),
	    .len = PSLog_headerSize },
	.version = PLUGINFW_PROTO_VERSION,
	.type = PLGN_FIN_ACK,
	.sender = -1};

    sendMsg(&msg);

    plugindbg(PLUGIN_LOG_FW, "%s: %s finalized\n", __func__,
	      PSC_printTID(sender));
}

/**
 * @brief Handle socket connected to a forwarder
 *
 * Handle the socket @a fd connected to a forwarder. @a info is
 * expected to point to the task structure describing the forwarder to
 * receive a message from.
 *
 * @param fd Socket connected to a forwarder
 *
 * @param info Pointer to the task structure describing the forwarder
 *
 * @return Always return 0
 */
static int handleFwSock(int fd, void *info)
{
    DDBufferMsg_t msg; /* ensure we'll have enough space */
    PSLog_Msg_t *lmsg = (PSLog_Msg_t *)&msg;
    PStask_t *task = info;
    Forwarder_Data_t *fw = task->info;

    if (!PSIDclient_recv(fd, &msg, sizeof(msg))) {
	if (!task->sigChldCB) {
	    /* SIGCHLD already received */
	    if (fw && fw->callback) fw->callback(fw->fwExitStatus, fw);
	    ForwarderData_delete(fw);
	    task->info = NULL;
	}
	PSIDclient_delete(fd);
	return 0;
    }

    if (!(fw->hideFWctrlMsg && msg.header.type == PSP_CC_MSG
	&& lmsg->type <= 64)) {
	if (fw->handleFwMsg && fw->handleFwMsg(lmsg, fw)) return 0;
    } else if (fw->fwChildOE
	       && (lmsg->type == STDOUT || lmsg->type == STDERR)) {
	/* forward child STDOUT/STDERR if requested */
	if (fw->handleFwMsg) fw->handleFwMsg(lmsg, fw);
	return 0;
    }

    switch (lmsg->type) {
    case STDIN:
    case FINALIZE:
	sendMsg(lmsg);
	break;
    case PLGN_CHILD:
	handleChildStart(fw, lmsg);
	break;
    case PLGN_ACCOUNT:
	PSID_handleMsg((DDBufferMsg_t *) &lmsg->buf);
	break;
    case PLGN_CODE:
	handleChildCode(fw, lmsg);
	break;
    case PLGN_EXIT:
	handleChildExit(fw, lmsg);
	break;
    case PLGN_FIN:
	handleChildFin(msg.header.sender);
	break;
    default:
	pluginlog("%s: invalid cmd %i from %s\n", __func__, lmsg->type,
		  PSC_printTID(lmsg->header.sender));
    }

    return 0;
}

bool startForwarder(Forwarder_Data_t *fw)
{
    if (!fw) {
	pluginlog("%s: no forwarder defined\n", __func__);
	return false;
    }
    if (!fw->killSession) {
	pluginlog("%s: no killSessions() given\n", __func__);
	return false;
    }

    PStask_t *task = PStask_new();

    task->group = TG_PLUGINFW;
    task->ptid = PSC_getMyTID();
    task->uid = getuid();
    task->gid = getgid();
    task->info = fw;
    task->argc = 1 + !!fw->jobID;
    task->argv = malloc(task->argc * sizeof(*task->argv));
    if (!task->argv) {
	pluginlog("%s: out of memory\n", __func__);
	goto ERROR;
    }
    task->argv[0] = strdup(fw->pTitle);
    if (fw->jobID) task->argv[1] = strdup(fw->jobID);
    task->sigChldCB = sigChldCB;

    /* start the new forwarder */
    if (PSIDspawn_localTask(task, execForwarder, handleFwSock)) {
	pluginlog("%s: creating forwarder %s failed\n", __func__, fw->pTitle);
	goto ERROR;
    }
    fw->tid = task->tid;

    return true;

ERROR:
    task->info = NULL;
    PStask_delete(task);
    return false;
}

bool signalForwarderChild(Forwarder_Data_t *fw, int sig)
{
    if (fw->cSid > 0) {
	fw->killSession(fw->cSid, sig);
	pluginlog("%s: session %i signal %i\n", __func__, fw->cSid, sig);

	/* Trigger the forwarder itself, too */
	if ((sig == SIGTERM || sig == SIGKILL) && fw->tid > 0) {
	    pskill(PSC_getPID(fw->tid), SIGTERM, 0);
	}
	return true;
    } else if (fw->tid != -1 && PSC_getPID(fw->tid)) {
	PSLog_Msg_t msg = {
	    .header = {
		.type = PSP_CC_MSG,
		.dest = fw->tid,
		.sender = PSC_getMyTID(),
		.len = PSLog_headerSize },
	    .version = PLUGINFW_PROTO_VERSION,
	    .type = PLGN_SIGNAL_CHLD,
	    .sender = -1};

	PSP_putMsgBuf((DDBufferMsg_t*)&msg, "sig", &sig, sizeof(sig));

	sendMsg(&msg);
	return true;
    } else {
	pluginlog("%s: unable to signal forwarder\n", __func__);
    }
    return false;
}

void startGraceTime(Forwarder_Data_t *fw)
{
    PSLog_Msg_t msg = {
	.header = {
	    .type = PSP_CC_MSG,
	    .dest = fw->tid,
	    .sender = PSC_getMyTID(),
	    .len = PSLog_headerSize },
	.version = PLUGINFW_PROTO_VERSION,
	.type = PLGN_START_GRACE,
	.sender = -1};

    if (fw->tid == -1 || !PSC_getPID(fw->tid)) return;

    sendMsg(&msg);
}

void shutdownForwarder(Forwarder_Data_t *fw)
{
    PSLog_Msg_t msg = {
	.header = {
	    .type = PSP_CC_MSG,
	    .dest = fw->tid,
	    .sender = PSC_getMyTID(),
	    .len = PSLog_headerSize },
	.version = PLUGINFW_PROTO_VERSION,
	.type = PLGN_SHUTDOWN,
	.sender = -1};

    if (fw->tid == -1 || !PSC_getPID(fw->tid)) return;

    sendMsg(&msg);
}
