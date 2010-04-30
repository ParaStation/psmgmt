/*
 *               ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2010 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * \file
 * psid: ParaStation Daemon
 *
 * $Id$
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <popt.h>

#include "selector.h"
#include "mcast.h"
#include "rdp.h"
#include "config_parsing.h"

#include "pscommon.h"
#include "psprotocol.h"
#include "psdaemonprotocol.h"

#include "psidutil.h"
#include "psidnodes.h"
#include "psidtask.h"
#include "psidtimer.h"
#include "psidspawn.h"
#include "psidsignal.h"
#include "psidclient.h"
#include "psidrdp.h"
#include "psidcomm.h"
#include "psidinfo.h"
#include "psidoption.h"
#include "psidpartition.h"
#include "psidstatus.h"
#include "psidhw.h"
#include "psidaccount.h"
#include "psidstate.h"
#include "psidplugin.h"
#include "psidscripts.h"

struct timeval selectTime;

char psid_cvsid[] = "$Revision$";

/**
 * @brief Handle message on file-descriptor
 *
 * Handle an incoming messages on file-descriptor @a fd. It is
 * expected that a message is actually available on @a fd, i.e. that
 * select() was called on the descriptor beforehand.
 *
 * @param fd File-descriptor to handle.
 *
 * @return No return value.
 */
static void psicontrol(int fd)
{
    DDHugeMsg_t msg;

    int msglen;

    /* read the whole msg */
    msglen = recvMsg(fd, (DDMsg_t*)&msg, sizeof(msg));

    if (msglen==0) {
	/*
	 * closing connection
	 */
	if (fd == RDPSocket) {
	    PSID_log(-1, "%s: msglen 0 on RDPsocket\n", __func__);
	} else {
	    PSID_log(PSID_LOG_CLIENT, "%s(%d): closing connection\n",
		     __func__, fd);
	    deleteClient(fd);
	}
    } else if (msglen==-1) {
	if ((fd != RDPSocket) || (errno != EAGAIN)) {
	    PSID_warn(-1, errno, "%s(%d): recvMsg()", __func__, fd);
	}
    } else {
	if (msg.header.type == PSP_CD_CLIENTCONNECT) {
	    /* pass the (new) fd to corresponding handler function */
	    size_t off = msg.header.len - sizeof(msg.header);
	    *(int *) (msg.buf+off) = fd;
	}

	if (!PSID_handleMsg((DDBufferMsg_t *)&msg)) {
	    PSID_log(-1, "%s: Problem on socket %d\n", __func__, fd);
	}
    }
}

/**
 * @brief MCast callback handler
 *
 * Handle a callback from the MCast facility. The callback-type is
 * passed within @a msgid. @a buf might hold additional information
 * related to the callback. Currently two types of callback are
 * handled:
 *
 * - MCAST_NEW_CONNECTION: a new partner unaccessible before was detected.
 *
 * - MCAST_LOST_CONNECTION: a partner that was recently accessible
 *   disappeared.
 *
 * @param msgid Type of callback to handle
 *
 * @param buf Buffer holding extra information related to the
 * callback.
 *
 * @return No return value.
 */
static void MCastCallBack(int msgid, void *buf)
{
    int node;

    switch(msgid) {
    case MCAST_NEW_CONNECTION:
	node = *(int*)buf;
	PSID_log(PSID_LOG_STATUS | PSID_LOG_MCAST,
		 "%s(MCAST_NEW_CONNECTION,%d)\n", __func__, node);
	if (node!=PSC_getMyID() && !PSIDnodes_isUp(node)) {
	    if (send_DAEMONCONNECT(node)<0) {
		PSID_warn(PSID_LOG_STATUS, errno,
			  "%s: send_DAEMONCONNECT()", __func__);
	    }
	}
	break;
    case MCAST_LOST_CONNECTION:
	node = *(int*)buf;
	PSID_log(PSID_LOG_STATUS | PSID_LOG_MCAST,
		 "%s(MCAST_LOST_CONNECTION,%d)\n",
		 __func__, node);
	if (node != PSC_getMyID()) declareNodeDead(node, 0, 0);
	/*
	 * Send CONNECT msg via RDP. This should timeout and tell RDP that
	 * the connection is down.
	 */
	send_DAEMONCONNECT(node);
	break;
    default:
	PSID_log(-1, "%s(%d,%p). Unhandled message\n", __func__, msgid, buf);
    }
}

