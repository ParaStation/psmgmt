/*
 * ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file psilogger: Log-daemon for ParaStation I/O forwarding facility
 */
#include "psilogger.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/resource.h>    // IWYU pragma: keep
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "pscio.h"
#include "pscommon.h"
#include "psprotocol.h"
#include "selector.h"
#include "timer.h"
#include "linenoise.h"
#include "psiloggermerge.h"
#include "psiloggerclient.h"

int usize = 0;


/** Number of clients to be expected -- more clients might appear */
int np = 0;

/** width of field to fit ranks up to np-1 */
static int npWidth = 1;

bool enableGDB = false;

bool useValgrind = false;

/** Scan output for Valgrind PID patterns?  Set from PSI_USE_VALGRIND */
static bool rawIO = false;

/** Display source and length of each message? Set from PSI_SOURCEPRINTF */
static bool prependSource = false;

/** Merge output lines of different ranks?  Set from PSI_MERGEOUTPUT */
static bool mergeOutput = false;

/** Number of service forwarders expected to connect; at least the spawner */
static int numService = 1;

/** Verbosity of Forwarders. Set from PSI_FORWARDERDEBUG */
static bool forw_verbose = false;

/** Flag display of usage info. Set from PSI_USAGE */
static bool showUsage = false;

/** Total number of forwarder that have connected during the job's runtime */
static int32_t maxConnected = 0;

/** The socket connecting to the local ParaStation daemon */
static int daemonSock = -1;

/** Maximum runtime of the job allowed by user */
static long maxTime = 0;

/** Timer used to control maximum run-time */
static int maxTimeID = -1;

/** The value which will be returned when the logger stops execution */
static int retVal = 0;

/** Flag marking a client got signaled */
static bool signaled = false;

logger_t PSIlog_stdout;
logger_t PSIlog_stderr;
logger_t PSIlog_logger;

