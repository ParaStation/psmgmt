/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2008 ParTec Cluster Competence Center GmbH, Munich
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
#include "kvscommon.h"
#include "psdaemonprotocol.h"
#include "pslog.h"
#include "psidmsgbuf.h"
#include "psidnodes.h"
#include "psidpmiprotocol.h"

#include "psidforwarder.h"

typedef struct {
    int session;
    unsigned long utime;
    unsigned long stime;
    unsigned long cutime;
    unsigned long cstime;
    unsigned long threads;
    unsigned long maxvsize;
    long maxrss;
} AccountData;

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

/** Set of fds the forwarder listens to */
fd_set readfds;

/** Set of fds the forwarder writes to (this is stdinSock) */
fd_set writefds;

/** Number of currently open file descriptors (except to one to the daemon) */
int openfds = 0;

static enum {
    IDLE,
    CONNECTED,
    CLOSED,
} pmiStatus = IDLE;

/** List of messages waiting to be sent to
 */
msgbuf_t *oldMsgs = NULL;

/** Set to 1 if the forwarder should do accounting */
int accounting = 0;

/** Interval in sec for polling on accounting information, e.g. memory */
int acctPollTime = 0;

/** Structure which holds the accounting information */
AccountData accData;

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
    FD_CLR(daemonSock, &readfds);
    daemonSock = -1;
}

/**
 * @brief Close socket which is connected to the pmi client.
 *
 * @return No return value.
 */
static void closePMIClientSocket(void)
{
    /* close pmi client socket */
    if (PMIClientSock > 0 ) {
	FD_CLR(PMIClientSock, &readfds);	
	close(PMIClientSock);
	PMIClientSock = -1;
    }
}

/**
 * @brief Close the socket which listens for new pmi connections.
 *
 * @return No return value.
 */
static void closePMIAcceptSocket(void)
{
    /* close pmi accept socket */
    if (PMISock > 0 ) {
	FD_CLR(PMISock, &readfds);	
	close(PMISock);
	PMISock = -1;
    }	
}

/**
 * @brief Finds all child processes auf @pid and
 * returns the sum of the used vmem in kb.
 *
 * @param pid The pid of the parent process. 
 *
 * @param buf Buffer for reading the proc structure.
 *
 * @param size The size of the buffer.
 *
 * @param dir Handle to directory where to read the
 * information ("/proc/")
 *
 * @return Returns the sum of the used vmem by all childs.
 */
static long getAllChildsVMem(int pid, char *buf, int size, DIR *dir)
{
    /** Format string of /proc/pid/stat */
    static char stat_format[] = "%*d %*s %*c %d %*d %*d %*d %*d %*u %*u \
    %*u %*u %*u %*d %*d %*d %*d %*d %*d %*u \
    %*u %*u %lu %*ld %*u %*u %*u %*u %*u \
    %*u %*u %*u %*u %*u %*u %*u %*u %*d %*d";
    
    FILE *fd;
    struct dirent *dent;
    int newpid, ppid;
    unsigned long vmem = 0;
    unsigned long newvmem = 0;

    rewinddir(dir);

    while ((dent = readdir(dir)) != NULL){
	if (!isdigit(dent->d_name[0])){
	    continue;
	}
	newpid = atoi(dent->d_name);
	
	if (!isdigit(newpid) || newpid < 0) {
	    continue;
	}

	snprintf(buf, size, "/proc/%i/stat", newpid);
	if ((fd = fopen(buf,"r")) == NULL) {
	    continue;
	}
	if ((fscanf(fd, stat_format, &ppid, &newvmem)) != 2) {
	    fclose(fd);
	    continue;
	}
	fclose(fd);
	if (ppid == pid) {
	    vmem += newvmem; 
	    vmem += getAllChildsVMem(newpid, buf, size, dir);
	}
    }
    return vmem;
}

/**
 * @brief Finds all child processes auf @pid and
 * returns the sum of the used rss(mem).
 *
 * @param pid The pid of the parent process. 
 *
 * @param buf Buffer for reading the proc structure.
 *
 * @param size The size of the buffer.
 *
 * @param dir Handle to directory where to read the
 * information ("/proc/")
 *
 * @return Returns the sum of the used rss by all childs.
 */