/**
 * @brief RDP callback handler
 *
 * Handle a callback from the RDP facility. The callback-type is
 * passed within @a msgid. @a buf might hold additional information
 * related to the callback. Currently four types of callback are
 * handled:
 *
 * - RDP_NEW_CONNECTION: a new partner unaccessible before was detected.
 *
 * - RDP_LOST_CONNECTION: a partner that was recently accessible
 *   disappeared.
 *
 * - RDP_PKT_UNDELIVERABLE: RDP was not able to deliver a packet
 *   originating on the local node. The actual packet is passed back
 *   within @a buf in order to create a suitable answer.
 *
 * - RDP_CAN_CONTINUE: RDP's flow control signals the possibility to
 *   send further packets to the destination indicated in @a buf.
 *
 * @param msgid Type of callback to handle
 *
 * @param buf Buffer holding extra information related to the
 * callback.
 *
 * @return No return value.
 */
static void RDPCallBack(int msgid, void *buf)
{
    switch(msgid) {
    case RDP_NEW_CONNECTION:
	if (! (PSID_getDaemonState() & PSID_STATE_SHUTDOWN)) {
	    int node = *(int*)buf;
	    PSID_log(PSID_LOG_STATUS | PSID_LOG_RDP,
		     "%s(RDP_NEW_CONNECTION,%d)\n", __func__, node);
	    if (node != PSC_getMyID() && !PSIDnodes_isUp(node)) {
		if (send_DAEMONCONNECT(node)<0) { // @todo Really necessary ?
		    PSID_warn(PSID_LOG_STATUS, errno,
			      "%s: send_DAEMONCONNECT()", __func__);
		}
	    }
	}
	break;
    case RDP_PKT_UNDELIVERABLE:
    {
	DDMsg_t *msg = (DDMsg_t*)((RDPDeadbuf*)buf)->buf;
	PSID_log(PSID_LOG_RDP,
		 "%s(RDP_PKT_UNDELIVERABLE, dest %x source %x type %s)\n",
		 __func__, msg->dest, msg->sender,
		 PSDaemonP_printMsg(msg->type));

	handleDroppedMsg(msg);
	break;
    }
    case RDP_LOST_CONNECTION:
	if (! (PSID_getDaemonState() & PSID_STATE_SHUTDOWN)) {
	    int node = *(int*)buf;
	    PSID_log(node != PSC_getMyID() ? PSID_LOG_STATUS|PSID_LOG_RDP : -1,
		     "%s(RDP_LOST_CONNECTION,%d)\n", __func__, node);
	    if (node != PSC_getMyID()) declareNodeDead(node, 1, 0);
	}
	break;
    case RDP_CAN_CONTINUE:
    {
	int node = *(int*)buf;
	flushRDPMsgs(node);
	break;
    }
    default:
	PSID_log(-1, "%s(%d,%p). Unhandled message\n", __func__, msgid, buf);
    }
}

/**
 * @brief Signal handler
 *
 * Handle signals catched by the local daemon. Most prominent all
 * SIGCHILD signals originating from processes fork()ed from the local
 * daemon are handled here.
 *
 * @param sig Signale to handle.
 *
 * @return No return value.
 */
