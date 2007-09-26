/*
 *               ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2006 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file
 * psilogger: Log-daemon for ParaStation I/O forwarding facility
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
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
#include <termios.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>

#include "pscommon.h"
#include "pstask.h"
#include "pslog.h"
#include "psiloggerkvs.h"
#include "psiloggermerge.h"

#include "psilogger.h"
#include "timer.h"

/**
 * Shall source and length of each message be displayed ?  (1=Yes, 0=No)
 *
 * Set in main() to 1 if environment variable PSI_SOURCEPRINTF is defined.
 */
int PrependSource = 0;

/**
 * Shall output lines of different ranks be merged ?  (1=Yes, 0=No)
 *
 * Set in main() to 1 if environment variable PSI_MERGEOUTPUT is defined.
 */
int MergeOutput = 0;

/**
 * Parse STDIN for special commands changing the input destination.
 *
 * Set in main() to 1 if environment variable PSI_ENABLE_GDB is defined.
 */
int enableGDB = 0;

/**
 * Number of maximal processes in this job.
 */
int np = 0;

/**
 * The rank of the process all input is forwarded to.
 *
 * Set in main() to the value given by the environment variable
 * PSI_INPUTDEST. Will be set to -1 if no stdin is connected.
 */
int *InputDest;

/**
 * Task ID of the process that get's stdin
 */
PStask_ID_t *forwardInputTID;

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
PStask_ID_t *clientTID;

/** The actual size of #clientTID. */
int maxClients = 64;

/** The socket connecting to the local ParaStation daemon */
int daemonSock;

/** The value which will be returned when the logger stops execution */
int retVal = 0;

/** Flag marking a client got signaled */
int signaled = 0;

/** Enables kvs support in logger */
int enable_kvs = 0;

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

    daemonSock=-1;
    FD_CLR(tmp, &myfds);
    PSLog_close();
    close(tmp);
}

/**
 * @brief Parse the string @ref input were STDIN should be
 * forwarded and save result in @ref InputDest.
 *
 * @param input The input string to parse.
 *
 * @return No return value.
 */
