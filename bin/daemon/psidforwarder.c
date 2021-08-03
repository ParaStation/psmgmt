/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/signalfd.h>

#include "pscio.h"
#include "pscommon.h"
#include "psserial.h"
#include "pstask.h"
#include "selector.h"
#include "kvscommon.h"
#include "psdaemonprotocol.h"
#include "pslog.h"
#include "psidcomm.h"
#include "psidhook.h"
#include "psidmsgbuf.h"
#include "psidutil.h"

#include "psidforwarder.h"

static char tag[] = "PSID_forwarder";
/** Forwarder's verbosity. Set in connectLogger() on behalf of logger's info. */
static bool verbose = false;

/** The ParaStation task ID of the logger */
static PStask_ID_t loggerTID = -1;

/** Description of the local child-task */
static PStask_t *childTask = NULL;

/** The socket connecting to the local ParaStation daemon */
static int daemonSock = -1;

/** Number of open file descriptors to wait for */
static int openfds = 0;

/** Flag for real SIGCHLD received */
static bool gotSIGCHLD = false;

/** List of messages waiting to be sent */
static LIST_HEAD(oldMsgs);

/**
 * Timeout for connecting to logger. This will be set according to the
 * number of children the logger has to handle within @ref
 * PSID_forwarder(). Might be overruled via __PSI_LOGGER_TIMEOUT
 * environment
 */
static int loggerTimeout = 60;

/**
 * @brief Close socket to daemon.
 *
 * Close the socket connecting the forwarder with the local daemon.
 *
 * @return No return value.
 */
static void closeDaemonSock(void)
{
    if (daemonSock < 0) return;

    PSLog_close();
    loggerTID = -1;

    Selector_remove(daemonSock);
    close(daemonSock);
    daemonSock = -1;
}

/**
 * @brief Send message to logger
 *
 * Send a message of type @a type and size @a len within @a buf to the
 * logger. This is done via the PSLog facility.
 *
 * @param type The type of the message to send
 *
 * @param buf Buffer holding the message to send
 *
 * @param len The length of the message to send
 *
 * @return On success, the number of bytes written is returned,
 * i.e. usually this is @a len. On error, -1 is returned, and errno is
 * set appropriately.
 *
 * @see PSLog_write()
 */
static ssize_t sendLogMsg(PSLog_msg_t type, char *buf, size_t len)
{
    int ret = 0;
    static bool first = true;

    if (loggerTID < 0) {
	if (first) {
	    PSID_log(-1, "%s(%d): not connected\n", __func__, type);
	    PSID_log(-1, "%s(%d): %s\n", __func__, type, buf);
	    first = false;
	}
	errno = EPIPE;

	return -1;
    }

    ret = PSLog_write(loggerTID, type, buf, len);

    if (ret < 0) {
	PSID_warn(-1, errno, "%s: PSLog_write()", __func__);
	closeDaemonSock();
    }

    return ret;
}

int PSIDfwd_printMsg(PSLog_msg_t type, char *buf)
{
    return sendLogMsg(type, buf, strlen(buf));
}

int PSIDfwd_printMsgf(PSLog_msg_t type, const char *format, ...)
{
    char buf[PSIDfwd_printMsgf_len];
    int n;

    va_list ap;
    va_start(ap, format);
    n = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    if (n >= 0) {
	n = sendLogMsg(type, buf, n);
    }
    return n;
}

/**
 * @brief Deliver signal
 *
 * Deliver signal @a signal to controlled process. If the child is
 * interactive, the signal is send to the foreground process group of
 * the controlling tty. Otherwise it's first tried to delivered to the
 * process-group with ID @a dest. If this fails, the process with ID
 * @a dest gets the signal.
 *
 * @param dest Process ID of the process to send signal to. Not used
 * for interactive children.
 *
 * @param signal The signal to send
 *
 * @return No return value
 */
static void sendSignal(pid_t dest, int signal)
{
    pid_t pid = (dest > 0) ? -dest : dest;

    /* determine foreground process group for or default pid */
    if (childTask->interactive && dest > 0) {
	pid = tcgetpgrp(childTask->stderr_fd);
	if (pid == -1) {
	    PSID_warn((errno == EBADF) ? PSID_LOG_SIGNAL : -1, errno,
		      "%s: tcgetpgrp()", __func__);
	    pid = -dest;
	} else if (pid) {
	    /* Send signal to process-group */
	    PSID_log(PSID_LOG_SIGNAL, "%s: got from tcgetpgrp()\n", __func__);
	    pid = -pid;
	} else {
	    pid = (dest > 0) ? -dest : dest;
	    PSID_log(PSID_LOG_SIGNAL, "%s: tcgetpgrp() said 0, try %d\n",
		     __func__, pid);
	}
    }

    /* actually send the signal */
    if (signal == SIGKILL) kill(pid, SIGCONT);
    if (kill(pid, signal) == -1) {
	PSID_warn((errno==ESRCH) ? PSID_LOG_SIGNAL : -1, errno,
		  "%s: kill(%d, %d)", __func__, pid, signal);
	/* Maybe try again, now to the single process */
	if ((errno==ESRCH) && dest > 0 && kill(dest, signal) == -1) {
	    PSID_warn((errno==ESRCH) ? PSID_LOG_SIGNAL : -1, errno,
		      "%s: kill(%d, %d)", __func__, dest, signal);
	}
    }
}

/**
 * @brief Handle signal message.
 *
 * Handle the signal message @a msg.
 *
 * @param msg Message containing destination and signal to deliver.
 *
 * @return No return value.
 */
