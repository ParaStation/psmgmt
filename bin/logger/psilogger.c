/*
 *               ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2011 ParTec Cluster Competence Center GmbH, Munich
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
static char vcid[] __attribute__((used)) =
    "$Id$";
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
#include <readline/readline.h>
#include <readline/history.h>
#include <limits.h>

#include "pscommon.h"
#include "pstask.h"
#include "psprotocol.h"
#include "pslog.h"
#include "psiloggerkvs.h"
#include "psiloggermerge.h"
#include "psiloggerclient.h"

#include "psilogger.h"
#include "timer.h"

/**
 * Shall source and length of each message be displayed ?  (1=Yes, 0=No)
 *
 * Set in main() to 1 if environment variable PSI_SOURCEPRINTF is defined.
 */
static int prependSource = 0;

/**
 * Shall output lines of different ranks be merged ?  (1=Yes, 0=No)
 *
 * Set in main() to 1 if environment variable PSI_MERGEOUTPUT is defined.
 */
static int mergeOutput = 0;

/**
 * Parse STDIN for special commands changing the input destination.
 *
 * Set in main() to 1 if environment variable PSI_ENABLE_GDB is defined.
 */
int enableGDB = 0;

/**
 * Maximum number of processes in this job.
 */
int np = 0;

/**
 * Maximum number of service-processes in this job.
 */
int numService = 0;

/**
 * Verbosity of Forwarders (1=Yes, 0=No)
 *
 * Set in main() to 1 if environment variable PSI_FORWARDERDEBUG is defined.
 */
int forw_verbose = 0;

/**
 * Shall we display usage info (1=Yes, 0=No)
 *
 * Set in main() to 1 if environment variable PSI_USAGE is defined.
 */
static int showUsage = 0;

/** Number of maximum connected forwarders during runtime */
int maxConnected = 0;

/** Set of fds, the logger listens to. This is mainly STDIN and daemonSock */
static fd_set myfds;

/** The socket connecting to the local ParaStation daemon */
static int daemonSock;

/** Maximum runtime of the job allowed by user */
static long maxTime = 0;

/** Timer used to control maximum run-time */
static int maxTimeID = -1;

/** The value which will be returned when the logger stops execution */
int retVal = 0;

/** Flag marking a client got signaled */
int signaled = 0;

/** Enables kvs support in logger */
int enable_kvs = 0;

logger_t *PSIlog_stdoutLogger = NULL;
logger_t *PSIlog_stderrLogger = NULL;
logger_t *PSIlog_logger = NULL;

/* Wrapper functions for logging */
void PSIlog_initLogs(FILE *logfile)
{
    PSIlog_logger = logger_init("PSIlogger", logfile ? logfile : stderr);
    if (!PSIlog_logger) {
	fprintf(logfile ? logfile : stderr, "Failed to initialize logger\n");
	exit(1);
    }

    PSIlog_stdoutLogger = logger_init(NULL, stdout);
    if (!PSIlog_stdoutLogger) {
	PSIlog_exit(errno, "%s: Failed to initialize stdout", __func__);
    }

    PSIlog_stderrLogger = logger_init(NULL, stderr);
    if (!PSIlog_stderrLogger) {
	PSIlog_exit(errno, "%s: Failed to initialize stderr", __func__);
    }
}

int32_t PSIlog_getDebugMask(void)
{
    return logger_getMask(PSIlog_logger);
}

void PSIlog_setDebugMask(int32_t mask)
{
    logger_setMask(PSIlog_logger, mask);
}

char PSIlog_getTimeFlag(void)
{
    return logger_getTimeFlag(PSIlog_logger);
}

void PSIlog_setTimeFlag(char flag)
{
    logger_setTimeFlag(PSIlog_logger, flag);
    logger_setTimeFlag(PSIlog_stdoutLogger, flag);
    logger_setTimeFlag(PSIlog_stderrLogger, flag);
}

void PSIlog_finalizeLogs(void)
{
    logger_finalize(PSIlog_logger);
    PSIlog_logger = NULL;
    logger_finalize(PSIlog_stdoutLogger);
    PSIlog_stdoutLogger = NULL;
    logger_finalize(PSIlog_stderrLogger);
    PSIlog_stderrLogger = NULL;
}

/**
 * If STDIN get's closed, don't re-add it to the fd-set. This stores
 * our STDIN-fd.
 */
static int unavailSTDIN = -1;

void addToFDSet(int fd)
{
    if (fd == unavailSTDIN) return;
    FD_SET(fd, &myfds);
}

void remFromFDSet(int fd)
{
    FD_CLR(fd, &myfds);
}

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
    remFromFDSet(tmp);
    PSLog_close();
    close(tmp);
}

int GDBcmdEcho = 0;

char GDBprompt[128];


/**
 * @brief This function will be called from
 * readline if the gdb debugging mode is enabled
 *
 * @param line The line read from STDIN.
 *
 * @return No return value.
 */
