/*
 *               ParaStation3
 * psilogger.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psilogger.c,v 1.23 2003/02/21 12:41:45 eicker Exp $
 *
 */
/**
 * @file
 * psilogger: Log-daemon for ParaStation I/O forwarding facility
 *
 * $Id: psilogger.c,v 1.23 2003/02/21 12:41:45 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psilogger.c,v 1.23 2003/02/21 12:41:45 eicker Exp $";
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

#include "pscommon.h"
#include "pslog.h"

/**
 * Should source and length of each message be displayed ?  (1=Yes, 0=No)
 *
 * Set in main() to 1 if environment variable PSI_SOURCEPRINTF is defined.
 */
int PrependSource = 0;

/**
 * The rank of the process all input is forwarded to.
 *
 * Set in main() to the value given by the environment variable PSI_INPUTDEST.
 */
int InputDest = 0;

/**
 * Verbosity of Forwarders (1=Yes, 0=No)
 *
 * Set in main() to 1 if environment variable PSI_FORWARDERDEBUG is defined.
 */
int forw_verbose = 0;

/**
 * Verbosity of Logger (1=Yes, 0=No)
 *
 * Set in main() to 1 if environment variable PSI_LOGGERDEBUG is defined.
 */
int verbose = 0;

/**
 * Shall we display usage info (1=Yes, 0=No)
 *
 * Set in main() to 1 if environment variable PSI_USAGE is defined.
 */
int usage = 0;

/** Number of connected forwarders */
int noClients;

/** Set of fds, the logger listens to. This is mainly STDIN and daemonSock. */
fd_set myfds;

/** Array to store the forwarder TIDs indexed by the clients rank. */
long *clientTID;

/** The actual size of #clientTID. */
int maxClients = 64;

/**
 * @brief Send a PSLog message.
 *
 * Send a PSLog message of length @a count referenced by @a buf with
 * type @a type to @a destTID.
 *
 * This is mainly a wrapper for PSLog_write().
 *
 *
 * @param tid ParaStation task ID of the task the message is sent to.
 *
 * @param type Type of the message.
 *
 * @param buf Pointer to the buffer containing the data to send within
 * the body of the message. If @a buf is NULL, the body of the PSLog
 * message will be empty.
 *
 * @param len Amount of meaningfull data within @a buf in bytes. If @a
 * len is larger the 1024, more than one message will be generated.
 * The number of messages can be computed by (len/1024 + 1).
 *
 *
 * @return On success, the number of bytes written is returned,
 * i.e. usually this is @a len. On error, -1 is returned, and errno is
 * set appropriately.
 */
static int sendMsg(long tid, PSLog_msg_t type, char *buf, size_t len)
{
    char txt[128];
    int ret = 0;

    ret = PSLog_write(tid, type, buf, len);

    if (ret < 0) {
	char *errstr = strerror(errno);
	fprintf(stderr, "PSIlogger: sendMsg(): error (%d): %s\n",
		errno, errstr ? errstr : "UNKNOWN");
    }

    return ret;
}

/**
 * @brief Read a PSLog message.
 *
 * Read a PSLog message and store it to @a msg. This function will
 * block until a message is available.
 *
 * This is mainly a wrapper for PSLog_read(). Furthermore PSP_CC_ERROR
 * messages are handled correctly.
 *
 *
 * @param msg Address of a buffer the received message is stored to.
 *
 *
 * @return On success, the number of bytes read are returned. On error,
 * -1 is returned, and errno is set appropriately.
 */
static int recvMsg(PSLog_Msg_t *msg)
{
    char txt[128];
    int ret;

    ret = PSLog_read(msg, NULL);

    if (!ret) return ret;

    if (ret < 0) {
	char *errstr = strerror(errno);
	fprintf(stderr, "PSIlogger: recvMsg(): error (%d): %s\n",
		errno, errstr ? errstr : "UNKNOWN");

	return ret;
    }

    switch (msg->header.type) {
    case PSP_CC_ERROR:
    {
	/* Try to find the corresponding client */
	int i = 0;

	while ((i < maxClients) && (clientTID[i] != msg->header.sender)) i++;

	if (i == maxClients) {
	    fprintf(stderr, "PSIlogger: recvMsg():"
		    " CC_ERROR from unknown task %s.\n",
		    PSC_printTID(msg->header.sender));
	    ret = 0;
	} else {
	    fprintf(stderr, "PSIlogger: recvMsg():"
		    " forwarder %s (rank %d) disappeared.\n",
		    PSC_printTID(msg->header.sender), i);

	    clientTID[i] = -1;
	    noClients--;

	    errno = EPIPE;
	    ret = -1;
	}
	break;
    }
    case PSP_CC_MSG:
	break;
    default:
	fprintf(stderr, "PSIlogger: recvMsg(): Unknown message type %s.\n",
		PSP_printMsg(msg->header.type));

	ret = 0;
    }

    return ret;
}