static void handleSignalMsg(DDBufferMsg_t *msg)
{
    size_t used = offsetof(PSLog_Msg_t, buf) - offsetof(DDBufferMsg_t, buf);

    /* Get destination */
    pid_t pid;
    PSP_getMsgBuf(msg, &used, "pid", &pid, sizeof(pid));

    /* Get signal to send */
    int32_t signal;
    PSP_getMsgBuf(msg, &used, "signal", &signal, sizeof(signal));

    sendSignal(pid, signal);
}

/**
 * @brief Receive message from local daemon
 *
 * Receive a message from the local daemon and store it to @a msg. If
 * @a timeout is given, it is tried to receive a message for the
 * period defined therein. Otherwise this function will block until a
 * message is available.
 *
 * If the receive times out, i.e. the period defined in @a timeout
 * elapsed without receiving a complete message, 0 is returned.
 *
 * If a message was received during the period to wait, upon return @a
 * timeout will get updated and hold the remaining time of the
 * original timeout.
 *
 * @param msg Buffer to store the message to.
 *
 * @param timeout The timeout after which the function returns. If this
 * is NULL, this function will block indefinitely. Upon return this
 * value will get updated and hold the remnant of the original
 * timeout.
 *
 * @return On success, the number of bytes read are returned. On
 * error, -1 is returned, and errno is set appropriately.
 *
 * @see PSLog_read()
 */
static ssize_t recvDaemonMsg(DDBufferMsg_t *msg, struct timeval *timeout)
{
    if (daemonSock < 0) {
	PSID_log(-1, "%s: not connected\n", __func__);
	errno = EPIPE;
	return -1;
    }

    ssize_t ret;
    while (true) {
	ret = PSCio_recvMsgT(daemonSock, msg, timeout);

	if (ret < 0) {
	    int eno = errno;
	    switch (errno) {
	    case EOPNOTSUPP:
		if (timeout) continue;
		break;
	    case ETIME:
		break;
	    default:
		PSID_warn(-1, eno, "%s: PSCio_recvMsgT()", __func__);
		closeDaemonSock();
	    }
	    errno = eno;
	} else if (!ret) {
	    PSID_log(-1, "%s: daemon connection lost\n", __func__);
	    closeDaemonSock();
	}
	break;
    }

    return ret;
}

ssize_t sendDaemonMsg(void *amsg)
{
    if (daemonSock < 0) {
	errno = ENOTCONN;
	return -1;
    }
    if (!amsg) {
	errno = ENOMSG;
	return -1;
    }

    DDMsg_t *msg = (DDMsg_t *)amsg;
    ssize_t ret = PSCio_sendF(daemonSock, msg, msg->len);

    if (ret < 0) {
	PSID_warn(-1, errno ,"%s: doWriteF()", __func__);
	closeDaemonSock();
	return ret;
    } else if (!ret) {
	PSID_log(-1, "%s: Lost connection to daemon\n", __func__);
	closeDaemonSock();
	return ret;
    }

    if (verbose && loggerTID >= 0) {
	PSIDfwd_printMsgf(STDERR, "%s: %s: type %s (len=%d) to %s\n",
			  tag, __func__, PSDaemonP_printMsg(msg->type),
			  msg->len, PSC_printTID(msg->dest));
    }

    return msg->len;
}

/**
 * @brief Connect to the logger.
 *
 * Connect to the logger described by the unique task ID @a tid. Wait
 * @a timeout seconds for #INITIALIZE answer and set @ref loggerTID and
 * @ref verbose accordingly.
 *
 * @param tid The logger's ParaStation task ID.
 *
 * @param timeout Number of seconds to wait for #INITIALIZE answer.
 *
 * @return On success, true is returned and @ref loggerTID is
 * set; or false in case off error which sets @ref loggerTID to -1
 */
static bool connectLogger(PStask_ID_t tid)
{
    loggerTID = tid; /* Only set for the first sendLogMsg()/recvMsg() pair */

    sendLogMsg(INITIALIZE, (char *)&childTask->group, sizeof(childTask->group));

    struct timeval timeout = {loggerTimeout, 0};
    while (true) {
	DDBufferMsg_t msg;
	PSLog_Msg_t *lmsg = (PSLog_Msg_t *)&msg;
	ssize_t ret = recvDaemonMsg(&msg, &timeout);

	if (ret <= 0) {
	    PSID_warn(-1, errno, "%s(%s): Connection refused\n",
		      __func__, PSC_printTID(tid));
	    break;
	}

	if (msg.header.len < PSLog_headerSize + (int) sizeof(int)) {
	    PSID_log(-1, "%s(%s): rank %i : Message too short\n", __func__,
		     PSC_printTID(tid), childTask->rank);
	    break;
	} else if (msg.header.type != PSP_CC_MSG) {
	    PSID_log(-1 ,"%s(%s): Protocol messed up, got %s\n", __func__,
		     PSC_printTID(tid), PSDaemonP_printMsg(msg.header.type));
	    continue;
	} else if (lmsg->type != INITIALIZE) {
	    PSID_log(-1 ,"%s(%s): Protocol messed up\n",
		     __func__, PSC_printTID(tid));
	    break;
	} else if (msg.header.sender != tid) {
	    PSID_log(-1, "%s(%s): Got INITIALIZE", __func__, PSC_printTID(tid));
	    PSID_log(-1, " from %s\n", PSC_printTID(msg.header.sender));
	    break;
	} else {
	    size_t used =
		offsetof(PSLog_Msg_t, buf) - offsetof(DDBufferMsg_t, buf);

	    /* Get verbosity */
	    uint32_t verb;
	    PSP_getMsgBuf(&msg, &used, "verb", &verb, sizeof(verb));
	    verbose = verb;
	    PSID_log(PSID_LOG_SPAWN, "%s(%s): Connected\n", __func__,
		     PSC_printTID(tid));
	    return true;
	}
    }

    loggerTID = -1;
    return false;
}