/* create a file handle pointing to <fNameBase>-<PID>.<ext> or exit() */
static FILE * getFile(char *fNameBase, char *ext)
{
    if (!fNameBase || !ext) return NULL;

    char fpath[PATH_MAX];
    snprintf(fpath, sizeof(fpath), "%s-%i.%s", fNameBase, getpid(), ext);
    int fd = open(fpath, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
    if (fd == -1) {
	if (PSIlog_logger) {
	    PSIlog_exit(errno, "failed to open %s file %s\n", ext, fpath);
	} else {
	    fprintf(stderr, "failed to open %s file %s: %m\n", ext, fpath);
	    exit(1);
	}
    }
    FILE *file = fdopen(fd, "w");
    if (!file) {
	if (PSIlog_logger) {
	    PSIlog_exit(errno, "no file handle for %s\n", fpath);
	} else {
	    fprintf(stderr, "no file handle for %s: %m\n", fpath);
	    exit(1);
	}
    }
    return file;
}

/**
 * @brief Initialize psilogger's logging facilities
 *
 * Initialize psilogger's logging facilities. This is mainly a wrapper
 * to @ref logger_new() but additionally also initializes the
 * facilities handling output to stdout and stderr.
 *
 * If @a fNameBase is given, all logs will be redirected into separate
 * files. All filenames are of the form <fNameBase>-<PID>.<ext> where
 * <ext> is "log", "stdout", or "stderr", respectively. exit() will be
 * called if creation of either of the files fails.
 *
 * @param fNameBase Base name for logging files to create
 *
 * @return No return value
 *
 * @see PSIlog_finalizeLogs()
 */
static void initLogs(char *fNameBase)
{
    FILE *logFile = fNameBase ? getFile(fNameBase, "log") : stderr;
    PSIlog_logger = logger_new("PSIlogger", logFile);
    if (!PSIlog_logger) {
	fprintf(logFile, "Failed to initialize logger\n");
	exit(1);
    }

    FILE *stdoutFile = fNameBase ? getFile(fNameBase, "stdout") : stdout;
    PSIlog_stdout = logger_new(NULL, stdoutFile);
    if (!PSIlog_stdout) {
	PSIlog_exit(errno, "Failed to initialize stdout");
    }

    FILE *stderrFile = fNameBase ? getFile(fNameBase, "stderr") : stderr;
    PSIlog_stderr = logger_new(NULL, stderrFile);
    if (!PSIlog_stderr) {
	PSIlog_exit(errno, "Failed to initialize stderr");
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

bool PSIlog_getTimeFlag(void)
{
    return logger_getTimeFlag(PSIlog_logger);
}

void PSIlog_setTimeFlag(bool flag)
{
    logger_setTimeFlag(PSIlog_logger, flag);
    logger_setTimeFlag(PSIlog_stdout, flag);
    logger_setTimeFlag(PSIlog_stderr, flag);
}

void PSIlog_finalizeLogs(void)
{
    logger_finalize(PSIlog_logger);
    PSIlog_logger = NULL;
    logger_finalize(PSIlog_stdout);
    PSIlog_stdout = NULL;
    logger_finalize(PSIlog_stderr);
    PSIlog_stderr = NULL;
}

/**
 * @brief Close socket to daemon.
 *
 * Close the socket @a fd connecting the logger to the local daemon.
 *
 * @param fd File-descriptor connecting to the local daemon
 *
 * @return No return value.
 */
static void closeDaemonSock(int fd)
{
    if (fd < 0) return;

    daemonSock=-1;
    PSLog_close();
    Selector_remove(fd);
    close(fd);
}

bool GDBcmdEcho = false;

char GDBprompt[128];

/**
 * @brief Read line for GDB mode
 *
 * Helper function to be called from linenoise if the gdb debugging
 * mode is enabled. It handles the @a line determined by linenoise.
 *
 * @param line The line read from STDIN
 *
 * @return No return value
 */
static void readGDBInput(const char *line)
{
    if (!line) return;

    /* add the newline again */
    char buf[1000];
    snprintf(buf, sizeof(buf), "%s\n", line);
    PSIlog_stdout(-1, "\n");

    /*add to history */
    if (line[0]) linenoiseHistoryAdd(line);

    /* check for input changing cmd */
    size_t len = strlen(buf);
    if (len > 2 && buf[0] == '[' && buf[len -2] == ']') {
	/* remove trailing garbage */
	buf[len-1] = '\0';

	PSIlog_stdout(-1, "Changing input dest to: %s", buf);

	setupDestList(buf);
	PSIlog_stdout(-1, " -> [%s]\n", getDestStr(128));

	/* modify linenoise's prompt */
	snprintf(GDBprompt, sizeof(GDBprompt), "[%s]: (gdb) ", getDestStr(128));
	linenoiseSetPrompt(GDBprompt);
	return;
    }

    forwardInputStr(buf, len);
    /* Expect command's echo */
    GDBcmdEcho = true;
    linenoiseSetPrompt("");

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
 * @param len Amount of meaningful data within @a buf in bytes. If @a
 * len is larger the 1024, more than one message will be generated.
 * The number of messages can be computed by (len/1024 + 1).
 *
 * @return On success, the number of bytes written is returned,
 * i.e. usually this is @a len. On error -1 is returned and errno is
 * set appropriately.
 */
int sendMsg(PStask_ID_t tid, PSLog_msg_t type, char *buf, size_t len)
{
    int ret = PSLog_write(tid, type, buf, len);
    if (ret < 0 && errno != EBADF) {
	PSIlog_warn(-1, errno, "%s: error (%d)", __func__, errno);
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
static int sendDaemonMsg(DDMsg_t *msg)
{
    if (daemonSock < 0) {
	errno = EBADF;
	return -1;
    }

    ssize_t ret = PSCio_sendF(daemonSock, msg, msg->len);
    if (ret < 0) {
	PSIlog_exit(errno, "%s: PSCio_sendF()", __func__);
    } else if (!ret) {
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
    PSIlog_log(-1, "Terminating the job\n");

    /* send kill signal to all children */
    DDSignalMsg_t msg = {
	.header = {
	    .type = PSP_CD_SIGNAL,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getMyTID(),
	    .len = sizeof(msg) },
	.signal = -1,
	.param = getuid(),
	.pervasive = 1,
	.answer = 0 };
    sendDaemonMsg((DDMsg_t *)&msg);
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
static void sighandler(int sig)
{
    static bool firstTERM = true;
    DDSignalMsg_t msg;

    switch(sig) {
    case SIGTERM:
	if (firstTERM) {
	    PSIlog_log(PSILOG_LOG_VERB, "Got SIGTERM. Problem with child?\n");
	    firstTERM = false;
	}
	PSIlog_log(PSILOG_LOG_VERB, "%d clients. Open logs:", getNoClients());
	for (int i = getMinRank(); i <= getMaxRank(); i++) {
	    PStask_ID_t tid = getClientTID(i);
	    if (tid == -1) continue;
	    PSIlog_log(PSILOG_LOG_VERB, " %d (%s)", i, PSC_printTID(tid));
	}
	PSIlog_log(PSILOG_LOG_VERB, "\n");

	msg = (DDSignalMsg_t) {
	    .header = {
		.type = PSP_CD_SIGNAL,
		.sender = PSC_getMyTID(),
		.dest = PSC_getMyTID(),
		.len = sizeof(msg) },
	    .signal = sig,
	    .param = getuid(),
	    .pervasive = 1,
	    .answer = 0, };
	sendDaemonMsg((DDMsg_t *)&msg);
	break;
    case SIGINT:
	PSIlog_log(PSILOG_LOG_VERB, "Got SIGINT\n");
	if (getenv("PSI_SSH_COMPAT_HOST"))
	    fprintf(stderr, "Killed by signal 2\n");

	msg = (DDSignalMsg_t) {
	    .header = {
		.type = PSP_CD_SIGNAL,
		.sender = PSC_getMyTID(),
		.dest = PSC_getMyTID(),
		.len = sizeof(msg) },
	    .signal = sig,
	    .param = getuid(),
	    .pervasive = 1,
	    .answer = 0, };
	sendDaemonMsg((DDMsg_t *)&msg);
	break;
    case SIGTTIN:
	PSIlog_log(PSILOG_LOG_VERB, "Got SIGTTIN\n");
	break;
    case SIGWINCH:
	for (int i = getMinRank(); i <= getMaxRank(); i++) {
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
	if (!getenv("__SLURM_INFORM_TIDS")) {
	    PSIlog_log(-1, "%d clients. Open logs:", getNoClients());
	    for (int i = getMinRank(); i <= getMaxRank(); i++) {
		PStask_ID_t tid = getClientTID(i);
		if (tid == -1) continue;
		PSIlog_log(-1, " %d (%s)", i, PSC_printTID(tid));
	    }
	    PSIlog_log(-1, "\n");
	    PSIlog_log(-1, "allActiveThere is %d\n", allActiveThere());
	    break;
	} /* else fallthrough */
    case SIGHUP:
    case SIGTSTP:
    case SIGCONT:
    case SIGUSR2:
    case SIGQUIT:
	PSIlog_log(PSILOG_LOG_VERB, "Got signal %d\n", sig);
	msg = (DDSignalMsg_t) {
	    .header = {
		.type = PSP_CD_SIGNAL,
		.sender = PSC_getMyTID(),
		.dest = PSC_getMyTID(),
		.len = sizeof(msg) },
	    .signal = (sig==SIGTSTP) ? SIGSTOP : sig,
	    .param = getuid(),
	    .pervasive = 1,
	    .answer = 0, };
	sendDaemonMsg((DDMsg_t *)&msg);

	if (sig == SIGTSTP) raise(SIGSTOP);
	break;
    default:
	PSIlog_log(-1, "Got signal %d\n", sig);
    }

    fflush(stdout);
    fflush(stderr);
}

static void handleMaxTime(void)
{
    PSIlog_log(-1, "Maximum runtime of %ld seconds elapsed.\n", maxTime);

    DDSignalMsg_t msg = {
	.header = {
	    .type = PSP_CD_SIGNAL,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getMyTID(),
	    .len = sizeof(msg) },
	.signal = -1,
	.param = getuid(),
	.pervasive = 1,
	.answer = 0 };
    sendDaemonMsg((DDMsg_t *)&msg);

    if (maxTimeID != -1) Timer_remove(maxTimeID);
}

/* Raw mode handling. raw mode is needed for correct functioning of pssh */
static struct termios _saved_tio;
static bool _in_raw_mode = false;

static void leaveRawMode(void)
{
    if (!_in_raw_mode) return;
    if (tcsetattr(STDIN_FILENO, TCSADRAIN, &_saved_tio) == -1) {
	PSIlog_warn(-1, errno, "%s: tcsetattr()", __func__);
    } else {
	PSIlog_log(PSILOG_LOG_VERB, "Leaving raw-mode\n");
	_in_raw_mode = false;
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
	_in_raw_mode = true;
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
 * @return On success, true is returned. On error, false is returned.
 */
static bool newRequest(PSLog_Msg_t *msg)
{
    char *ptr = msg->buf;
    PStask_group_t group = *(PStask_group_t *) ptr;
    //ptr += sizeof(PStask_group_t);

    int rank = msg->sender;
    if (!registerClient(rank, msg->header.sender, group)) return false;

    if (enableGDB) {
	snprintf(GDBprompt, sizeof(GDBprompt), "[%s]: (gdb) ", getDestStr(128));
	linenoiseSetPrompt(GDBprompt);
    }

    if (group == TG_KVS) numService++;
    maxConnected++;
    PSIlog_log(PSILOG_LOG_VERB, "%s: new connection from %s (%d)\n", __func__,
	       PSC_printTID(msg->header.sender), rank);

    if (msg->version < 3) {
	PSIlog_log(PSILOG_LOG_VERB, "%s: unsupported PSLog msg version '%i' "
		    "from %s (%d)\n", __func__, msg->version,
		    PSC_printTID(msg->header.sender), rank);
	terminateJob();
    }

    /* send init answer */
    uint32_t verb = forw_verbose;
    sendMsg(msg->header.sender, INITIALIZE, (char *) &verb, sizeof(verb));

    return true;
}

/**
 * @brief Forward input to client.
 *
 * Read input data from the file descriptor @a std_in and forward it
 * to all forwarder(s) that are expected to receive input.
 *
 * @param std_in File descriptor to read STDIN data from.
 *
 * @return No return value.
 */
static void forwardInput(int std_in)
{
    char buf[1024];
    ssize_t len = read(std_in, buf,
		       sizeof(buf) > SSIZE_MAX ? SSIZE_MAX : sizeof(buf));
    switch (len) {
    case -1:
	if (errno == EBADF) {
	    Selector_remove(std_in);
	} else if (errno != EIO) {
	    PSIlog_warn(-1, errno, "%s: read()", __func__);
	    Selector_remove(std_in);
	    close(std_in);
	} else {
	    /* ignore */
	}
	break;
    case 0:
	Selector_remove(std_in);
	close(std_in);
	/* fallthrough */
    default:
	forwardInputStr(buf, len);

	PSIlog_log(PSILOG_LOG_VERB, "%s: %zd bytes\n", __func__, len);
    }
}

/**
 * @brief Handle SERVICE msg from forwarder.
 *
 * @param msg Return the mininum service rank.
 *
 * @return No return value.
 */
static void handleServiceMsg(PSLog_Msg_t *msg)
{
    int32_t nextRank = getNextServiceRank();
    sendMsg(msg->header.sender, SERV_RNK, (char *) &nextRank, sizeof(nextRank));
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
    if (!showUsage) return;

    struct rusage usage;
    memcpy(&usage, msg->buf, sizeof(usage));

    PSIlog_log(-1, "Child with rank %d used %.6f/%.6f sec (user/sys)\n",
	       msg->sender,
	       usage.ru_utime.tv_sec + usage.ru_utime.tv_usec * 1.0e-6,
	       usage.ru_stime.tv_sec + usage.ru_stime.tv_usec * 1.0e-6);
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
    static int lastSender = lastSenderMagic;
    static bool nlAtEnd = true;
    PSIlog_log(PSILOG_LOG_VERB, "Got %zd bytes from %s\n",
	       msg->header.len - PSLog_headerSize,
	       PSC_printTID(msg->header.sender));

    if (mergeOutput && usize > 1) {
	/* collect all ouput */
	cacheOutput(msg, outfd);
    } else if (prependSource) {
	char prefix[30];
	char *buf = msg->buf;
	size_t count = msg->header.len - PSLog_headerSize;

	if (PSIlog_getDebugMask() & PSILOG_LOG_VERB) {
	    snprintf(prefix, sizeof(prefix), "%*d,%zd: ", npWidth, msg->sender,
		     msg->header.len - PSLog_headerSize);
	} else if (count > 0) {
	    snprintf(prefix, sizeof(prefix), "%*d: ", npWidth, msg->sender);
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
		nlAtEnd = true;
	    } else {
		count = 0;
		lastSender = msg->sender;
		nlAtEnd = false;
	    }
	}
    } else {
	switch (outfd) {
	case STDOUT_FILENO:
	    if (rawIO) {
		PSIlog_writeout(msg->buf, msg->header.len-PSLog_headerSize);
		break;
	    }

	    if (msg->sender != lastSender && !nlAtEnd) {
		PSIlog_stdout(-1, "\n");
	    }
	    PSIlog_stdout(-1, "%.*s",
			  (int)(msg->header.len-PSLog_headerSize), msg->buf);
	    break;
	case STDERR_FILENO:
	    if (msg->sender != lastSender && !nlAtEnd) {
		PSIlog_stdout(-1, "\n");
	    }
	    PSIlog_stderr(-1, "%.*s",
			  (int)(msg->header.len-PSLog_headerSize), msg->buf);
	    break;
	default:
	    PSIlog_log(-1, "%s: unknown outfd %d\n", __func__, outfd);
	}
	if (msg->buf[msg->header.len-PSLog_headerSize-1] == '\n') {
	    lastSender = lastSenderMagic;
	    nlAtEnd = true;
	} else {
	    lastSender = msg->sender;
	    nlAtEnd = false;
	}
    }
}

/**
 * @brief Handle FINALIZE msg from forwarder.
 *
 * @param msg The received msg to handle.
 *
 * @return If service-process (with rank -2) has exited with
 * non-trivial status, true is returned. Otherwise false is returned.
 */
static bool handleFINALIZEMsg(PSLog_Msg_t *msg)
{
    bool ret = false;

    if (msg->sender >= 0 && getNoClients()==1) leaveRawMode();
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

	    PSIlog_log(key, "Child with rank %d exited on signal %d: %s\n",
		       msg->sender, sig, strsignal(sig));
	    signaled = true;
	}

	if (WIFEXITED(status)) {
	    int exitStatus = WEXITSTATUS(status);
	    if (exitStatus) {
		int logLevel = -1;
		if (msg->sender == -2) {
		    ret = true;
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

    if (!getNoClients()) Selector_startOver();

    return ret;
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
    char *IDstr = getenv("PBS_JOBID");
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
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_ACCOUNT,
	    .dest = PSC_getTID(-1, 0),
	    .sender = PSC_getMyTID(),
	    .len = 0, /* to be set by PSP_putTypedMsgBuf */ },
	.type = PSP_ACCOUNT_LOG,
	.buf = { '\0' } };

    /* logger's TID, this identifies a task uniquely */
    PStask_ID_t myTID = PSC_getMyTID();
    PSP_putTypedMsgBuf(&msg, "my/logger TID", &myTID, sizeof(myTID));

    /* current rank */
    int32_t myRank = -1;
    PSP_putTypedMsgBuf(&msg, "rank", &myRank, sizeof(myRank));

    /* child's uid */
    uid_t myUID = getuid();
    PSP_putTypedMsgBuf(&msg, "uid", &myUID, sizeof(myUID));

    /* child's gid */
    gid_t myGID = getgid();
    PSP_putTypedMsgBuf(&msg, "uid", &myGID, sizeof(myGID));

    /* maximum number of connected children */
    PSP_putTypedMsgBuf(&msg, "max conn", &maxConnected, sizeof(maxConnected));

    /* job id */
    char *IDstr = getIDstr();
    if (IDstr) {
	char *tailPtr = IDstr;
	size_t len = strlen(IDstr);
	size_t bufferFree = sizeof(msg) - msg.header.len - 1;
	if (len > bufferFree) {
	    char dots[] = "...";
	    PSP_putTypedMsgBuf(&msg, "dots", dots, strlen(dots));
	    tailPtr = IDstr + len - bufferFree + strlen(dots);
	}
	PSP_putTypedMsgBuf(&msg, "tail", tailPtr, strlen(tailPtr));
    }
    char nullChr = '\0';
    PSP_putTypedMsgBuf(&msg, "terminating \\0", &nullChr, sizeof(nullChr));

    sendDaemonMsg((DDMsg_t *)&msg);
}

/**
 * @brief Forward input from regular file to client.
 *
 * Read input data from the file descriptor @a fd which is assumed to
 * be a regular file and forward it to all forwarder(s) that are
 * expected to receive input.
 *
 * @param fd File descriptor to read STDIN data from.
 *
 * @return No return value.
 */
static void forwardInputFile(int fd)
{
    char buf[1024 > SSIZE_MAX ? SSIZE_MAX : 1024];
    ssize_t len;
    do {
	len = read(fd, buf, sizeof(buf));
	switch (len) {
	case -1:
	    PSIlog_warn(-1, errno, "%s: read()", __func__);
	    close(fd);
	    break;
	case 0:
	    close(fd);
	    /* fallthrough */
	default:
	    forwardInputStr(buf, len);

	    PSIlog_log(PSILOG_LOG_VERB, "%s: %zd bytes\n", __func__, len);
	}
    } while (len > 0);
}

static int readFromStdin(int fd, void *data)
{
    /* if we debug with gdb, use linenoise's callback for stdin */
    if (enableGDB) {
	linenoiseReadChar();
    } else {
	forwardInput(fd);
    }
    return 0;
}

/** Minimum amount of time (in seconds) to wait for clients */
#define MIN_WAIT 5

static int timeoutval = 0;

static void handleCCMsg(PSLog_Msg_t *msg)
{
    static bool stdinHandled = false;
    int outfd = STDOUT_FILENO;

    if (msg->type == INITIALIZE) {
	if (newRequest(msg) && maxConnected >= np + numService) {
	    timeoutval = MIN_WAIT;
	    if (allActiveThere() && !stdinHandled) {
		if (Selector_register(STDIN_FILENO, readFromStdin, NULL) < 0) {
		    /*
		     * Selector registration failed; stdin might be a
		     * regular file
		     */
		    forwardInputFile(STDIN_FILENO);
		}
		/* If STDIN get's closed, don't re-add it to the selector */
		stdinHandled = true;
	    }
	}
    } else if (msg->sender > getMaxRank()) {
	PSIlog_log(-1, "%s: sender %s (rank %d) out of range.\n",
		   __func__, PSC_printTID(msg->header.sender),
		   msg->sender);
    } else if (getClientTID(msg->sender) != msg->header.sender) {
	int rank = msg->sender;
	int key = ((msg->type == FINALIZE) && clientIsGone(rank)) ?
	    PSILOG_LOG_VERB : -1;

	PSIlog_log(key, "%s: %s sends as rank %d ", __func__,
		   PSC_printTID(msg->header.sender), rank);
	PSIlog_log(key, "(%s) type %d\n",
		   PSC_printTID(getClientTID(rank)), msg->type);
    } else {
	switch(msg->type) {
	case STDERR:
	    outfd = STDERR_FILENO;
	    /* fallthrough */
	case STDOUT:
	    handleOutMsg(msg, outfd);
	    break;
	case USAGE:
	    handleUSAGEMsg(msg);
	    break;
	case FINALIZE:
	    if (handleFINALIZEMsg(msg)) timeoutval = MIN_WAIT;
	    break;
	case STOP:
	    handleSTOPMsg(msg);
	    break;
	case CONT:
	    handleCONTMsg(msg);
	    break;
	case SERV_RNK:
	    handleServiceMsg(msg);
	    break;
	default:
	    PSIlog_log(-1, "%s: Unknown message type %d from %s\n",
		       __func__, msg->type,
		       PSC_printTID(msg->header.sender));
	}
    }
}

static int handleDebugMsg(int fd, void *info)
{
    char buf[256];
    ssize_t len = read(fd, buf,
		       (sizeof(buf) > SSIZE_MAX) ? SSIZE_MAX : sizeof(buf));

    int eno;
    switch (len) {
    case -1:
	eno = errno;
	PSIlog_warn(-1, eno, "%s: read()", __func__);
	if (eno == EBADF) {
	    Selector_remove(fd);
	} else if (eno != EIO) {
	    Selector_remove(fd);
	    close(fd);
	} else {
	    /* ignore */
	}
	break;
    case 0:
	PSIlog_log(PSILOG_LOG_VERB, "%s: debugger disconnected\n", __func__);
	Selector_remove(fd);
	close(fd);
	break;
    default:
	PSIlog_log(PSILOG_LOG_VERB, "%s: %zd bytes\n", __func__, len);
    }

    if (len < 1) return 0;

    PSIlog_log(PSILOG_LOG_VERB, "%s: received '%d'\n", __func__, *(int *)buf);

    dprintf(fd, "%d clients. Open logs:\n", getNoClients());
    for (int i = getMinRank(); i <= getMaxRank(); i++) {
	PStask_ID_t tid = getClientTID(i);
	if (tid == -1) continue;
	dprintf(fd, "\t%d (%s)\n", i, PSC_printTID(tid));
    }
    dprintf(fd, "allActiveThere is %d\n", allActiveThere());

    Selector_remove(fd);
    close(fd);

    return 0;
}

/**
 * @brief Handle debug socket
 *
 * Handle the debug socket @a fd, i.e. accept requests for connecting
 * the logger from new debugging client. This will only register the
 * new debugger's file-descriptor.
 *
 * This function is expected to be registered to the selector facility.
 *
 * @param fd The file-descriptor from which the new connection might
 * be accepted.
 *
 * @param info Extra info. Currently ignored.
 *
 * @return On success 0 is returned. Otherwise -1 is returned and
 * errno is set appropriately.
 */
static int handleDebugSock(int fd, void *info)
{
    PSIlog_log(PSILOG_LOG_VERB, "%s: accepting new connection\n", __func__);

    int ssock = accept(fd, NULL, 0);
    if (ssock < 0) {
	PSIlog_warn(-1, errno, "%s: error while accept()", __func__);
	return -1;
    }

    Selector_register(ssock, handleDebugMsg, NULL);

    PSIlog_log(PSILOG_LOG_VERB, "%s: new socket is %d\n", __func__, ssock);

    struct linger linger = { .l_onoff = 1,
			     .l_linger= 1};
    if (setsockopt(ssock, SOL_SOCKET, SO_LINGER, &linger, sizeof(linger)) < 0) {
	PSIlog_warn(-1, errno, "%s: cannot setsockopt(SO_LINGER)", __func__);
	close(ssock);
	return -1;
    }

    return 0;
}

#define debugSockName "psilogger_%d.sock"

static void enableDebugSock(void)
{
    int debugSock = socket(PF_UNIX, SOCK_STREAM, 0);
    if (debugSock < 0) {
	PSIlog_exit(errno, "Unable to create socket for debugging");
    }

    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));

    sa.sun_family = AF_UNIX;
    sa.sun_path[0] = '\0';
    snprintf(sa.sun_path+1, sizeof(sa.sun_path)-1, debugSockName, getpid());

    /* bind the socket to the right address */
    if (bind(debugSock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
	PSIlog_warn(-1, errno, "Not able to debug logger ");
	return;
    }

    if (listen(debugSock, 20) < 0) {
	PSIlog_warn(-1, errno, "Error while trying to listen");
	return;
    }

    Selector_register(debugSock, handleDebugSock, NULL);

    PSIlog_log(PSILOG_LOG_VERB, "Local Debug Port (%d) enabled.\n", debugSock);
}

/**
 * @brief Handle daemon message
 *
 * Handle message available on the daemon socket @a fd, i.e. receive
 * the message and pass it to the appropriate handler.
 *
 * This function is expected to be registered to the selector facility.
 *
 * @param fd The file-descriptor connected to the daemon. The message
 * is read from here.
 *
 * @param info Extra info. Currently ignored.
 *
 * @return On success 1 is returned. Otherwise -1 is returned and
 * errno is set appropriately.
 */
static int handleDaemonMsg(int fd, void *info)
{
    DDBufferMsg_t msg;
    ssize_t ret = PSCio_recvMsg(fd, &msg);

    /* Ignore all errors */
    if (ret < 0) {
	PSIlog_warn(-1, errno, "%s: PSCio_recvMsg()", __func__);
	return 0;
    }

    if (!ret) {
	PSIlog_log(-1, "daemon died. Exiting\n");
	PSIlog_finalizeLogs();
	exit(1);
    }

    switch(msg.header.type) {
    case PSP_CC_MSG:
	handleCCMsg((PSLog_Msg_t *)&msg);
	break;
    case PSP_CC_ERROR:
	/* Try to find the corresponding client */
	if (getClientRank(msg.header.sender) > getMaxRank()) {
	    PSIlog_log(PSILOG_LOG_VERB, "%s: CC_ERROR from unknown task %s.\n",
		       __func__, PSC_printTID(msg.header.sender));
	} else {
	    int rank = getClientRank(msg.header.sender);
	    PSIlog_log(-1, "%s: forwarder %s (rank %d) disappeared.\n",
		       __func__, PSC_printTID(msg.header.sender), rank);

	    deregisterClient(rank);
	}
	break;
    case PSP_CD_ERROR:
	/* Ignore */
	break;
    case PSP_CD_SENDSTOP:
	handleSENDSTOP(&msg);
	break;
    case PSP_CD_SENDCONT:
	handleSENDCONT(&msg);
	break;
    default:
	PSIlog_log(-1, "%s: Unknown message type %s from %s\n", __func__,
		   PSP_printMsg(msg.header.type),
		   PSC_printTID(msg.header.sender));
    }

    return 0;
}

static void enableDaemonSock(int fd)
{
    /* Receive data from local daemon */
    Selector_register(fd, handleDaemonMsg, NULL);

    /* Enable sendDaemonMsg() to send data */
    daemonSock = fd;

    /* Initialize PSLog accordingly */
    PSLog_init(fd, -1 /* nodeID */, 2 /* versionID */);

    PSIlog_log(PSILOG_LOG_VERB, "Daemon Socket (%d) enabled.\n", fd);
}

/**
 * @brief The main loop
 *
 * Does all the logging work. All forwarders can connect and log via
 * the logger. Forwarders send I/O data via @ref STDOUT and @ref
 * STDERR messages. Furthermore USAGE and FINALIZE messages from the
 * forwarders are handled.
 *
 * @return No return value.
 */
static void loop(void)
{
    timeoutval = 0;

    /*
     * Loop until there is no connection left. Pay attention to the startup
     * phase, while no connection exists. Wait at least MIN_WAIT * 1000 msec.
     */
    while (getNoClients() > 0 || timeoutval < MIN_WAIT) {
	if (mergeOutput && usize > 1) displayCachedOutput(false);

	if (Swait(1000 /* msec */) < 0) {
	    if (errno != EINTR) PSIlog_warn(-1, errno, "select()");
	    continue;
	}
	if (!getNoClients()) timeoutval++;
    }
    if (mergeOutput && usize > 1) {
	displayCachedOutput(false);
	/* flush output */
	displayCachedOutput(true);
    }
    if (!getenv("PSI_NOMSGLOGGERDONE")) {
	PSIlog_stderr(-1, "\n");
	PSIlog_log(-1, "done\n");
    }
    if (enableGDB) linenoiseRemoveHandlerCallback();

    return;
}

/**
 * @brief The main program
 *
 * After registering several signals gets daemon socket and local
 * ParaStation ID from arguments. Furthermore the global variables
 * @ref forw_verbose, @ref prependSource @ref InputDest and @ref usage
 * are set from environment. After setting up logging finally @ref
 * loop() is called. Upon return from this central loop, all further
 * arguments to the two already evaluated above are executed via
 * system() calls.
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
 */
int main( int argc, char**argv)
{
    char *logFileName = getenv("__PSI_LOGGER_IO_FILE");
    initLogs(logFileName);

    /* block SIGTTIN so logger works also in background */
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGTTIN);
    if (sigprocmask(SIG_BLOCK, &set, NULL)) {
	PSIlog_warn(-1, errno, "sigprocmask()");
    }

    PSC_setSigHandler(SIGTERM,  sighandler);
    PSC_setSigHandler(SIGINT,   sighandler);
    PSC_setSigHandler(SIGHUP,   sighandler);
    PSC_setSigHandler(SIGTSTP,  sighandler);
    PSC_setSigHandler(SIGCONT,  sighandler);
    PSC_setSigHandler(SIGWINCH, sighandler);
    PSC_setSigHandler(SIGUSR1,  sighandler);
    PSC_setSigHandler(SIGUSR2,  sighandler);
    PSC_setSigHandler(SIGQUIT,  sighandler);

    if (argc < 3) {
	PSIlog_log(-1, "Sorry, program must be called correctly"
		" inside an application.\n");
	PSIlog_log(-1, "%d arguments:", argc);
	for (int i = 0; i < argc; i++) PSIlog_log(-1, " '%s'", argv[i]);
	PSIlog_log(-1, "\n");
	PSIlog_finalizeLogs();
	exit(1);
    }

    /* daemon socket lost during exec() */
    char *end;
    int dSock = strtol(argv[1], &end, 10);
    if (*end != '\0' || (dSock==0 && errno==EINVAL)) {
	PSIlog_log(-1, "Sorry, program must be called correctly"
		   " inside an application.\n");
	PSIlog_log(-1, "'%s' is not a socket number.\n", argv[1]);
	PSIlog_finalizeLogs();
	exit(1);
    }
    if (dSock < 0) {
	PSIlog_log(-1, "Not connected to local daemon. Exiting.\n");
	PSIlog_finalizeLogs();
	exit(1);
    }

    /* ParaStation ID lost during exec() */
    int psID = strtol(argv[2], &end, 10);
    if (*end != '\0' || (psID == 0 && errno == EINVAL)) {
	PSIlog_log(-1, "Sorry, program must be called correctly"
		   " inside an application.\n");
	PSIlog_log(-1, "'%s' is not a ParaStation ID.\n", argv[2]);
	PSIlog_finalizeLogs();
	exit(1);
    }
    PSC_setMyID(psID);

    if (getenv("PSI_LOGGERDEBUG")) {
	PSIlog_setDebugMask(PSILOG_LOG_VERB);
	PSIlog_log(PSILOG_LOG_VERB, "Going to be verbose.\n");
	PSIlog_log(PSILOG_LOG_VERB, "Daemon on %d\n", dSock);
	PSIlog_log(PSILOG_LOG_VERB, "My ID is %d\n", psID);
    }

    if (getenv("PSI_TERMINATED")) {
	PSIlog_setDebugMask(PSIlog_getDebugMask() | PSILOG_LOG_TERM);
	PSIlog_log(PSILOG_LOG_VERB, "Going to show terminated processes.\n");
    }

    if (getenv("PSI_FORWARDERDEBUG")) {
	forw_verbose=true;
	PSIlog_log(PSILOG_LOG_VERB, "Forwarders will be verbose, too.\n");
    }

    if (getenv("PSI_TIMESTAMPS")) {
	PSIlog_log(PSILOG_LOG_VERB, "Print detailed time-marks.\n");
	PSIlog_setTimeFlag(true);
    }

    if (getenv("PSI_SOURCEPRINTF")) {
	prependSource = true;
	PSIlog_log(PSILOG_LOG_VERB, "Will print source-info.\n");
    }

    char *envstr = getenv("PSI_NP_INFO");
    if (envstr) {
	np = atoi(envstr);
	int tmp = np-1;
	while (tmp > 9) {
	    npWidth++;
	    tmp /= 10;
	}
    }

    if ((envstr = getenv("PSI_USIZE_INFO"))) {
	usize = atoi(envstr);
    }

    /* Destination list has to be set before initClients() */
    char *input = "";
    if (dSock) {
	input = getenv("PSI_INPUTDEST");
	if (!input) input = "0";

	PSIlog_log(PSILOG_LOG_VERB, "Input goes to [%s].\n", input);
    } else {
	/* dSock = 0 implies no stdin connected */
	PSIlog_log(PSILOG_LOG_VERB, "No stdin available.\n");
    }
    setupDestList(input);

    if (getenv("PSI_ENABLE_GDB")) {
	enableGDB = true;
	linenoiseSetHandlerCallback(NULL, readGDBInput);
	PSIlog_log(PSILOG_LOG_VERB, "Enabling gdb functions.\n");
	snprintf(GDBprompt, sizeof(GDBprompt), "[%s]: (gdb) ", getDestStr(128));
	linenoiseSetPrompt(GDBprompt);
    }

    initClients(-2, np ? np-1 : 0);
    if (getenv("PSI_MERGEOUTPUT")) {
	mergeOutput = true;
	PSIlog_log(PSILOG_LOG_VERB, "Will merge the output of all ranks.\n");
	outputMergeInit();
    }

    if (getenv("PSI_USE_VALGRIND")) {
	useValgrind = true;
	PSIlog_log(PSILOG_LOG_VERB, "Running on Valgrind cores.\n");
    }

    if (getenv("PSI_RUSAGE")) {
	showUsage = true;
	PSIlog_log(PSILOG_LOG_VERB, "Going to show resource usage.\n");
    }

    if (getenv("__PSI_RAW_IO")) {
	PSIlog_log(PSILOG_LOG_VERB, "Using raw IO\n");
	rawIO = true;

	if (prependSource) {
	    PSIlog_log(-1, "Option clash between 'prepend source' and"
		       " 'raw IO'\n");
	    exit(1);
	}
	if (mergeOutput) {
	    PSIlog_log(-1, "Option clash between 'merging' and 'raw IO'\n");
	    exit(1);
	}
	if (enableGDB) {
	    PSIlog_log(-1, "Option clash between 'remote GDB' and 'raw IO'\n");
	    exit(1);
	}
    }

    if (getenv("PSI_LOG_SILENT")) {
	PSIlog_log(PSILOG_LOG_VERB, "Silent operation.\n");
	logger_finalize(PSIlog_logger);
	PSIlog_logger = NULL;
    }

    Selector_init(stderr);

    /* register Selector, etc. */
    enableDaemonSock(dSock);

    /* init the timer structure */
    if (!Timer_isInitialized()) {
	Timer_init(stderr);
    }

    if (getenv("PSI_LOGGER_RAW_MODE") && isatty(STDIN_FILENO)) {
	enterRawMode();
	if (PSIlog_stdout) logger_setWaitNLFlag(PSIlog_stdout, false);
	if (PSIlog_stderr) logger_setWaitNLFlag(PSIlog_stderr, false);
    }

    if (getenv("__PSI_LOGGER_UNBUFFERED")) {
	if (PSIlog_stdout) logger_setWaitNLFlag(PSIlog_stdout, false);
	if (PSIlog_stderr) logger_setWaitNLFlag(PSIlog_stderr, false);
    }

    if ((envstr = getenv("PSI_MAXTIME"))) {
	if (!*envstr) {
	    PSIlog_log(-1, "PSI_MAXTIME is empty.\n");
	} else {
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

    enableDebugSock();

    /* call the loop which does all the work */
    loop();

    leaveRawMode();

    sendAcctData();

    closeDaemonSock(dSock);

    for (int i = 3; i < argc; i++) {
	PSIlog_log(PSILOG_LOG_VERB, "Execute '%s'\n", argv[i]);
	int ret = system(argv[i]);
	if (ret < 0) {
	    PSIlog_log(-1, "Failed to execute '%s' via system()\n", argv[i]);
	}
    }

    PSIlog_finalizeLogs();

    return retVal ? retVal : (signaled ? -1 : 0);
}
