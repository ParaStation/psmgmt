/*
 *               ParaStation3
 * psidforwarder.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidforwarder.c,v 1.8 2003/04/04 09:33:42 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidforwarder.c,v 1.8 2003/04/04 09:33:42 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>

#include "psidutil.h"
#include "pscommon.h"
#include "psdaemonprotocol.h"
#include "pslog.h"

#include "psidforwarder.h"

/** @todo Update and complete docu */

/**
 * Verbosity of Forwarder (1=Yes, 0=No)
 *
 * Set by connectLogger() on behalf of info from logger.
 */
int verbose = 0;

/** The ParaStation task ID of the logger */
long loggerTID = -1;

/** The rank of the local task */
int childRank = -1;

/** The socket connecting to the local ParaStation daemon */
int daemonSock = -1;

/** The socket connected to the stdin port of the client */
int stdinSock = -1;

/** The socket connected to the stdout port of the client */
int stdoutSock = -1;

/** The socket connected to the stderr port of the client */
int stderrSock = -1;

/** Set of fds the forwarder listens to */
fd_set myfds;

/** Flag marking all controlled fds closed */
int canend = 0;


static int sendMsg(PSLog_msg_t type, char *buf, size_t len)
{
    char txt[128];
    int ret = 0;

    if (loggerTID < 0) {
	snprintf(txt, sizeof(txt), "%s():  not connected.\n", __func__);
	PSID_errlog(txt, 1);
	return -1;
    }

    ret = PSLog_write(loggerTID, type, buf, len);

    if (ret < 0) {
	char *errstr = strerror(errno);
	snprintf(txt, sizeof(txt), "%s(): error (%d): %s\n",
		 __func__, errno, errstr ? errstr : "UNKNOWN");
	PSID_errlog(txt, 0);

	loggerTID = -1;
    }

    return ret;
}

static int printMsg(PSLog_msg_t type, char *buf)
{
    return sendMsg(type, buf, strlen(buf));
}

static int recvMsg(PSLog_Msg_t *msg, struct timeval *timeout)
{
    char txt[128];
    int ret;

    if (loggerTID < 0) {
	snprintf(txt, sizeof(txt), "%s(): not connected\n", __func__);
	PSID_errlog(txt, 1);
	errno = EPIPE;

	return -1;
    }

    ret = PSLog_read(msg, timeout);

    if (ret < 0) {
	char *errstr = strerror(errno);
	snprintf(txt, sizeof(txt), "%s(): error (%d): %s\n",
		 __func__, errno, errstr ? errstr : "UNKNOWN");
	PSID_errlog(txt, 0);

	loggerTID = -1;

	return ret;
    }

    if (!ret) return ret;

    switch (msg->header.type) {
    case PSP_CC_ERROR:
	if (msg->header.sender == loggerTID) {
	    snprintf(txt, sizeof(txt), "%s(): logger %s disappeared.\n",
		     __func__, PSC_printTID(loggerTID));
	    PSID_errlog(txt, 1);

	    loggerTID = -1;

	    errno = EPIPE;
	    ret = -1;
	} else {
	    snprintf(txt, sizeof(txt), "%s(): CC_ERROR from %s.\n",
		     __func__, PSC_printTID(loggerTID));
	    PSID_errlog(txt, 0);

	    ret = 0;
	}
	break;
    case PSP_CC_MSG:
	break;
    default:
	snprintf(txt, sizeof(txt), "%s(): Unknown message type %s.\n",
		 __func__, PSDaemonP_printMsg(msg->type));
	PSID_errlog(txt, 0);

	ret = 0;
    }

    return ret;
}

static int sendDaemonMsg(DDErrorMsg_t *msg)
{
    char txt[128];
    char *buf = (void *)msg;
    size_t c = msg->header.len;
    int n;

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
	snprintf(txt, sizeof(txt), "%s(): error (%d): %s\n",
		 __func__, errno, errstr ? errstr : "UNKNOWN");
	PSID_errlog(txt, 0);

	loggerTID = -1;

        FD_CLR(daemonSock, &myfds);
        close(daemonSock);

	daemonSock = -1;

	return n;
    } else if (!n) {
	snprintf(txt, sizeof(txt),
		 "%s(): Lost connection to daemon\n", __func__);
	PSID_errlog(txt, 0);

	loggerTID = -1;

        FD_CLR(daemonSock, &myfds);
        close(daemonSock);

	daemonSock = -1;

	return n;
    }

    if (verbose) {
        snprintf(txt, sizeof(txt), "%s() type %s (len=%d) to %s\n",
                 __func__, PSDaemonP_printMsg(msg->header.type),
		 msg->header.len, PSC_printTID(msg->header.dest));
        printMsg(STDERR, txt);
    }

    return msg->header.len;
}

