/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2012 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/utsname.h>

#include "psidutil.h"
#include "pscommon.h"
#include "pstask.h"
#include "selector.h"
#include "kvscommon.h"
#include "psdaemonprotocol.h"
#include "pslog.h"
#include "psidmsgbuf.h"
#include "psidnodes.h"
#include "psidpmiprotocol.h"

#include "psidforwarder.h"

static char tag[] = "PSID_forwarder";
/**
 * Verbosity of Forwarder (1=Yes, 0=No)
 *
 * Set by connectLogger() on behalf of info from logger.
 */
int verbose = 0;

/** The ParaStation task ID of the logger */
PStask_ID_t loggerTID = -1;

/** Description of the local child-task */
PStask_t *childTask = NULL;

/** The socket connecting to the local ParaStation daemon */
int daemonSock = -1;

/** The socket listening for connection from the pmi client (tcp/ip only) */
int PMISock = -1;

/** The type of the connection between forwarder and client */
PMItype_t pmiType = -1;

/** The socket connected to the pmi client */
int PMIClientSock = -1;

/** The socket connected to the stdin port of the client */
int stdinSock = -1;

/** The socket connected to the stdout port of the client */
int stdoutSock = -1;

/** The socket connected to the stderr port of the client */
int stderrSock = -1;

/** Set of fds the forwarder writes to (this is stdinSock) */
fd_set writefds;

/** Number of open file descriptors to wait for */
int openfds = 0;

/** Flag for real SIGCHLD received */
int gotSIGCHLD = 0;

static enum {
    IDLE,
    CONNECTED,
    CLOSED,
} pmiStatus = IDLE;

/** List of messages waiting to be sent */
LIST_HEAD(oldMsgs);

/**
 * Timeout for connecting to logger. This will be set according to the
 * number of children the logger has to handle within @ref
 * PSID_forwarder(). Might be overruled via __PSI_LOGGER_TIMEOUT
 * environment
 */
int loggerTimeout = 60;

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

    close(daemonSock);
    Selector_remove(daemonSock);
    daemonSock = -1;
}

/**
 * @brief Close socket which is connected to the pmi client.
 *
 * @return No return value.
 */