static void setupInputDestList(char *input)
{
    const char delimiters[] ="[], \n";
    char *ranks, *saveptr, *sep;
    int first, last, i;

    for (i=0; i<maxClients; i++) InputDest[i] = -1;

    if (!input) {
	InputDest[0] = 0;
	return;
    }
    
    
    ranks = strtok_r(input,delimiters,&saveptr);
    
    if (!strcmp(ranks, "all")) {
	for(i=0; i<np; i++) {
	    InputDest[i] = i;
	}
	return;
    }
   
    while (ranks != NULL) {
	if ((sep = strchr(ranks, '-'))) {
	    first = last = 0;
	    if ((sscanf(ranks, "%d-%d", &first, &last)) != 2) {
		fprintf(stderr, "PSIlogger: invalid range for input redirection\n");
		return;
	    }
	    for(i=first; i<=last; i++) {
		if (i >= np) {
		    fprintf(stderr, "PSIlogger: input forward to non existing rank:[%d], valid ranks:[0-%i]\n", i, np -1);
		    exit(1);
		}
		InputDest[i] = i;
	    }
	} else {
	    i = atoi(ranks);
	    if (i >= np) {
		fprintf(stderr, "PSIlogger: input forward to non existing rank:[%d], valid ranks:[0-%i]\n", i, np -1);
		exit(1);
	    }
	    InputDest[i] = i;
	}	
	ranks = strtok_r(NULL,delimiters,&saveptr);
    }
}

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
int sendMsg(PStask_ID_t tid, PSLog_msg_t type, char *buf, size_t len)
{
    int ret = 0;

    if (daemonSock < 0) {
	errno = EBADF;
	return -1;
    }

    ret = PSLog_write(tid, type, buf, len);

    if (ret < 0) {
	char *errstr = strerror(errno);
	fprintf(stderr, "PSIlogger: %s: error (%d): %s\n", __func__,
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
    int ret;

    if (daemonSock < 0) {
	errno = EBADF;
	return -1;
    }

    ret = PSLog_read(msg, NULL);

    if (!ret) return ret;

    if (ret < 0) {
	char *errstr = strerror(errno);
	fprintf(stderr, "PSIlogger: %s: error (%d): %s\n", __func__,
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
	    fprintf(stderr, "PSIlogger: %s: CC_ERROR from unknown task %s.\n",
		    __func__, PSC_printTID(msg->header.sender));
	    errno = EBADMSG;
	    ret = -1;
	} else {
	    fprintf(stderr,
		    "PSIlogger: %s: forwarder %s (rank %d) disappeared.\n",
		    __func__, PSC_printTID(msg->header.sender), i);

	    clientTID[i] = -1;
	    noClients--;

	    errno = EPIPE;
	    ret = -1;
	}
	break;
    }
    case PSP_CC_MSG:
    case PSP_CD_ERROR:
	/* Ignore */
	break;
    default:
	fprintf(stderr, "PSIlogger: %s: Unknown message type %s from %s.\n",
		__func__, PSP_printMsg(msg->header.type),
		PSC_printTID(msg->header.sender));

	errno = EPROTO;
	ret = -1;
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
 * @param daemonSock Socket connected with the local daemon.
 *
 * @return On success, the number of bytes send is returned,
 * i.e. usually @a msg->header.len. Otherwise -1 is returned and errno
 * is set appropriately.
 */
static int sendDaemonMsg(DDSignalMsg_t *msg)
{
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
	fprintf(stderr, "PSIlogger: %s: error (%d): %s\n", __func__,
		errno, errstr ? errstr : "UNKNOWN");

        closeDaemonSock();

	return n;
    } else if (!n) {
	fprintf(stderr, "PSIlogger: %s(): Daemon connection lost\n", __func__);

        closeDaemonSock();

	return n;
    }

    if (verbose) {
        fprintf(stderr, "PSIlogger: %s type %s (len=%d) to %s\n", __func__,
		PSP_printMsg(msg->header.type), msg->header.len,
		PSC_printTID(msg->header.dest));
    }

    return msg->header.len;
}

/**
 * @brief Callback functions to handle barrier timeouts.
 * Terminate the job, send all children term signal.
 *
 * @return No return value.
 */
void handleBarrierTimeout(void)
{
    fprintf(stderr,
		"PSIlogger: Timeout: Not all clients joined the pmi barrier, terminating.\n");
    
    {
	DDSignalMsg_t msg;

	msg.header.type = PSP_CD_SIGNAL;
	msg.header.sender = PSC_getMyTID();
	msg.header.dest = PSC_getMyTID();
	msg.header.len = sizeof(msg);
	msg.signal = SIGTERM;
	msg.param = getuid();
	msg.pervasive = 1;
	msg.answer = 0;

	sendDaemonMsg(&msg);
    }
}

/**
 * @brief Handle signals.
 *
 * Handle the signal @a sig sent to the psilogger.
 *
 * @param sig Signal to handle.
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
	{
	    DDSignalMsg_t msg;

	    msg.header.type = PSP_CD_SIGNAL;
	    msg.header.sender = PSC_getMyTID();
	    msg.header.dest = PSC_getMyTID();
	    msg.header.len = sizeof(msg);
	    msg.signal = sig;
	    msg.param = getuid();
	    msg.pervasive = 1;
	    msg.answer = 0;

	    sendDaemonMsg(&msg);
	}
	break;
    case SIGINT:
	if (verbose) {
	    fprintf(stderr, "PSIlogger: Got SIGINT.\n");
	}
	{
	    DDSignalMsg_t msg;

	    msg.header.type = PSP_CD_SIGNAL;
	    msg.header.sender = PSC_getMyTID();
	    msg.header.dest = PSC_getMyTID();
	    msg.header.len = sizeof(msg);
	    msg.signal = sig;
	    msg.param = getuid();
	    msg.pervasive = 1;
	    msg.answer = 0;

	    sendDaemonMsg(&msg);
	}
	break;
    case SIGTTIN:
	if (verbose) {
	    fprintf(stderr, "PSIlogger: Got SIGTTIN.\n");
	    // usleep(200000);
	}
	break;
    case SIGWINCH:
	for (i=0; i < maxClients; i++) {
	    if (forwardInputTID[i] != -1) {
		/* Create WINCH-messages and send */
		struct winsize ws;
		int buf[4];
		int len = 0;

		if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) < 0) break;
		buf[len++] = ws.ws_col;
		buf[len++] = ws.ws_row;
		buf[len++] = ws.ws_xpixel;
		buf[len++] = ws.ws_ypixel;

		sendMsg(forwardInputTID[i], WINCH, (char *)buf, len*sizeof(*buf));

		if (verbose) {
		    fprintf(stderr, "PSIlogger: %s: WINCH to col %d row %d",
			    __func__, ws.ws_col, ws.ws_row);
		    fprintf(stderr, " xpixel %d ypixel %d\n",
			    ws.ws_xpixel, ws.ws_ypixel);
		}
	    }
	}
	break;
    case SIGHUP:
    case SIGTSTP:
    case SIGCONT:
    case SIGUSR1:
    case SIGUSR2:
    case SIGQUIT:
	if (verbose) {
	    fprintf(stderr, "PSIlogger: Got signal %d.\n", sig);
	}

	{
	    DDSignalMsg_t msg;

	    msg.header.type = PSP_CD_SIGNAL;
	    msg.header.sender = PSC_getMyTID();
	    msg.header.dest = PSC_getMyTID();
	    msg.header.len = sizeof(msg);
	    msg.signal = (sig==SIGTSTP) ? SIGSTOP : sig;
	    msg.param = getuid();
	    msg.pervasive = 1;
	    msg.answer = 0;

	    sendDaemonMsg(&msg);
	}
	if (sig == SIGTSTP) raise(SIGSTOP);
	break;
    default:
	fprintf(stderr, "PSIlogger: Got signal %d.\n", sig);
    }

    fflush(stdout);
    fflush(stderr);

    signal(sig, sighandler);
}