/**
 * @brief Close connection to logger
 *
 * Send a #FINALIZE message to the logger and wait for an #EXIT
 * message as reply. Within the #FINALIZE message the exit @a status
 * of the controlled child process is send to the logger.
 *
 * @param status Exit status of the controlled child process. This
 * status will be sent within the #FINALIZE message to the logger.
 *
 * @return No return value
 */
static void releaseLogger(int status)
{
    if (loggerTID < 0) return;

    send_again:
    while (true) {
	sendLogMsg(FINALIZE, (char *)&status, sizeof(status));

	struct timeval timeout = {10, 0};
	while (timerisset(&timeout)) {
	    DDBufferMsg_t msg;
	    PSLog_Msg_t *lmsg = (PSLog_Msg_t *)&msg;
	    ssize_t ret = recvDaemonMsg(&msg, &timeout);

	    if (ret < 0) {
		switch (errno) {
		case EINTR:
		    continue;
		case EPIPE:
		    PSID_log(-1, "%s: logger %s already disappeared\n",
			     __func__, PSC_printTID(childTask->loggertid));
		    return;
		case ETIME:
		    PSID_log(-1, "%s: receive timeout. Send again\n", __func__);
		    goto send_again;
		default:
		    PSID_warn(-1, errno, "%s: recvMsg()", __func__);
		}
	    }
	    if (lmsg->header.type == PSP_CC_MSG && lmsg->type == EXIT) {
		PSID_log(PSID_LOG_SPAWN, "%s(%d): Released %s\n", __func__,
			 status, PSC_printTID(loggerTID));
		loggerTID = -1;
		return;
	    }
	    if (!PSID_handleMsg(&msg)) {
		PSIDfwd_printMsgf(STDERR, "%s: %s: Unexpected msg type %s"
				  " from %s\n", tag, __func__,
				  PSP_printMsg(msg.header.type),
				  PSC_printTID(msg.header.sender));
	    }
	}
    }
    loggerTID = -1;
}

/**
 * @brief Write to client's stdin
 *
 * Write the message @a msg to the file descriptor connected to the
 * clients stdin starting at an offset of @a offset bytes. It is
 * expected that the previous parts of the message were sent in
 * earlier calls to this function.
 *
 * @param msg Message to transmit
 *
 * @param offset Number of bytes sent in earlier calls
 *
 * @return On success, the total number of bytes written is returned,
 * i.e. usually this is the length of @a msg. If the file descriptor
 * blocks, this might also be smaller. In this case the total number
 * of bytes sent in this and all previous calls is returned. 0 will be
 * returned if due to the payload of @a msg the file descriptor is
 * closed. If an error occurs, e.g. the stdin file descriptor is
 * already closed, -1 is returned and errno is set appropriately.
 */
static ssize_t doClntWrite(PSLog_Msg_t *msg, size_t offset)
{
    size_t cnt = msg->header.len - PSLog_headerSize;
    int sock = childTask->stdin_fd;
    if (sock < 0) {
	errno = ENOTCONN;
	return -1;
    }

    if (!cnt) {
	/* close clients stdin */
	shutdown(sock, SHUT_WR);
	if (Selector_isRegistered(sock)) Selector_vacateWrite(sock);

	/* Interactive jobs might use a single file-descriptor */
	if (sock != childTask->stdout_fd && sock != childTask->stderr_fd) {
	    close(sock);
	}

	childTask->stdin_fd = -1;
	return 0;
    }

    size_t sent;
    ssize_t ret = PSCio_sendProg(sock, &msg->buf[offset], cnt - offset, &sent);
    if (ret < 0) {
	int eno = errno;
	if (eno == EAGAIN) {
	    return offset + sent;
	} else if (eno == EPIPE) {
	    sendLogMsg(STOP, NULL, 0);
	    if (Selector_isRegistered(sock)) Selector_vacateWrite(sock);
	    childTask->stdin_fd = -1;
	} else {
	    char *errstr = strerror(eno);
	    PSIDfwd_printMsgf(STDERR, "%s: %s: errno %d on stdinSock: %s\n",
			      tag, __func__, eno, errstr ? errstr : "UNKNOWN");
	}
	errno = eno;
	return ret;
    }
    return offset + ret;
}

/**
 * @brief Store message in oldMsgs list
 *
 * Store the message @a msg to be transmitted to the child's stdin
 * file descriptor to the @ref oldMsgs list. @a offset contains the
 * number of bytes already transmitted to the child's stdin file
 * descriptor.
 *
 * @param msg Message to store
 *
 * @param offset Number of bytes already transmitted
 *
 * @return Upon successful storing @a msg true is returned; or
 * false otherwise
 */
static bool storeMsg(PSLog_Msg_t *msg, int offset)
{
    PSIDmsgbuf_t *msgbuf = PSIDMsgbuf_get(msg->header.len);
    if (!msgbuf) return false;

    memcpy(msgbuf->msg, msg, msg->header.len);
    msgbuf->offset = offset;

    list_add_tail(&msgbuf->next, &oldMsgs);
    return true;
}

/**
 * @brief Drop all messages stored to oldMsgs list
 *
 * Drop all messages stored to the @ref oldMsgs list waiting to be
 * transmitted to the child's stdin file descriptor. This function
 * shall be called once the corresponding file descriptor is closed,
 * especially if this happens unexpectedly.
 *
 * @return No return value
 */