static void readGDBInput(char *line)
{
    char buf[1000];
    size_t len;

    if (!line) return;

    /* add the newline again */
    snprintf(buf, sizeof(buf), "%s\n", line);

    /*add to history */
    HIST_ENTRY *last = history_get(history_length);
    if (line && line[0] != '\0' && (!last || strcmp(last->line, line))) {
	add_history(line);
    }

    /* check for input changing cmd */
    len = strlen(buf);
    if (len > 2 && buf[0] == '[' && buf[len -2] == ']') {
	/* remove trailing garbage */
	buf[len-1] = '\0';

	PSIlog_log(-1, "Changing input dest to: %s", buf);

	setupDestList(buf);
	PSIlog_log(-1, " -> [%s]\n", getDestStr(128));

	/* modify readline's prompt */
	snprintf(GDBprompt, sizeof(GDBprompt), "[%s]: (gdb) ", getDestStr(128));
	rl_set_prompt(GDBprompt);
	return;
    }

    forwardInputStr(buf, len);
    /* Expect command's echo */
    GDBcmdEcho = 1;
    rl_set_prompt("");

    PSIlog_log(PSILOG_LOG_VERB, "%s: %zd bytes\n", __func__, len);
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
	PSIlog_warn(-1, errno, "%s: error (%d)", __func__, errno);
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
	PSIlog_warn(-1, errno, "%s: error (%d)", __func__, errno);
	return ret;
    }

    switch (msg->header.type) {
    case PSP_CC_ERROR:
    {
	/* Try to find the corresponding client */
	int rank = getClientRank(msg->header.sender);

	if (rank > getMaxRank()) {
	    PSIlog_log(PSILOG_LOG_VERB, "%s: CC_ERROR from unknown task %s.\n",
		       __func__, PSC_printTID(msg->header.sender));
	    errno = EBADMSG;
	    ret = -1;
	} else {
	    PSIlog_log(-1, "%s: forwarder %s (rank %d) disappeared.\n",
		       __func__, PSC_printTID(msg->header.sender), rank);

	    deregisterClient(rank);

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
	PSIlog_log(-1, "%s: Unknown message type %s from %s.\n",
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
static int sendDaemonMsg(DDMsg_t *msg)
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
	PSIlog_exit(errno, "%s: send()", __func__);
    } else if (!n) {
	PSIlog_log(-1, "%s(): Daemon connection lost\n", __func__);
	PSIlog_finalizeLogs();
	exit(1);
    }

    PSIlog_log(PSILOG_LOG_VERB, "%s type %s (len=%d) to %s\n", __func__,
	       PSP_printMsg(msg->type), msg->len, PSC_printTID(msg->dest));

    return msg->len;
}

void terminateJob(void)
{
    PSIlog_log(-1, "Terminating the job.\n");

    /* send kill signal to all children */
    {
	DDSignalMsg_t msg;

	msg.header.type = PSP_CD_SIGNAL;
	msg.header.sender = PSC_getMyTID();
	msg.header.dest = PSC_getMyTID();
	msg.header.len = sizeof(msg);
	msg.signal = -1;
	msg.param = getuid();
	msg.pervasive = 1;
	msg.answer = 0;

	sendDaemonMsg((DDMsg_t *)&msg);
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
	if (firstTERM) {
	    PSIlog_log(PSILOG_LOG_VERB, "Got SIGTERM. Problem with child?\n");
	    firstTERM = 0;
	}
	PSIlog_log(PSILOG_LOG_VERB,
		   "No of clients: %d open logs:", getNoClients());
	for (i=getMinRank(); i<=getMaxRank(); i++) {
	    PStask_ID_t tid = getClientTID(i);
	    if (tid == -1) continue;
	    PSIlog_log(PSILOG_LOG_VERB, "%d (%s) ", i, PSC_printTID(tid));
	}
	PSIlog_log(PSILOG_LOG_VERB, "\b\n");
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

	    sendDaemonMsg((DDMsg_t *)&msg);
	}
	break;
    case SIGINT:
	PSIlog_log(PSILOG_LOG_VERB, "Got SIGINT.\n");
	if (getenv("PSI_SSH_COMPAT_HOST"))
	    fprintf(stderr, "Killed by signal 2\n");
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

	    sendDaemonMsg((DDMsg_t *)&msg);
	}
	break;
    case SIGTTIN:
	PSIlog_log(PSILOG_LOG_VERB, "Got SIGTTIN.\n");
	break;
    case SIGWINCH:
	for (i=getMinRank(); i <= getMaxRank(); i++) {
	    PStask_ID_t tid = getClientTID(i);
	    if (!clientIsActive(i) || tid == -1) continue;

	    /* Create WINCH-messages and send */
	    struct winsize ws;
	    int buf[4];
	    int len = 0;

	    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) < 0) break;
	    buf[len++] = ws.ws_col;
	    buf[len++] = ws.ws_row;
	    buf[len++] = ws.ws_xpixel;
	    buf[len++] = ws.ws_ypixel;

	    sendMsg(tid, WINCH, (char *)buf, len*sizeof(*buf));

	    PSIlog_log(PSILOG_LOG_VERB, "%s: WINCH to col %d row %d",
		       __func__, ws.ws_col, ws.ws_row);
	    PSIlog_log(PSILOG_LOG_VERB, " xpixel %d ypixel %d\n",
		       ws.ws_xpixel, ws.ws_ypixel);
	}
	break;
    case SIGUSR1:
	PSIlog_log(-1, "No of clients: %d open logs:", getNoClients());
	for (i=getMinRank(); i<=getMaxRank(); i++) {
	    PStask_ID_t tid = getClientTID(i);
	    if (tid == -1) continue;
	    PSIlog_log(-1, "%d (%s) ", i, PSC_printTID(tid));
	}
	PSIlog_log(-1, "\b\n");
	PSIlog_log(-1, "allActiveThere is %d\n", allActiveThere());
	break;
    case SIGHUP:
    case SIGTSTP:
    case SIGCONT:
    case SIGUSR2:
    case SIGQUIT:
	PSIlog_log(PSILOG_LOG_VERB, "Got signal %d.\n", sig);
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

	    sendDaemonMsg((DDMsg_t *)&msg);
	}
	if (sig == SIGTSTP) raise(SIGSTOP);
	break;
    default:
	PSIlog_log(-1, "Got signal %d.\n", sig);
    }

    fflush(stdout);
    fflush(stderr);

    signal(sig, sighandler);
}