/* Raw mode handling. raw mode is needed for correct functioning of pssh */
static struct termios _saved_tio;
static int _in_raw_mode = 0;

void leave_raw_mode(void)
{
    if (!_in_raw_mode)
	return;
    if (tcsetattr(STDIN_FILENO, TCSADRAIN, &_saved_tio) == -1)
	perror("tcsetattr");
    else
	_in_raw_mode = 0;
}

void enter_raw_mode(void)
{
    struct termios termios;

    if (tcgetattr(STDIN_FILENO, &termios) == -1) {
	perror("tcgetattr()");
	return;
    }
    _saved_tio = termios;
    termios.c_iflag |= IGNPAR;
    termios.c_iflag &= ~(ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXANY | IXOFF);
#ifdef IUCLC
    termios.c_iflag &= ~IUCLC;
#else
#error no IUCLC
#endif
    termios.c_lflag &= ~(ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHONL);
#ifdef IEXTEN
    termios.c_lflag &= ~IEXTEN;
#else
#error no IEXTEN
#endif
    termios.c_oflag &= ~OPOST;
    termios.c_cc[VMIN] = 1;
    termios.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSADRAIN, &termios) == -1) {
	perror("tcsetattr");
    } else
	_in_raw_mode = 1;
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
	forwardInputTID = realloc(forwardInputTID, sizeof(*forwardInputTID) * 2 * msg->sender);
	InputDest = realloc(InputDest, sizeof(*InputDest) * 2 * msg->sender);

	if (!clientTID || !forwardInputTID || !InputDest) {
	    fprintf(stderr, "PSIlogger: %s: realloc(%ld) failed.\n", __func__,
		    (long) sizeof(*clientTID) * 2 * msg->sender);
	    exit(1);
	}	    
	for (i=maxClients; i<2*msg->sender; i++) clientTID[i] = -1;
	for (i=maxClients; i<2*msg->sender; i++) forwardInputTID[i] = -1;
	for (i=maxClients; i<2*msg->sender; i++) InputDest[i] = -1;

	/* realloc output buffer */
	if (MergeOutput) reallocClientOutBuf(msg);		

	maxClients = 2*msg->sender;
    }

    if (clientTID[msg->sender] != -1) {
	if (clientTID[msg->sender] == msg->header.sender) {
	    fprintf(stderr, "PSIlogger: %s: %s (rank %d) already connected.\n",
		    __func__, PSC_printTID(msg->header.sender), msg->sender);
	} else {
	    fprintf(stderr, "PSIlogger: %s: %s (rank %d)",
		    __func__, PSC_printTID(clientTID[msg->sender]),
		    msg->sender);
	    fprintf(stderr, " connects as %s.\n",
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
    char* errstr;

    for (fd=0;fd<FD_SETSIZE;) {
	if (FD_ISSET(fd,openfds)) {
	    memset(&rfds, 0, sizeof(rfds));
	    FD_SET(fd,&rfds);

	    tv.tv_sec=0;
	    tv.tv_usec=0;
	    if (select(FD_SETSIZE, &rfds, (fd_set *)0, (fd_set *)0, &tv) < 0) {
		/* error : check if it is a wrong fd in the table */
		fprintf(stderr,"%s(%d): ", __func__, fd);
		switch(errno) {
		case EBADF :
		    fprintf(stderr,"EBADF -> close socket\n");
		    close(fd);
		    FD_CLR(fd,openfds);
		    fd++;
		    break;
		case EINTR:
		    fprintf(stderr,"EINTR -> trying again\n");
		    break;
		case EINVAL:
		    fprintf(stderr,"EINVAL -> close socket\n");
		    close(fd);
		    FD_CLR(fd,openfds);
		    break;
		case ENOMEM:
		    fprintf(stderr,"ENOMEM -> close socket\n");
		    close(fd);
		    FD_CLR(fd,openfds);
		    break;
		default:
		    errstr=strerror(errno);
		    fprintf(stderr, "unrecognized error (%d): %s\n",
			    errno, errstr ? errstr : "UNKNOWN");
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
 * to the forwarder(s) with ParaStation task IDs in forwardInputTID.
 *
 * @param std_in File descriptor to read STDIN data from.
 *
 *
 * @return No return value.
 */
static void forwardInput(int std_in)
{
    char buf[1000];
    int len, i;
    
    len = read(std_in, buf, sizeof(buf)>SSIZE_MAX ? SSIZE_MAX : sizeof(buf));
    switch (len) {
    case -1:
	if (errno != EIO) {
	    char *errstr = strerror(errno);
	    fprintf(stderr, "PSIlogger: %s: read() failed with errno %d: %s",
		    __func__, errno, errstr ? errstr : "UNKNOWN");

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
	if (enableGDB) {
	    if (buf[0] == '[' && buf[len -2] == ']') {
		fprintf(stderr, "Changed input dest to: %s",buf);
		setupInputDestList(buf);
		return;
	    }
	}
	for (i=0; i<maxClients; i++) {
	    if (InputDest[i] != -1 && forwardInputTID[i] != -1) {
		sendMsg(forwardInputTID[i], STDIN, buf, len);
	    }
	}
	if (verbose) {
	    fprintf(stderr, "PSIlogger: %s: %d bytes\n", __func__, len);
	}
    }
}

/**
 * @brief Handle USAGE msg from forwarder.
 *
 * @param msg The received msg to handle.
 *
 * @return No return value.
 */
static void handleUSAGEMsg(PSLog_Msg_t msg)
{
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
}

/**
 * @brief Handle STDOUT/STDERR msgs from forwarder. 
 *
 * @param msg The received msg to handle.
 *
 * @param outfd The file descriptor to write the output
 * to.
 *
 * @return No return value.
 */
static void handleSTDOUTMsg(PSLog_Msg_t msg, int outfd)
{
    int ret;

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

	    ret = write(outfd, prefix, strlen(prefix));
	    ret = write(outfd, buf, nl ? (size_t)(nl - buf):count);

	    if (nl) {
		count -= nl - buf;
		buf = nl;
	    } else {
		count = 0;
	    }
	}
    } else {
	if (MergeOutput && np > 1) {
	    /* collect all ouput */ 
	    cacheOutput(msg, outfd);
	} else {
	    ret = write(outfd, msg.buf,
		    msg.header.len-PSLog_headerSize);
	}
    }
}

/**
 * @brief Handle FINALIZE msg from forwarder. 
 *
 * @param msg The received msg to handle.
 *
 * @return No return value.
 */
static void handleFINALIZEMsg(PSLog_Msg_t msg)
{
    int i;
    leave_raw_mode();
    if (getenv("PSI_SSH_COMPAT_HOST")) {
	char *host = getenv("PSI_SSH_COMPAT_HOST");
	int status = *(int *) msg.buf;

	if (WIFSIGNALED(status)) retVal = -1;
	if (WIFEXITED(status)) retVal = WEXITSTATUS(status);

	if (getenv("PSI_SSH_INTERACTIVE"))
	    fprintf(stderr, "Connection to %s closed.\n", host);
    } else {
	int status = *(int *) msg.buf;

	if (WIFSIGNALED(status)) {
	    fprintf(stderr, "PSIlogger: Child with rank %d"
		    " exited on signal %d", msg.sender,
		    WTERMSIG(status));
	    psignal(WTERMSIG(status), "");
	    signaled = 1;
	}

	if (WIFEXITED(status)) {
	    if (WEXITSTATUS(status)) {
		fprintf(stderr, "PSIlogger: Child with rank %d"
			" exited with status %d.\n", msg.sender,
			WEXITSTATUS(status));
		if (!retVal) retVal = WEXITSTATUS(status);
	    } else if (verbose) {
		fprintf(stderr, "PSIlogger: Child with rank %d"
			" exited normally.\n", msg.sender);
	    }
	}
    }

    if (verbose)
	fprintf(stderr, "PSIlogger: closing %s (rank %d) on FINALIZE\n",
		PSC_printTID(msg.header.sender), msg.sender);

    PSLog_write(msg.header.sender, EXIT, NULL, 0);

    for (i=0; i < maxClients; i++) {
	if (msg.header.sender == forwardInputTID[i]) {
	    /* disable input forwarding */
	    //FD_CLR(STDIN_FILENO, &myfds);
	    forwardInputTID[i] = -1;
	}
    }
    clientTID[msg.sender] = -1;
    noClients--;
}

/**
 * @brief Handle STOP msg from forwarder.
 *
 * @param msg The received msg to handle.
 *
 * @return No return value.
 */
static void handleSTOPMsg(PSLog_Msg_t msg)
{
    int i;

    for (i=0; i<maxClients; i++) {
	if (msg.sender == InputDest[i]) {
	    /* rank InputDest wants pause */
	    FD_CLR(STDIN_FILENO,&myfds);
	    if (verbose) {
		fprintf(stderr, "PSIlogger: forward input is paused\n");
	    }
	    return;
	} 
    }
    fprintf(stderr, "PSIlogger: STOP from wrong rank: %d\n", msg.sender);
}

/**
 * @brief Handle CONT msg from forwarder.
 *
 * @param msg The received msg to handle.
 *
 * @return No return value.
 */
static void handleCONTMsg(PSLog_Msg_t msg)
{
    int i;

    for (i=0; i<maxClients; i++) {
	if (msg.sender == InputDest[i]
	    && forwardInputTID[msg.sender] == msg.header.sender) {
	    /* rank InputDest wants the input again */
	    FD_SET(STDIN_FILENO,&myfds);
	    if (verbose) {
		fprintf(stderr, "PSIlogger: forward input continues\n");
	    }
	    return;
	} 
    }
    fprintf(stderr, "PSIlogger: CONT from wrong rank: %d\n", msg.sender);
}

/**
 * @brief Handle KVS msg from forwarder.
 *
 * @param msg The received msg to handle.
 *
 * @return No return value.
 */
static void handleKVSMsg(PSLog_Msg_t msg)
{
    if (enable_kvs) {
	handleKvsMsg(msg);
    } else {
	/* return kvs disabled msg */
	sendMsg(msg.header.sender, KVS, "cmd=kvs_not_available\n", 
	    strlen("cmd=kvs_not_available\n"));
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
static void loop(void)
{
    struct timeval mytv={1,0}, atv;
    PSLog_Msg_t msg;
    int timeoutval;

    FD_ZERO(&myfds);
    FD_SET(daemonSock, &myfds);

    noClients = 0;
    timeoutval=0;

    /*
     * Loop until there is no connection left. Pay attention to the startup
     * phase, while no connection exists. Thus wait at least 10 * mytv.
     */
    while ( noClients > 0 || timeoutval < 10 ) {
	fd_set afds;
	memcpy(&afds, &myfds, sizeof(afds));
	atv = mytv;
	Timer_handleSignals();
	if (MergeOutput && np >1) displayCachedOutput(0);
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
	    if (ret < 0) continue;
	    if (!ret) {
		fprintf(stderr, "PSIlogger: daemon died. Exiting\n");
		exit(1);
	    }

	    if (msg.type == INITIALIZE) {
		if (newrequest(&msg)) {
		    int i;
		    timeoutval = 10;
		    forwardInputTID[msg.sender] = msg.header.sender;
		    for (i=0; i<maxClients; i++) {
			if (msg.sender == InputDest[i]) {
			    /* rank InputDest wants the input */
			    FD_SET(STDIN_FILENO, &myfds);
			    break;
			}
		    }	
		}
	    } else if (msg.sender > maxClients) {
		fprintf(stderr, "PSIlogger: %s:"
			" sender %s (rank %d) out of range.\n", __func__,
			PSC_printTID(msg.header.sender), msg.sender);
	    } else if (clientTID[msg.sender] != msg.header.sender) {
		fprintf(stderr, "PSIlogger: %s:"
			" client %s (rank %d) sends as %s.\n", __func__,
			PSC_printTID(clientTID[msg.sender]), msg.sender,
			PSC_printTID(msg.header.sender));
	    } else switch(msg.type) {
	    case STDERR:
		outfd = STDERR_FILENO;
	    case STDOUT:
		handleSTDOUTMsg(msg, outfd);
		break;
	    case USAGE:
		handleUSAGEMsg(msg);
		break;
	    case FINALIZE:
		handleFINALIZEMsg(msg);
		break;
	    case STOP:
		handleSTOPMsg(msg);
		break;
	    case CONT:
		handleCONTMsg(msg);
		break;
	    case KVS:
		handleKVSMsg(msg);
		break;
	    default:
		fprintf(stderr, "PSIlogger: %s: Unknown message type %d!\n",
			__func__, msg.type);
	    }
	} else if (FD_ISSET(STDIN_FILENO, &afds)) {
	    forwardInput(STDIN_FILENO);
	}
	if ( noClients==0 ) {
	    timeoutval++;
	}
    }
    if (MergeOutput && np >1) {
	displayCachedOutput(0);
	displayCachedOutput(1);
    }
    if ( getenv("PSI_NOMSGLOGGERDONE")==NULL ) {
	fprintf(stderr,"\nPSIlogger: done\n");
    }

    return;
}

/**
 * @brief The main program
 *
 * After registering several signals gets global variables @ref
 * daemonSock and local ParaStation ID from arguments. Furthermore the
 * global variables @ref verbose, @ref forw_verbose, @ref
 * PrependSource @ref InputDest and * @ref usage are set from
 * environment. After setting up logging finally @ref loop() is
 * called. Upon return from this central loop, all further arguments
 * to the two allready evaluated above are executed via system()
 * calls.
 *
 * @param argc The number of arguments in @a argv.
 *
 * @param argv Array of character strings containing the arguments.
 *
 * This program expects at least 2 additional argument:
 *
 * -# The file descriptor number of the socket connected to the local
 * ParaStation daemon.
 *
 * -# The local ParaStation ID.
 *
 * All further arguments will be executed via system() calls.
 *
 * @return Always returns 0.  */
int main( int argc, char**argv)
{
    char *envstr, *end, *input;
    int i;

    sigset_t set;

    /* block SIGTTIN so logger works also in background */
    sigemptyset(&set);
    sigaddset(&set, SIGTTIN);
    if (sigprocmask(SIG_BLOCK, &set, NULL)) {
	perror("PSIlogger: blockSig(): sigprocmask()");
    }

    signal(SIGTERM,  sighandler);
    signal(SIGINT,   sighandler);
//    signal(SIGTTIN,  sighandler);
    signal(SIGHUP,   sighandler);
    signal(SIGTSTP,  sighandler);
    signal(SIGCONT,  sighandler);
    signal(SIGWINCH, sighandler);
    signal(SIGUSR1,  sighandler);
    signal(SIGUSR2,  sighandler);
    signal(SIGQUIT,  sighandler);

    if (argc < 3) {
	fprintf(stderr, "PSIlogger: Sorry, program must be called correctly"
		" inside an application.\n");
	fprintf(stderr, "%d arguments:", argc);
	for (i=0; i<argc; i++)
	    fprintf(stderr, " '%s'", argv[i]);
	fprintf(stderr, "\n");

	exit(1);
    }

    /* daemonSock lost during exec() */
    daemonSock = strtol(argv[1], &end, 10);
    if (*end != '\0' || (daemonSock==0 && errno==EINVAL)) {
	fprintf(stderr, "PSIlogger: Sorry, program must be called correctly"
		" inside an application.\n");
	fprintf(stderr, "PSIlogger: '%s' is not a socket number.\n", argv[1]);

	exit(1);
    }

    /* ParaStation ID lost during exec() */
    i = strtol(argv[2], &end, 10);
    if (*end != '\0' || (i==0 && errno==EINVAL)) {
	fprintf(stderr, "PSIlogger: Sorry, program must be called correctly"
		" inside an application.\n");
	fprintf(stderr, "PSIlogger: '%s' is not a ParaStation ID.\n", argv[2]);

	exit(1);
    }
    PSC_setMyID(i);

    if (getenv("PSI_LOGGERDEBUG")) {
	verbose=1;
	fprintf(stderr, "PSIlogger: Going to be verbose.\n");
	fprintf(stderr, "PSIlogger: Daemon on %d\n", daemonSock);
	fprintf(stderr, "PSIlogger: My ID is %d\n", i);
    }

    if (getenv("PSI_FORWARDERDEBUG")) {
	forw_verbose=1;
	if (verbose) {
	    fprintf(stderr, "PSIlogger: Forwarders will be verbose, too.\n");
	}
    }
    
    if (getenv("PSI_ENABLE_GDB")) {
	enableGDB = 1;
	if (verbose) {
	    fprintf(stderr, "PSIlogger: Enabling gdb functions.\n");
	}
    }

    if (getenv("PSI_SOURCEPRINTF")) {
	PrependSource = 1;
	if (verbose) {
	    fprintf(stderr, "PSIlogger: Will print source-info.\n");
	}
    }

    if ((envstr = getenv("PSI_NP_INFO"))) {
	np = atoi(envstr);
    }

    if (getenv("PSI_MERGEOUTPUT")) {
	MergeOutput  = 1;
	if (verbose) {
	    fprintf(stderr,
		    "PSIlogger: Will merge the output of all ranks.\n");
	}
	outputMergeInit();
    }
    
    clientTID = malloc (sizeof(*clientTID) * maxClients);
    for (i=0; i<maxClients; i++) clientTID[i] = -1;
    
    forwardInputTID = malloc (sizeof(*forwardInputTID) * maxClients);
    for (i=0; i<maxClients; i++) forwardInputTID[i] = -1;
    
    InputDest = malloc (sizeof(*InputDest) * maxClients);

    if (!clientTID || !forwardInputTID || !InputDest) {
	fprintf(stderr, "PSIlogger: Out of memory.\n");
	exit(1);
    }

    if ((input = getenv("PSI_INPUTDEST"))) {
	if (daemonSock && verbose) {
	    fprintf(stderr, "PSIlogger: Input goes to [%s].\n", input);
	}
	setupInputDestList(input);
    } else {
	InputDest[0] = 0;
    }

    if (daemonSock) {
	if (verbose) {
	    if (!input) {
		fprintf(stderr, "PSIlogger: Input goes to [0].\n");
	    }	
	}
    } else {
	/* daemonSock = 0, this means there is no stdin connected */
	if (verbose) {
	    fprintf(stderr, "PSIlogger: No stdin available.\n");
	}
	/* Never ever forward input */
	for (i=0; i<maxClients; i++) {
	    InputDest[i] = -1;
	}
    }

    if (getenv("PSI_RUSAGE")) {
	usage=1;
	if (verbose) {
	    fprintf(stderr, "PSIlogger: Going to show resource usage.\n");
	}
    }

    PSLog_init(daemonSock, -1, 1);

    /* init the timer structure */
    if (!Timer_isInitialized()) {
	Timer_init(stderr);
    }

    if (getenv("PSI_LOGGER_RAW_MODE") && isatty(STDIN_FILENO)) {
	if (verbose) {
	    fprintf(stderr, "PSIlogger: Entering raw-mode\n");
	}
	enter_raw_mode();
    }

    /* enable kvs and pmi */
    if ((envstr = getenv("KVS_ENABLE"))) {
	enable_kvs = 1;
	initLoggerKvs(verbose);
    }

    /* call the loop which does all the work */
    loop();

    leave_raw_mode();

    closeDaemonSock();

    for (i=3; i<argc; i++) {
	int ret;
	if (verbose) fprintf(stderr, "Execute '%s'\n", argv[i]);
	ret = system(argv[i]);
    }

    return retVal ? retVal : (signaled ? -1 : 0);
}
