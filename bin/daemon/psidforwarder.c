/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2017 ParTec Cluster Competence Center GmbH, Munich
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
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/signalfd.h>

#include "psidutil.h"
#include "pscommon.h"
#include "pstask.h"
#include "selector.h"
#include "kvscommon.h"
#include "psdaemonprotocol.h"
#include "pslog.h"
#include "psidmsgbuf.h"
#include "psidhook.h"

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
 * @brief Send message to logger.
 *
 * Send a message of type @a type and size @a len within @a buf to the
 * logger. This is done via the PSLog facility.
 *
 * @param type The type of the message to send.
 *
 * @param buf Buffer holding the message to send.
 *
 * @param len The length of the message to send.
 *
 * @return On success, the number of bytes written is returned,
 * i.e. usually this is @a len. On error, -1 is returned, and errno is
 * set appropriately.
 *
 * @see PSLog_write()
 */
static int sendMsg(PSLog_msg_t type, char *buf, size_t len)
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
    return sendMsg(type, buf, strlen(buf));
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
	n = sendMsg(type, buf, n);
    }
    return n;
}

/**
 * @brief Deliver signal.
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
 * @param signal The signal to send.
 *
 * @return No return value.
 */
static void sendSignal(pid_t dest, int signal)
{
    pid_t pid = (dest > 0) ? -dest : dest;

    /* determine foreground process group for or default pid */
    if (childTask->interactive && dest > 0) {
	pid = tcgetpgrp(childTask->stderr_fd);
	if (pid == -1) {
	    PSID_warn((errno==EBADF) ? PSID_LOG_SIGNAL : -1, errno,
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
static void handleSignalMsg(PSLog_Msg_t *msg)
{
    char *ptr = msg->buf;
    pid_t pid;
    int signal;

    /* Get destination */
    pid = *(int32_t *)ptr;
    ptr += sizeof(int32_t);

    /* Get signal to send */
    signal = *(int32_t *)ptr;
    //ptr += sizeof(int32_t);

    sendSignal(pid, signal);
}

/**
 * @brief Receive message from logger.
 *
 * Receive a message from the logger and store it to @a msg. If @a
 * timeout is given, it is tried to receive a message for the period
 * defined therein. Otherwise this function will block until a message
 * is available.
 *
 * If the receive times out, i.e. the period defined in @a timeout
 * elapsed without receiving a complete message, a error is
 * returned.
 *
 * If a message was received during the period to wait, upon return @a
 * timeout will get updated and hold the remaining time of the
 * original timeout.
 *
 * This is done via the PSLog facility.
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
static int recvMsg(PSLog_Msg_t *msg, struct timeval *timeout)
{
    int ret, res;

    if (daemonSock < 0) {
	PSID_log(-1, "%s: not connected\n", __func__);
	errno = EPIPE;

	return -1;
    }

again:
    ret = PSLog_read(msg, timeout);

    if (ret < 0) {
	switch (errno) {
	case EOPNOTSUPP:
	    if (timeout) goto again;
	    break;
	default:
	    PSID_warn(-1, errno, "%s: PSLog_read()", __func__);
	    closeDaemonSock();
	}
    } else if (!ret) {
	if (!timeout) {
	    PSID_log(-1, "%s: logger closed connection", __func__);
	    closeDaemonSock();
	}
    } else {
	switch (msg->header.type) {
	case PSP_CC_ERROR:
	    if (msg->header.sender == loggerTID) {
		PSID_log(-1, "%s: logger %s disappeared\n",
			 __func__, PSC_printTID(loggerTID));
		loggerTID = -1;
		errno = EPIPE;
		ret = -1;
	    } else {
		/* the pspmi plugin may spawn children and therefore
		 * we need to handle PSP_CC_ERROR for them  */
		if ((PSIDhook_call(PSIDHOOK_FRWRD_CC_ERROR, msg)) == 1) {
		    ret = 1;
		    break;
		}

		PSID_log(-1, "%s: CC_ERROR from %s\n",
			 __func__, PSC_printTID(msg->header.sender));
		ret = 0;
	    }
	    break;
	case PSP_CC_MSG:
	    switch (msg->type) {
	    case SIGNAL:
		handleSignalMsg(msg);
		if (timeout) goto again;
		errno = EINTR;
		ret = -1;
		break;
	    default:
		break;
	    }
	    break;
	case PSP_CD_RELEASERES:
	    /* release the client */
	    res = 1;
	    PSIDhook_call(PSIDHOOK_FRWRD_RESCLIENT, &res);
	    break;
	case PSP_DD_CHILDACK:
	case PSP_DD_CHILDDEAD:
	    break;
	case PSP_CD_SPAWNFAILED:
	case PSP_CD_SPAWNSUCCESS:
	case PSP_CD_SENDSTOP:
	case PSP_CD_SENDCONT:
	case PSP_CD_WHODIED:
	    /* ignore */
	    break;
	default:
	    PSID_log(-1, "%s: Unknown message type %s\n",
		     __func__, PSDaemonP_printMsg(msg->type));
	    ret = 0;
	}
    }

    return ret;
}