static void dropAllMsgs(void)
{
    list_t *m, *tmp;
    list_for_each_safe(m, tmp, &oldMsgs) {
	PSIDmsgbuf_t *msgbuf = list_entry(m, PSIDmsgbuf_t, next);
	list_del(&msgbuf->next);
	PSIDMsgbuf_put(msgbuf);
    }
}

/**
 * @brief Flush messages stored in oldMsgs list
 *
 * Flush messages stored in the @ref oldMsgs list to be transmitted to
 * child's stdin file descriptor. All arguments of this function are
 * ignored and only present to act as a writeHandler for the Selector
 * facility.
 *
 * @param fd Ignored
 *
 * @param info Ignored
 *
 * @return According to the Selector facility's expectations -1, 0, or
 * 1 is returned
 */
static int flushMsgs(int fd /* dummy */, void *info /* dummy */)
{
    list_t *m, *tmp;
    list_for_each_safe(m, tmp, &oldMsgs) {
	PSIDmsgbuf_t *msgbuf = list_entry(m, PSIDmsgbuf_t, next);
	PSLog_Msg_t *msg = (PSLog_Msg_t *)msgbuf->msg;
	int len = msg->header.len - PSLog_headerSize;

	ssize_t written = doClntWrite(msg, msgbuf->offset);
	if (written < 0) {
	    if (errno == EPIPE || errno == ENOTCONN) dropAllMsgs();
	    return written;
	}
	if (written != len) {
	    msgbuf->offset = written;
	    break;
	}
	list_del(&msgbuf->next);
	PSIDMsgbuf_put(msgbuf);
    }

    if (!list_empty(&oldMsgs)) return 1;

    int stdinSock = childTask->stdin_fd;
    if (Selector_isRegistered(stdinSock)) Selector_vacateWrite(stdinSock);
    sendLogMsg(CONT, NULL, 0);

    return 0;
}

/**
 * @brief Transmit message to child's stdin file descriptor
 *
 * Transmit the payload of the STDIN message @a msg to the child's
 * stdin file descriptor. For this, first messages queued in the @ref
 * oldMsgs list are flushed before the payload of @a msg is
 * transmitted. If the payload can not or only partially be
 * transmitted, @a msg will be queued via @ref storeMsg() to @ref
 * oldMsgs, too.
 *
 * @param msg Message to be written
 *
 * @return No return value
 */
static void writeMsg(PSLog_Msg_t *msg)
{
    if (!list_empty(&oldMsgs)) flushMsgs(0, NULL);

    bool emptyList = list_empty(&oldMsgs);
    ssize_t written = 0;
    if (emptyList) written = doClntWrite(msg, 0);
    if (written < 0) return;

    int len = msg->header.len - PSLog_headerSize;
    if (written != len || !emptyList) {
	if (!storeMsg(msg, written)) return;
	if (emptyList && childTask->stdin_fd != -1) {
	    Selector_awaitWrite(childTask->stdin_fd, flushMsgs, NULL);
	    if (len) sendLogMsg(STOP, NULL, 0);
	}
    }
}

static void handleWINCH(PSLog_Msg_t *msg)
{
    struct winsize w;
    int count, len = 0;
    int *buf;

    if (childTask->stdin_fd < 0 || !msg) return;

    count = msg->header.len - PSLog_headerSize;
    buf = (int *)msg->buf;

    if (count != 4 * sizeof(*buf)) {
	PSIDfwd_printMsgf(STDERR, "%s: %s: Corrupted WINCH message\n", tag,
			  __func__);
	return;
    }

    w.ws_col = buf[len++];
    w.ws_row = buf[len++];
    w.ws_xpixel = buf[len++];
    w.ws_ypixel = buf[len++];

    if (ioctl(childTask->stdin_fd, TIOCSWINSZ, &w) < 0)
	PSIDfwd_printMsgf(STDERR, "%s: %s: ioctl(TIOCSWINSZ): %s\n", tag,
			  __func__, strerror(errno));

    if (verbose) {
	PSIDfwd_printMsgf(STDERR, "%s: %s: WINCH to col %d row %d"
			  " xpixel %d ypixel %d\n", tag, __func__,
			  w.ws_col, w.ws_row, w.ws_xpixel, w.ws_ypixel);
    }
}

/**
 * @brief Read input from local daemon
 *
 * Read and handle input from the local daemon. Usually this will be
 * input designated to the local client process which will be
 * forwarded. As an extension this might be an #EXIT message
 * displaying that the controlling logger is going to die. Thus also
 * the forwarder will stop execution and exit() which finally will
 * result in the local daemon killing the client process.
 *
 * @param fd The file-descriptor to read from
 *
 * @param data Some additional data. Currently not in use.
 *
 * @return If a fatal error occurred, -1 is returned and errno is set
 * appropriately. Otherwise 0 is returned.
 */
static int readFromDaemon(int fd, void *data)
{
    DDBufferMsg_t msg;

    ssize_t ret = recvDaemonMsg(&msg, NULL);

    if (ret <= 0) {
	/* The connection to the daemon died. Kill the client the hard way. */
	if (!ret) sendSignal(PSC_getPID(childTask->tid), SIGKILL);

	return ret;
    }

    if (!PSID_handleMsg(&msg)) {
	PSIDfwd_printMsgf(STDERR, "%s: %s: Unexpected msg type %s\n",
			  tag, __func__, PSP_printMsg(msg.header.type));
    }

    return 0;
}

static bool msgRELEASERES(DDBufferMsg_t *msg)
{
    /* release the client */
    int res = 1;
    PSIDhook_call(PSIDHOOK_FRWRD_EXIT, &res);
    return true;
}