static void sighandler(int sig)
{
    char sigStr[10];

    snprintf(sigStr, sizeof(sigStr), "%d", sig);

    switch (sig) {
    case SIGABRT:
    case SIGSEGV:
	PSID_log(-1, "Received signal %s. Shut down hardly.\n",
		 sys_siglist[sig] ? sys_siglist[sig] : sigStr);
	exit(-1);
	break;
    case SIGTERM:
	PSID_log(-1, "Received signal %s. Shut down.\n",
		 sys_siglist[sig] ? sys_siglist[sig] : sigStr);
	PSID_shutdown();
	break;
    case SIGCHLD:
    {
	pid_t pid;         /* pid of the child process */
	PStask_ID_t tid;   /* tid of the child process */
	int estatus;       /* termination status of the child process */

	while ((pid = waitpid(-1, &estatus, WNOHANG)) > 0){
	    /*
	     * Delete the task now. These should mainly be forwarders.
	     */
	    PStask_t *task;
	    int logClass = (WEXITSTATUS(estatus)||WIFSIGNALED(estatus)) ?
		-1 : PSID_LOG_CLIENT;

	    /* I'll just report it to the logfile */
	    PSID_log(logClass,
		     "Received SIGCHLD for pid %d (0x%06x) with status %d",
		     pid, pid, WEXITSTATUS(estatus));
	    if (WIFSIGNALED(estatus)) {
		PSID_log(logClass, " after signal %d", WTERMSIG(estatus));
	    }
	    PSID_log(logClass, "\n");

	    tid = PSC_getTID(-1, pid);

	    task = PStasklist_find(&managedTasks, tid);
	    if (task) {
		if (!task->killat) {
		    task->killat = time(NULL) + 10;
		}
		if (task->fd != -1) {
		    /* Make sure we get all pending messages */
		    FD_SET(task->fd, &PSID_readfds);
		} else {
		    /* task not connected, remove from tasklist */
		    PStask_cleanup(tid);
		}
	    }
	}
    }
    break;

    case  SIGPIPE:   /* write on a pipe with no one to read it */
	/* Ignore silently */
	break;
    case  SIGHUP:    /* hangup, generated when terminal disconnects */
//  case  SIGINT:    /* interrupt, generated from terminal special char */
    case  SIGQUIT:   /* (*) quit, generated from terminal special char */
    case  SIGTSTP:   /* (@) interactive stop */
    case  SIGCONT:   /* (!) continue if stopped */
    case  SIGVTALRM: /* virtual time alarm (see setitimer) */
    case  SIGPROF:   /* profiling time alarm (see setitimer) */
    case  SIGWINCH:  /* (+) window size changed */
    case  SIGALRM:   /* alarm clock timeout */
	PSID_log(-1, "Received signal %s. Continue\n",
		 sys_siglist[sig] ? sys_siglist[sig] : sigStr);
	break;
    case  SIGILL:    /* (*) illegal instruction (not reset when caught)*/
    case  SIGTRAP:   /* (*) trace trap (not reset when caught) */
    case  SIGFPE:    /* (*) floating point exception */
    case  SIGBUS:    /* (*) bus error (specification exception) */
#ifdef SIGEMT
    case  SIGEMT:    /* (*) EMT instruction */
#endif
#ifdef SIGSYS
    case  SIGSYS:    /* (*) bad argument to system call */
#endif
#ifdef SIGINFO
    case  SIGINFO:   /* (+) information request */
#endif
#ifdef SIGURG
    case  SIGURG:    /* (+) urgent contition on I/O channel */
#endif
#ifdef SIGIO
    case  SIGIO:     /* (+) I/O possible, or completed */
#endif
    case  SIGTTIN:   /* (@) background read attempted from control terminal*/
    case  SIGTTOU:   /* (@) background write attempted to control terminal */
    case  SIGXCPU:   /* cpu time limit exceeded (see setrlimit()) */
    case  SIGXFSZ:   /* file size limit exceeded (see setrlimit()) */
    case  SIGUSR1:   /* user defined signal 1 */
    case  SIGUSR2:   /* user defined signal 2 */
    default:
	PSID_log(-1, "Received signal %s. Shut down\n",
		 sys_siglist[sig] ? sys_siglist[sig] : sigStr);
	PSID_shutdown();
	break;
    }

    /* reset the sighandler */
    signal(sig, sighandler);
}

/**
 * @brief Initialize signal handlers
 *
 * Initialize all the signal handlers needed within the daemon.
 *
 * Additionally, SIGALRM's handler might be tweaked within the Timer
 * module.
 *
 * @return No return value.
 */
static void initSigHandlers(void)
{
    signal(SIGINT   ,sighandler);
    signal(SIGQUIT  ,sighandler);
    signal(SIGILL   ,sighandler);
    signal(SIGTRAP  ,sighandler);
    signal(SIGBUS   ,sighandler);
    signal(SIGFPE   ,sighandler);
    signal(SIGUSR1  ,sighandler);
    signal(SIGUSR2  ,sighandler);
    signal(SIGPIPE  ,sighandler);
    signal(SIGTERM  ,sighandler);
    signal(SIGCHLD  ,sighandler);
    signal(SIGCONT  ,sighandler);
    signal(SIGTSTP  ,sighandler);
    signal(SIGTTIN  ,sighandler);
    signal(SIGTTOU  ,sighandler);
    signal(SIGURG   ,sighandler);
    signal(SIGXCPU  ,sighandler);
    signal(SIGXFSZ  ,sighandler);
    signal(SIGVTALRM,sighandler);
    signal(SIGPROF  ,sighandler);
    signal(SIGWINCH ,sighandler);
    signal(SIGIO    ,sighandler);
#if defined(__alpha)
    /* Linux on Alpha*/
    signal( SIGSYS  ,sighandler);
    signal( SIGINFO ,sighandler);
#else
    signal(SIGSTKFLT,sighandler);
#endif
    signal(SIGHUP   ,SIG_IGN);
}