static void closePMIClientSocket(void)
{
    if (PMIClientSock < 0) return;

    close(PMIClientSock);
    Selector_remove(PMIClientSock);
    PMIClientSock = -1;
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
    static int first = 1;

    if (loggerTID < 0) {
	if (first) {
	    PSID_log(-1, "%s(%d): not connected\n", __func__, type);
	    PSID_log(-1, "%s(%d): %s\n", __func__, type, buf);
	    first = 0;
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
 * interactive, the signal is send to the forground process group of
 * the controlling tty. Otherwise it's first tried to delivered to the
 * process-group with ID @a dest. If this failes, the process with ID
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
	pid = tcgetpgrp(stderrSock);
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
    ptr += sizeof(int32_t);

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
 * This is done via the PSLog facility.
 *
 * @param msg Buffer to store the message to.
 *
 * @param timout The timeout after which the function returns. If this
 * is NULL, this function will block indefinitely.
 *
 * @return On success, the number of bytes read are returned. On
 * error, -1 is returned, and errno is set appropriately.
 *
 * @see PSLog_read()
 */
static int recvMsg(PSLog_Msg_t *msg, struct timeval *timeout)
{
    int ret, blocked;

    if (daemonSock < 0) {
	PSID_log(-1, "%s: not connected\n", __func__);
	errno = EPIPE;

	return -1;
    }

again:
    ret = PSLog_read(msg, timeout);

    blocked = PSID_blockSig(1, SIGCHLD);

    if (ret < 0) {
	switch (errno) {
	case EOPNOTSUPP:
	    if (timeout) {
		PSID_blockSig(blocked, SIGCHLD);
		goto again;
	    }
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
		PSID_log(-1, "%s: CC_ERROR from %s\n",
			 __func__, PSC_printTID(msg->header.sender));
		ret = 0;
	    }
	    break;
	case PSP_CC_MSG:
	    switch (msg->type) {
	    case SIGNAL:
		handleSignalMsg(msg);
		if (timeout) {
		    PSID_blockSig(blocked, SIGCHLD);
		    goto again;
		}
		errno = EINTR;
		ret = -1;
		break;
	    default:
		break;
	    }
	    break;
	case PSP_CD_RELEASERES:
	    /* finaly release the pmi client */
	    if (pmiType != PMI_DISABLED) {
		/* release the pmi client */
		pmi_finalize();

		/*close connection */
		closePMIClientSocket();
	    }
	    break;
	case PSP_DD_CHILDACK:
	case PSP_DD_CHILDDEAD:
	    break;
	default:
	    PSID_log(-1, "%s: Unknown message type %s\n",
		     __func__, PSDaemonP_printMsg(msg->type));
	    ret = 0;
	}
    }

    PSID_blockSig(blocked, SIGCHLD);
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
	PSIDfwd_printMsgf(STDERR, "%s: type %s (len=%d) to %s\n",
			  __func__, PSDaemonP_printMsg(msg->type),
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

    sendMsg(INITIALIZE, NULL, 0);

again:
    PSID_blockSig(0, SIGCHLD);
    ret = recvMsg(&msg, &timeout);
    PSID_blockSig(1, SIGCHLD);

    if (ret <= 0) {
	PSID_log(-1, "%s(%s): Connection refused\n",
		 __func__, PSC_printTID(tid));
	loggerTID = -1;
	errno = ECONNREFUSED;
	return -1;
    }

    if (msg.header.len < PSLog_headerSize + (int) sizeof(int)) {
	PSID_log(-1, "%s(%s): Message too short\n", __func__,
		 PSC_printTID(tid));
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

	verbose = *(int *)ptr;
	ptr += sizeof(int);
	PSID_log(PSID_LOG_SPAWN, "%s(%s): Connected\n", __func__,
		 PSC_printTID(tid));

	if (msg.header.len >=
	    PSLog_headerSize + (int)(sizeof(int)+2*sizeof(PStask_ID_t))) {
	    PStask_ID_t pred, succ;
	    pred = *(PStask_ID_t *) ptr;
	    ptr += sizeof(PStask_ID_t);
	    succ = *(PStask_ID_t *) ptr;
	    ptr += sizeof(PStask_ID_t);

	    pmi_set_pred(pred);
	    pmi_set_succ(succ);
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
    struct timeval timeout = {loggerTimeout, 0};
    int ret;

    if (loggerTID < 0) return;

 send_again:
    sendMsg(FINALIZE, (char *)&status, sizeof(status));

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
    } else if (msg.header.type != PSP_CC_MSG) {
	PSID_log(-1 ,"%s: Protocol messed up, got %s message\n", __func__,
		 PSDaemonP_printMsg(msg.header.type));
	goto again;
    } else if (msg.type != EXIT) {
	if (msg.type == INITIALIZE || msg.type == STDIN || msg.type == KVS) {
	     /* Ignore late STDIN / KVS messages */
	    goto again;
	}
	PSID_log(-1, "%s: Protocol messed up (type %d) from %s\n",
		 __func__, msg.type, PSC_printTID(msg.header.sender));
    }

    loggerTID = -1;
}

/**
 * @brief Write to @ref stdinSock descriptor
 *
 * Write the message @a msg to the @ref stdinSock file-descriptor,
 * starting at by @a offset of the message. It is expected that the
 * previos parts of the message were sent in earlier calls to this
 * function.
 *
 * @param msg The message to transmit.
 *
 * @param offset Number of bytes sent in earlier calls.
 *
 * @return On success, the total number of bytes written is returned,
 * i.e. usually this is the length of @a msg. If the @ref stdinSock
 * file-descriptor blocks, this might also be smaller. In this case
 * the total number of bytes sent in this and all previous calls is
 * returned. If an error occurs, -1 or 0 is returned and errno is set
 * appropriately.
 */
static int do_write(PSLog_Msg_t *msg, int offset)
{
    int n, i;
    int count = msg->header.len - PSLog_headerSize;

    if (!count) {
	/* close clients stdin */
	shutdown(stdinSock, SHUT_WR);
	FD_CLR(stdinSock, &writefds);
	stdinSock = -1;
	return 0;
    }

    for (n=offset, i=1; (n<count) && (i>0);) {
	char *errstr;
	i = write(stdinSock, &msg->buf[n], count-n);
	if (i<=0) {
	    switch (errno) {
	    case EINTR:
		break;
	    case EAGAIN:
		return n;
		break;
	    default:
		errstr = strerror(errno);
		PSIDfwd_printMsgf(STDERR, "%s: got error %d on stdinSock: %s",
				  __func__, errno, errstr ? errstr : "UNKNOWN");
		return i;
	    }
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

static int flushMsgs(void)
{
    list_t *m, *tmp;

    list_for_each_safe(m, tmp, &oldMsgs) {
	msgbuf_t *msgbuf = list_entry(m, msgbuf_t, next);
	PSLog_Msg_t *msg = (PSLog_Msg_t *)msgbuf->msg;
	int len = msg->header.len - PSLog_headerSize;
	int written = do_write(msg, msgbuf->offset);

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
	if (stdinSock != -1) FD_CLR(stdinSock, &writefds);
	Selector_startOver();
	sendMsg(CONT, NULL, 0);
    }

    return 0;
}

static int writeMsg(PSLog_Msg_t *msg)
{
    int len = msg->header.len - PSLog_headerSize, written = 0;

    if (!list_empty(&oldMsgs)) flushMsgs();
    if (list_empty(&oldMsgs)) written = do_write(msg, 0);

    if (written<0) return written;
    if ((written != len) || (!list_empty(&oldMsgs))) {
	if (!storeMsg(msg, written)) errno = EWOULDBLOCK;
	if (stdinSock != -1) FD_SET(stdinSock, &writefds);
	Selector_startOver();
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

    if (stdinSock<0 || !msg) return;

    count = msg->header.len - PSLog_headerSize;
    buf = (int *)msg->buf;

    if (count != 4 * sizeof(*buf)) {
	PSIDfwd_printMsgf(STDERR, "%s: Corrupted WINCH message\n", __func__);
	return;
    }

    w.ws_col = buf[len++];
    w.ws_row = buf[len++];
    w.ws_xpixel = buf[len++];
    w.ws_ypixel = buf[len++];

    (void) ioctl(stdinSock, TIOCSWINSZ, &w);

    if (verbose) {
	PSIDfwd_printMsgf(STDERR, "%s: WINCH to"
			  " col %d row %d xpixel %d ypixel %d\n", __func__,
			  w.ws_col, w.ws_row, w.ws_xpixel, w.ws_ypixel);
    }
}

/**
 * @brief Read input from logger
 *
 * Read and handle input from the logger. Usually this will be input
 * designated to the local client process which will be forwarded. As
 * an extension this might be an #EXIT message displaying that the
 * controlling logger is going to die. Thus also the forwarder will
 * stop execution and exit() which finally will result in the local
 * daemon killing the client process.
 *
 * @param fd The file-descriptor to read from
 *
 * @param data Some additional data. Currently not in use.
 *
 * @return If a fatal error occured, -1 is returned and errno is set
 * appropriately. Otherwise 0 is returned.
 */
static int readFromLogger(int fd, void *data)
{
    PSLog_Msg_t msg;
    int ret;

    ret = recvMsg(&msg, NULL);

    if (ret <= 0) {
	/* The connection to the daemon died. Kill the client the hard way. */
	if (!ret) sendSignal(PSC_getPID(childTask->tid), SIGKILL);

	return ret;
    }

    switch (msg.header.type) {
    case PSP_CD_RELEASERES:
	break;
    case PSP_CC_MSG:
	switch (msg.type) {
	case STDIN:
	    if (verbose) {
		PSIDfwd_printMsgf(STDERR, "%s: %d byte received on STDIN\n",
				  __func__, msg.header.len - PSLog_headerSize);
	    }
	    if (stdinSock<0) {
		PSIDfwd_printMsgf(STDERR, "%s: STDIN already closed\n",
				  __func__);
	    } else {
		writeMsg(&msg);
	    }
	    break;
	case EXIT:
	    /* Logger is going to die */
	    /* Release the pmi client */
	    closePMIClientSocket();
	    /* Release the daemon */
	    closeDaemonSock();
	    /* Cleanup child */
	    sendSignal(PSC_getPID(childTask->tid), SIGKILL);
	    exit(0);
	    break;
	case KVS:
	    pmi_handleKvsRet(&msg);
	    break;
	case WINCH:
	    /* Logger detected change in window-size */
	    handleWINCH(&msg);
	    break;
	case INITIALIZE:
	    /* ignore late INITIALIZE answer */
	    break;
	default:
	    PSIDfwd_printMsgf(STDERR,"%s: Unknown type %d\n",
			      __func__, msg.type);
	}
	break;
    default:
	PSIDfwd_printMsgf(STDERR, "%s: Unexpected msg type %s\n", __func__,
			  PSP_printMsg(msg.header.type));
    }

    return 0;
}

/* @brief  Read pmi message from the client.
 *
 * @param fd The file-descriptor to read from
 *
 * @param data Some additional data. Currently not in use.
 *
 * @return If a fatal error occured, -1 is returned and errno is set
 * appropriately. Otherwise 0 is returned.
 */
static int readFromPMIClient(int fd, void *data)
{
    char msgBuf[PMIU_MAXLINE];
    ssize_t len;
    int ret;

    len = recv(fd, msgBuf, sizeof(msgBuf), 0);

    /* no data received from client */
    if (!len) {
	PSIDfwd_printMsgf(STDERR, "%s: lost connection to the pmi client\n",
			  __func__);

	/*close connection */
	closePMIClientSocket();
	return 0;
    }

    /* socket error occured */
    if (len < 0) {
	PSIDfwd_printMsgf(STDERR,"%s: error on pmi socket occured\n", __func__);
	return 0;
    }

    /* truncate msg to received bytes */
    msgBuf[len] = '\0';

    pmiStatus = CONNECTED;

    /* parse and handle the pmi msg */
    ret = pmi_parse_msg(msgBuf);

    /* pmi communication finished */
    if (ret == PMI_FINALIZED) {

	/* release the child */
	DDSignalMsg_t msg;

	msg.header.type = PSP_CD_RELEASE;
	msg.header.sender = PSC_getMyTID();
	msg.header.dest = childTask->tid;
	msg.header.len = sizeof(msg);
	msg.signal = -1;
	msg.answer = 1;

	sendDaemonMsg((DDMsg_t *)&msg);

	pmiStatus = CLOSED;
    }
    return 0;
}

/**
 * @brief Collect some output before forwarding
 *
 * This is a replacement for the read(2) function call. read(2) is
 * called repeatedly until an EOL ('\n') is received as the last
 * character or a timeout occured.
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
 * denotes an EOF. If an error occured, -1 is returned and errno is
 * set approriately. If the last round timed out, -1 is returned and
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
 * @param data Some additional data. Currently not in use.
 *
 * @return If a fatal error occured, -1 is returned and errno is set
 * appropriately. Otherwise 0 is returned.
 */
static int readFromChild(int fd, void *data)
{
    char buf[4000];
    PSLog_msg_t type = 0;
    size_t total;
    int n;

    if (fd == stdoutSock) {
	type = STDOUT;
    } else if (fd == stderrSock) {
	type = STDERR;
    } else {
	PSIDfwd_printMsgf(STDERR, "%s: PANIC: %s: socket %d active"
			  " (neither stdout (%d) nor stderr (%d))\n",
			  tag, __func__, fd, stdoutSock, stderrSock);
	/* At least, read this stuff and throw it away */
	n = read(fd, buf, sizeof(buf));
	close(fd);
	Selector_remove(fd);
	return 0;
    }

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

	shutdown(fd, SHUT_RD);
	Selector_remove(fd);
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
 * Send the collected accouting information to the corresponding
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
    ptr += sizeof(int32_t);
    msg.header.len += sizeof(int32_t);

    sendDaemonMsg((DDMsg_t *)&msg);
}

/** Contains child's PID after SIGCHLD received */
static pid_t childPID = 0;

/** Contains child's exit-status after SIGCHLD received */
static int childStatus;

/** Contains child's ressource usage after SIGCHLD received */
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
 * - SIGCHLD This is usually generated by the dying client process
 * controlled by the forwarder. It will result in reading and
 * forwarding the remaining output of the client and sending post
 * mortem information like exit status and usage to the logger
 * process. Finally the forwarder will exit on this signal.
 *
 * @param sig The signal to handle.
 *
 * @return No return value.
 */
static void sighandler(int sig)
{
    int i;

    char txt[80];

    switch (sig) {
    case SIGUSR1:
	snprintf(txt, sizeof(txt),
		 "[%d] %s: open sockets left:", childTask->rank, tag);
	for (i=0; i<FD_SETSIZE; i++) {
	    if (Selector_isRegistered(i)) {
		snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), " %d", i);
	    }
	}
	snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), "\n");
	PSIDfwd_printMsg(STDERR, txt);
	PSID_log(-1, "%s: %s", __func__, txt);
	break;
    case SIGTTIN:
	PSIDfwd_printMsg(STDERR, "got SIGTTIN\n");
	break;
    case SIGCHLD:
	if (verbose) PSIDfwd_printMsgf(STDERR, "%s: Got SIGCHLD\n", tag);

	/* if child is stopped, return */
	childPID = wait3(&childStatus, WUNTRACED | WCONTINUED | WNOHANG,
			 &childRUsage);
	if (WIFSTOPPED(childStatus) || WIFCONTINUED(childStatus)) break;

	gotSIGCHLD = 1;
    }

    signal(sig, sighandler);
}

void finalizeForwarder(void)
{
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

    /* Release, if no error occurred and not already done */
    if (pmiStatus == IDLE
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
	&& childTask->group != TG_SERVICE_SIG) {
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

    /* Release the pmi client */
    closePMIClientSocket();

    /* Release the daemon */
    closeDaemonSock();

    exit(0);
}


/**
 * @brief  Accept a new pmi client connection.
 *
 * @param fd The file-descriptor to accept from
 *
 * @param data Some additional data. Currently not in use.
 *
 * @return No return value.
 */
static int acceptPMIClient(int fd, void *data)
{
    unsigned int clientlen;
    struct sockaddr_in SAddr;

    /* check if a client is already connected */
    if (PMIClientSock != -1) {
	PSIDfwd_printMsgf(STDERR, "%s: only one pmi connection allowed\n",
			  __func__);
	return -1;
    }

    /* accept new pmi connection */
    clientlen = sizeof(SAddr);
    if ((PMIClientSock = accept(fd, (void *)&SAddr, &clientlen)) == -1) {
	PSIDfwd_printMsgf(STDERR, "%s: error on accepting new pmi connection\n",
			  __func__);
	return -1;
    }

    /* init the pmi interface */
    pmi_init(PMIClientSock, childTask->loggertid, childTask->rank);

    Selector_register(PMIClientSock, readFromPMIClient, NULL);

    /* close the socket which waits for new connections */
    close(fd);
    Selector_remove(fd);

    return 0;
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
	collectRead(stderrSock, ptr, bufAvail, &read);
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
    bufUsed = answer.header.len - sizeof(answer.header);

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

static void registerSelectHandlers(void)
{
    if (stdoutSock != -1) {
	Selector_register(stdoutSock, readFromChild, NULL);
	openfds++;
    }
    if (stderrSock != -1 && stderrSock != stdoutSock) {
	Selector_register(stderrSock, readFromChild, NULL);
	openfds++;
    }

    Selector_register(daemonSock, readFromLogger, NULL);

    if (PMISock != -1) Selector_register(PMISock, acceptPMIClient, NULL);

    if (PMIClientSock != -1)
	Selector_register(PMIClientSock, readFromPMIClient, NULL);

    FD_ZERO(&writefds);
}

/**
 * @brief The main loop
 *
 * Does all the forwarding work. A tasks is connected and output forwarded
 * to the logger. I/O data is expected on stdoutport and stderrport.
 * It is send via #STDOUT and #STDERR messages respectively.
 *
 * @return No return value.
 *
 */
static void loop(void)
{
    fd_set wfds;
    int sock;

    if (verbose) {
	PSIDfwd_printMsgf(STDERR, "%s: childTask=%s daemon=%d",
			  tag, PSC_printTID(childTask->tid), daemonSock);
	PSIDfwd_printMsgf(STDERR, " stdin=%d stdout=%d stderr=%d\n",
			  stdinSock, stdoutSock, stderrSock);
    }

    /* Loop forever. We exit on SIGCHLD. */
    PSID_blockSig(0, SIGCHLD);
	
    while (openfds || !gotSIGCHLD) {
	memcpy(&wfds, &writefds, sizeof(wfds));

	if (Sselect(FD_SETSIZE, NULL, &wfds, NULL, NULL) < 0) {
	    if (errno && errno != EINTR) {
		PSIDfwd_printMsgf(STDERR, "%s: %s: error on Sselect(): %s\n",
				  tag, __func__, strerror(errno));
	    }
	    continue;
	}

	/* check the remaining sockets for any outputs */
	PSID_blockSig(1, SIGCHLD);
	for (sock=0; sock<FD_SETSIZE; sock++) {
	    if (FD_ISSET(sock, &wfds)) { /* socket ready */
		if (sock == stdinSock) {
		    flushMsgs();
		} else {
		    PSIDfwd_printMsgf(STDERR, "%s: write to %d?\n", tag, sock);
		}
	    }
	}
	PSID_blockSig(0, SIGCHLD);
    }

    /* send usage message */
    finalizeForwarder();

    return;
}

static void waitForChildsDead(void)
{
    PSID_blockSig(0, SIGCHLD);
    PSID_log(-1, "%s: start\n", __func__);
    while (!gotSIGCHLD) {
	PSLog_Msg_t msg;
	struct timeval timeout = {10, 0};
	int ret;

	PSID_log(-1, "%s: recvMsg()\n", __func__);
	ret = recvMsg(&msg, &timeout); /* sleep in recvMsg */
	PSID_blockSig(1, SIGCHLD);
	if (ret > 0) {
	    PSID_log(-1, "%s: recvMsg type %s from %s len %d\n", __func__,
		     PSDaemonP_printMsg(msg.header.type),
		     PSC_printTID(msg.header.sender), msg.header.len);
	}
	sendSignal(PSC_getPID(childTask->tid), SIGKILL);
	PSID_blockSig(0, SIGCHLD);
    }
 
    PSID_log(-1, "%s: done\n", __func__);
    finalizeForwarder();
}

/* see header file for docu */
void PSID_forwarder(PStask_t *task, int daemonfd, int eno, int PMISocket,
		    PMItype_t PMItype)
{
    char pTitle[50];
    char *envStr, *timeoutStr;
    long flags, val;

    childTask = task;
    daemonSock = daemonfd;

    stdinSock = task->stdin_fd;
    stdoutSock = task->stdout_fd;
    stderrSock = task->stderr_fd;

    /* catch SIGCHLD from client */
    PSID_blockSig(1, SIGCHLD);
    signal(SIGCHLD, sighandler);
    signal(SIGUSR1, sighandler);
    signal(SIGTTIN, sighandler);

    PSLog_init(daemonSock, childTask->rank, 2);

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
	if (sendChildBorn(childTask) != 0) waitForChildsDead();
    }

    /* Make stdin nonblocking for us */
    flags = fcntl(stdinSock, F_GETFL);
    flags |= O_NONBLOCK;
    fcntl(stdinSock, F_SETFL, flags);

    /* Set up pmi connection */
    pmiType = PMItype;
    switch (pmiType) {
    case PMI_OVER_UNIX:
	PMIClientSock = PMISocket;
	break;
    case PMI_OVER_TCP:
	PMISock = PMISocket;
	break;
    case PMI_DISABLED:
	break;
    default:
	PSID_log(-1, "%s: wrong pmi type: %i\n", __func__, pmiType);
	pmiType = PMI_DISABLED;
    }

    registerSelectHandlers();

    if (connectLogger(childTask->loggertid) != 0) waitForChildsDead();

    /* Send this message late. No connection to logger before */
    if (loggerTimeout != val)
	PSIDfwd_printMsgf(STDERR,
			  "%s: Illegal value '%s' for __PSI_LOGGER_TIMEOUT\n",
			  tag, timeoutStr);

    /* Set up pmi connection */
    pmiType = PMItype;
    if (pmiType == PMI_OVER_UNIX) {
	/* init the pmi interface */
	pmi_init(PMIClientSock, childTask->loggertid, childTask->rank);
    }

    /* set the process title */
    snprintf(pTitle, sizeof(pTitle), "psidforwarder TID[%d:%d] R%d",
		PSC_getID(childTask->loggertid),
		PSC_getPID(childTask->loggertid), childTask->rank);
    PSC_setProcTitle((char **)PSID_argv, PSID_argc, pTitle, 0);

    /* call the loop which does all the work */
    loop();
}