static bool msgCC(DDBufferMsg_t *msg)
{
    PSLog_Msg_t *lmsg = (PSLog_Msg_t *)msg;
    int res = 0;

    switch (lmsg->type) {
    case SIGNAL:
	handleSignalMsg(msg);
	break;
    case STDIN:
	if (verbose) {
	    PSIDfwd_printMsgf(STDERR, "%s: %s: %zd bytes on STDIN\n", tag,
			      __func__, lmsg->header.len - PSLog_headerSize);
	}
	if (childTask->stdin_fd < 0) {
	    static bool first = true;
	    if (first) {
		PSIDfwd_printMsgf(STDERR, "%s: %s: STDIN already closed\n",
				  tag, __func__);
		first = false;
	    }
	} else {
	    writeMsg(lmsg);
	}
	break;
    case EXIT:
	/* Logger is going to die */
	/* Release the client */
	PSIDhook_call(PSIDHOOK_FRWRD_EXIT, &res);
	/* Release the daemon */
	closeDaemonSock();
	/* Cleanup child */
	sendSignal(PSC_getPID(childTask->tid), SIGKILL);
	exit(0);
	break;
    case WINCH:
	/* Logger detected change in window-size */
	handleWINCH(lmsg);
	break;
    case INITIALIZE:
	/* ignore late INITIALIZE answer */
	break;
    default:
	PSIDfwd_printMsgf(STDERR,"%s: %s: Unknown type %d from %s\n",
			  tag, __func__, lmsg->type,
			  PSC_printTID(msg->header.sender));
    }
    return true;
}

static bool msgCC_ERROR(DDBufferMsg_t *msg)
{
    if (msg->header.sender == loggerTID) {
	PSID_log(-1, "%s: logger %s disappeared\n", __func__,
		 PSC_printTID(loggerTID));
	loggerTID = -1;
	return true;
    }
    return false;
}
/**
 * @brief Collect some output before forwarding
 *
 * This is a replacement for the read(2) function call. read(2) is
 * called repeatedly until an EOL ('\n') is received as the last
 * character or a timeout occurred.
 *
 * @param sock Socket to read(2) from.
 *
 * @param buf Array to store to
 *
 * @param count Size of @a *buf. At most this number of bytes are received.
 *
 * @param total Actual number of bytes received.
 *
 *
 * @return The number of bytes received within the last round. 0
 * denotes an EOF. If an error occurred, -1 is returned and errno is
 * set appropriately. If the last round timed out, -1 is returned and
 * errno is set to ETIME.
 */
static size_t collectRead(int sock, char *buf, size_t count, size_t *total)
{
    int n;

    *total = 0;

    if (sock < 0) {
	errno = EBADF;
	return -1;
    }

    do {
	fd_set fds;
	struct timeval timeout;

	FD_ZERO(&fds);
	FD_SET(sock, &fds);
	timeout = (struct timeval) {0, 1000};

	n = select(sock+1, &fds, NULL, NULL, &timeout);
	if (n < 0) {
	    if (errno == EINTR) continue;

	    PSIDfwd_printMsgf(STDERR, "%s: %s: error on select(): %s\n", tag,
			      __func__, strerror(errno));
	    break;
	}

	if (n) {
	    n = read(sock, &buf[*total], count-*total);
	    if (n > 0) *total += n;
	} else {
	    /* Only return 0 on close */
	    errno = ETIME;
	    n = -1;
	}
    } while (n > 0 && *total < count && buf[*total-1] != '\n');

    return n;
}

/* @brief  Read child's output.
 *
 * @param fd The file-descriptor to read from
 *
 * @param data Some additional data. Used for the type of PSLog message.
 *
 * @return If a fatal error occurred, -1 is returned and errno is set
 * appropriately. Otherwise 0 is returned.
 */
static int readFromChild(int fd, void *data)
{
    char buf[4000];
    PSLog_msg_t type = *(PSLog_msg_t *)data;
    size_t total;
    int n;

    n = collectRead(fd, buf, sizeof(buf), &total);
    if (verbose) {
	PSIDfwd_printMsgf(STDERR, "%s: %s: got %ld bytes on sock %d\n",
			  tag, __func__, (long) total, fd);
    }
    if (n==0 || (n<0 && errno==EIO)) {
	/* socket closed */
	if (verbose) {
	    PSIDfwd_printMsgf(STDERR, "%s: %s: closing %d\n",
			      tag, __func__, fd);
	}

	Selector_remove(fd);
	shutdown(fd, SHUT_RD);
	openfds--;
	if (!openfds && verbose) {
	    /* stdout and stderr closed -> wait for SIGCHLD */
	    PSIDfwd_printMsgf(STDERR, "%s: %s: wait for SIGCHLD\n",
			      tag, __func__);
	}
	if (!openfds) Selector_startOver();
    } else if (n<0 && errno!=ETIME && errno!=ECONNRESET) {
	/* ignore the error */
	PSIDfwd_printMsgf(STDERR, "%s: %s: collectRead(): %s\n",
			  tag, __func__, strerror(errno));
    }
    if (total) {
	/* forward it to logger */
	sendLogMsg(type, buf, total);
    }

    return 0;
}

/**
 * @brief Send accounting information.
 *
 * Send the collected accounting information to the corresponding
 * accounting daemons.
 *
 * @param rusage The rusage information received from the child.
 *
 * @param status The returned status of the child.
 *
 * @return No return value.
 */