/**
 * @brief Print welcome
 *
 * Print a welcome message to the current log destination.
 *
 * @return No return value
 */
static void printWelcome(void)
{
    PSID_log(-1, "Starting ParaStation DAEMON\n");
    PSID_log(-1, "RPM Version %s-%s\n", VERSION_psmgmt, RELEASE_psmgmt);
    PSID_log(-1, "Protocol Version %d\n", PSProtocolVersion);
    PSID_log(-1, "Daemon-Protocol Version %d\n", PSDaemonProtocolVersion);
    PSID_log(-1, " (c) ParTec Cluster Competence Center GmbH "
	     "(www.par-tec.com)\n");

    return;
}

/**
 * @brief Checks file table after select has failed.
 *
 * Detailed checking of the file table on validity after a select(2)
 * call has failed. Thus all file descriptors within the set @a
 * controlfds are examined and handled if necessary.
 *
 * @param controlfds Set of file descriptors that have to be checked.
 *
 * @return No return value.
 */
static void checkFileTable(fd_set *controlfds)
{
    fd_set fdset;
    int fd;
    struct timeval tv;

    for (fd=0; fd<FD_SETSIZE; fd++) {
	if (FD_ISSET(fd, controlfds)) {
	    FD_ZERO(&fdset);
	    FD_SET(fd, &fdset);

	    tv.tv_sec=0;
	    tv.tv_usec=0;
	    if (select(FD_SETSIZE, &fdset, NULL, NULL, &tv) < 0) {
		/* error : check if it is a wrong fd in the table */
		switch (errno) {
		case EBADF:
		    PSID_log(-1, "%s(%d): EBADF -> close\n", __func__, fd);
		    deleteClient(fd);
		    break;
		case EINTR:
		    PSID_log(-1, "%s(%d): EINTR -> try again\n", __func__, fd);
		    fd--; /* try again */
		    break;
		case EINVAL:
		    PSID_log(-1, "%s(%d): wrong filenumber -> exit\n",
			     __func__, fd);
		    PSID_shutdown();
		    break;
		case ENOMEM:
		    PSID_log(-1, "%s(%d): not enough memory. exit\n",
			     __func__, fd);
		    PSID_shutdown();
		    break;
		default:
		    PSID_warn(-1, errno, "%s(%d): unrecognized error (%d)\n",
			      __func__, fd, errno);
		    break;
		}
	    }
	}
    }
}

/**
 * @brief Check for obstinate tasks
 *
 * The purpose of this function is twice; one the one hand it checks
 * for obstinate tasks and sends SIGKILL signales until the task
 * disappears. On the other hand it garbage-collects all deleted task
 * structures within the list of managed tasks and frees them.
 *
 * @return No return value.
 */
void checkObstinate(void)
{
    time_t now = time(NULL);
    list_t *t, *tmp;

    list_for_each_safe(t, tmp, &managedTasks) {
	PStask_t *task = list_entry(t, PStask_t, next);

	if (task->deleted) {
	    /* If task is still connected, wait for connection closed */
	    if (task->fd == -1) {
		PStasklist_dequeue(task);
		PStask_delete(task);
	    }
	} else if (task->killat && now > task->killat) {
	    int ret;
	    if (task->group != TG_LOGGER) {
		/* Send the signal to the whole process group */
		ret = PSID_kill(-PSC_getPID(task->tid), SIGKILL, task->uid);
	    } else {
		/* Unless it's a logger, which will never fork() */
		ret = PSID_kill(PSC_getPID(task->tid), SIGKILL, task->uid);
	    }
	    if (ret && errno == ESRCH) {
		if (!task->removeIt) {
		    PStask_cleanup(task->tid);
		} else {
		    task->deleted = 1;
		}
	    }
	}
    }

    cleanupSpawnTasks();
}