static void handleMaxTime(void)
{
    DDSignalMsg_t msg;

    PSIlog_log(-1, "Maximum runtime of %ld seconds elapsed.\n", maxTime);

    msg.header.type = PSP_CD_SIGNAL;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = PSC_getMyTID();
    msg.header.len = sizeof(msg);
    msg.signal = -1;
    msg.param = getuid();
    msg.pervasive = 1;
    msg.answer = 0;

    sendDaemonMsg((DDMsg_t *)&msg);

    if (maxTimeID != -1) Timer_remove(maxTimeID);
}

/* Raw mode handling. raw mode is needed for correct functioning of pssh */
static struct termios _saved_tio;
static int _in_raw_mode = 0;

static void leaveRawMode(void)
{
    if (!_in_raw_mode) return;
    if (tcsetattr(STDIN_FILENO, TCSADRAIN, &_saved_tio) == -1) {
	PSIlog_warn(-1, errno, "%s: tcsetattr()", __func__);
    } else {
	PSIlog_log(PSILOG_LOG_VERB, "Leaving raw-mode\n");
	_in_raw_mode = 0;
    }
}

static void enterRawMode(void)
{
    struct termios termios;

    PSIlog_log(PSILOG_LOG_VERB, "Entering raw-mode\n");
    if (tcgetattr(STDIN_FILENO, &termios) == -1) {
	PSIlog_warn(-1, errno, "%s: tcgetattr()", __func__);
	return;
    }
    _saved_tio = termios;
    termios.c_iflag |= IGNPAR;
    termios.c_iflag &= ~(ISTRIP|INLCR|IGNCR|ICRNL|IXON|IXANY|IXOFF);
#ifdef IUCLC
    termios.c_iflag &= ~IUCLC;
#else
#error no IUCLC
#endif
    termios.c_lflag &= ~(ISIG|ICANON|ECHO|ECHOE|ECHOK|ECHONL);
#ifdef IEXTEN
    termios.c_lflag &= ~IEXTEN;
#else
#error no IEXTEN
#endif
    termios.c_oflag &= ~OPOST;
    termios.c_cc[VMIN] = 1;
    termios.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSADRAIN, &termios) == -1) {
	PSIlog_warn(-1, errno, "%s: tcsetattr()", __func__);
    } else
	_in_raw_mode = 1;
}

/**
 * @brief Try to send answer to INITIALIZE message
 *
 * Try to send an answer to an INITIALIZE message to rank @a
 * rank. This type of send is used in order to enable the daisy-chain
 * broadcasts. Therefore, it is tested, if the both, predecessing and
 * successing rank are already known. If one neighbor is still
 * unknown, no message is send. Otherwise, an extended INITIALIZE
 * answer is delivered containing information on the neighboring
 * processes.
 *
 * @param rank Rank of the client the response is send to
 *
 * @return No return value.
 */