static void sendAcctData(struct rusage rusage, int32_t status)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_ACCOUNT,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getTID(-1, 0),
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_ACCOUNT_END };

    /* logger's TID, this identifies a task uniquely */
    PSP_putTypedMsgBuf(&msg, "loggerTID", &childTask->loggertid,
		       sizeof(childTask->loggertid));

    /* current rank */
    PSP_putTypedMsgBuf(&msg, "rank", &childTask->rank, sizeof(childTask->rank));

    /* child's uid */
    PSP_putTypedMsgBuf(&msg, "uid", &childTask->uid, sizeof(childTask->uid));

    /* child's gid */
    PSP_putTypedMsgBuf(&msg, "gid", &childTask->gid, sizeof(childTask->gid));

    /* child's pid */
    pid_t pid = PSC_getPID(childTask->tid);
    PSP_putTypedMsgBuf(&msg, "pid", &pid, sizeof(pid));

    /* actual rusage structure */
    PSP_putTypedMsgBuf(&msg, "rusage", &rusage, sizeof(rusage));

    /* pagesize */
    int64_t pagesize = sysconf(_SC_PAGESIZE);
    if (pagesize < 1) pagesize = 0;
    PSP_putTypedMsgBuf(&msg, "pagesize", &pagesize, sizeof(pagesize));

    /* walltime used by child */
    struct timeval now, walltime;
    gettimeofday(&now, NULL);
    timersub(&now, &childTask->started, &walltime);
    PSP_putTypedMsgBuf(&msg, "walltime", &walltime, sizeof(walltime));

    /* child's return status */
    PSP_putTypedMsgBuf(&msg, "status", &status, sizeof(status));

    /* extended msg flag, will be overwritten by accounting plugin */
    int32_t extend = 0;
    PSP_putTypedMsgBuf(&msg, "extend", &extend, sizeof(extend));

    sendDaemonMsg(&msg);
}

/** Contains child's PID after SIGCHLD received */
static pid_t childPID = 0;

/** Contains child's exit-status after SIGCHLD received */
static int childStatus;

/** Contains child's resource-usage after SIGCHLD received */
static struct rusage childRUsage;

/**
 * @brief Signal handler
 *
 * The forwarders signal handler functions. At the moment the
 * following signals @a sig are handled:
 *
 * - SIGUSR1 Print messages about open sockets and do some more
 * debugging stuff. This is mainly for internal use as testing and
 * debugging.
 *
 * - SIGUSR2 Raise SIGSEGV in order to dump core
 *
 * - SIGTERM Catch and forward to child
 *
 * - SIGTTIN Just report
 *
 * - SIGPIPE Just report
 *
 * @param sig The signal to handle.
 *
 * @return No return value.
 */
static void sighandler(int sig)
{
    int i, maxFD;

    char txt[80];

    switch (sig) {
    case SIGUSR1:
	maxFD = sysconf(_SC_OPEN_MAX);
	snprintf(txt, sizeof(txt),
		 "[%d] %s: open sockets left:", childTask->rank, tag);
	for (i = 0; i < maxFD; i++) {
	    if (Selector_isRegistered(i)) {
		snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), " %d", i);
	    }
	}
	snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), "\n");
	PSIDfwd_printMsg(STDERR, txt);
	PSID_log(-1, "%s: %s", __func__, txt);
	break;
    case SIGUSR2:
	PSIDfwd_printMsgf(STDERR, "%s: Got %s\n", tag, strsignal(sig));
	raise(SIGSEGV);
	break;
    case SIGTERM:
	sendSignal(PSC_getPID(childTask->tid), SIGTERM);
	break;
    case SIGTTIN:
    case SIGPIPE:
	PSIDfwd_printMsgf(STDERR, "%s: got %s\n", tag, strsignal(sig));
	break;
    }
}

static void finalizeForwarder(void)
{
    if (openfds) PSIDfwd_printMsgf(STDERR, "%s: %s: %d open file-descriptors"
				   " remaining\n", tag, __func__, openfds);

    if (!gotSIGCHLD) PSIDfwd_printMsgf(STDERR, "%s: %s: SIGCHLD not yet"
				       " received\n", tag, __func__);

    sendLogMsg(USAGE, (char *) &childRUsage, sizeof(childRUsage));

    int clientStat = PSIDhook_call(PSIDHOOK_FRWRD_CLNT_RLS, childTask);

    /* Release, if no error occurred and not already done */
    if ((!clientStat || clientStat == PSIDHOOK_NOFUNC)
	&& (WIFEXITED(childStatus) && !WEXITSTATUS(childStatus))
	&& !WIFSIGNALED(childStatus)) {
	/* release the child */
	DDSignalMsg_t msg = {
	    .header = {
		.type = PSP_CD_RELEASE,
		.sender = PSC_getMyTID(),
		.dest = childTask->tid,
		.len = sizeof(msg) },
	    .signal = -1,
	    .answer = 0 };  /* Don't expect answer in this late stage */
	sendDaemonMsg(&msg);
    }

    /* Send ACCOUNT message to daemon; will forward to accounters */
    if (childTask->group != TG_ADMINTASK
	&& childTask->group != TG_SERVICE
	&& childTask->group != TG_SERVICE_SIG
	&& childTask->group != TG_KVS) {
	sendAcctData(childRUsage, childStatus);
    }

    /*
     * first release logger -- otherwise forwarder might remain,
     * if CC_MSG get's lost due to closed connection
     */
    releaseLogger(childStatus);

    /* Send CHILDDEAD message to the daemon */
    DDErrorMsg_t msg = {
	.header = {
	    .type = PSP_DD_CHILDDEAD,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getTID(-1, 0),
	    .len = sizeof(msg) },
	.error = childStatus,
	.request = PSC_getTID(-1, childPID) };
    sendDaemonMsg(&msg);

    /* Send SIGKILL to process group in order to stop fork()ed children */
    sendSignal(-PSC_getPID(childTask->tid), SIGKILL);

    /* Release the client */
    int res = 0;
    PSIDhook_call(PSIDHOOK_FRWRD_EXIT, &res);

    /* Release the daemon */
    closeDaemonSock();

    exit(0);
}