static long getAllChildsMem(int pid, char *buf, int size, DIR *dir)
{
    /** Format string of /proc/pid/stat */
    static char stat_format[] = "%*d %*s %*c %d %*d %*d %*d %*d %*u %*u \
    %*u %*u %*u %*d %*d %*d %*d %*d %*d %*u \
    %*u %*u %*lu %ld %*u %*u %*u %*u %*u \
    %*u %*u %*u %*u %*u %*u %*u %*u %*d %*d";
    
    FILE *fd;
    struct dirent *dent;
    int newpid, ppid;
    long mem = 0;
    long newmem = 0;

    rewinddir(dir);

    while ((dent = readdir(dir)) != NULL){
	if (!isdigit(dent->d_name[0])){
	    continue;
	}
	newpid = atoi(dent->d_name);
	
	if (!isdigit(newpid) || newpid < 0) {
	    continue;
	}

	snprintf(buf, size, "/proc/%i/stat", newpid);
	if ((fd = fopen(buf,"r")) == NULL) {
	    continue;
	}
	
	if ((fscanf(fd, stat_format, &ppid, &newmem)) != 2) {
	    fclose(fd);
	    continue;
	}
	fclose(fd);
	
	if (ppid == pid) {
	    mem += newmem; 
	    mem += getAllChildsMem(newpid, buf, size, dir);
	}
    }
    return mem;
}

/**
 * @brief Update Accounting Data.
 *
 * Reads data from /proc/pid/stat and updates the
 * accounting data structure accData. Used for generating
 * accounting files and to replace the resmom of torque.
 * For accounting the memory usage the rss and vsize or monitored.
 *
 * VSIZE (Virtual memory SIZE) - The amount of memory the process is
 * currently using. This includes the amount in RAM and the amount in
 * swap.
 *
 * RSS (Resident Set Size) - The portion of a process that exists in
 * physical memory (RAM). The rest of the program exists in swap. If
 * the computer has not used swap, this number will be equal to VSIZE.
 *
 * @return No return value.
 */
