/*
 *               ParaStation3
 * psidforwarder.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidforwarder.c,v 1.2 2003/02/21 13:21:41 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidforwarder.c,v 1.2 2003/02/21 13:21:41 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>

#include "psidutil.h"
#include "pscommon.h"
#include "psprotocol.h"
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
	snprintf(txt, sizeof(txt), "%s():  not connected%s\n", __func__);
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
		 __func__, PSP_printMsg(msg->type));
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
                 __func__, PSP_printMsg(msg->header.type), msg->header.len,
		 PSC_printTID(msg->header.dest));
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
	snprintf(txt, sizeof(txt), "%s(): Connection refused\n", __func__);
	PSID_errlog(txt, 0);
	errno = ECONNREFUSED;
	return -1;
    }

    if (msg.header.len != PSLog_headerSize + (int) sizeof(int)) {
	snprintf(txt, sizeof(txt), "%s(): Message to short\n", __func__);
	PSID_errlog(txt, 0);

	errno = ECONNREFUSED;
	return -1;
    } else if (msg.type != INITIALIZE) {
	snprintf(txt, sizeof(txt), "%s(): Protocol messed up\n", __func__);
	PSID_errlog(txt, 0);

	errno = ECONNREFUSED;
	return -1;
    } else if (msg.header.sender != tid) {
	snprintf(txt, sizeof(txt),
		 "%s(): Got INITIALIZE not from logger\n", __func__);
	PSID_errlog(txt, 0);

	errno = ECONNREFUSED;
	return -1;
    } else {
	loggerTID = tid;
	verbose = *(int *) msg.buf;
	snprintf(txt, sizeof(txt), "%s(): Connected\n", __func__);
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
		     "%s(): logger already dissapeared\n", __func__);
	    PSID_errlog(txt, 0);
	} else {
	    char *errstr = strerror(errno);
	    snprintf(txt, sizeof(txt), "%s(): error (%d): %s\n",
		     __func__, errno, errstr ? errstr : "UNKNOWN");
	    PSID_errlog(txt, 0);
	}
    } else if (!ret) {
	snprintf(txt, sizeof(txt),
		 "%s(): receive timed out. logger dissapeared\n", __func__);
	PSID_errlog(txt, 0);
    } else if (msg.type != EXIT) {
	snprintf(txt, sizeof(txt),
		 "%s(): Protocol messed up (type %d) from %s\n",
		 __func__, msg.type, PSC_printTID(msg.header.sender));
	PSID_errlog(txt, 0);
    }

    loggerTID = -1;
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
	    struct timeval atv={0,0};
	    int ret;

	    memcpy(&afds, &myfds, sizeof(afds));
	    
	    ret = select(FD_SETSIZE, &afds, NULL, NULL, &atv);
	    if (ret < 0) {
		if (errno != EINTR) {
		    snprintf(txt, sizeof(txt),
			     "PSID_forwarder: error on select(%d): %s\n",
			     errno, strerror(errno));
		    printMsg(STDERR, txt);
		}
	    } else if (ret) {
		int sock, n;
		char buf[4000];
		PSLog_msg_t type;

		for (sock=0; sock<FD_SETSIZE; sock++) {
		    if (FD_ISSET(sock, &afds)) { /* socket ready */
			if (sock==stdoutSock) {
			    type=STDOUT;
			} else if (sock==stderrSock) {
			    type=STDERR;
			} else {
			    continue;
			}

			n = read(sock, buf, sizeof(buf));
			if (verbose) {
			    snprintf(txt, sizeof(txt), "PSID_forwarder:"
				     " got %d bytes on sock %d\n", n, sock);
			    printMsg(STDERR, txt);
			}
			if (n==0 || (n<0 && errno==EIO)) {
			    /* socket closed */
			} else if (n<0) {
			    /* ignore the error */
			    snprintf(txt, sizeof(txt), "PSID_forwarder:"
				     " read():%s\n", strerror(errno));
			    printMsg(STDERR, txt);
			} else {
			    /* forward it to logger */
			    sendMsg(type, buf, n);
			}
			break;
		    }
		}
	    } // else ret == 0
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
    int n;                       /* number of bytes received */
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

		n = read(sock, buf, sizeof(buf));
		if (verbose) {
		    snprintf(obuf, sizeof(obuf),
			     "PSID_forwarder: got %d bytes on sock %d\n",
			     n, sock);
		    printMsg(STDERR, obuf);
		}
		if (n==0 || (n<0 && errno==EIO)) {
		    /* socket closed */
		    PSID_blockSig(1, SIGCHLD);
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
		    PSID_blockSig(0, SIGCHLD);
		} else if (n<0) {
		    /* ignore the error */
		    snprintf(obuf, sizeof(obuf),
			     "PSID_forwarder: read():%s\n", strerror(errno));
		    printMsg(STDERR, obuf);
		} else {
		    /* forward it to logger */
		    sendMsg(type, buf, n);
		}
		break;
	    }
	}
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
