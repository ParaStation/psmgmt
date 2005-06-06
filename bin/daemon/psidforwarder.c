/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>

#include "psidutil.h"
#include "pscommon.h"
#include "pstask.h"
#include "psdaemonprotocol.h"
#include "psidmsgbuf.h"
#include "pslog.h"

#include "psidforwarder.h"

/**
 * Verbosity of Forwarder (1=Yes, 0=No)
 *
 * Set by connectLogger() on behalf of info from logger.
 */
int verbose = 0;

/** The ParaStation task ID of the logger */
PStask_ID_t loggerTID = -1;

/** The rank of the local task */
int childRank = -1;

/** The PID of the local task */
pid_t childPID = 0;

/** The socket connecting to the local ParaStation daemon */
int daemonSock = -1;

/** The socket connected to the stdin port of the client */
int stdinSock = -1;

/** The socket connected to the stdout port of the client */
int stdoutSock = -1;

/** The socket connected to the stderr port of the client */
int stderrSock = -1;

/** Set of fds the forwarder listens to */
fd_set readfds;

/** Set of fds the forwarder writes to (this is stdinSock) */
fd_set writefds;

/** Flag marking all controlled fds closed */
int canend = 0;

/** */
msgbuf_t *oldMsgs = NULL;

/**
 * @brief Close socket to daemon.
 *
 * Close the socket connecting the forwarder with the local daemon.
 *
 * @return No return value.
 */
static void closeDaemonSock(void)
{
    int tmp = daemonSock;

    if (daemonSock < 0) return;

    daemonSock = -1;
    FD_CLR(tmp, &readfds);
    loggerTID = -1;
    PSLog_close();
    close(tmp);
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
    char txt[128];
    int ret = 0;

    if (loggerTID < 0) {
	snprintf(txt, sizeof(txt), "%s:  not connected.\n", __func__);
	PSID_errlog(txt, 1);
	errno = EPIPE;

	return -1;
    }

    ret = PSLog_write(loggerTID, type, buf, len);

    if (ret < 0) {
	char *errstr = strerror(errno);
	snprintf(txt, sizeof(txt), "%s: error (%d): %s\n",
		 __func__, errno, errstr ? errstr : "UNKNOWN");
	PSID_errlog(txt, 0);

	loggerTID = -1;
    }

    return ret;
}

/**
 * @brief Send string to logger.
 *
 * Send the NULL terminated string stored within @a buf as a message
 * of type @a type to the logger. This is done via the PSLog facility.
 *
 * @param type The type of the message to send.
 *
 * @param buf Buffer holding the character string to send.
 *
 * @return On success, the number of bytes written is returned,
 * i.e. usually this is strlen(@a buf). On error, -1 is returned, and
 * errno is set appropriately.
 *
 * @see PSLog_print()
 */
static int printMsg(PSLog_msg_t type, char *buf)
{
    return sendMsg(type, buf, strlen(buf));
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
    char txt[128];
    int ret;

    if (loggerTID < 0) {
	snprintf(txt, sizeof(txt), "%s: not connected\n", __func__);
	PSID_errlog(txt, 1);
	errno = EPIPE;

	return -1;
    }

    ret = PSLog_read(msg, timeout);

    if (ret < 0) {
	char *errstr = strerror(errno);
	snprintf(txt, sizeof(txt), "%s: error (%d): %s\n",
		 __func__, errno, errstr ? errstr : "UNKNOWN");
	PSID_errlog(txt, 0);

	loggerTID = -1;

	return ret;
    }

    if (!ret) return ret;

    switch (msg->header.type) {
    case PSP_CC_ERROR:
	if (msg->header.sender == loggerTID) {
	    snprintf(txt, sizeof(txt), "%s: logger %s disappeared.\n",
		     __func__, PSC_printTID(loggerTID));
	    PSID_errlog(txt, 1);

	    loggerTID = -1;

	    errno = EPIPE;
	    ret = -1;
	} else {
	    snprintf(txt, sizeof(txt), "%s: CC_ERROR from %s.\n",
		     __func__, PSC_printTID(loggerTID));
	    PSID_errlog(txt, 0);

	    ret = 0;
	}
	break;
    case PSP_CC_MSG:
	break;
    default:
	snprintf(txt, sizeof(txt), "%s: Unknown message type %s.\n",
		 __func__, PSDaemonP_printMsg(msg->type));
	PSID_errlog(txt, 0);

	ret = 0;
    }

    return ret;
}