static void updateAccountData(void)
{
    /** Format string of /proc/pid/stat */
    static char stat_format[] = 
		    "%*d "	    /* pid */
		    "%*s "	    /* comm */
		    "%*c "	    /* state */
		    "%*d %*d "	    /* ppid pgrp */
		    "%d "	    /* session */
		    "%*d "	    /* tty_nr */
		    "%*d "	    /* tpgid */
		    "%*u "	    /* flags */
		    "%*lu %*lu "    /* minflt cminflt */
		    "%*lu %*lu "    /* majflt cmajflt */ 
		    "%lu %lu "	    /* utime stime */
		    "%lu %lu "	    /* cutime cstime */
		    "%*d "	    /* priority */
		    "%*d "	    /* nice */
		    "%lu "	    /* num_threads */
		    "%*u "	    /* itrealvalue */
		    "%*u "	    /* starttime */
		    "%lu %lu "	    /* vsize rss*/
		    "%*u "	    /* rlim */
		    "%*u %*u "	    /* startcode endcode startstack */
		    "%*u "	    /* startstack */
		    "%*u %*u "	    /* kstkesp kstkeip */
		    "%*u %*u "	    /* signal blocked */
		    "%*u %*u "	    /* sigignore sigcatch */
		    "%*u "	    /* wchan */
		    "%*u %*u "	    /* nswap cnswap */
		    "%*d "	    /* exit_signal (kernel 2.1.22) */
		    "%*d "	    /* processor  (kernel 2.2.8) */
		    "%*lu "	    /* rt_priority (kernel 2.5.19) */
		    "%*lu "	    /* policy (kernel 2.5.19) */
		    "%*llu";	    /* delayacct_blkio_ticks (kernel 2.6.18) */
    int pid, res;
    long rssnew;
    unsigned long vsizenew = 0, newthreads = 0;
    static char buf[1024];
    FILE *fd;
    DIR *dir;
    static int rate = 0;
    
    pid = PSC_getPID(childTask->tid);

    if (rate <= 0) {
	rate = sysconf(_SC_CLK_TCK);
	if (rate < 0) {
	    PSID_warn(-1, errno, "%s: sysconf(_SC_CLK_TCK) failed", __func__);
	    acctPollTime = 0; 
	    return;
	}
    }
    
    snprintf(buf, sizeof(buf), "/proc/%i/stat", pid);
    if (!(fd = fopen(buf,"r"))) {
	PSID_warn(-1, errno, "%s: fopen(/proc/%i/stat) failed", __func__, pid);
	acctPollTime = 0; 
	return;
    }

    res = fscanf(fd, stat_format,
	    &accData.session,
	    &accData.utime,
	    &accData.stime,
	    &accData.cutime,
	    &accData.cstime,
	    &newthreads,
	    &vsizenew,
	    &rssnew);

    fclose(fd);

    if (res != 8) {
	PSID_log(-1, "%s: error reading accounting data,"
		 " invalid kernel version?\n", __func__);
	acctPollTime = 0; 
	return;
    }

    /* get mem and vmem for all childs  */
    if ((dir = opendir("/proc/"))) {
	rssnew += getAllChildsMem(pid, buf, sizeof(buf), dir);
	vsizenew += getAllChildsVMem(pid, buf, sizeof(buf), dir);
	closedir(dir);
    }

    /* set max rss (resident set size) */
    if (rssnew > accData.maxrss) accData.maxrss = rssnew;

    /* set max threads */
    if (newthreads > accData.threads) accData.threads = newthreads;

    /* set max vmem */
    if (vsizenew > accData.maxvsize) accData.maxvsize = vsizenew;
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

    if (loggerTID < 0) {
	PSID_log(-1, "%s:  not connected\n", __func__);
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
    int ret;
   
    if (daemonSock < 0) {
	PSID_log(-1, "%s: not connected\n", __func__);
	errno = EPIPE;

	return -1;
    }

    ret = PSLog_read(msg, timeout);

    if (ret < 0) {
	PSID_warn(-1, errno, "%s: PSLog_read()", __func__);
	closeDaemonSock();

	return ret;
    }

    if (!ret) {
	PSID_warn(-1, errno, "%s: nothing received", __func__);
	return ret;
    }

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
    default:
	PSID_log(-1, "%s: Unknown message type %s\n",
		 __func__, PSDaemonP_printMsg(msg->type));
	ret = 0;
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

    if (verbose) {
	char txt[128];
        snprintf(txt, sizeof(txt), "%s type %s (len=%d) to %s\n",
                 __func__, PSDaemonP_printMsg(msg->type),
		 msg->len, PSC_printTID(msg->dest));
        PSIDfwd_printMsg(STDERR, txt);
    }

    return msg->len;
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
    PSLog_Msg_t msg;
    struct timeval timeout = {10, 0};
    int ret;

    loggerTID = tid; /* Only set for the first sendMsg()/recvMsg() pair */

    sendMsg(INITIALIZE, NULL, 0);

    ret = recvMsg(&msg, &timeout);

    loggerTID = -1;

    if (ret <= 0) {
	PSID_log(-1, "%s(%s): Connection refused\n",
		 __func__, PSC_printTID(tid));
	errno = ECONNREFUSED;
	return -1;
    }

    if (msg.header.len != PSLog_headerSize + (int) sizeof(int)) {
	PSID_log(-1, "%s(%s): Message to short\n",
		 __func__, PSC_printTID(tid));
	errno = ECONNREFUSED;
	return -1;
    } else if (msg.type != INITIALIZE) {
	PSID_log(-1 ,"%s(%s): Protocol messed up\n",
		 __func__, PSC_printTID(tid));
	errno = ECONNREFUSED;
	return -1;
    } else if (msg.header.sender != tid) {
	PSID_log(-1, "%s(%s): Got INITIALIZE not from logger\n",
		 __func__, PSC_printTID(tid));
	errno = ECONNREFUSED;
	return -1;
    } else {
	loggerTID = tid;
	verbose = *(int *) msg.buf;
	PSID_log(PSID_LOG_SPAWN, "%s(%s): Connected\n",
		 __func__, PSC_printTID(tid));
    }

    return 0;
}

/**
 * @brief Deliver signal.
 *
 * Deliver signal @a signal to controlled process. If the child is
 * interactive, the signal is send to the forground process group of
 * the controlling tty. Otherwise it's delivered to process with ID @a
 * dest.
 *
 * @param dest Process ID of the process to send signal to. Not used
 * for interactive childs.
 *
 * @param signal The signal to send.
 *
 * @return No return value.
 */
static void sendSignal(pid_t dest, int signal)
{
    pid_t pid = 0;

    /* determine foreground process group for or default pid */
    if (childTask->interactive) {
	pid = tcgetpgrp(stderrSock);
	if (pid == -1) {
	    PSID_warn((errno==EBADF) ? PSID_LOG_SIGNAL : -1, errno,
		      "%s: tcgetpgrp()", __func__);
	    pid = 0;
	}
	/* Send signal to process-group */
	pid = -pid;
    }
    if (!pid) pid = dest;

    /* actually send the signal */
    if (kill(pid, signal) == -1) {
	PSID_warn((errno==ESRCH) ? PSID_LOG_SIGNAL : -1, errno,
		  "%s: kill(%d, %d)", __func__, pid, signal);
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
static void handleSignalMsg(PSLog_Msg_t msg)
{
    char *ptr = msg.buf;
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
    int ret;
    struct timeval timeout = {10, 0};

    sendMsg(FINALIZE, (char *)&status, sizeof(status));

 again:
    ret = recvMsg(&msg, &timeout);

    if (ret < 0) {
	if (errno == EPIPE) {
	    PSID_log(-1, "%s: logger %s already disappeared\n", __func__,
		     PSC_printTID(childTask->loggertid));
	} else {
	    PSID_warn(-1, errno, "%s: recvMsg()", __func__);
	}
    } else if (!ret) {
	PSID_log(-1, "%s: receive timed out. logger %s disappeared\n", __func__,
		 PSC_printTID(childTask->loggertid));
    } else if (msg.type != EXIT) {
	if (msg.type == STDIN) goto again; /* Ignore late STDIN messages */
	if (msg.type == SIGNAL) {
	    handleSignalMsg(msg);
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
			 "%s: got error %d on stdinSock: %s",
			 __func__, errno, errstr ? errstr : "UNKNOWN");
		PSIDfwd_printMsg(STDERR, obuf);
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
	if (stdinSock != -1) FD_CLR(stdinSock, &writefds);
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
    if ((written != len) || (oldMsgs && !len)) {
        if (!storeMsg(msg, written)) errno = EWOULDBLOCK;
	if (stdinSock != -1) FD_SET(stdinSock, &writefds);
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
	case PSP_CD_RELEASERES:
	    break;
	case PSP_CC_MSG:
	    switch (msg.type) {
	    case STDIN:
		if (verbose) {
		    snprintf(obuf, sizeof(obuf),
			     "%s: %d byte received on STDIN\n", __func__,
			     msg.header.len - PSLog_headerSize);
		    PSIDfwd_printMsg(STDERR, obuf);
		}
		if (stdinSock<0) {
		    snprintf(obuf, sizeof(obuf),
			     "%s: STDIN already closed\n", __func__);
		    PSIDfwd_printMsg(STDERR, obuf);
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
		pmi_handleKvsRet(msg);
		break;
	    case SIGNAL:
		handleSignalMsg(msg);
		break;
	    case WINCH:
		/* Logger detected change in window-size */
		if (stdinSock>=0) {
		    struct winsize w;
		    int count = msg.header.len - PSLog_headerSize, len = 0;
		    int *buf = (int *)msg.buf;

		    if (count != 4 * sizeof(*buf)) {
			snprintf(obuf, sizeof(obuf),
				 "%s: Corrupted WINCH message\n", __func__);
			PSIDfwd_printMsg(STDERR, obuf);
			break;
		    }

		    w.ws_col = buf[len++];
		    w.ws_row = buf[len++];
		    w.ws_xpixel = buf[len++];
		    w.ws_ypixel = buf[len++];

		    (void) ioctl(stdinSock, TIOCSWINSZ, &w);

		    if (verbose) {
			snprintf(obuf, sizeof(obuf), "%s: WINCH to"
				 " col %d row %d xpixel %d ypixel %d\n",
				 __func__, w.ws_col, w.ws_row, 
				 w.ws_xpixel, w.ws_ypixel);
			PSIDfwd_printMsg(STDERR, obuf);
		    }
		}
		break;
	    default:
		snprintf(obuf, sizeof(obuf),
			 "%s: Unknown type %d\n", __func__, msg.type);
		PSIDfwd_printMsg(STDERR, obuf);
	    }
	    break;
	default:
	    snprintf(obuf, sizeof(obuf), "%s: Unexpected msg type %s\n",
		     __func__, PSP_printMsg(msg.header.type));
	    PSIDfwd_printMsg(STDERR, obuf);
	}
    } else if (!ret) {
	/* The connection to the daemon died. Kill the client the hard way. */
	kill(-PSC_getPID(childTask->tid), SIGKILL);
    }
	
    return ret;
}

/* @brief  Read pmi message from the client.
 *
 * @return Usually the number of bytes received is returned. If an
 * error occured, -1 is returned and errno is set appropriately.
 *
 */
static int readFromPMIClient(void)
{
    char msgBuf[PMIU_MAXLINE], obuf[120];
    ssize_t len;
    int ret;

    len = recv(PMIClientSock, msgBuf, sizeof(msgBuf), 0);

    /* no data received from client */
    if (!len) {
	snprintf(obuf, sizeof(obuf),
		 "%s: lost connection to the pmi client\n", __func__);
	PSIDfwd_printMsg(STDERR, obuf);

	/*close connection */
	closePMIClientSocket();
	return -1;
    }

    /* socket error occured */
    if (len < 0) {
	snprintf(obuf, sizeof(obuf),
		 "%s: error on pmi socket occured\n", __func__);
	PSIDfwd_printMsg(STDERR, obuf);
	return -1;
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
	msg.header.sender = PSC_getTID(-1, getpid());
	msg.header.dest = childTask->tid;
	msg.header.len = sizeof(msg);
	msg.signal = -1;
	msg.answer = 1;

	sendDaemonMsg((DDMsg_t *)&msg);

	pmiStatus = CLOSED;
    }
    return len;
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
	    if (errno == EINTR) {
		continue;
	    } else {
		char txt[128];
		snprintf(txt, sizeof(txt),
			 "PSID_forwarder: %s: error on select(): %s\n",
			 __func__, strerror(errno));
		PSIDfwd_printMsg(STDERR, txt);
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
static void sendAccoutingData(struct rusage rusage, int status)
{
    DDTypedBufferMsg_t msg;
    char *ptr = msg.buf;
    struct timeval now, walltime;
    long pagesize;

    msg.header.type = PSP_CD_ACCOUNT;
    msg.header.dest = PSC_getTID(-1, 0);
    msg.header.sender = PSC_getTID(-1, getpid());
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

    /* size of max used mem */
    *(uint64_t *)ptr = (uint64_t)accData.maxrss;
    ptr += sizeof(uint64_t);
    msg.header.len += sizeof(uint64_t);

    /* size of max used vmem */
    *(uint64_t *)ptr = (uint64_t)accData.maxvsize;
    ptr += sizeof(uint64_t);
    msg.header.len += sizeof(uint64_t);
    
    /* walltime used by child */
    gettimeofday(&now, NULL);
    timersub(&now, &childTask->started, &walltime);
    memcpy(ptr, &walltime, sizeof(walltime));
    ptr += sizeof(walltime);
    msg.header.len += sizeof(walltime);

    /* number of threads */
    *(uint32_t *)ptr = accData.threads;
    ptr += sizeof(uint32_t);
    msg.header.len += sizeof(uint32_t);
    
    /* session id of job */
    *(int32_t *)ptr = accData.session;
    ptr += sizeof(int32_t);
    msg.header.len += sizeof(int32_t);

    /* child's return status */
    *(int32_t *)ptr = status;
    ptr += sizeof(int32_t);
    msg.header.len += sizeof(int32_t);

    sendDaemonMsg((DDMsg_t *)&msg);
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
    pid_t pid = 0;

    char txt[80];

    switch (sig) {
    case SIGUSR1:
	snprintf(txt, sizeof(txt),
		 "[%d] PSID_forwarder: open sockets left:", childTask->rank);
	for (i=0; i<FD_SETSIZE; i++) {
	    if (FD_ISSET(i, &readfds) && i != daemonSock) {
		snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), " %d", i);
	    }
	}
	snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), "\n");
	PSIDfwd_printMsg(STDERR, txt);
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
		PSIDfwd_printMsg(STDERR, txt2);
		PSIDfwd_printMsg(STDOUT, buf);
	    }
	}
	break;
    case SIGCHLD:
	if (verbose) {
	    snprintf(txt, sizeof(txt),
		     "[%d] PSID_forwarder: Got SIGCHLD\n", childTask->rank);
	    PSIDfwd_printMsg(STDERR, txt);
	}
	
	/* if child is stopped, return */
	pid = wait3(&status, WUNTRACED | WNOHANG, &rusage);
	if (WIFSTOPPED(status)) break;
	
	/* Read all the remaining stuff from the controlled fds */
	while (openfds) {
	    fd_set fds;
	    int ret;
	
	    memcpy(&fds, &readfds, sizeof(fds));
	    
	    ret = select(FD_SETSIZE, &fds, NULL, NULL, NULL);
	    if (ret < 0) {
		if (errno != EINTR) {
		    snprintf(txt, sizeof(txt),
			     "PSID_forwarder: %s: error on select(): %s\n",
			     __func__, strerror(errno));
		    PSIDfwd_printMsg(STDERR, txt);
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
			} else if (sock==daemonSock) {
			    /* Read new input */
			    readFromLogger();
			    continue;
			} else if (sock==PMIClientSock) {
			    /* Read new pmi msg from client */
			    readFromPMIClient();
			    continue;
			} else {
			    /* close open sockets so we don't wait infinite */
			    ret--;
			    FD_CLR(sock, &readfds);
			    close(sock);
			    openfds--;
			    continue;
			}

			n = collectRead(sock, buf, sizeof(buf), &total);
			if (verbose) {
			    snprintf(txt, sizeof(txt), "PSID_forwarder:"
				     " got %d bytes on sock %d %d %d\n",
				     (int) total, sock, n, errno);
			    PSIDfwd_printMsg(STDERR, txt);
			}
			if (n==0 || (n<0 && errno==EIO)) {
			    /* socket closed */
			    close(sock);
			    FD_CLR(sock,&readfds);
			    openfds--;
			} else if (n<0 && errno!=ETIME && errno!=ECONNRESET) {
			    /* ignore the error */
			    snprintf(txt, sizeof(txt),
				     "PSID_forwarder: collectRead():%s\n",
				     strerror(errno));
			    PSIDfwd_printMsg(STDERR, txt);
			}
			if (total) {
			    /* something received. forward it to logger */
			    sendMsg(type, buf, total);
			}
		    }
		}
	    }
	}
	
	if (pid < 1) {
	    pid = wait3(&status, WUNTRACED, &rusage);
	    if (WIFSTOPPED(status)) break;
	}

	sendMsg(USAGE, (char *) &rusage, sizeof(rusage));

	/* Release, if no error occurred and not already done */
	if (pmiStatus == IDLE && WIFEXITED(status) && !WIFSIGNALED(status)) {
	    /* release the child */
	    DDSignalMsg_t msg;
	    msg.header.type = PSP_CD_RELEASE;
	    msg.header.sender = PSC_getTID(-1, getpid());
	    msg.header.dest = childTask->tid;
	    msg.header.len = sizeof(msg);
	    msg.signal = -1;
	    msg.answer = 0;  /* Don't expect answer in this late stage */
	    sendDaemonMsg((DDMsg_t *)&msg);
	}

	/* Send ACCOUNT message to daemon; will forward to accounters */
	if (accounting && childTask->group != TG_ADMINTASK
	    && childTask->group != TG_SERVICE) {
	    sendAccoutingData(rusage, status);
	}

	/* Send CHILDDEAD message to the daemon */
	{
	    DDErrorMsg_t msg;
	    msg.header.type = PSP_DD_CHILDDEAD;
	    msg.header.dest = PSC_getTID(-1, 0);
	    msg.header.sender = PSC_getTID(-1, getpid());
	    msg.error = status;
	    msg.request = PSC_getTID(-1, pid);
	    msg.header.len = sizeof(msg);
	    sendDaemonMsg((DDMsg_t *)&msg);
	}

	releaseLogger(status);

	/* Release the pmi client */
	closePMIClientSocket();

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
	PSIDfwd_printMsg(STDERR, txt);
    }

    signal(sig, sighandler);
}