/**
 * @brief Send PSP_CD_SPAWNFAILED message
 *
 * Send a PSP_CD_SPAWNFAILED message. This signals the local daemon
 * that something went wrong during the actual spawn of the child
 * process.
 *
 * @param task Structure describing the child-process failed to be spawned.
 *
 * @param eno Error-number (i.e. errno) describing the problem
 * preventing the child-process from being spawned.
 *
 * @return No return value
 */
static void sendSpawnFailed(PStask_t *task, int eno)
{
    DDBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_SPAWNFAILED,
	    .sender = PSC_getMyTID(),
	    .dest = task->ptid,
	    .len = sizeof(DDErrorMsg_t) } };
    DDErrorMsg_t *errMsg = (DDErrorMsg_t *)&msg;

    errMsg->request = task->rank;
    errMsg->error = eno;

    size_t bufUsed = msg.header.len - sizeof(msg.header);
    char *ptr = msg.buf + bufUsed;
    size_t bufAvail = sizeof(msg.buf) - bufUsed;

    size_t read;
    do {
	collectRead(task->stderr_fd, ptr, bufAvail, &read);
	bufAvail -= read;
	msg.header.len += read;
	ptr += read;
    } while (read && bufAvail);

    if (!bufAvail) {
	msg.buf[sizeof(msg.buf) - 5] = '.';
	msg.buf[sizeof(msg.buf) - 4] = '.';
	msg.buf[sizeof(msg.buf) - 3] = '.';
	msg.buf[sizeof(msg.buf) - 2] = '\n';
	msg.buf[sizeof(msg.buf) - 1] = '\0';
    }
    sendDaemonMsg(&msg);
}

/**
 * @brief Send PSP_DD_CHILDBORN message
 *
 * Send a PSP_DD_CHILDBORN message. This signals the local daemon that
 * the actual spawn of the child-process was successful and the
 * forwarder will switch to normal operations.
 *
 * After receiving a PSP_DD_CHILDACK message as an answer it will
 * either signal the child process -- by just closing @a clientFD --
 * to execv() the actual client process or to just exit() -- upon
 * receiving some non-NULL data.
 *
 * @param task Structure describing the client-process that was
 * successfully fork()ed and waits to be execv()ed
 *
 * @param clientFD File descriptor connected to the client process
 *
 * @return On success true is returned or false if an error occurred
 */
static bool sendChildBorn(PStask_t *task, int clientFD)
{
    if (!task) return false;

    DDErrorMsg_t msg = {
	.header = {
	    .type = PSP_DD_CHILDBORN,
	    .sender = PSC_getMyTID(),
	    .dest = task->ptid,
	    .len = sizeof(msg) },
	.request = task->tid,
	.error = 0 };

send_again:
    sendDaemonMsg(&msg);

    struct timeval timeout = {10, 0};
    DDBufferMsg_t answer;

    while (true) {
	ssize_t ret = recvDaemonMsg(&answer, &timeout);
	if (ret < 0) {
	    switch (errno) {
	    case EINTR:
		continue;
	    case ETIME:
		PSID_log(-1, "%s: receive timed out. Send again\n", __func__);
		goto send_again;
	    default:
		PSID_warn(-1, errno, "%s: recvDaemonMsg()", __func__);
	    }
	} else if (!ret) {
	    PSID_log(-1, "%s: daemon closed connection\n", __func__);
	} else if (answer.header.type == PSP_DD_CHILDACK) {
	    if (clientFD > -1) {
		close(clientFD);
		return true;
	    } else {
		PSID_log(-1, "%s: cannot start child\n", __func__);
	    }
	} else if (PSID_handleMsg(&answer)) {
	    continue;
	} else {
	    ssize_t written = 0;
	    PSID_log(-1, "%s: wrong answer type %s, don't execve() child\n",
		     __func__, PSDaemonP_printMsg(answer.header.type));
	    if (clientFD > -1) {
		written = write(clientFD, "x", 1);
		close(clientFD);
	    }
	    if (written != 1) {
		PSID_log(-1, "%s: cannot stop child, try to kill\n", __func__);
		sendSignal(PSC_getPID(childTask->tid), SIGKILL);
	    }
	}
	break;
    }

    return false;
}

/**
 * @brief Handling of SIGCHLD
 *
 * Process all pending SIGCHLD. Will cleanup corresponding
 * task-structures, etc.
 *
 * @param fd File-selector providing info on died child processes.
 *
 * @param info Dummy pointer to extra info. Ignored.
 *
 * @return Always return 0
 */
static int handleSIGCHLD(int fd, void *info)
{
    struct signalfd_siginfo sigInfo;

    /* Ignore data available on fd. We rely on waitpid() alone */
    if (read(fd, &sigInfo, sizeof(sigInfo)) < 0) {
	PSID_warn(-1, errno, "%s: read()", __func__);
    }

    /* if child is stopped, return */
    childPID = wait3(&childStatus, WUNTRACED | WCONTINUED | WNOHANG,
		     &childRUsage);
    if (childPID && !WIFSTOPPED(childStatus) && !WIFCONTINUED(childStatus)) {
	if (verbose) PSIDfwd_printMsgf(STDERR, "%s: SIGCHLD for %d\n", tag,
				       childPID);
	gotSIGCHLD = true;

	/* Get rid of now useless selector */
	Selector_remove(fd);
	Selector_startOver();
	close(fd);

	/* Tell attached plugins about child's exit status */
	PSIDhook_call(PSIDHOOK_FRWRD_CLNT_RES, &childStatus);
    }

    return 0;
}