/**
 * @brief Send a message to the local daemon.
 *
 * Send the message @a msg to the local daemon.
 *
 * @param msg The message to send.
 *
 * @return On success, the number of bytes send is returned,
 * i.e. usually @a msg->header.len. Otherwise -1 is returned and errno
 * is set appropriately.
 */
static int sendDaemonMsg(DDErrorMsg_t *msg)
{
    char txt[128];
    char *buf = (void *)msg;
    size_t c = msg->header.len;
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
	char *errstr = strerror(errno);
	snprintf(txt, sizeof(txt), "%s: error (%d): %s\n",
		 __func__, errno, errstr ? errstr : "UNKNOWN");
	PSID_errlog(txt, 0);

	closeDaemonSock();

	return n;
    } else if (!n) {
	snprintf(txt, sizeof(txt),
		 "%s: Lost connection to daemon\n", __func__);
	PSID_errlog(txt, 0);

	closeDaemonSock();

	return n;
    }

    if (verbose) {
        snprintf(txt, sizeof(txt), "%s type %s (len=%d) to %s\n",
                 __func__, PSDaemonP_printMsg(msg->header.type),
		 msg->header.len, PSC_printTID(msg->header.dest));
        printMsg(STDERR, txt);
    }

    return msg->header.len;
}

/**
 * @brief Connect to the logger.
 *
 * Connect to the logger described by the unique task ID @a tid. Wait
 * for #INITIALIZE message and set #loggerTID and #verbose correctly.
 *
 * @param tid The logger's ParaStation task ID.
 *
 * @return On success, 0 is returned. Simultaneously #loggerTID is
 * set. On error, -1 is returned, and errno is set appropriately.
 */