static void trySendInitAnswer(int rank)
{
    PStask_ID_t task, pred, succ;

    if (rank < 0 || rank >= getNumKvsClients()) return;

    pred = (rank) ? getClientTID(rank-1) : PSC_getMyTID();
    task = getClientTID(rank);
    succ = (rank == getNumKvsClients()-1) ? PSC_getMyTID():getClientTID(rank+1);

    /* Check, if current request can be answered */
    if (task != -1
	&& (pred != -1 || clientIsGone(rank-1))
	&& (succ != -1 || clientIsGone(rank+1))) {
	char buf[sizeof(forw_verbose) + 2*sizeof(PStask_ID_t)], *ptr = buf;

	*(int *)ptr = forw_verbose;
	ptr += sizeof(int);
	*(PStask_ID_t *)ptr = pred;
	ptr += sizeof(PStask_ID_t);
	*(PStask_ID_t *)ptr = succ;
	ptr += sizeof(PStask_ID_t);

	sendMsg(task, INITIALIZE, buf, sizeof(buf));
	PSIlog_log(PSILOG_LOG_VERB, "%s: send answer to %s (%d)\n", __func__,
		   PSC_printTID(task), rank);
    }
}

/**
 * @brief Handle connection requests from new forwarders.
 *
 * Handles connection requests from new forwarder. In order to
 * connect, the forwarder has sent a INITIALIZE PSLog message. After
 * some testing, if everything is okay, the new client will be
 * registered and a INITIALIZE reply is sent to the forwarder.
 *
 * @param msg INITIALIZE message used to contact the logger.
 *
 * @return On success, 1 is returned. On error, 0 is returned.
 */