/**
 * @brief Print version info.
 *
 * Print version infos of the current psid.c CVS revision number to stderr.
 *
 * @return No return value.
 */
static void printVersion(void)
{
    char revision[] = "$Revision$";
    fprintf(stderr, "psid %s\b \n", revision+11);
}

int main(int argc, const char *argv[])
{
    poptContext optCon;   /* context for parsing command-line options */

    int rc, version = 0, debugMask = 0;
    char *logdest = NULL, *configfile = "/etc/parastation.conf";
    FILE *logfile = NULL;
    struct rlimit rlimit;

    struct poptOption optionsTable[] = {
	{ "debug", 'd', POPT_ARG_INT, &debugMask, 0,
	  "enable debugging with mask <mask>", "mask"},
	{ "configfile", 'f', POPT_ARG_STRING, &configfile, 0,
	  "use <file> as config-file (default is /etc/parastation.conf)",
	  "file"},
	{ "logfile", 'l', POPT_ARG_STRING, &logdest, 0,
	  "use <file> for logging (default is syslog(3))."
	  " <file> may be 'stderr' or 'stdout'", "file"},
	{ "version", 'v', POPT_ARG_NONE, &version, 0,
	  "output version information and exit", NULL},
	POPT_AUTOHELP
	{ NULL, '\0', 0, NULL, 0, NULL, NULL}
    };

    optCon = poptGetContext(NULL, argc, argv, optionsTable, 0);
    rc = poptGetNextOpt(optCon);

    if (version) {
	printVersion();
	return 0;
    }

    if (logdest) {
	if (strcasecmp(logdest, "stderr")==0) {
	    logfile = stderr;
	} else if (strcasecmp(logdest, "stdout")==0) {
	    logfile = stdout;
	} else {
	    logfile = fopen(logdest, "a+");
	    if (!logfile) {
		char *errstr = strerror(errno);
		fprintf(stderr, "Cannot open logfile '%s': %s\n", logdest,
			errstr ? errstr : "UNKNOWN");
		exit(1);
	    }
	}
    }

    if (!logfile) {
	openlog("psid",LOG_PID|LOG_CONS,LOG_DAEMON);
    }
    PSID_initLog(logfile);
    PSC_initLog(logfile);

    printWelcome();

    if (rc < -1) {
	/* an error occurred during option processing */
	poptPrintUsage(optCon, stderr, 0);
	PSID_log(-1, "%s: %s\n",
		 poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
		 poptStrerror(rc));
	if (!logfile)
	    fprintf(stderr, "%s: %s\n",
		    poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
		    poptStrerror(rc));

	return 1;
    }

    if (!debugMask || (logfile!=stderr && logfile!=stdout)) {
	/* Start as daemon */
	switch (fork()) {
	case -1:
	    PSID_exit(errno, "unable to fork server process");
	    break;
	case 0: /* I'm the child (and running further) */
	    break;
	default: /* I'm the parent and exiting */
	    return 0;
	    break;
       }
    }

    PSID_blockSig(1,SIGCHLD);
    initSigHandlers();

#define _PATH_TTY "/dev/tty"
    /* First disconnect from the old controlling tty. */
    {
	int fd = open(_PATH_TTY, O_RDWR | O_NOCTTY);
	if (fd >= 0) {
	    if (ioctl(fd, TIOCNOTTY, NULL)) {
		PSID_warn(-1, errno, "%s: ioctl(TIOCNOTTY)", __func__);
	    }
	    close(fd);
	}
    }


    /*
     * Disable stdin,stdout,stderr and install dummy replacement
     * Take care if stdout/stderr is used for logging
     */
    {
	int dummy_fd;

	dummy_fd=open("/dev/null", O_WRONLY , 0);
	dup2(dummy_fd, STDIN_FILENO);
	if (logfile!=stdout) dup2(dummy_fd, STDOUT_FILENO);
	if (logfile!=stderr) dup2(dummy_fd, STDERR_FILENO);
	close(dummy_fd);
    }

    /* Forget about inherited window sizes */
    unsetenv("LINES");
    unsetenv("COLUMNS");

    if (debugMask) {
	PSID_setDebugMask(debugMask);
	PSC_setDebugMask(debugMask);
	PSID_log(-1, "Debugging mode (mask 0x%x) enabled\n", debugMask);
    }

    /* Init fd sets */
    FD_ZERO(&PSID_readfds);
    FD_ZERO(&PSID_writefds);

    /* Try to get a lock. This will guarantee exlusiveness */
    PSID_getLock();

    /*
     * Create the Local Service Port as early as possible. Actual
     * handling is enabled later. This gives psiadmin the chance to
     * connect.
     */
    PSID_createMasterSock();

    /* read the config file */
    PSID_readConfigFile(logfile, configfile);
    /* Now we can rely on the config structure */

    {
	in_addr_t addr;

	PSID_log(-1, "My ID is %d\n", PSC_getMyID());

	addr = PSIDnodes_getAddr(PSC_getMyID());
	PSID_log(-1, "My IP is %s\n", inet_ntoa(*(struct in_addr *) &addr));
    }

    if (!logfile && config->logDest!=LOG_DAEMON) {
	PSID_log(-1, "Changing logging dest from LOG_DAEMON to %s\n",
		 config->logDest==LOG_KERN ? "LOG_KERN":
		 config->logDest==LOG_LOCAL0 ? "LOG_LOCAL0" :
		 config->logDest==LOG_LOCAL1 ? "LOG_LOCAL1" :
		 config->logDest==LOG_LOCAL2 ? "LOG_LOCAL2" :
		 config->logDest==LOG_LOCAL3 ? "LOG_LOCAL3" :
		 config->logDest==LOG_LOCAL4 ? "LOG_LOCAL4" :
		 config->logDest==LOG_LOCAL5 ? "LOG_LOCAL5" :
		 config->logDest==LOG_LOCAL6 ? "LOG_LOCAL6" :
		 config->logDest==LOG_LOCAL7 ? "LOG_LOCAL7" :
		 "UNKNOWN");
	closelog();

	openlog("psid", LOG_PID|LOG_CONS, config->logDest);
	printWelcome();
    }

    /* call startupScript, if any */
    if (config->startupScript && *config->startupScript) {
	int ret = PSID_execScript(config->startupScript, NULL, NULL, NULL);

	if (ret > 1) {
	    PSID_log(-1, "startup script '%s' failed. Exiting...\n",
		     config->startupScript);

	    exit(1);
	}
    }

    /* Catch SIGSEGV and SIGABRT if core dumps are suppressed */
    getrlimit(RLIMIT_CORE, &rlimit);
    if (!rlimit.rlim_cur) {
	signal(SIGSEGV ,sighandler);
	signal(SIGABRT ,sighandler);
    }
    if (config->coreDir) {
	if (chdir(config->coreDir) < 0) {
	    PSID_warn(-1, errno, "Unable to chdir() to coreDirectory '%s'",
		      config->coreDir);
	}
    }

    /* Start up all the hardware */
    PSID_log(PSID_LOG_HW, "%s: starting up the hardware\n", __func__);

    PSIDnodes_setProtoVersion(PSC_getMyID(), PSProtocolVersion);
    PSIDnodes_setDaemonProtoVersion(PSC_getMyID(), PSDaemonProtocolVersion);
    PSIDnodes_setHWStatus(PSC_getMyID(), 0);

    /* Bring node up with correct numbers of CPUs */
    if (!Selector_isInitialized()) Selector_init(logfile);
    declareNodeAlive(PSC_getMyID(), PSID_getPhysCPUs(), PSID_getVirtCPUs());

    /* Initialize timers */
    selectTime.tv_sec = config->selectTime;
    selectTime.tv_usec = 0;
    PSID_initStarttime();

    /* initialize various modules */
    initComm();  /* This has to be first since it gives msgHandler hash */

    initClients();
    initState();
    initOptions();
    initStatus();
    initSignal();
    initSpawn();
    initPartition();
    initHW();
    initAccount();
    initInfo();
    initPlugins();

    /* Now we start all the hardware -- this might include the accounter */
    PSID_startAllHW();
    PSIDnodes_setAcctPollI(PSC_getMyID(), config->acctPollInterval);

    /*
     * Prepare hostlist to initialize RDP and MCast
     */
    {
	in_addr_t *hostlist;
	int i;

	hostlist = malloc(PSC_getNrOfNodes() * sizeof(unsigned int));
	if (!hostlist) {
	    PSID_log(-1, "Not enough memory for hostlist\n");
	    exit(1);
	}

	for (i=0; i<PSC_getNrOfNodes(); i++) {
	    hostlist[i] = PSIDnodes_getAddr(i);
	}

	if (config->useMCast) {
	    /* Initialize MCast */
	    int MCastSock = initMCast(PSC_getNrOfNodes(),
				      config->MCastGroup, config->MCastPort,
				      logfile, hostlist,
				      PSC_getMyID(), MCastCallBack);
	    if (MCastSock<0) {
		PSID_exit(errno, "Error while trying initMCast");
	    }
	    setDeadLimitMCast(config->deadInterval);

	    PSID_log(-1, "MCast and ");
	} else {
	    setStatusTimeout(config->statusTimeout);
	    setMaxStatBCast(config->statusBroadcasts);
	    setDeadLimit(config->deadLimit);
	    setTmOutRDP(config->RDPTimeout);
	}

	/* Initialize RDP */
	RDPSocket = initRDP(PSC_getNrOfNodes(), config->RDPPort, logfile,
			    hostlist, RDPCallBack);
	if (RDPSocket<0) {
	    PSID_exit(errno, "Error while trying initRDP");
	}

	PSID_log(-1, "RDP (%d) initialized.\n", RDPSocket);

	FD_SET(RDPSocket, &PSID_readfds);

	free(hostlist);
    }

    /* Now start to listen for clients */
    PSID_enableMasterSock();

    PSID_log(-1, "SelectTime=%d sec    DeadInterval=%d\n",
	     config->selectTime, config->deadInterval);

    /* Trigger status stuff, if necessary */
    if (config->useMCast) {
	declareMaster(PSC_getMyID());
    } else {
	int id = 0;
	while (id < PSC_getMyID()
	       && (send_DAEMONCONNECT(id) < 0 && errno == EHOSTUNREACH)) {
	    id++;
	}
	if (id == PSC_getMyID()) declareMaster(id);
    }

    /*
     * Main loop
     */
    while (1) {
	struct timeval tv;  /* timeval for waiting on select()*/
	fd_set rfds;        /* read file descriptor set */
	fd_set wfds;        /* write file descriptor set */
	int fd;

	timerset(&tv, &selectTime);
	PSID_blockSig(0, SIGCHLD); /* Handle deceased child processes */
	PSID_blockSig(1, SIGCHLD);
	memcpy(&rfds, &PSID_readfds, sizeof(rfds));
	memcpy(&wfds, &PSID_writefds, sizeof(wfds));

	if (Sselect(FD_SETSIZE, &rfds, &wfds, (fd_set *)NULL, &tv) < 0) {
	    PSID_warn(-1, errno, "Error while Sselect");

	    checkFileTable(&PSID_readfds);
	    checkFileTable(&PSID_writefds);

	    PSID_log(PSID_LOG_VERB, "Error while Sselect: continue\n");
	    continue;
	}

	/*
	 * check the client sockets for any closing connections
	 * or control msgs
	 */
	for (fd=0; fd<FD_SETSIZE; fd++) {
	    if (FD_ISSET(fd, &rfds) && fd != RDPSocket /* handled below */) {
		psicontrol(fd);
	    }
	}
	for (fd=0; fd<FD_SETSIZE; fd++) {
	    if (FD_ISSET(fd, &wfds)) {
		if (!flushClientMsgs(fd)) FD_CLR(fd, &PSID_writefds);
	    }
	}
	/*
	 * Read all RDP messages
	 */
	while (FD_ISSET(RDPSocket, &rfds)) {
	    psicontrol(RDPSocket);
	    FD_ZERO(&rfds);
	    FD_SET(RDPSocket, &rfds);

	    tv.tv_sec = 0;
	    tv.tv_usec = 0;
	    if (Sselect(RDPSocket+1,
			&rfds, (fd_set *)NULL, (fd_set *)NULL, &tv) < 0) {
		PSID_warn(-1, errno, "Error in Sselect");
		break;
	    }
	}

	/* Check for partition requests */
	handlePartRequests();

	/* Check for obstinate tasks */
	checkObstinate();

	/* @todo: We might want to get rit of this. As an alternative, a
	 * special timer might be registered to the daemon's timer facility.
	 */
	/* Check for reset state */
	if (PSID_getDaemonState() & PSID_STATE_RESET) PSID_reset();

	/* Check for shutdown state */
	if (PSID_getDaemonState() & PSID_STATE_SHUTDOWN) PSID_shutdown();
    }
}