static int connectLogger(PStask_ID_t tid)
{
    char txt[128];
    PSLog_Msg_t msg;
    struct timeval timeout = {10, 0};
    int ret;

    loggerTID = tid; /* Only set for the first sendMsg()/recvMsg() pair */

    sendMsg(INITIALIZE, NULL, 0);

    ret = recvMsg(&msg, &timeout);

    loggerTID = -1;

    if (ret <= 0) {
	snprintf(txt, sizeof(txt), "%s(%s): Connection refused\n",
		 __func__, PSC_printTID(tid));
	PSID_errlog(txt, 0);
	errno = ECONNREFUSED;
	return -1;
    }

    if (msg.header.len != PSLog_headerSize + (int) sizeof(int)) {
	snprintf(txt, sizeof(txt), "%s(%s): Message to short\n",
		 __func__, PSC_printTID(tid));
	PSID_errlog(txt, 0);

	errno = ECONNREFUSED;
	return -1;
    } else if (msg.type != INITIALIZE) {
	snprintf(txt, sizeof(txt), "%s(%s): Protocol messed up\n",
		 __func__, PSC_printTID(tid));
	PSID_errlog(txt, 0);

	errno = ECONNREFUSED;
	return -1;
    } else if (msg.header.sender != tid) {
	snprintf(txt, sizeof(txt), "%s(%s): Got INITIALIZE not from logger\n",
		 __func__, PSC_printTID(tid));
	PSID_errlog(txt, 0);

	errno = ECONNREFUSED;
	return -1;
    } else {
	loggerTID = tid;
	verbose = *(int *) msg.buf;
	snprintf(txt, sizeof(txt), "%s(%s): Connected\n",
		 __func__, PSC_printTID(tid));
	PSID_errlog(txt, 10);
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
    char txt[128];
    PSLog_Msg_t msg;
    int ret;
    struct timeval timeout = {10, 0};

    sendMsg(FINALIZE, (char *)&status, sizeof(status));

    ret = recvMsg(&msg, &timeout);

    if (ret < 0) {
	if (errno == EPIPE) {
	    snprintf(txt, sizeof(txt),
		     "%s: logger already dissapeared\n", __func__);
	    PSID_errlog(txt, 0);
	} else {
	    char *errstr = strerror(errno);
	    snprintf(txt, sizeof(txt), "%s: error (%d): %s\n",
		     __func__, errno, errstr ? errstr : "UNKNOWN");
	    PSID_errlog(txt, 0);
	}
    } else if (!ret) {
	snprintf(txt, sizeof(txt),
		 "%s: receive timed out. logger dissapeared\n", __func__);
	PSID_errlog(txt, 0);
    } else if (msg.type != EXIT) {
	snprintf(txt, sizeof(txt),
		 "%s: Protocol messed up (type %d) from %s\n",
		 __func__, msg.type, PSC_printTID(msg.header.sender));
	PSID_errlog(txt, 1);
    }

    loggerTID = -1;
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
    char txt[128];
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
	    if (errno == EINTR) {
		continue;
	    } else {
		snprintf(txt, sizeof(txt),
			 "PSID_forwarder: error on select(%d): %s\n",
			 errno, strerror(errno));
		printMsg(STDERR, txt);
		break;
	    }
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
    int i, status;
    struct rusage rusage;
    pid_t pid;
    DDErrorMsg_t msg;

    char txt[80];

    switch (sig) {
    case SIGUSR1:
	snprintf(txt, sizeof(txt),
		 "[%d] PSID_forwarder: open sockets left:", childRank);
	for (i=0; i<FD_SETSIZE; i++) {
	    if (FD_ISSET(i, &readfds) && i != daemonSock) {
		snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), " %d", i);
	    }
	}
	snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), "\n");
	printMsg(STDERR, txt);
	for (i=0; i<FD_SETSIZE; i++) {
	    if (FD_ISSET(i, &readfds) && i != daemonSock) {
		int n;
		char buf[128], txt2[128];

		/* Try to read from fd */
		errno=0;
		n = read(i, buf, sizeof(buf)-1);
		buf[sizeof(buf)-1]='\0';
		snprintf(txt2, sizeof(txt2),
			 "read(%d) returned %d, errno %d\n", i, n, errno);
		printMsg(STDERR, txt2);
		printMsg(STDOUT, buf);
	    }
	}
	break;
    case SIGCHLD:
	if (verbose) {
	    snprintf(txt, sizeof(txt),
		     "[%d] PSID_forwarder: Got SIGCHLD.\n", childRank);
	    printMsg(STDERR, txt);
	}

	if (!canend) {
	    /* Read all the remaining stuff from the controlled fds */
	    fd_set fds;
	    struct timeval atv;
	    int ret;

	    do {
		memcpy(&fds, &readfds, sizeof(fds));
		atv = (struct timeval) {0,1000};

		ret = select(FD_SETSIZE, &fds, NULL, NULL, &atv);
		if (ret < 0) {
		    if (errno != EINTR) {
			snprintf(txt, sizeof(txt),
				 "PSID_forwarder: error on select(%d): %s\n",
				 errno, strerror(errno));
			printMsg(STDERR, txt);
		    }
		    break;
		} else if (ret > 0) {
		    int sock, n;
		    size_t total;
		    char buf[256];
		    PSLog_msg_t type;

		    for (sock=0; sock<FD_SETSIZE; sock++) {
			if (FD_ISSET(sock, &fds)) { /* socket ready */
			    if (sock==stdoutSock) {
				type=STDOUT;
			    } else if (sock==stderrSock) {
				type=STDERR;
			    } else {
				/* ignore */
				ret--;
				continue;
			    }

			    n = collectRead(sock, buf, sizeof(buf), &total);
			    if (verbose) {
				snprintf(txt, sizeof(txt), "PSID_forwarder:"
					 " got %d bytes on sock %d %d %d\n",
					 (int) total, sock, n, errno);
				printMsg(STDERR, txt);
			    }
			    if (n==0 || (n<0 && errno==EIO)) {
				/* socket closed */
				close(sock);
				FD_CLR(sock,&readfds);
			    } else if (n<0
				       && errno!=ETIME && errno!=ECONNRESET) {
				/* ignore the error */
				snprintf(txt, sizeof(txt),
					 "PSID_forwarder: collectRead():%s\n",
					 strerror(errno));
				printMsg(STDERR, txt);
			    }
			    if (total) {
				/* something received. forward it to logger */
				sendMsg(type, buf, total);
			    }
			}
		    }
		}
	    } while (ret);
	}

	pid = wait3(&status, WUNTRACED, &rusage);

	if (WIFSTOPPED(status)) break;

	sendMsg(USAGE, (char *) &rusage, sizeof(rusage));

	/* Send CHILDDEAD message to the daemon */
	msg.header.type = PSP_DD_CHILDDEAD;
	msg.header.dest = PSC_getTID(-1, 0);
	msg.header.sender = PSC_getTID(-1, getpid());
	msg.error = status;
	msg.request = PSC_getTID(-1, pid);
	msg.header.len = sizeof(msg);
	sendDaemonMsg(&msg);

	releaseLogger(status);

	/* Release the daemon */
	closeDaemonSock();

	exit(0);

	break;
    }

    if (verbose) {
	snprintf(txt, sizeof(txt), "PSID_forwarder: open sockets left:");
	for (i=0; i<FD_SETSIZE; i++) {
	    if (FD_ISSET(i, &readfds)) {
		snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), " %d", i);
	    }
	}
	snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), "\n");
	printMsg(STDERR, txt);
    }

    signal(sig, sighandler);
}