static int newrequest(PSLog_Msg_t *msg)
{
    int rank = msg->sender;
    static int triggerOld = 0, kvsConnected = 0;

    if (!registerClient(rank, msg->header.sender)) return 0;

    if (enableGDB) {
	snprintf(GDBprompt, sizeof(GDBprompt), "[%s]: (gdb) ", getDestStr(128));
	rl_set_prompt(GDBprompt);
    }

    maxConnected++;
    PSIlog_log(PSILOG_LOG_VERB, "%s: new connection from %s (%d)\n", __func__,
	       PSC_printTID(msg->header.sender), rank);

    if (msg->version < 2) triggerOld = 1;

    if (rank < 0 || !enable_kvs) {
	/* not part of kvs, answer immediately */
	sendMsg(msg->header.sender, INITIALIZE,
		(char *) &forw_verbose, sizeof(forw_verbose));
    } else {

	if (rank-1 > 0) trySendInitAnswer(rank - 1);
	if (rank > 0) trySendInitAnswer(rank);
	trySendInitAnswer(rank + 1);

	kvsConnected++;
    }

    if (enable_kvs && kvsConnected == getNumKvsClients()) {
	/* All clients there, answer to rank 0 */
	if (triggerOld) {
	    PSIlog_log(PSILOG_LOG_VERB, "%s: old triggered at %s\n", __func__,
		       PSC_printTID(getClientTID(0)));
	    sendMsg(getClientTID(0), INITIALIZE,
		    (char *) &forw_verbose, sizeof(forw_verbose));
	} else {
	    trySendInitAnswer(0);
	    switchDaisyChain(1);
	}
    }

    return 1;
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
		PSIlog_log(-1, "%s(%d): ", __func__, fd);
		switch(errno) {
		case EBADF :
		    PSIlog_log(-1, "EBADF -> close socket\n");
		    close(fd);
		    FD_CLR(fd,openfds);
		    fd++;
		    break;
		case EINTR:
		    PSIlog_log(-1, "EINTR -> trying again\n");
		    break;
		case EINVAL:
		    PSIlog_log(-1, "EINVAL -> close socket\n");
		    close(fd);
		    FD_CLR(fd,openfds);
		    break;
		case ENOMEM:
		    PSIlog_log(-1, "ENOMEM -> close socket\n");
		    close(fd);
		    FD_CLR(fd,openfds);
		    break;
		default:
		    errstr=strerror(errno);
		    PSIlog_warn(-1, errno, "unrecognized error");
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
    size_t len;

    len = read(std_in, buf, sizeof(buf)>SSIZE_MAX ? SSIZE_MAX : sizeof(buf));
    switch (len) {
    case -1:
	if (errno == EBADF) {
	    remFromFDSet(std_in);
	} else if (errno != EIO) {
	    PSIlog_warn(-1, errno, "%s: read()", __func__);
	    remFromFDSet(std_in);
	    close(std_in);
	} else {
	    /* ignore */
	}
	break;
    case 0:
	remFromFDSet(std_in);
	close(std_in);
	unavailSTDIN = std_in;
    default:
	forwardInputStr(buf, len);

	PSIlog_log(PSILOG_LOG_VERB, "%s: %zd bytes\n", __func__, len);
    }
}

/**
 * @brief Handle USAGE msg from forwarder.
 *
 * @param msg The received msg to handle.
 *
 * @return No return value.
 */
static void handleUSAGEMsg(PSLog_Msg_t *msg)
{
    if (showUsage) {
	struct rusage usage;

	memcpy(&usage, msg->buf, sizeof(usage));

	PSIlog_log(-1, "Child with rank %d used %.6f/%.6f sec (user/sys)\n",
		   msg->sender,
		   usage.ru_utime.tv_sec + usage.ru_utime.tv_usec * 1.0e-6,
		   usage.ru_stime.tv_sec + usage.ru_stime.tv_usec * 1.0e-6);
    }
}

#define lastSenderMagic -13

/**
 * @brief Handle STDOUT/STDERR msgs from forwarder.
 *
 * @param msg The received msg to handle.
 *
 * @param outfd The file descriptor to write the output to.
 *
 * @return No return value.
 */
static void handleOutMsg(PSLog_Msg_t *msg, int outfd)
{
    static int lastSender = lastSenderMagic, nlAtEnd = 1;
    PSIlog_log(PSILOG_LOG_VERB, "Got %d bytes from %s\n",
	       msg->header.len - PSLog_headerSize,
	       PSC_printTID(msg->header.sender));

    if (mergeOutput && np > 1) {
	/* collect all ouput */
	cacheOutput(msg, outfd);
    } else if (prependSource) {
	char prefix[30];
	char *buf = msg->buf;
	size_t count = msg->header.len - PSLog_headerSize;

	if (PSIlog_getDebugMask() & PSILOG_LOG_VERB) {
	    snprintf(prefix, sizeof(prefix), "[%d, %d]: ", msg->sender,
		     msg->header.len - PSLog_headerSize);
	} else if (count > 0) {
	    snprintf(prefix, sizeof(prefix), "[%d]: ", msg->sender);
	}

	while (count>0) {
	    char *nl = memchr(buf, '\n', count);
	    int num =  nl ? (int)(nl - buf) + 1 : (int)count;

	    if (nl) nl++; /* Thus nl points behind the newline */

	    switch (outfd) {
	    case STDOUT_FILENO:
		if (lastSender!=msg->sender && !nlAtEnd) {
		    PSIlog_stdout(-1, "\\\n");
		}
		PSIlog_stdout(-1, "%s%.*s",
			      (lastSender==msg->sender) ? "":prefix, num, buf);
		break;
	    case STDERR_FILENO:
		if (lastSender!=msg->sender && !nlAtEnd) {
		    PSIlog_stderr(-1, "\\\n");
		}
		PSIlog_stderr(-1, "%s%.*s",
			      (lastSender==msg->sender) ? "":prefix, num, buf);
		break;
	    default:
		PSIlog_log(-1, "%s: unknown outfd %d\n", __func__, outfd);
	    }

	    if (nl) {
		count -= nl - buf;
		buf = nl;
		lastSender = lastSenderMagic;
		nlAtEnd = 1;
	    } else {
		count = 0;
		lastSender = msg->sender;
		nlAtEnd = 0;
	    }
	}
    } else {
	switch (outfd) {
	case STDOUT_FILENO:
	    if (msg->sender != lastSender && !nlAtEnd) {
		PSIlog_stdout(-1, "\n");
	    }
	    PSIlog_stdout(-1, "%.*s",
			  msg->header.len-PSLog_headerSize, msg->buf);
	    break;
	case STDERR_FILENO:
	    if (msg->sender != lastSender && !nlAtEnd) {
		PSIlog_stdout(-1, "\n");
	    }
	    PSIlog_stderr(-1, "%.*s",
			  msg->header.len-PSLog_headerSize, msg->buf);
	    break;
	default:
	    PSIlog_log(-1, "%s: unknown outfd %d\n", __func__, outfd);
	}
	if (msg->buf[msg->header.len-PSLog_headerSize-1] == '\n') {
	    lastSender = lastSenderMagic;
	    nlAtEnd = 1;
	} else {
	    lastSender = msg->sender;
	    nlAtEnd = 0;
	}
    }
}

/**
 * @brief Handle FINALIZE msg from forwarder.
 *
 * @param msg The received msg to handle.
 *
 * @return If service-process (with rank -2) has exited with
 * non-trivial status, 1 is returned. Otherwise 0 is returned.
 */
static int handleFINALIZEMsg(PSLog_Msg_t *msg)
{
    int ret = 0;

    if (msg->sender >= 0) leaveRawMode();
    if (getenv("PSI_SSH_COMPAT_HOST")) {
	char *host = getenv("PSI_SSH_COMPAT_HOST");
	int status = *(int *) msg->buf;

	if (WIFSIGNALED(status)) retVal = -1;
	if (WIFEXITED(status)) retVal = WEXITSTATUS(status);

	if (getenv("PSI_SSH_INTERACTIVE"))
	    PSIlog_log(-1, "Connection to %s closed.\n", host);
    } else {
	int status = *(int *) msg->buf;

	if (WIFSIGNALED(status)) {
	    int sig = WTERMSIG(status);
	    int key = (sig == SIGTERM) ? PSILOG_LOG_TERM|PSILOG_LOG_VERB : -1;
	    const char *sigStr = sys_siglist[sig];

	    PSIlog_log(key, "Child with rank %d exited on signal %d",
		       msg->sender, WTERMSIG(status));
	    PSIlog_log(key, ": %s\n", sigStr ? sigStr : "Unknown signal");
	    signaled = 1;
	}

	if (WIFEXITED(status)) {
	    int exitStatus = WEXITSTATUS(status);
	    if (exitStatus) {
		int logLevel = -1;
		if (msg->sender == -2) {
		    ret = 1;
		    logLevel = PSILOG_LOG_VERB;
		}
		PSIlog_log(logLevel,
			   "Child with rank %d exited with status %d.\n",
			   msg->sender, exitStatus);
		if (retVal < exitStatus) retVal = exitStatus;
	    } else {
		PSIlog_log(PSILOG_LOG_VERB,
			   "Child with rank %d exited normally.\n",
			   msg->sender);
	    }
	}
    }

    PSIlog_log(PSILOG_LOG_VERB, "closing %s (rank %d) on FINALIZE\n",
	       PSC_printTID(msg->header.sender), msg->sender);

    PSLog_write(msg->header.sender, EXIT, NULL, 0);

    deregisterClient(msg->sender);

    return ret;
}

/**
 * @brief Handle KVS msg from forwarder.
 *
 * @param msg The received msg to handle.
 *
 * @return No return value.
 */
static void handleKVSMsg(PSLog_Msg_t *msg)
{
    if (enable_kvs) {
	handleKvsMsg(msg);
    } else {
	/* return kvs disabled msg */
	sendMsg(msg->header.sender, KVS, "cmd=kvs_not_available\n",
	    strlen("cmd=kvs_not_available\n"));
    }
}

/**
 * @brief Determine job-ID
 *
 * Determine if the current job is started within a batch
 * environment. If a batch-system is detected, return a string
 * describing the current job.
 *
 * @return If a batch system is found, a string containing the
 * corresponding job-ID is returned. Otherwise NULL is returned.
 */
static char * getIDstr(void)
{
    char *IDstr;

    IDstr = getenv("PBS_JOBID");
    if (IDstr) return IDstr;

    /* @todo Add support for other batch-systems */

    return NULL;
}

/**
 * @brief Send accounting info
 *
 * Generate accounting information only known to the logger process
 * and send this information to the local daemon. From there it will
 * be distributed to all registered accounters.
 *
 * @return No return value.
 */
static void sendAcctData(void)
{
    DDTypedBufferMsg_t msg;
    char *ptr = msg.buf, *IDstr;

    msg.header.type = PSP_CD_ACCOUNT;
    msg.header.dest = PSC_getTID(-1, 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg.header);

    msg.type = PSP_ACCOUNT_LOG;
    msg.header.len += sizeof(msg.type);

    /* logger's TID, this identifies a task uniquely */
    *(PStask_ID_t *)ptr = PSC_getMyTID();
    ptr += sizeof(PStask_ID_t);
    msg.header.len += sizeof(PStask_ID_t);

    /* current rank */
    *(int32_t *)ptr = -1;
    ptr += sizeof(int32_t);
    msg.header.len += sizeof(int32_t);

    /* child's uid */
    *(uid_t *)ptr = getuid();
    ptr += sizeof(uid_t);
    msg.header.len += sizeof(uid_t);

    /* child's gid */
    *(gid_t *)ptr = getgid();
    ptr += sizeof(gid_t);
    msg.header.len += sizeof(gid_t);

    /* maximum number of connected childs */
    *(int32_t *)ptr = maxConnected;
    ptr += sizeof(int32_t);
    msg.header.len += sizeof(int32_t);

    /* job id */
    if ((IDstr = getIDstr())) {
	size_t len = strlen(IDstr), offset=0;
	size_t maxLen = sizeof(msg)-msg.header.len-1;

	if (len > maxLen) {
	    strcpy(ptr, "...");
	    ptr += 3;
	    msg.header.len += 3;

	    offset = len-maxLen+3;
	    len = maxLen-3;
	}
	strcpy(ptr, &IDstr[offset]);
	ptr += len;
	msg.header.len += len;
    }
    *ptr = '\0';
    ptr++;
    msg.header.len++;

    sendDaemonMsg((DDMsg_t *)&msg);
}

/** Minimum amount of time (in seconds) to wait for clients */
#define MIN_WAIT 5

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
    addToFDSet(daemonSock);

    timeoutval = 0;

    /*
     * Loop until there is no connection left. Pay attention to the startup
     * phase, while no connection exists. Thus wait at least 10 * mytv.
     */
    while (getNoClients() > 0 || timeoutval < MIN_WAIT) {
	fd_set afds;
	memcpy(&afds, &myfds, sizeof(afds));
	atv = mytv;
	Timer_handleSignals();
	if (mergeOutput && np >1) displayCachedOutput(0);
	if (select(daemonSock + 1, &afds, NULL,NULL,&atv) < 0) {
	    if (errno == EINTR) {
		/* Interrupted syscall, just start again */
		continue;
	    }
	    PSIlog_warn(-1, errno, "select()");
	    CheckFileTable(&myfds);
	    continue;
	}
	if (FD_ISSET(daemonSock, &afds)) {
	    /* message from the daemon */
	    int ret;
	    int outfd = STDOUT_FILENO;

	    ret = recvMsg(&msg);

	    /* Ignore all errors */
	    if (ret < 0) continue;
	    if (!ret) {
		PSIlog_log(-1, "daemon died. Exiting\n");
		PSIlog_finalizeLogs();
		exit(1);
	    }

	    if (msg.type == INITIALIZE) {
		if (newrequest(&msg) && maxConnected >= np + numService) {
		    timeoutval = MIN_WAIT;
		    if (allActiveThere()) addToFDSet(STDIN_FILENO);
		}
	    } else if (msg.sender > getMaxRank()) {
		PSIlog_log(-1, "%s: sender %s (rank %d) out of range.\n",
			   __func__, PSC_printTID(msg.header.sender),
			   msg.sender);
	    } else if (getClientTID(msg.sender) != msg.header.sender) {
		int rank = msg.sender;
		PSIlog_log(-1, "%s: %s sends as rank %d ", __func__,
			   PSC_printTID(msg.header.sender), rank);
		PSIlog_log(-1, "(%s) type %d", PSC_printTID(getClientTID(rank)),
			   msg.type);
	    } else switch(msg.type) {
	    case STDERR:
		outfd = STDERR_FILENO;
	    case STDOUT:
		handleOutMsg(&msg, outfd);
		break;
	    case USAGE:
		handleUSAGEMsg(&msg);
		break;
	    case FINALIZE:
		if (handleFINALIZEMsg(&msg)) timeoutval = MIN_WAIT;
		break;
	    case STOP:
		handleSTOPMsg(&msg);
		continue; /* Don't handle STDIN now */
		break;
	    case CONT:
		handleCONTMsg(&msg);
		break;
	    case KVS:
		handleKVSMsg(&msg);
		break;
	    default:
		PSIlog_log(-1, "%s: Unknown message type %d!\n", __func__,
			   msg.type);
	    }
	}
	if (FD_ISSET(STDIN_FILENO, &afds)) {
	    /* if we debug with gdb, use readline callback for stdin */
	    if (enableGDB) {
		rl_callback_read_char();
	    } else {
		forwardInput(STDIN_FILENO);
	    }
	}
	if (!getNoClients()) timeoutval++;
    }
    if (mergeOutput && np > 1) {
	displayCachedOutput(0);
	/* flush output */
	displayCachedOutput(1);
    }
    if (!getenv("PSI_NOMSGLOGGERDONE")) {
	PSIlog_stderr(-1, "\n");
	PSIlog_log(-1, "done\n");
    }
    if (enableGDB) rl_callback_handler_remove();

    return;
}