/**
 * @brief Connect to the logger
 *
 * Connect to the logger listening at @a node on @a port. Wait for
 * #INITIALIZE message and set #loggerTID and #verbose correctly.
 *
 * @param tid The ParaStation task ID of the logger.
 *
 * @return On success, 0 is returned.
 * Simultaneously #loggerTID is set.
 * On error, -1 is returned, and errno is set appropriately.
 */
static int connectLogger(long tid)
{
    char txt[128];
    PSLog_Msg_t msg;
    struct timeval timeout = {10, 0};
    int ret;

    loggerTID = tid;

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
	    snprintf(txt, sizeof(txt), "%s(): error (%d): %s\n",
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
size_t collectRead(int sock, char *buf, size_t count, size_t *total)
{
    char txt[128];
    int n;

    *total = 0;

    do {
	fd_set readfds;
	struct timeval timeout;

	FD_ZERO(&readfds);
	FD_SET(sock, &readfds);
	timeout = (struct timeval) {0, 1000};

	n = select(sock+1, &readfds, NULL, NULL, &timeout);
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
 * This handles the 
 *  sighandler(signal)
 */
static void sighandler(int sig)
{
    int i, status;
    struct rusage rusage;
    pid_t pid;
    static int firstCall = 1;
    DDErrorMsg_t msg;

    char txt[80];

    switch (sig) {
    case SIGUSR1:
	snprintf(txt, sizeof(txt),
		 "[%d] PSID_forwarder: open sockets left:", childRank);
	for (i=0; i<FD_SETSIZE; i++) {
	    if (FD_ISSET(i, &myfds) && i != daemonSock) {
		snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), " %d", i);
	    }
	}
	snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), "\n");
	printMsg(STDERR, txt);
	for (i=0; i<FD_SETSIZE; i++) {
	    if (FD_ISSET(i, &myfds) && i != daemonSock) {
		int n;
		char buf[128], txt2[128];

		/* Try to read from fd */
		n = read(i, buf, sizeof(buf));
		snprintf(txt2, sizeof(txt2),
			 "read(%d) returned %d, errno %d\n", i, n, errno);
		printMsg(STDERR, txt2);
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
	    fd_set afds;
	    struct timeval atv;
	    int ret;

	    do {
		memcpy(&afds, &myfds, sizeof(afds));
		atv = (struct timeval) {0,1000};

		ret = select(FD_SETSIZE, &afds, NULL, NULL, &atv);
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
			if (FD_ISSET(sock, &afds)) { /* socket ready */
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
					 " got %ld bytes on sock %d %d %d\n",
					 (long) total, sock, n, errno);
				printMsg(STDERR, txt);
			    }
			    if (n==0 || (n<0 && errno==EIO)) {
				/* socket closed */
				close(sock);
				FD_CLR(sock,&myfds);
			    } else if (n<0 && errno!=ETIME) {
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

	pid = wait3(&status, 0, &rusage);

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
	close(daemonSock);

	exit(0);

	break;
    }

    if (verbose) {
	snprintf(txt, sizeof(txt), "PSID_forwarder: open sockets left:");
	for (i=0; i<FD_SETSIZE; i++) {
	    if (FD_ISSET(i, &myfds)) {
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

    for(fd=0;fd<FD_SETSIZE;){
	if (FD_ISSET(fd,openfds)) {
	    memset(&rfds, 0, sizeof(rfds));
	    FD_SET(fd,&rfds);

	    tv.tv_sec=0;
	    tv.tv_usec=0;
	    if (select(FD_SETSIZE, &rfds, NULL, NULL, &tv) < 0){
		/* error : check if it is a wrong fd in the table */
		switch(errno){
		case EBADF :
		    snprintf(buf, sizeof(buf), "checkFileTable(%d): EBADF"
			     " -> close socket\n", fd);
		    printMsg(STDERR, buf);
		    close(fd);
		    FD_CLR(fd,openfds);
		    fd++;
		    break;
		case EINTR:
		    snprintf(buf, sizeof(buf), "checkFileTable(%d): EINTR"
			     " -> trying again\n", fd);
		    printMsg(STDERR, buf);
		    break;
		case EINVAL:
		    snprintf(buf, sizeof(buf), "checkFileTable(%d): EINVAL"
			     " -> close socket\n", fd);
		    printMsg(STDERR, buf);
		    close(fd);
		    FD_CLR(fd,openfds);
		    break;
		case ENOMEM:
		    snprintf(buf , sizeof(buf), "checkFileTable(%d): ENOMEM"
			    " -> close socket\n",fd);
		    printMsg(STDERR, buf);
		    close(fd);
		    FD_CLR(fd,openfds);
		    break;
		default:
		    errtxt=strerror(errno);
		    snprintf(buf, sizeof(buf), "checkFileTable(%d):"
			    " unrecognized error (%d):%s\n", fd, errno,
			    errtxt?errtxt:"UNKNOWN errno");
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

static int writeall(int fd, void *buf, int count)
{
    int len;
    int c = count;

    while (c>0){
	len = write(fd, buf, c);
	if (len<0) return -1;
	c -= len;
	((char*)buf) += len;
    }
    return count;
}

static int read_from_logger(int logfd, int stdinport)
{
    PSLog_Msg_t msg;
    char obuf[120];
    int ret;

    ret = recvMsg(&msg, NULL);
    if (ret > 0) {
	if ((msg.header.type == PSP_CC_MSG) && (msg.type == STDIN)) {
	    if (verbose) {
		snprintf(obuf, sizeof(obuf),
			 "PSID_forwarder: receive %ld byte for STDIN\n",
			 (long) msg.header.len-(sizeof(msg)-sizeof(msg.buf)));
		printMsg(STDERR, obuf);
	    }
	    writeall(stdinport, msg.buf, msg.header.len - PSLog_headerSize); 
	} else 	if ((msg.header.type == PSP_CC_MSG) && (msg.type == EXIT)) {
	    /* Logger is going to die */
	    /* Release the daemon */
	    close(daemonSock);

	    exit(0);
	} else {
	    /* unexpected message. Ignore. */
	}
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
 * @param stdoutport The port, on which stdout-data is expected.
 * @param stderrport The port, on which stderr-data is expected.
 *
 * @return No return value.
 *
 */
static void loop(void)
{
    int sock;      /* client socket */
    fd_set afds;
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

    FD_ZERO(&myfds);
    FD_SET(stdoutSock, &myfds);
    FD_SET(stderrSock, &myfds);
    FD_SET(daemonSock, &myfds);

    /*
     * Loop forever. We exit on SIGCHLD.
     */
    while (1) {
	memcpy(&afds, &myfds, sizeof(afds));
	atv = mytv;
	if (select(FD_SETSIZE, &afds, NULL, NULL, &atv) < 0) {
	    if (errno != EINTR) {
		snprintf(obuf, sizeof(obuf),
			 "PSID_forwarder: error on select(%d): %s\n",
			 errno, strerror(errno));
		printMsg(STDERR, obuf);
		checkFileTable(&myfds);
	    }
	    continue;
	}
	/*
	 * check the remaining sockets for any outputs
	 */
	PSID_blockSig(1, SIGCHLD);
	for (sock=0; sock<FD_SETSIZE; sock++) {
	    if (FD_ISSET(sock, &afds)) { /* socket ready */
		if (sock==daemonSock) {
		    /* Read new input */
		    read_from_logger(daemonSock, stdinSock);
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
		    FD_CLR(sock,&myfds);
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
		} else if (n<0 && errno!=ETIME) {
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
	}
	PSID_blockSig(0, SIGCHLD);
    }

    return;
}

void PSID_forwarder(PStask_t *task, int daemonfd,
		    int stdinfd, int stdoutfd, int stderrfd)
{
    childRank = task->rank;
    daemonSock = daemonfd;
    stdinSock = stdinfd;
    stdoutSock = stdoutfd;
    stderrSock = stderrfd;

    /* catch SIGCHLD from client */
    signal(SIGCHLD, sighandler);
    signal(SIGUSR1, sighandler);

    PSLog_init(daemonSock, childRank, 1);

    PSID_blockSig(0, SIGCHLD);

    if (connectLogger(task->loggertid) != 0) {
	/* There is no logger. Just wait for the client to finish. */

	while (1) sleep(10);
    }

    /* call the loop which does all the work */
    loop();
}