static void waitForChildsDead(void)
{
    while (!gotSIGCHLD) {
	Swait(10 * 1000);  /* sleep in Swait */

	sendSignal(PSC_getPID(childTask->tid), SIGKILL);
    }

    finalizeForwarder();
}

/* see header file for documentation */
void PSID_forwarder(PStask_t *task, int clientFD, int eno)
{
    static PSLog_msg_t stdoutType = STDOUT, stderrType = STDERR;
    char pTitle[50];
    char *envStr, *timeoutStr;

    /* Ensure that PSCio can handle file descriptor to daemon via select() */
    if (task->fd >= FD_SETSIZE) {
	int newFD = dup(task->fd);
	if (newFD >= FD_SETSIZE) {
	    PSID_exit(ECHRNG, "%s: task->fd %d", __func__, newFD);
	}
	close(task->fd);
	task->fd = newFD;
    }

    /* Allow to create coredumps */
    prctl(PR_SET_DUMPABLE, 1, /* rest ignored */ 0, 0, 0);

    childTask = task;
    daemonSock = task->fd;

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);
    PSID_initSignalFD(&set, handleSIGCHLD);

    /* Catch some additional signals */
    PSC_setSigHandler(SIGUSR1, sighandler);
    PSC_setSigHandler(SIGTERM, sighandler);
    PSC_setSigHandler(SIGTTIN, sighandler);
    PSC_setSigHandler(SIGPIPE, sighandler);
    if (getenv("PSIDFORWARDER_DUMP_CORE_ON_SIGUSR2")) {
	PSC_setSigHandler(SIGUSR2, sighandler);
    }

    PSLog_init(daemonSock, task->rank, 3);

    PSIDcomm_init(false);
    PSIDcomm_registerSendMsgFunc(NULL);

    /* silently ignore some message types */
    PSID_registerMsg(PSP_CD_SENDSTOP, NULL);
    PSID_registerMsg(PSP_CD_SENDCONT, NULL);
    PSID_registerMsg(PSP_CD_WHODIED, NULL);
    PSID_registerMsg(PSP_DD_CHILDDEAD, NULL);
    /* register message types to be always handled */
    PSID_registerMsg(PSP_CD_RELEASERES, msgRELEASERES);
    PSID_registerMsg(PSP_CC_MSG, msgCC);
    PSID_registerMsg(PSP_CC_ERROR, msgCC_ERROR);

    PSIDMsgbuf_init();

    /* Enable handling of daemon messages */
    Selector_register(daemonSock, readFromDaemon, NULL);

    /* scale logger's timeout according to number of clients */
    if ((envStr = getenv("PSI_NP_INFO"))) {
	int np = atoi(envStr);
	if (np > 0) loggerTimeout += np / 200; /* add 5 millisec per client */
    }

    long val = loggerTimeout;
    timeoutStr = getenv("__PSI_LOGGER_TIMEOUT");
    if (timeoutStr) {
	char *end;
	val = strtol(timeoutStr, &end, 0);
	if (*timeoutStr && !*end && val>0) loggerTimeout = val;
    }

    if (eno) {
	sendSpawnFailed(task, eno);
	exit(0);
    }

    if (!sendChildBorn(task, clientFD)) waitForChildsDead();

    /* Make stdin non-blocking for us */
    PSCio_setFDblock(task->stdin_fd, false);

    if (!connectLogger(task->loggertid)) waitForChildsDead();

    /* Once the logger is connected, I/O forwarding is feasible */
    if (task->stdout_fd != -1) {
	Selector_register(task->stdout_fd, readFromChild, &stdoutType);
	openfds++;
    }
    if (task->stderr_fd != -1 && task->stderr_fd != task->stdout_fd) {
	Selector_register(task->stderr_fd, readFromChild, &stderrType);
	openfds++;
    }

    /* Send this message late. No connection to logger before */
    if (loggerTimeout != val)
	PSIDfwd_printMsgf(STDERR,
			  "%s: Illegal value '%s' for __PSI_LOGGER_TIMEOUT\n",
			  tag, timeoutStr);

    /* init plugin client functions */
    PSIDhook_call(PSIDHOOK_FRWRD_INIT, task);

    /* set the process title */
    snprintf(pTitle, sizeof(pTitle), "psidfw TID[%d:%d] R%d",
	     PSC_getID(task->loggertid), PSC_getPID(task->loggertid),
	     task->rank);
    PSC_setProcTitle(PSID_argc, PSID_argv, pTitle, 1);

    if (verbose) {
	PSIDfwd_printMsgf(STDERR, "%s: childTask=%s daemon=%d",
			  tag, PSC_printTID(task->tid), daemonSock);
	PSIDfwd_printMsgf(STDERR, " stdin=%d stdout=%d stderr=%d\n",
			  task->stdin_fd, task->stdout_fd,  task->stderr_fd);
    }

    /* Loop forever. We exit on SIGCHLD. */
    while (openfds || !gotSIGCHLD) {
	if (Swait(-1) < 0) {
	    if (errno && errno != EINTR) {
		PSIDfwd_printMsgf(STDERR, "%s: %s: error on Swait(): %s\n",
				  tag, __func__, strerror(errno));
	    }
	}
    }

    /* send usage message */
    finalizeForwarder();
}