/**
 * @brief The main program
 *
 * After registering several signals gets global variables @ref
 * daemonSock and local ParaStation ID from arguments. Furthermore the
 * global variables @ref forw_verbose, @ref prependSource @ref
 * InputDest and @ref usage are set from environment. After setting up
 * logging finally @ref loop() is called. Upon return from this
 * central loop, all further arguments to the two already evaluated
 * above are executed via system() calls.
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

    PSIlog_initLogs(NULL);

    /* block SIGTTIN so logger works also in background */
    sigemptyset(&set);
    sigaddset(&set, SIGTTIN);
    if (sigprocmask(SIG_BLOCK, &set, NULL)) {
	PSIlog_warn(-1, errno, "sigprocmask()");
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
	PSIlog_log(-1, "Sorry, program must be called correctly"
		" inside an application.\n");
	PSIlog_log(-1, "%d arguments:", argc);
	for (i=0; i<argc; i++)
	    PSIlog_log(-1, " '%s'", argv[i]);
	PSIlog_log(-1, "\n");
	PSIlog_finalizeLogs();
	exit(1);
    }

    /* daemonSock lost during exec() */
    daemonSock = strtol(argv[1], &end, 10);
    if (*end != '\0' || (daemonSock==0 && errno==EINVAL)) {
	PSIlog_log(-1, "Sorry, program must be called correctly"
		   " inside an application.\n");
	PSIlog_log(-1, "'%s' is not a socket number.\n", argv[1]);
	PSIlog_finalizeLogs();
	exit(1);
    }
    if (daemonSock < 0) {
	PSIlog_log(-1, "Not connected to local daemon. Exiting.\n");
	PSIlog_finalizeLogs();
	exit(1);
    }

    /* ParaStation ID lost during exec() */
    i = strtol(argv[2], &end, 10);
    if (*end != '\0' || (i==0 && errno==EINVAL)) {
	PSIlog_log(-1, "Sorry, program must be called correctly"
		   " inside an application.\n");
	PSIlog_log(-1, "'%s' is not a ParaStation ID.\n", argv[2]);
	PSIlog_finalizeLogs();
	exit(1);
    }
    PSC_setMyID(i);

    if (getenv("PSI_LOGGERDEBUG")) {
	PSIlog_setDebugMask(PSILOG_LOG_VERB);
	PSIlog_log(PSILOG_LOG_VERB, "Going to be verbose.\n");
	PSIlog_log(PSILOG_LOG_VERB, "Daemon on %d\n", daemonSock);
	PSIlog_log(PSILOG_LOG_VERB, "My ID is %d\n", i);
    }

    if (getenv("PSI_TERMINATED")) {
	PSIlog_setDebugMask(PSIlog_getDebugMask() | PSILOG_LOG_TERM);
	PSIlog_log(PSILOG_LOG_VERB, "Going to show terminated processes.\n");
    }

    if (getenv("PSI_FORWARDERDEBUG")) {
	forw_verbose=1;
	PSIlog_log(PSILOG_LOG_VERB, "Forwarders will be verbose, too.\n");
    }

    if (getenv("PSI_TIMESTAMPS")) {
	PSIlog_log(PSILOG_LOG_VERB, "Print detailed time-marks.\n");
	PSIlog_setTimeFlag(1);
    }

    if (getenv("PSI_SOURCEPRINTF")) {
	prependSource = 1;
	PSIlog_log(PSILOG_LOG_VERB, "Will print source-info.\n");
    }

    if ((envstr = getenv("PSI_NP_INFO"))) {
	np = atoi(envstr);
    }

    if ((envstr = getenv(ENV_NUM_SERVICE_PROCS))) {
	numService = atoi(envstr);
    }

    /* Destination list has to be set before initClients() */
    if (daemonSock) {
	input = getenv("PSI_INPUTDEST");
	if (!input) input = "0";

	PSIlog_log(PSILOG_LOG_VERB, "Input goes to [%s].\n", input);
    } else {
	/* daemonSock = 0, this means there is no stdin connected */
	PSIlog_log(PSILOG_LOG_VERB, "No stdin available.\n");
	input = "";
    }
    setupDestList(input);

    if (getenv("PSI_ENABLE_GDB")) {
	enableGDB = 1;
	rl_callback_handler_install(NULL, (rl_vcpfunc_t*)readGDBInput);
	PSIlog_log(PSILOG_LOG_VERB, "Enabling gdb functions.\n");
	snprintf(GDBprompt, sizeof(GDBprompt), "[%s]: (gdb) ", getDestStr(128));
	rl_set_prompt(GDBprompt);
    }

    initClients(-2, np ? np-1 : 0);
    if (getenv("PSI_MERGEOUTPUT")) {
	mergeOutput = 1;
	PSIlog_log(PSILOG_LOG_VERB, "Will merge the output of all ranks.\n");
	outputMergeInit();
    }

    if (getenv("PSI_RUSAGE")) {
	showUsage=1;
	PSIlog_log(PSILOG_LOG_VERB, "Going to show resource usage.\n");
    }

    PSLog_init(daemonSock, -1, 2);

    /* init the timer structure */
    if (!Timer_isInitialized()) {
	Timer_init(stderr);
    }

    if (getenv("PSI_LOGGER_RAW_MODE") && isatty(STDIN_FILENO)) {
	enterRawMode();
	if (PSIlog_stdoutLogger) logger_setWaitNLFlag(PSIlog_stdoutLogger, 0);
	if (PSIlog_stderrLogger) logger_setWaitNLFlag(PSIlog_stderrLogger, 0);
    }

    /* enable kvs and pmi */
    if (getenv("KVS_ENABLE")) {
	enable_kvs = 1;
	initLoggerKvs();
    }

    if ((envstr = getenv("PSI_MAXTIME"))) {
	if (!*envstr) {
	    PSIlog_log(-1, "PSI_MAXTIME is empty.\n");
	} else {
	    char *end;

	    maxTime = strtol(envstr, &end, 10);
	    if (*end) {
		PSIlog_log(-1, "PSI_MAXTIME '%s' is invalid.\n", envstr);
	    } else {
		struct timeval timer;

		/* timeout from user */
		timer.tv_sec = maxTime;
		timer.tv_usec = 0;

		maxTimeID = Timer_register(&timer, handleMaxTime);

		PSIlog_log(PSILOG_LOG_VERB,
			   "Runtime limited to %ld seconds.\n", maxTime);
	    }
	}
    }

    /* call the loop which does all the work */
    loop();

    leaveRawMode();

    sendAcctData();

    closeDaemonSock();

    for (i=3; i<argc; i++) {
	int ret;
	PSIlog_log(PSILOG_LOG_VERB, "Execute '%s'\n", argv[i]);
	ret = system(argv[i]);
    }

    PSIlog_finalizeLogs();

    return retVal ? retVal : (signaled ? -1 : 0);
}