/**
 * @brief Checks file table after select has failed.
 *
 * Detailed checking of the file table on validity after a select()
 * call has failed.
 *
 * @param openfds Set of file descriptors that have to be checked.
 *
 * @return No return value.
 *
 */
static void checkFileTable(fd_set* openfds)
{
    fd_set rfds;
    int fd;
    struct timeval tv;
    char *errtxt, buf[80];

    for (fd=0; fd<FD_SETSIZE;) {
	if (FD_ISSET(fd, openfds)) {
	    memset(&rfds, 0, sizeof(rfds));
	    FD_SET(fd, &rfds);

	    tv.tv_sec=0;
	    tv.tv_usec=0;
	    if (select(FD_SETSIZE, &rfds, NULL, NULL, &tv) < 0){
		/* error : check if it is a wrong fd in the table */
		switch(errno){
		case EBADF :
		    snprintf(buf, sizeof(buf),
			     "%s(%d): EBADF -> close socket\n", __func__, fd);
		    printMsg(STDERR, buf);
		    close(fd);
		    FD_CLR(fd, openfds);
		    fd++;
		    break;
		case EINTR:
		    snprintf(buf, sizeof(buf),
			     "%s(%d): EINTR -> trying again\n", __func__, fd);
		    printMsg(STDERR, buf);
		    break;
		case EINVAL:
		    snprintf(buf, sizeof(buf),
			     "%s(%d): EINVAL -> close socket\n", __func__, fd);
		    printMsg(STDERR, buf);
		    close(fd);
		    FD_CLR(fd, openfds);
		    break;
		case ENOMEM:
		    snprintf(buf , sizeof(buf),
			     "%s(%d): ENOMEM -> close socket\n", __func__, fd);
		    printMsg(STDERR, buf);
		    close(fd);
		    FD_CLR(fd, openfds);
		    break;
		default:
		    errtxt=strerror(errno);
		    snprintf(buf, sizeof(buf),
			     "%s(%d): unrecognized error (%d):%s\n", __func__,
			     fd, errno, errtxt ? errtxt : "UNKNOWN errno");
		    printMsg(STDERR, buf);
		    fd ++;
		    break;
		}
	    }else
		fd ++;
	}else
	    fd ++;
    }
}


/**
 * @brief Write to a file descriptor
 *
 * Write the first @a count bytes within @a buf to the file descriptor
 * @a fd. If less than the requested number of bytes within an atomic
 * write(2) call were written, further attempts are made in order to
 * send the rest of the buffer.
 *
 * @param fd The file descriptor to write to.
 *
 * @param buf The buffer containing the bytes to send.
 *
 * @param count The number of bytes to send.
 *
 * @return On success, the total number of bytes written is returned,
 * i.e. usually this is @a count. Otherwise -1 is returned and errno
 * is set appropriately.
 */

static int do_write(PSLog_Msg_t *msg, int offset)
{
    int n, i;
    int count = msg->header.len - PSLog_headerSize;

    for (n=offset, i=1; (n<count) && (i>0);) {
	i = write(stdinSock, &msg->buf[n], count-n);
	if (i<=0) {
	    switch (errno) {
	    case EINTR:
		break;
	    case EAGAIN:
		return n;
		break;
	    default:
	    {
		char obuf[120];
		char *errstr = strerror(errno);

		snprintf(obuf, sizeof(obuf),
			 "%s(): got error %d on stdinSock: %s",
			 __func__, errno, 
			 errstr ? errstr : "UNKNOWN");
		printMsg(STDERR, obuf);
		return i;
	    }
	    }
	} else
	    n+=i;
    }
    return n;
}