/**
 * @brief Checks file table after select has failed.
 *
 * Detailed checking of the file table on validity after a select()
 * call has failed.
 *
 * @param fds Set of file descriptors that have to be checked.
 *
 * @return No return value.
 */
static void checkFileTable(fd_set *fds)
{
    fd_set testfds;
    int fd;
    struct timeval tv;
    char *errstr, buf[80];

    for (fd=0; fd<FD_SETSIZE; fd++) {
	if (!FD_ISSET(fd, fds)) continue;

	FD_ZERO(&testfds);
	FD_SET(fd, &testfds);

	tv.tv_sec=0;
	tv.tv_usec=0;

	if (select(FD_SETSIZE, &testfds, NULL, NULL, &tv) >= 0) continue;

	/* error : check if it's a wrong fd in the table or an interrupt */
	switch(errno){
	case EBADF :
	case EINVAL:
	case ENOMEM:
	    snprintf(buf, sizeof(buf), "%s(%d): %s -> close socket\n",
		     __func__, fd,
		     (errno==EBADF) ? "EBADF" :
		     (errno==EINVAL) ? "EINVAL" : "ENOMEM");
	    PSIDfwd_printMsg(STDERR, buf);
	    close(fd);
	    FD_CLR(fd, fds);
	    openfds--;
	    break;
	case EINTR:
	    snprintf(buf, sizeof(buf),
		     "%s(%d): EINTR -> trying again\n", __func__, fd);
	    PSIDfwd_printMsg(STDERR, buf);
	    fd--; /* try again */
	    break;
	default:
	    errstr=strerror(errno);
	    snprintf(buf, sizeof(buf),
		     "%s(%d): unrecognized error (%d): %s\n", __func__,
		     fd, errno, errstr ? errstr : "UNKNOWN");
	    PSIDfwd_printMsg(STDERR, buf);
	    break;
	}
    }
}