/**
 * @brief Handle signals.
 *
 * Handle the signal @a sig send to the psilogger. Up to now, this
 * function knowns about the TERM, TTIN, TSTP and CONT signal. The
 * following measures are taken on the various signals:
 *
 * - SIGTERM: Create some logging messages and ignore the signal.
 *
 * - SIGTTIN: Create logging. Don't know about the meaning of this signal.
 *
 * - SIGTSTP: Ignored up to now. Will send SIGSTOP to all childs then SIGSTOP
 *            to myself.
 *
 * - SIGCONT: Ignored up to now. Will send SIGCONT to all childs.
 *
 *
 * @param sig Signal to handle.
 *
 *
 * @return No return value.
 */
void sighandler(int sig)
{
    int i;
    static int firstTERM = 1;

    switch(sig) {
    case SIGTERM:
	if (verbose && firstTERM) {
	    fprintf(stderr, "PSIlogger: Got SIGTERM. Problem with child?\n");
	    firstTERM = 0;
	}
	if (verbose) {
	    fprintf(stderr,
		    "PSIlogger: No of clients: %d open logs:", noClients);
	    for (i=0; i<maxClients; i++)
		if (clientTID[i] != -1)
		    fprintf(stderr, "%d (%s) ", i, PSC_printTID(clientTID[i]));
	    fprintf(stderr, "\b\n");
	}
	break;
    case SIGTTIN:
	if (verbose) {
	    fprintf(stderr, "PSIlogger %d: Got SIGTTIN.\n", getpid());
	    // usleep(200000);
	}
	break;
    case SIGTSTP:
	if (verbose) {
	    fprintf(stderr, "PSIlogger %d: Got SIGTSTP.\n", getpid());
	}
	/* @todo:
	 * Send STOP to childs, then really stop ( kill(getpid(),SIGSTOP); ).
	 */
    case SIGCONT:
	if (verbose) {
	    fprintf(stderr, "PSIlogger %d: Got SIGCONT.\n", getpid());
	}
	/* @todo:
	 * Send CONT to childs.
	 */
    default:
	fprintf(stderr, "PSIlogger %d: Got signal %d.\n", getpid(), sig);
    }

    fflush(stdout);
    fflush(stderr);

    signal(sig, sighandler);
}

/**
 * @brief Handle connection requests from new forwarders.
 *
 * Handles connection requests from new forwarder. In order to
 * connect, the forwarder has sent a INITIALIZE PSLog message. After
 * some testing, if everything is okay, the connection is accepted and
 * a INITIALIZE reply is sent to the forwarder. The new client will be
 * registered within the #clientTID array which was expanded if the
 * actual size was not sufficient.
 *
 *
 * @param msg INITIALIZE message used to contact the logger.
 *
 * @return On success, 1 is returned. On error, 0 is returned, if the
 * error was not fatal. If a fatal error occurred, i.e. if the
 * expansion of #clientTID failed, the logger is ceased using exit().
 */
static int newrequest(PSLog_Msg_t *msg)
{
    int ret=0;

    if (msg->sender >= maxClients) {
	int i;
	clientTID = realloc(clientTID, sizeof(*clientTID) * 2 * msg->sender);
	if (!clientTID) {
	    fprintf(stderr, "PSIlogger: newrequest(): realloc(%d) failed.\n",
		    sizeof(*clientTID) * 2 * msg->sender);
	    exit(1);
	}	    
	for (i=maxClients; i<2*msg->sender; i++) clientTID[i] = -1;
	maxClients = 2*msg->sender;
    }

    if (clientTID[msg->sender] != -1) {
	if (clientTID[msg->sender] == msg->header.sender) {
	    fprintf(stderr, "PSIlogger: newrequest():"
		    " %s (rank %d) already connected.\n",
		    PSC_printTID(msg->header.sender), msg->sender);
	} else {
	    fprintf(stderr, "PSIlogger: newrequest():"
		    " %s (rank %d) wants to connect as %s.\n",
		    PSC_printTID(clientTID[msg->sender]), msg->sender,
		    PSC_printTID(msg->header.sender));
	}
    } else {
	sendMsg(msg->header.sender, INITIALIZE,
		(char *) &forw_verbose, sizeof(forw_verbose));

	clientTID[msg->sender] = msg->header.sender;
	noClients++;
	if (verbose) {
	    fprintf(stderr, "PSIlogger: new connection from %s (%d)\n",
		    PSC_printTID(msg->header.sender), msg->sender);
	}
	ret = 1;
    }

    return ret;
}