static int storeMsg(PSLog_Msg_t *msg, int offset)
{
    msgbuf_t *msgbuf = oldMsgs;

    if (msgbuf) {
        /* Search for end of list */
        while (msgbuf->next) msgbuf = msgbuf->next;
        msgbuf->next = getMsg();
        msgbuf = msgbuf->next;
    } else {
        msgbuf = oldMsgs = getMsg();
    }

    if (!msgbuf) {
        errno = ENOMEM;
        return -1;
    }

    msgbuf->msg = malloc(msg->header.len);
    if (!msgbuf->msg) {
        errno = ENOMEM;
        return -1;
    }
    memcpy(msgbuf->msg, msg, msg->header.len);

    msgbuf->offset = offset;

    return 0;
}

static int flushMsgs(void)
{
    while (oldMsgs) {
	msgbuf_t *msg = oldMsgs;
	int len = msg->msg->len - PSLog_headerSize;
	int written = do_write((PSLog_Msg_t *)msg->msg, msg->offset);

	if (written<0) return written;
	if (written != len) {
	    msg->offset = written;
	    break;
	}

	oldMsgs = msg->next;
	freeMsg(msg);
    }

    if (oldMsgs) {
	errno = EWOULDBLOCK;
	return -1;
    } else {
	sendMsg(CONT, NULL, 0);
    }

    return 0;
}