/**
 * @brief  Accept a new pmi client connection.
 *
 * @return No return value. 
 */
static void acceptPMIClient(void)
{
    unsigned int clientlen;
    struct sockaddr_in SAddr;
    char obuf[120];

    /* check if a client is already connected */
    if (PMIClientSock != -1) {
	snprintf(obuf, sizeof(obuf),
		 "%s: error only one pmi connection is allowed\n", __func__);
	PSIDfwd_printMsg(STDERR, obuf);
	return;
    }

    /* accept new pmi connection */
    clientlen = sizeof(SAddr);
    if ((PMIClientSock = accept( PMISock, (void *)&SAddr, &clientlen)) == -1) {
	snprintf(obuf, sizeof(obuf),
		 "%s: error on accepting new pmi connection\n", __func__);
	PSIDfwd_printMsg(STDERR, obuf);
	return;
    }

    /* init the pmi interface */
    pmi_init(PMIClientSock, childTask->loggertid, childTask->rank);

    /* close the socket which waits for new connections */
    closePMIAcceptSocket();
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
    time_t timeacc = 0, now;
    size_t total;
    PSLog_msg_t type;

    if (verbose) {
	snprintf(obuf, sizeof(obuf),
		 "PSID_forwarder: childTask=%s daemon=%d\n",
		 PSC_printTID(childTask->tid), daemonSock);
	PSIDfwd_printMsg(STDERR, obuf);
	snprintf(obuf, sizeof(obuf),
		 "PSID_forwarder: stdin=%d stdout=%d stderr=%d\n",
		 stdinSock, stdoutSock, stderrSock);
	PSIDfwd_printMsg(STDERR, obuf);
    }

    FD_ZERO(&readfds);
    if (stdoutSock != -1) {
	FD_SET(stdoutSock, &readfds);
	openfds++;
    }
    if (stderrSock != -1) {
	FD_SET(stderrSock, &readfds);
	openfds++;
    }
    FD_SET(daemonSock, &readfds);
    if (PMISock != -1) {
        FD_SET(PMISock, &readfds);
    }
    if (PMIClientSock != -1) {
	FD_SET(PMIClientSock, &readfds);
    }
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
			 "PSID_forwarder: %s: error on select(): %s\n",
			 __func__, strerror(errno));
		PSIDfwd_printMsg(STDERR, obuf);
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
		}else if (sock==PMISock) {
		    /* Accept new pmi connection */
		    acceptPMIClient();
		    FD_SET(PMIClientSock, &readfds);
		    continue;
		}else if (sock==PMIClientSock) {
		    /* Read new pmi msg from client */
		    readFromPMIClient();
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
		    PSIDfwd_printMsg(STDERR, obuf);
		    /* At least, read this stuff and throw it away */
		    n = read(sock, buf, sizeof(buf));
		    continue;
		}

		n = collectRead(sock, buf, sizeof(buf), &total);
		if (verbose) {
		    snprintf(obuf, sizeof(obuf),
			     "PSID_forwarder: got %ld bytes on sock %d\n",
			     (long) total, sock);
		    PSIDfwd_printMsg(STDERR, obuf);
		}
		if (n==0 || (n<0 && errno==EIO)) {
		    /* socket closed */
		    if (verbose) {
			snprintf(obuf, sizeof(obuf),
				 "PSID_forwarder: closing %d\n", sock);
			PSIDfwd_printMsg(STDERR, obuf);
		    }

		    shutdown(sock, SHUT_RD);
		    FD_CLR(sock, &readfds);
		    openfds--;
		    if (!openfds) {
			/* stdout and stderr closed -> wait for SIGCHLD */
			if (verbose) {
			    snprintf(obuf, sizeof(obuf),
				     "PSID_forwarder: wait for SIGCHLD\n");
			    PSIDfwd_printMsg(STDERR, obuf);
			}
		    }
		} else if (n<0 && errno!=ETIME && errno!=ECONNRESET) {
		    /* ignore the error */
		    snprintf(obuf, sizeof(obuf),
			     "PSID_forwarder: read():%s\n", strerror(errno));
		    PSIDfwd_printMsg(STDERR, obuf);
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
		    PSIDfwd_printMsg(STDERR, obuf);
		}
	    }
	}
	/* update Accounting Data*/
	if (accounting && acctPollTime
	    && childTask->group != TG_ADMINTASK
	    && childTask->group != TG_SERVICE 
	    && ((now = time(0)) - timeacc) > acctPollTime) {
	    timeacc = now;
	    updateAccountData();
	}

	PSID_blockSig(0, SIGCHLD);
    }

    return;
}