int sendDaemonMsg(DDMsg_t *msg)
{
    char *buf = (void *)msg;
    size_t c = msg->len;
    int n;

    if (daemonSock < 0) {
	errno = EBADF;
	return -1;
    }

    do {
	n = send(daemonSock, buf, c, 0);
	if (n < 0){
	    if (errno == EAGAIN){
		continue;
	    } else {
		break;             /* error, return < 0 */
	    }
	}
	c -= n;
	buf += n;
    } while (c > 0);

    if (n < 0) {
	PSID_warn(-1, errno ,"%s: send()", __func__);
	closeDaemonSock();

	return n;
    } else if (!n) {
	PSID_log(-1, "%s: Lost connection to daemon\n", __func__);
	closeDaemonSock();

	return n;
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
 * @a timeout seconds for #INITIALIZE answer and set #loggerTID and
 * #verbose accordingly.
 *
 * @param tid The logger's ParaStation task ID.
 *
 * @param timeout Number of seconds to wait for #INITIALIZE answer.
 *
 * @return On success, 0 is returned. Simultaneously #loggerTID is
 * set. On error, -1 is returned, and errno is set appropriately.
 */
static int connectLogger(PStask_ID_t tid)
{
    PSLog_Msg_t msg;
    struct timeval timeout = {loggerTimeout, 0};
    int ret;

    loggerTID = tid; /* Only set for the first sendMsg()/recvMsg() pair */

    sendMsg(INITIALIZE, (char *) &childTask->group, sizeof(childTask->group));

again:
    ret = recvMsg(&msg, &timeout);

    if (ret <= 0) {
	PSID_log(-1, "%s(%s): Connection refused\n",
		 __func__, PSC_printTID(tid));
	loggerTID = -1;
	errno = ECONNREFUSED;
	return -1;
    }

    if (msg.header.len < PSLog_headerSize + (int) sizeof(int)) {
	PSID_log(-1, "%s(%s): rank %i : Message too short\n", __func__,
		 PSC_printTID(tid), childTask->rank);
	loggerTID = -1;
	errno = ECONNREFUSED;
	return -1;
    } else if (msg.header.type != PSP_CC_MSG) {
	PSID_log(-1 ,"%s(%s): Protocol messed up, got %s message\n", __func__,
		 PSC_printTID(tid), PSDaemonP_printMsg(msg.header.type));
	goto again;
    } else if (msg.type != INITIALIZE) {
	PSID_log(-1 ,"%s(%s): Protocol messed up\n",
		 __func__, PSC_printTID(tid));
	loggerTID = -1;
	errno = ECONNREFUSED;
	return -1;
    } else if (msg.header.sender != tid) {
	PSID_log(-1, "%s(%s): Got INITIALIZE", __func__, PSC_printTID(tid));
	PSID_log(-1, " from %s\n", PSC_printTID(msg.header.sender));
	loggerTID = -1;
	errno = ECONNREFUSED;
	return -1;
    } else {
	char *ptr = msg.buf;

	verbose = !!(*(int *)ptr);
	ptr += sizeof(int);
	PSID_log(PSID_LOG_SPAWN, "%s(%s): Connected\n", __func__,
		 PSC_printTID(tid));

	if (msg.header.len >=
	    PSLog_headerSize + (int)(2*sizeof(int)+2*sizeof(PStask_ID_t))) {

	    /* set info about our pred/suc ranks */
	    PSIDhook_call(PSIDHOOK_FRWRD_CINFO, ptr);
	}
    }

    return 0;
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
 * @return No return value.
 */
static void releaseLogger(int status)
{
    PSLog_Msg_t msg;
    struct timeval timeout;
    int ret;

    if (loggerTID < 0) return;

 send_again:
    sendMsg(FINALIZE, (char *)&status, sizeof(status));

    timeout = (struct timeval) {10, 0};
 again:
    ret = recvMsg(&msg, &timeout);

    if (ret < 0) {
	switch (errno) {
	case EINTR:
	    goto again;
	    break;
	case EPIPE:
	    PSID_log(-1, "%s: logger %s already disappeared\n", __func__,
		     PSC_printTID(childTask->loggertid));
	    break;
	default:
	    PSID_warn(-1, errno, "%s: recvMsg()", __func__);
	}
    } else if (!ret) {
	PSID_log(-1, "%s: receive timed out. Send again\n", __func__);
	goto send_again;
    } else if (msg.header.type == PSP_CC_ERROR) {
	/* Ignore late spawned child gone messages */
	goto again;
    } else if (msg.header.type != PSP_CC_MSG) {
	PSID_log(-1 ,"%s: Protocol messed up, got %s message\n", __func__,
		 PSDaemonP_printMsg(msg.header.type));
	goto again;
    } else if (msg.type != EXIT) {
	if (msg.type == INITIALIZE || msg.type == STDIN || msg.type == KVS
	    || msg.type == SERV_EXT) {
	     /* Ignore late STDIN / KVS / SERVICE EXIT messages */
	    goto again;
	}
	PSID_log(-1, "%s: Protocol messed up (type %d) from %s\n",
		 __func__, msg.type, PSC_printTID(msg.header.sender));
    } else {
	PSID_log(PSID_LOG_SPAWN, "%s(%d): Released %s\n", __func__, status,
		 PSC_printTID(loggerTID));
    }

    loggerTID = -1;
}

/**
 * @brief Write to client's stdin
 *
 * Write the message @a msg to the file-descriptor connected to the
 * clients stdin starting at an offset of @a offset bytes. It is
 * expected that the previous parts of the message were sent in
 * earlier calls to this function.
 *
 * @param msg The message to transmit.
 *
 * @param offset Number of bytes sent in earlier calls.
 *
 * @return On success, the total number of bytes written is returned,
 * i.e. usually this is the length of @a msg. If the file-descriptor
 * blocks, this might also be smaller. In this case the total number
 * of bytes sent in this and all previous calls is returned. If an
 * error occurs, -1 or 0 is returned and errno is set appropriately.
 */
static int doWrite(PSLog_Msg_t *msg, int offset)
{
    int n, i;
    int count = msg->header.len - PSLog_headerSize;
    int stdinSock = childTask->stdin_fd;

    if (!count) {
	/* close clients stdin */
	shutdown(stdinSock, SHUT_WR);
	if (Selector_isRegistered(stdinSock)) Selector_vacateWrite(stdinSock);

	/* Interactive jobs might use a single file-descriptor */
	if (stdinSock != childTask->stdout_fd &&
	    stdinSock != childTask->stderr_fd) {
	    close(stdinSock);
	}

	childTask->stdin_fd = -1;
	return 0;
    }

    for (n=offset, i=1; (n<count) && (i>0);) {
	char *errstr;
	errno = 0;
	i = write(stdinSock, &msg->buf[n], count-n);
	if (i<0) {
	    int eno = errno;
	    switch (eno) {
	    case EINTR:
		i=1;
		continue;
	    case EAGAIN:
		return n;
	    case EPIPE:
		sendMsg(STOP, NULL, 0);
		if (Selector_isRegistered(stdinSock))
		    Selector_vacateWrite(stdinSock);
		childTask->stdin_fd = -1;
		break;
	    default:
		errstr = strerror(eno);
		PSIDfwd_printMsgf(STDERR,
				  "%s: %s: got error %d on stdinSock: %s\n",
				  tag, __func__, eno,
				  errstr ? errstr : "UNKNOWN");
	    }
	    errno = eno;
	    return i;
	} else
	    n+=i;
    }
    return n;
}

static int storeMsg(PSLog_Msg_t *msg, int offset)
{
    msgbuf_t *msgbuf =  PSIDMsgbuf_get(msg->header.len);

    if (!msgbuf) {
	errno = ENOMEM;
	return -1;
    }

    memcpy(msgbuf->msg, msg, msg->header.len);
    msgbuf->offset = offset;

    list_add_tail(&msgbuf->next, &oldMsgs);

    return 0;
}

static int flushMsgs(int fd /* dummy */, void *info /* dummy */)
{
    list_t *m, *tmp;
    int stdinSock = childTask->stdin_fd;

    list_for_each_safe(m, tmp, &oldMsgs) {
	msgbuf_t *msgbuf = list_entry(m, msgbuf_t, next);
	PSLog_Msg_t *msg = (PSLog_Msg_t *)msgbuf->msg;
	int len = msg->header.len - PSLog_headerSize;
	int written = doWrite(msg, msgbuf->offset);

	if (written<0) return written;
	if (written != len) {
	    msgbuf->offset = written;
	    break;
	}
	list_del(&msgbuf->next);
	PSIDMsgbuf_put(msgbuf);
    }

    if (!list_empty(&oldMsgs)) {
	errno = EWOULDBLOCK;
	return -1;
    } else {
	if (stdinSock != -1) Selector_vacateWrite(stdinSock);
	sendMsg(CONT, NULL, 0);
    }

    return 0;
}

static int writeMsg(PSLog_Msg_t *msg)
{
    int len = msg->header.len - PSLog_headerSize, written = 0;
    int stdinSock = childTask->stdin_fd;

    if (!list_empty(&oldMsgs)) flushMsgs(0, NULL);
    if (list_empty(&oldMsgs)) written = doWrite(msg, 0);

    if (written<0) return written;
    if ((written != len) || (!list_empty(&oldMsgs))) {
	if (!storeMsg(msg, written)) errno = EWOULDBLOCK;
	if (stdinSock != -1) Selector_awaitWrite(stdinSock, flushMsgs, NULL);
	if (len) sendMsg(STOP, NULL, 0);
	return -1;
    }

    return written;
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

    (void) ioctl(childTask->stdin_fd, TIOCSWINSZ, &w);

    /* @todo Maybe we shall send a SIGWINCH to the client ? */

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
    PSLog_Msg_t msg;
    int ret, res;

    ret = recvMsg(&msg, NULL);

    if (ret <= 0) {
	/* The connection to the daemon died. Kill the client the hard way. */
	if (!ret) sendSignal(PSC_getPID(childTask->tid), SIGKILL);

	return ret;
    }

    switch (msg.header.type) {
    case PSP_CD_SPAWNFAILED:
    case PSP_CD_SPAWNSUCCESS:
	PSIDhook_call(PSIDHOOK_FRWRD_SPAWNRES, &msg);
	break;
    case PSP_CD_SENDSTOP:
    case PSP_CD_SENDCONT:
    case PSP_CD_WHODIED:
    case PSP_CC_ERROR:
	/* ignore */
	break;
    case PSP_CD_RELEASERES:
	break;
    case PSP_CC_MSG:
	switch (msg.type) {
	case STDIN:
	    if (verbose) {
		PSIDfwd_printMsgf(STDERR, "%s: %s: %d bytes on STDIN\n", tag,
				  __func__, msg.header.len - PSLog_headerSize);
	    }
	    if (childTask->stdin_fd < 0) {
		static bool first = true;
		if (first) {
		    PSIDfwd_printMsgf(STDERR, "%s: %s: STDIN already closed\n",
				      tag, __func__);
		    first = false;
		}
	    } else {
		writeMsg(&msg);
	    }
	    break;
	case EXIT:
	    /* Logger is going to die */
	    /* Release the client */
	    res = 0;
	    PSIDhook_call(PSIDHOOK_FRWRD_RESCLIENT, &res);
	    /* Release the daemon */
	    closeDaemonSock();
	    /* Cleanup child */
	    sendSignal(PSC_getPID(childTask->tid), SIGKILL);
	    exit(0);
	    break;
	case KVS:
	case SERV_TID:
	case SERV_EXT:
	    PSIDhook_call(PSIDHOOK_FRWRD_KVS, &msg);
	    break;
	case WINCH:
	    /* Logger detected change in window-size */
	    handleWINCH(&msg);
	    break;
	case INITIALIZE:
	    /* ignore late INITIALIZE answer */
	    break;
	default:
	    PSIDfwd_printMsgf(STDERR,"%s: %s: Unknown type %d\n",
			      tag, __func__, msg.type);
	}
	break;
    default:
	PSIDfwd_printMsgf(STDERR, "%s: %s: Unexpected msg type %s\n",
			  tag, __func__, PSP_printMsg(msg.header.type));
    }

    return 0;
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
	sendMsg(type, buf, total);
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
static void sendAcctData(struct rusage rusage, int status)
{
    DDTypedBufferMsg_t msg;
    char *ptr = msg.buf;
    struct timeval now, walltime;
    long pagesize;

    msg.header.type = PSP_CD_ACCOUNT;
    msg.header.dest = PSC_getTID(-1, 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg.header);

    msg.type = PSP_ACCOUNT_END;
    msg.header.len += sizeof(msg.type);

    /* logger's TID, this identifies a task uniquely */
    *(PStask_ID_t *)ptr = childTask->loggertid;
    ptr += sizeof(PStask_ID_t);
    msg.header.len += sizeof(PStask_ID_t);

    /* current rank */
    *(int32_t *)ptr = childTask->rank;
    ptr += sizeof(int32_t);
    msg.header.len += sizeof(int32_t);

    /* child's uid */
    *(uid_t *)ptr = childTask->uid;
    ptr += sizeof(uid_t);
    msg.header.len += sizeof(uid_t);

    /* child's gid */
    *(gid_t *)ptr = childTask->gid;
    ptr += sizeof(gid_t);
    msg.header.len += sizeof(gid_t);

    /* child's pid */
    *(pid_t *)ptr = PSC_getPID(childTask->tid);
    ptr += sizeof(pid_t);
    msg.header.len += sizeof(pid_t);

    /* actual rusage structure */
    memcpy(ptr, &rusage, sizeof(rusage));
    ptr += sizeof(rusage);
    msg.header.len += sizeof(rusage);

    /* pagesize */
    if ((pagesize = sysconf(_SC_PAGESIZE)) < 1) {
	pagesize = 0;
    }
    *(uint64_t *)ptr = pagesize;
    ptr += sizeof(uint64_t);
    msg.header.len += sizeof(uint64_t);

    /* walltime used by child */
    gettimeofday(&now, NULL);
    timersub(&now, &childTask->started, &walltime);
    memcpy(ptr, &walltime, sizeof(walltime));
    ptr += sizeof(walltime);
    msg.header.len += sizeof(walltime);

    /* child's return status */
    *(int32_t *)ptr = status;
    ptr += sizeof(int32_t);
    msg.header.len += sizeof(int32_t);

    /* extended msg flag, will be overwritten
     * by accounting plugin */
    *(int32_t *)ptr = 0;
    //ptr += sizeof(int32_t);
    msg.header.len += sizeof(int32_t);

    sendDaemonMsg((DDMsg_t *)&msg);
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
	PSIDfwd_printMsgf(STDERR, "%s: Got SIGUSR2\n", tag);
	raise(SIGSEGV);
	break;
    case SIGTERM:
	sendSignal(PSC_getPID(childTask->tid), SIGTERM);
	break;
    case SIGTTIN:
	PSIDfwd_printMsgf(STDERR, "%s: got SIGTTIN\n", tag);
	break;
    case SIGPIPE:
	PSIDfwd_printMsgf(STDERR, "%s: Got SIGPIPE\n", tag);
	break;
    }

    signal(sig, sighandler);
}

static void finalizeForwarder(void)
{
    int res, clientStat;

    if (openfds) {
	PSIDfwd_printMsgf(STDERR,
			  "%s: %s: %d open file-descriptors remaining\n",
			  tag, __func__, openfds);
    }

    if (!gotSIGCHLD) {
	PSIDfwd_printMsgf(STDERR, "%s: %s: SIGCHLD not yet received\n",
			  tag, __func__);
    }

    sendMsg(USAGE, (char *) &childRUsage, sizeof(childRUsage));

    clientStat = PSIDhook_call(PSIDHOOK_FRWRD_CLIENT_STAT, childTask);

    /* Release, if no error occurred and not already done */
    if ((!clientStat || clientStat == PSIDHOOK_NOFUNC)
	&& (WIFEXITED(childStatus) && !WEXITSTATUS(childStatus))
	&& !WIFSIGNALED(childStatus)) {
	/* release the child */
	DDSignalMsg_t msg;
	msg.header.type = PSP_CD_RELEASE;
	msg.header.sender = PSC_getMyTID();
	msg.header.dest = childTask->tid;
	msg.header.len = sizeof(msg);
	msg.signal = -1;
	msg.answer = 0;  /* Don't expect answer in this late stage */
	sendDaemonMsg((DDMsg_t *)&msg);
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
    {
	DDErrorMsg_t msg;
	msg.header.type = PSP_DD_CHILDDEAD;
	msg.header.dest = PSC_getTID(-1, 0);
	msg.header.sender = PSC_getMyTID();
	msg.error = childStatus;
	msg.request = PSC_getTID(-1, childPID);
	msg.header.len = sizeof(msg);
	sendDaemonMsg((DDMsg_t *)&msg);
    }

    /* Send SIGKILL to process group in order to stop fork()ed children */
    sendSignal(-PSC_getPID(childTask->tid), SIGKILL);

    /* Release the client */
    res = 0;
    PSIDhook_call(PSIDHOOK_FRWRD_RESCLIENT, &res);

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
 * @return No return value.
 */
static void sendSpawnFailed(PStask_t *task, int eno)
{
    DDBufferMsg_t answer;
    DDErrorMsg_t *errMsg = (DDErrorMsg_t *)&answer;
    char *ptr;
    size_t bufUsed, bufAvail, read;

    answer.header.type = PSP_CD_SPAWNFAILED;
    answer.header.dest = task->ptid;
    answer.header.sender = PSC_getMyTID();
    answer.header.len = sizeof(*errMsg);

    errMsg->request = task->rank;
    errMsg->error = eno;

    bufUsed = answer.header.len - sizeof(answer.header);
    ptr = answer.buf + bufUsed;
    bufAvail = sizeof(answer.buf) - bufUsed;

    do {
	collectRead(childTask->stderr_fd, ptr, bufAvail, &read);
	bufAvail -= read;
	answer.header.len += read;
	ptr += read;
    } while (read && bufAvail);

    if (!bufAvail) {
	answer.buf[sizeof(answer.buf) - 5] = '.';
	answer.buf[sizeof(answer.buf) - 4] = '.';
	answer.buf[sizeof(answer.buf) - 3] = '.';
	answer.buf[sizeof(answer.buf) - 2] = '\n';
	answer.buf[sizeof(answer.buf) - 1] = '\0';
    }
    sendDaemonMsg((DDMsg_t *)&answer);

    exit(0);
}

/**
 * @brief Send PSP_DD_CHILDBORN message
 *
 * Send a PSP_DD_CHILDBORN message. This signals the local daemon that
 * the actual spawn of the child-process was successful and the
 * forwarder will switch to normal operations.
 *
 * @param task Structure describing the child-process that was
 * successfully spawned.
 *
 * @return On success, 0 is returned. Or -1, if an error
 * occurred. Then errno is set appropriately.
 */
static int sendChildBorn(PStask_t *task)
{
    DDErrorMsg_t msg;
    PSLog_Msg_t answer;
    struct timeval timeout;
    int ret, eno;

    msg.header.type = PSP_DD_CHILDBORN;
    msg.header.dest = task->ptid;
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg);

    msg.request = task->tid;
    msg.error = 0;

send_again:
    sendDaemonMsg((DDMsg_t *)&msg);

    timeout = (struct timeval) {10, 0};
again:
    ret = recvMsg(&answer, &timeout);

    if (ret < 0) {
	switch (errno) {
	case EINTR:
	    goto again;
	    break;
	default:
	    eno = errno;
	    PSID_warn(-1, eno, "%s: recvMsg()", __func__);
	    errno = eno;
	    return -1;
	}
    } else if (!ret) {
	PSID_log(-1, "%s: receive timed out. Send again\n", __func__);
	goto send_again;
    } else if (answer.header.type == PSP_DD_CHILDACK) {
	if (childTask->fd > -1) {
	    close(childTask->fd);
	    childTask->fd = -1;
	} else {
	    PSID_log(-1, "%s: cannot start child", __func__);
	    errno = EPIPE;
	    return -1;
	}
    } else {
	ssize_t ret = 0;
	PSID_log(-1, "%s: wrong answer type %s, don't execve() child\n",
		 __func__, PSDaemonP_printMsg(answer.header.type));
	if (childTask->fd > -1) {
	    ret = write(childTask->fd, "x", 1);
	    close(childTask->fd);
	}
	if (ret != 1) {
	    PSID_log(-1, "%s: cannot stop child, try to kill", __func__);
	    sendSignal(PSC_getPID(childTask->tid), SIGKILL);
	}
	errno = ECHILD;
	return -1;
    }

    return 0;
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
    }

    return 0;
}

static void initSignalFD(int sig, Selector_CB_t handler)
{
    sigset_t set;
    int sigFD;

    sigemptyset(&set);
    sigaddset(&set, sig);

    if (sigprocmask(SIG_BLOCK, &set, NULL) < 0) {
	PSID_exit(errno, "%s(%s): sigprocmask()", __func__, strsignal(sig));
    }

    sigFD = signalfd(-1, &set, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sigFD < 0) {
	PSID_exit(errno, "%s(%s): signalfd()", __func__, strsignal(sig));
    }

    if (Selector_register(sigFD, handler, NULL) < 0) {
	PSID_exit(errno, "%s(%s): Selector_register()", __func__,
		  strsignal(sig));
    }
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
void PSID_forwarder(PStask_t *task, int daemonfd, int eno)
{
    static PSLog_msg_t stdoutType = STDOUT, stderrType = STDERR;
    char pTitle[50];
    char *envStr, *timeoutStr;
    long flags, val;

    /* Ensure that PSLog can handle daemonfd via select() */
    if (daemonfd >= FD_SETSIZE) {
	int newFD = dup(daemonfd);
	if (newFD >= FD_SETSIZE) {
	    PSID_exit(ECHRNG, "%s: daemonfd %d", __func__, newFD);
	}
	close(daemonfd);
	daemonfd = newFD;
    }

    /* Allow to create coredumps */
    prctl(PR_SET_DUMPABLE, 1, /* rest ignored */ 0, 0, 0);

    childTask = task;
    daemonSock = daemonfd;

    initSignalFD(SIGCHLD, handleSIGCHLD);

    /* Catch some additional signals */
    signal(SIGUSR1, sighandler);
    signal(SIGTERM, sighandler);
    signal(SIGTTIN, sighandler);
    signal(SIGPIPE, sighandler);
    if (getenv("PSIDFORWARDER_DUMP_CORE_ON_SIGUSR2")) {
	signal(SIGUSR2, sighandler);
    }

    PSLog_init(daemonSock, childTask->rank, 3);

    /* Enable handling of daemon messages */
    Selector_register(daemonSock, readFromDaemon, NULL);

    /* scale logger's timeout according to number of clients */
    if ((envStr = getenv("PSI_NP_INFO"))) {
	int np = atoi(envStr);
	if (np > 0) loggerTimeout += np / 200; /* add 5 millisec per client */
    }

    val = loggerTimeout;
    timeoutStr = getenv("__PSI_LOGGER_TIMEOUT");
    if (timeoutStr) {
	char *end;
	val = strtol(timeoutStr, &end, 0);
	if (*timeoutStr && !*end && val>0) loggerTimeout = val;
    }

    if (eno) {
	sendSpawnFailed(childTask, eno);
	exit(1);
    } else {
	if (sendChildBorn(childTask)) waitForChildsDead();
    }

    /* Make stdin non-blocking for us */
    flags = fcntl(childTask->stdin_fd, F_GETFL);
    flags |= O_NONBLOCK;
    fcntl(childTask->stdin_fd, F_SETFL, flags);

    if (connectLogger(childTask->loggertid)) waitForChildsDead();

    /* Once the logger is connected, I/O forwarding is feasible */
    if (task->stdout_fd != -1) {
	Selector_register(task->stdout_fd, readFromChild, &stdoutType);
	openfds++;
    }
    if (childTask->stderr_fd != -1 && childTask->stderr_fd != task->stdout_fd) {
	Selector_register(childTask->stderr_fd, readFromChild, &stderrType);
	openfds++;
    }

    /* Send this message late. No connection to logger before */
    if (loggerTimeout != val)
	PSIDfwd_printMsgf(STDERR,
			  "%s: Illegal value '%s' for __PSI_LOGGER_TIMEOUT\n",
			  tag, timeoutStr);

    /* init plugin client functions */
    PSIDhook_call(PSIDHOOK_FRWRD_INIT, childTask);
    PSIDhook_call(PSIDHOOK_FRWRD_DSOCK, &daemonSock);

    /* set the process title */
    snprintf(pTitle, sizeof(pTitle), "psidfw TID[%d:%d] R%d",
		PSC_getID(childTask->loggertid),
		PSC_getPID(childTask->loggertid), childTask->rank);
    PSC_setProcTitle(PSID_argc, (char **)PSID_argv, pTitle, 1);

    if (verbose) {
	PSIDfwd_printMsgf(STDERR, "%s: childTask=%s daemon=%d",
			  tag, PSC_printTID(task->tid), daemonfd);
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