/**
 * @brief Checks file table after select has failed.
 *
 * @param openfds Set of file descriptors that have to be checked.
 *
 * @return No return value.
 */
static void CheckFileTable(fd_set* openfds)
{
    fd_set rfds;
    int fd;
    struct timeval tv;
    char* errtxt;

    for (fd=0;fd<FD_SETSIZE;) {
	if (FD_ISSET(fd,openfds)) {
	    memset(&rfds, 0, sizeof(rfds));
	    FD_SET(fd,&rfds);

	    tv.tv_sec=0;
	    tv.tv_usec=0;
	    if (select(FD_SETSIZE, &rfds, (fd_set *)0, (fd_set *)0, &tv) < 0) {
		/* error : check if it is a wrong fd in the table */
		switch(errno) {
		case EBADF :
		    fprintf(stderr,"CheckFileTable(%d):"
			    " EBADF -> close socket\n",fd);
		    close(fd);
		    FD_CLR(fd,openfds);
		    fd++;
		    break;
		case EINTR:
		    fprintf(stderr,"CheckFileTable(%d):"
			    " EINTR -> trying again\n",fd);
		    break;
		case EINVAL:
		    fprintf(stderr,"CheckFileTable(%d):"
			    " EINVAL -> close socket\n", fd);
		    close(fd);
		    FD_CLR(fd,openfds);
		    break;
		case ENOMEM:
		    fprintf(stderr,"CheckFileTable(%d):"
			    " ENOMEM -> close socket\n",fd);
		    close(fd);
		    FD_CLR(fd,openfds);
		    break;
		default:
		    errtxt=strerror(errno);
		    fprintf(stderr, "CheckFileTable(%d):"
			    " unrecognized error (%d):%s\n", fd, errno,
			    errtxt?errtxt:"UNKNOWN errno");
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
 * @brief Forward input to client.
 *
 * Read input data from the file descriptor @a std_in and forward it
 * to the forwarder with ParaStation task ID @a fwTID.
 *
 *
 * @param std_in File descriptor to read STDIN data from.
 *
 * @param fwTID ParaStation task ID of the forwarder to send data to.
 *
 *
 * @return No return value.
 */
static void forwardInput(int std_in, long fwTID)
{
    char buf[1000];
    int len;
    long arg;
    static int count=0;

    len = read(std_in, buf, sizeof(buf)>SSIZE_MAX ? SSIZE_MAX : sizeof(buf));
    switch (len) {
    case -1:
	if (errno != EIO) {
	    char *errstr = strerror(errno);
	    fprintf(stderr, "PSIlogger: read() failed in forwardInput()"
		    " with errno %d: %s",errno, errstr ? errstr : "UNKNOWN");

	    FD_CLR(std_in, &myfds);
	    close(std_in);
	} else {
	    /* ignore */
	}
	break;
    case 0:
	FD_CLR(std_in, &myfds);
	close(std_in);
    default:
	sendMsg(fwTID, STDIN, buf, len);
    }
}

/**
 * @brief The main loop
 *
 * Does all the logging work. All forwarders can connect and log via
 * the logger. Forwarders send I/O data via @ref STDOUT and @ref
 * STDERR messages. Furthermore USAGE and FINALIZE messages from the
 * forwarders are handled.
 *
 * @param daemonSock Socket connected to the local ParaStation daemon
 * to read from.
 *
 * @return No return value.
 */
static void loop(int daemonSock)
{
    int sock;            /* client socket */
    fd_set afds;
    struct timeval mytv={2,0}, atv;
    PSLog_Msg_t msg;
    int n;               /* number of bytes received */
    int timeoutval;
    long forwardInputTID = -1; /* client TID which wants stdin */

    FD_ZERO(&myfds);
    FD_SET(daemonSock, &myfds);

    noClients = 0;
    timeoutval=0;

    /*
     * Loop until there is no connection left. Pay attention to the startup
     * phase, while no connection exists. Thus wait at least 10 * mytv.
     */
    while ( noClients > 0 || timeoutval < 10 ) {
	memcpy(&afds, &myfds, daemonSock + 1);
	atv = mytv;
	if (select(daemonSock + 1, &afds, NULL,NULL,&atv) < 0) {
	    if (errno == EINTR) {
                /* Interrupted syscall, just start again */
                continue;
	    }
	    fprintf(stderr, "PSIlogger: error on select(%d): %s\n", errno,
		    strerror(errno));
	    CheckFileTable(&myfds);
	    continue;
	}
	if ( FD_ISSET(daemonSock, &afds) ) {
	    /* message from the daemon */
	    int ret;
	    int outfd = STDOUT_FILENO;

	    ret = recvMsg(&msg);

	    /* Ignore all errors */
	    if (ret <= 0) continue;

	    if (msg.type == INITIALIZE) {
		if (newrequest(&msg)) {
		    timeoutval = 10;
		    if (msg.sender == InputDest) {
			/* rank InputDest wants the input */
			forwardInputTID = msg.header.sender;
			FD_SET(STDIN_FILENO,&myfds);
			if (verbose) {
			    fprintf(stderr, "PSIlogger: loop():"
				    " forward input to %s (rank %d)\n",
				    PSC_printTID(forwardInputTID), msg.sender);
			}
		    }
		}
	    } else if (msg.sender > maxClients) {
		fprintf(stderr, "PSIlogger: loop():"
			" sender %s (rank %d) out of range.\n",
			PSC_printTID(msg.header.sender), msg.sender);
	    } else if (clientTID[msg.sender] != msg.header.sender) {
		fprintf(stderr, "PSIlogger: loop():"
			" client %s (rank %d) sends as %s.\n",
			PSC_printTID(clientTID[msg.sender]), msg.sender,
			PSC_printTID(msg.header.sender));
	    } else switch(msg.type) {
	    case STDERR:
		outfd = STDERR_FILENO;
	    case STDOUT:
	    {
		if (verbose) {
		    fprintf(stderr, "PSIlogger: Got %d bytes from %s\n",
			    msg.header.len - PSLog_headerSize,
			    PSC_printTID(msg.header.sender));
		}
		if (PrependSource) { 
		    char prefix[30];
		    char *buf = msg.buf;
		    size_t count = msg.header.len - PSLog_headerSize;

		    if (verbose) {
			snprintf(prefix, sizeof(prefix), "[%d, %d]:",
				 msg.sender,
				 msg.header.len - PSLog_headerSize);
		    } else if (count > 0) {
			snprintf(prefix, sizeof(prefix), "[%d]:", msg.sender);
		    }

		    while (count>0) {
			char *nl = memchr(buf, '\n', count);

			if (nl) nl++; /* Thus nl points behind the newline */

			write(outfd, prefix, strlen(prefix));
			write(outfd, buf, nl ? nl - buf : count);

			if (nl) {
			    count -= nl - buf;
			    buf = nl;
			} else {
			    count = 0;
			}
		    }
		} else {
		    write(outfd, msg.buf, msg.header.len - PSLog_headerSize);
		}
		break;
	    }
	    case USAGE:
		if (usage) {
		    struct rusage usage;

		    memcpy(&usage, msg.buf, sizeof(usage));

		    fprintf(stderr, "PSIlogger: Child with rank %d used"
			    " %.6f/%.6f sec (user/sys)\n",
			    msg.sender,
			    usage.ru_utime.tv_sec
			    + usage.ru_utime.tv_usec * 1.0e-6,
			    usage.ru_stime.tv_sec
			    + usage.ru_stime.tv_usec * 1.0e-6);
		}

		break;
	    case FINALIZE:
	    {
		int status = *(int *) msg.buf;

		if (WIFSIGNALED(status)) {
		    fprintf(stderr, "PSIlogger: "
			    "Child with rank %d exited on signal %d.\n",
			    msg.sender, WTERMSIG(status));
		}

		if (WIFEXITED(status)) {
		    if (WEXITSTATUS(status)) {
			fprintf(stderr, "PSIlogger: "
				"Child with rank %d exited with status %d.\n",
				msg.sender, WEXITSTATUS(status));
		    } else if (verbose) {
			fprintf(stderr, "PSIlogger: "
				"Child with rank %d exited normally.\n",
				msg.sender);
		    }
		}

		if (verbose)
		    fprintf(stderr,
			    "PSIlogger: closing %s (rank %d) on FINALIZE\n",
			    PSC_printTID(msg.header.sender), msg.sender);

		PSLog_write(msg.header.sender, EXIT, NULL, 0);

		if (msg.header.sender == forwardInputTID) {
		    /* disable input forwarding */
		    FD_CLR(STDIN_FILENO, &myfds);
		    forwardInputTID = -1;
		}

		clientTID[msg.sender] = -1;
		noClients--;

		break;
	    }
	    default:
		fprintf(stderr, "PSIlogger: loop():"
			" Unknown message type %d!\n", msg.type);
	    }
	} else if (FD_ISSET(STDIN_FILENO, &afds) && (forwardInputTID != -1)) {
	    forwardInput(STDIN_FILENO, forwardInputTID);
	}
	if ( noClients==0 ) {
	    timeoutval++;
	}
    }
    if ( getenv("PSI_NOMSGLOGGERDONE")==NULL ) {
	fprintf(stderr,"\nPSIlogger: done\n");
    }

    return;
}

/**
 * @brief The main program
 *
 * @todo Correct documentation
 *
 * After becoming process group leader, sets global variables @ref
 * verbose, @ref forw_verbose and @ref PrependSource from environment
 * and finally calls loop().
 *
 * @param argc The number of arguments in @a argv.
 * @param argv Array of character strings containing the arguments.
 *
 * This program expects at least 1 additional argument:
 *  -# The port number it will listen to.
 *
 * @return Always returns 0.  */
int main( int argc, char**argv)
{
    int daemonSock, i;
    char *envstr;

    sigset_t set;

    /* block SIGTTIN so logger works also in background */
    sigemptyset(&set);
    sigaddset(&set, SIGTTIN);
    if (sigprocmask(SIG_BLOCK, &set, NULL)) {
	perror("PSIlogger: blockSig(): sigprocmask()");
    }

    signal(SIGTERM, sighandler);
    signal(SIGTSTP, sighandler);
    signal(SIGCONT, sighandler);

    if (argc != 3) {
	fprintf(stderr, "PSIlogger: Sorry, program must be called correctly"
		" inside an application.\n");
	fprintf(stderr, "%d arguments:", argc);
	for (i=0; i<argc; i++)
	    fprintf(stderr, " '%s'", argv[i]);
	fprintf(stderr, "\n");
	exit(1);
    }

    /* daemonSock lost during exec() */
    daemonSock = atol(argv[1]);
    /* ParaStation ID lost during exec() */
    PSC_setMyID(atol(argv[2]));

    if (getenv("PSI_LOGGERDEBUG")) {
	verbose=1;
	fprintf(stderr, "PSIlogger: Going to be verbose.\n");
    }

    if (getenv("PSI_FORWARDERDEBUG")) {
	forw_verbose=1;
	if (verbose) {
	    fprintf(stderr, "PSIlogger: Forwarders will be verbose, too.\n");
	}
    }

    if (getenv("PSI_SOURCEPRINTF")) {
	PrependSource = 1;
	if (verbose) {
	    fprintf(stderr, "PSIlogger: Will print source-info.\n");
	}
    }

    envstr=getenv("PSI_INPUTDEST");
    if (envstr) {
	InputDest = atoi(envstr);
	if (verbose) {
	    fprintf(stderr, "PSIlogger: Input goes to [%d].\n", InputDest);
	}
    }

    if (getenv("PSI_RUSAGE")) {
	usage=1;
	if (verbose) {
	    fprintf(stderr, "PSIlogger: Going to show resource usage.\n");
	}
    }

    PSLog_init(daemonSock, -1, 1);

    clientTID = malloc (sizeof(*clientTID) * maxClients);
    for (i=0; i<maxClients; i++) clientTID[i] = -1;

    /* call the loop which does all the work */
    loop(daemonSock);

    close(daemonSock);

    return 0;
}