/* see header file for docu */
void PSID_forwarder(PStask_t *task, int daemonfd, int PMISocket,
		    PMItype_t PMItype, int doAccounting, int acctPollInterval)
{
    long flags;
    struct utsname uts;

    childTask = task;
    daemonSock = daemonfd;
    accounting = doAccounting;
    acctPollTime = acctPollInterval;

    stdinSock = task->stdin_fd;
    stdoutSock = task->stdout_fd;
    stderrSock = task->stderr_fd;

    /* catch SIGCHLD from client */
    signal(SIGCHLD, sighandler);
    signal(SIGUSR1, sighandler);

    PSLog_init(daemonSock, childTask->rank, 1);

    /* Make stdin nonblocking for us */
    flags = fcntl(stdinSock, F_GETFL);
    flags |= O_NONBLOCK;
    fcntl(stdinSock, F_SETFL, flags);

    if (connectLogger(childTask->loggertid) != 0) {
	/* There is no logger. Just wait for the client to finish. */
	PSID_blockSig(0, SIGCHLD);
	while (1) sleep(10);
    }

    /* Set up pmi connection */ 
    pmiType = PMItype;
    switch (pmiType) {
    case PMI_OVER_UNIX:
	PMIClientSock = PMISocket;    
	/* init the pmi interface */
	pmi_init(PMIClientSock, childTask->loggertid, childTask->rank);
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
   
    /* read plattform version */
    if (uname(&uts)) {
	PSID_log(-1, "%s: uname failed\n", __func__);
    } else {
	if (!!strcmp(uts.sysname, "Linux")) {
	    /* disable extended accouting if we are not running on linux */
	    accounting = 0;
	}
    }

    /* init accouting */
    if (accounting && acctPollTime) {
	/* accouting data structure */
	accData.session = 0;
	accData.utime = 0;
	accData.stime = 0;
	accData.cutime = 0;
	accData.cstime = 0;
	accData.threads = 0;
	accData.maxvsize = 0;
	accData.maxrss = 0;
    }
    
    /* call the loop which does all the work */
    loop();
}