static int writeMsg(PSLog_Msg_t *msg)
{
    int len = msg->header.len - PSLog_headerSize, written = 0;

    if (oldMsgs) flushMsgs();

    if (!oldMsgs) {
        written = do_write(msg, 0);
    }

    if (written<0) return written;
    if (written != len) {
        if (!storeMsg(msg, written)) errno = EWOULDBLOCK;
	sendMsg(STOP, NULL, 0);
        return -1;
    }

    return written;
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
 * @return Usually the number of bytes received is returned. If an
 * error occured, -1 is returned and errno is set appropriately.
 */
static int readFromLogger(void)
{
    PSLog_Msg_t msg;
    char obuf[120];
    int ret;

    ret = recvMsg(&msg, NULL);
    if (ret > 0) {
	switch (msg.header.type) {
	case PSP_CC_MSG:
	    switch (msg.type) {
	    case STDIN:
	    {
		int len = msg.header.len - PSLog_headerSize;
		if (verbose) {
		    snprintf(obuf, sizeof(obuf),
			     "%s: %d byte received on STDIN\n", __func__, len);
		    printMsg(STDERR, obuf);
		}
		if (len) {
		    if (stdinSock<0) {
			snprintf(obuf, sizeof(obuf),
				 "%s: STDIN already closed\n", __func__);
			printMsg(STDERR, obuf);
		    } else {
			writeMsg(&msg);
		    }
		} else {
		    /* close clients stdin */
		    shutdown(stdinSock, SHUT_WR);
		    stdinSock = -1;
		}
		break;
	    }
	    case EXIT:
		/* Logger is going to die */
		/* Release the daemon */
		closeDaemonSock();

		exit(0);
		break;
	    default:
		snprintf(obuf, sizeof(obuf),
			 "%s: Unknown type %d\n", __func__, msg.type);
		printMsg(STDERR, obuf);
	    }
	    break;
	default:
	    snprintf(obuf, sizeof(obuf), "%s: Unexpected msg type %s\n",
		     __func__, PSP_printMsg(msg.header.type));
	    printMsg(STDERR, obuf);
	}
    } else if (!ret) {
	/* The connection to the daemon died. Kill the client the hard way. */
	kill(-childPID, SIGKILL);
    }
	
    return ret;
}

/**
 * @brief The main loop
 *
 * Does all the forwarding work. A tasks is connected and output forwarded
 * to the logger. I/O data is expected on stdoutport and stderrport.
 * Is is send via #STDOUT and #STDERR messages respectively.
 *
 * @return No return value.
 *
 */
static void loop(void)
{
    int sock;      /* client socket */
    fd_set rfds, wfds;
    struct timeval mytv={2,0}, atv;
    char buf[4000], obuf[120];
    int n;
    size_t total;
    int openfds = 2;
    PSLog_msg_t type;

    if (verbose) {
	snprintf(obuf, sizeof(obuf),
		 "PSID_forwarder: stdin=%d stdout=%d stderr=%d\n",
		 stdinSock, stdoutSock, stderrSock);
	printMsg(STDERR, obuf);
    }

    FD_ZERO(&readfds);
    FD_SET(stdoutSock, &readfds);
    FD_SET(stderrSock, &readfds);
    FD_SET(daemonSock, &readfds);
    FD_ZERO(&writefds);

    /* Loop forever. We exit on SIGCHLD. */
    PSID_blockSig(0, SIGCHLD);
    while (1) {
	memcpy(&rfds, &readfds, sizeof(rfds));
	memcpy(&wfds, &writefds, sizeof(wfds));
	atv = mytv;
	if (select(FD_SETSIZE, &rfds, &wfds, NULL, &atv) < 0) {
	    if (errno != EINTR) {
		snprintf(obuf, sizeof(obuf),
			 "PSID_forwarder: error on select(%d): %s\n",
			 errno, strerror(errno));
		printMsg(STDERR, obuf);
		checkFileTable(&readfds);
	    }
	    continue;
	}
	/*
	 * check the remaining sockets for any outputs
	 */
	PSID_blockSig(1, SIGCHLD);
	for (sock=0; sock<FD_SETSIZE; sock++) {
	    if (FD_ISSET(sock, &rfds)) { /* socket ready */
		if (sock==daemonSock) {
		    /* Read new input */
		    readFromLogger();
		    continue;
		} else if (sock==stdoutSock) {
		    type=STDOUT;
		} else if (sock==stderrSock) {
		    type=STDERR;
		} else {
		    snprintf(obuf, sizeof(obuf),
			     "PSID_forwarder: PANIC: sock %d, which is neither"
			     " stdout (%d) nor stderr (%d) is active!!\n",
			     sock, stdoutSock, stderrSock);
		    printMsg(STDERR, obuf);
		    /* At least, read this stuff and throw it away */
		    n = read(sock, buf, sizeof(buf));
		    continue;
		}

		n = collectRead(sock, buf, sizeof(buf), &total);
		if (verbose) {
		    snprintf(obuf, sizeof(obuf),
			     "PSID_forwarder: got %ld bytes on sock %d\n",
			     (long) total, sock);
		    printMsg(STDERR, obuf);
		}
		if (n==0 || (n<0 && errno==EIO)) {
		    /* socket closed */
		    if (verbose) {
			snprintf(obuf, sizeof(obuf),
				 "PSID_forwarder: closing %d\n", sock);
			printMsg(STDERR, obuf);
		    }

		    close(sock);
		    FD_CLR(sock, &readfds);
		    openfds--;
		    if (!openfds) {
			/* stdout and stderr closed -> wait for SIGCHLD */
			if (verbose) {
			    snprintf(obuf, sizeof(obuf),
				     "PSID_forwarder: wait for SIGCHLD\n");
			    printMsg(STDERR, obuf);
			}
			canend = 1;
		    }
		} else if (n<0 && errno!=ETIME && errno!=ECONNRESET) {
		    /* ignore the error */
		    snprintf(obuf, sizeof(obuf),
			     "PSID_forwarder: read():%s\n", strerror(errno));
		    printMsg(STDERR, obuf);
		}
		if (total) {
		    /* forward it to logger */
		    sendMsg(type, buf, total);
		}
	    }
	    if (FD_ISSET(sock, &wfds)) { /* socket ready */
		if (sock == stdinSock) {
		    flushMsgs();
		} else {
		    snprintf(obuf, sizeof(obuf),
			     "PSID_forwarder: write to %d?\n", sock);
		    printMsg(STDERR, obuf);
		}
	    }
	}
	PSID_blockSig(0, SIGCHLD);
    }

    return;
}

/* see header file for docu */
void PSID_forwarder(PStask_t *task, int daemonfd,
		    int stdinfd, int stdoutfd, int stderrfd)
{
    long flags;

    childRank = task->rank;
    childPID = PSC_getPID(task->tid);
    daemonSock = daemonfd;
    stdinSock = stdinfd;
    stdoutSock = stdoutfd;
    stderrSock = stderrfd;

    /* catch SIGCHLD from client */
    signal(SIGCHLD, sighandler);
    signal(SIGUSR1, sighandler);

    PSLog_init(daemonSock, childRank, 1);

    /* Make stdin nonblocking for us */
    flags = fcntl(stdinSock, F_GETFL);
    flags |= O_NONBLOCK;
    fcntl(stdinSock, F_SETFL, flags);

    if (connectLogger(task->loggertid) != 0) {
	/* There is no logger. Just wait for the client to finish. */
	PSID_blockSig(0, SIGCHLD);
	while (1) sleep(10);
    }

    /* call the loop which does all the work */
    loop();
}
