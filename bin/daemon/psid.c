/*
 * ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file psid: ParaStation Daemon
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <popt.h>

#include "selector.h"
#include "timer.h"
#include "mcast.h"
#include "rdp.h"

#include "pscommon.h"
#include "psdaemonprotocol.h"
#include "psreservation.h"
#include "pssignal.h"

#include "psidaccount.h"
#include "psidclient.h"
#include "psidcomm.h"
#include "psidenv.h"
#include "psidflowcontrol.h"
#include "psidhook.h"
#include "psidhw.h"
#include "psidinfo.h"
#include "psidmsgbuf.h"
#include "psidnodes.h"
#include "psidoption.h"
#include "psidpartition.h"
#include "psidplugin.h"
#include "psidrdp.h"
#include "psidscripts.h"
#include "psidsession.h"
#include "psidsignal.h"
#include "psidspawn.h"
#include "psidstate.h"
#include "psidstatus.h"
#include "psidtask.h"
#include "psidutil.h"

/**
 * @brief MCast callback handler
 *
 * Handle a callback from the MCast facility. The callback-type is
 * passed within @a msgid. @a buf might hold additional information
 * related to the callback. Currently two types of callback are
 * handled:
 *
 * - MCAST_NEW_CONNECTION: a new partner inaccessible before was detected.
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
	if (node != PSC_getMyID()) declareNodeDead(node,
						   false /* sendDeadNode */,
						   false /* silent */ );
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
 * passed within @a type. @a buf might hold additional information
 * related to the callback. Currently four types of callback are
 * handled:
 *
 * - RDP_NEW_CONNECTION: a new partner inaccessible before was detected
 *
 * - RDP_LOST_CONNECTION: a partner that was recently accessible
 *   disappeared
 *
 * - RDP_PKT_UNDELIVERABLE: RDP was not able to deliver a packet
 *   originating on the local node. The actual packet is passed back
 *   within @a buf in order to create a suitable answer.
 *
 * - RDP_CAN_CONTINUE: RDP's flow control signals the possibility to
 *   send further packets to the destination indicated in @a buf
 *
 * - RDP_UNKNOWN_SENDER: RDP received a message from an unknown IP.
 *   @a buf points to this sender's sockaddr_in struct. A plugin might
 *   updated a node's IP(v4) address via RDP_updateNode() in its
 *   PSIDHOOK_SENDER_UNKNOWN hook.
 *
 * @param type Type of callback to handle
 *
 * @param buf Buffer holding extra information related to the
 * callback.
 *
 * @return No return value.
 */
static void RDPCallBack(RDP_CB_type_t type, void *buf)
{
    switch(type) {
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
	DDBufferMsg_t *msg = (DDBufferMsg_t*)((RDPDeadbuf*)buf)->buf;
	PSID_log(PSID_LOG_RDP,
		 "%s(RDP_PKT_UNDELIVERABLE, dest %x source %x type %s)\n",
		 __func__, msg->header.dest, msg->header.sender,
		 PSDaemonP_printMsg(msg->header.type));

	if (PSC_getPID(msg->header.sender)) {
	    DDMsg_t contmsg = { .type = PSP_DD_SENDCONT,
				.sender = msg->header.dest,
				.dest = msg->header.sender,
				.len = sizeof(contmsg) };
	    sendMsg(&contmsg);
	}
	PSID_dropMsg(msg);
	break;
    }
    case RDP_LOST_CONNECTION:
	if (! (PSID_getDaemonState() & PSID_STATE_SHUTDOWN)) {
	    int node = *(int*)buf;
	    PSID_log(node != PSC_getMyID() ? PSID_LOG_STATUS|PSID_LOG_RDP : -1,
		     "%s(RDP_LOST_CONNECTION,%d)\n", __func__, node);
	    if (node != PSC_getMyID()) declareNodeDead(node,
						       true /* sendDeadnode */,
						       false /* silent */);
	}
	break;
    case RDP_CAN_CONTINUE:
	flushRDPMsgs(*(int*)buf);
	break;
    case RDP_UNKNOWN_SENDER:
	PSIDhook_call(PSIDHOOK_SENDER_UNKNOWN, buf);
	break;
    default:
	PSID_log(-1, "%s(%d,%p): unhandled callback\n", __func__, type, buf);
    }
}

/**
 * @brief Handling of SIGCHLD
 *
 * Process all pending SIGCHLD. Will cleanup corresponding
 * task-structures, etc.
 *
 * @param fd File-selector providing info on dead child processes (ignored)
 *
 * @param info Dummy pointer to extra info. Ignored.
 *
 * @return Always returns 0
 */
static int handleSIGCHLD(int fd, void *info)
{
    struct signalfd_siginfo sigInfo;
    pid_t pid;         /* pid of the child process */
    int estatus;       /* termination status of the child process */

    /* Ignore data available on fd. We rely on waitpid() alone */
    if (read(fd, &sigInfo, sizeof(sigInfo)) < 0) {
	PSID_warn(-1, errno, "%s: read()", __func__);
    }

    while ((pid = waitpid(-1, &estatus, WNOHANG)) > 0){
	/*
	 * Delete the task now. These should mainly be forwarders.
	 */
	int logClass = (WEXITSTATUS(estatus)||WIFSIGNALED(estatus)) ?
	    -1 : PSID_LOG_CLIENT;

	/* I'll just report it to the logfile */
	PSID_log(logClass,
		 "Received SIGCHLD for pid %d (%#04x) with status %d",
		 pid, pid, WEXITSTATUS(estatus));
	if (WIFSIGNALED(estatus)) {
	    PSID_log(logClass, " after signal %d", WTERMSIG(estatus));
	}
	PSID_log(logClass, "\n");

	PStask_ID_t tid = PSC_getTID(-1, pid); /* tid of the child process */

	/*
	 * We can safely ignore obsoleteTasks here since those might
	 * still be connected but for sure were released via waitpid()
	 * before. Thus, all PIDs from waitpid() are for current tasks
	 */
	PStask_t *task = PStasklist_find(&managedTasks, tid);
	if (task && task->group != TG_DELEGATE) {
	    /* delegates are handled explicitly in psmom/psslurm */
	    if (!task->killat) {
		task->killat = time(NULL) + 10;
	    }
	    if (task->sigChldCB) {
		task->sigChldCB(estatus, task);
		task->sigChldCB = NULL;
	    }
	    if (task->fd != -1) {
		/* Make sure we get all pending messages */
		Selector_enable(task->fd);
	    } else {
		/* task not connected, remove from tasklist */
		PSIDtask_cleanup(task);
	    }
	}
    }

    return 0;
}

/**
 * @brief Handling of SIGUSR1
 *
 * Print some statistics on RDP, buffer usage, etc.
 *
 * @param fd Dummy file-descriptor. Ignored.
 *
 * @param info Dummy pointer to extra info. Ignored.
 *
 * @return Always returns 0
 */
static int handleSIGUSR1(int fd, void *info)
{
    struct signalfd_siginfo sigInfo;

    /* Ignore data available on fd */
    if (read(fd, &sigInfo, sizeof(sigInfo)) < 0) {
	PSID_warn(-1, errno, "%s: read()", __func__);
    }

    PSIDMsgbuf_printStat();
    PStask_printStat();
    RDP_printStat();
    PSIDFlwCntrl_printStat();
    PSIDcomm_printStat();
    Selector_printStat();
    PSIDscripts_printStat();
    PSIDsession_printStat();

    return 0;
}

static void printMallocInfo(void)
{
#ifdef HAVE_MALLINFO2
    struct mallinfo2 mi = mallinfo2();

    PSID_log(PSID_LOG_RESET, "%s:\n", __func__);
    PSID_log(PSID_LOG_RESET, "arena    %zu\n", mi.arena);
    PSID_log(PSID_LOG_RESET, "ordblks  %zu\n", mi.ordblks);
    PSID_log(PSID_LOG_RESET, "hblks    %zu\n", mi.hblks);
    PSID_log(PSID_LOG_RESET, "hblkhd   %zu\n", mi.hblkhd);
    PSID_log(PSID_LOG_RESET, "uordblks %zu\n", mi.uordblks);
    PSID_log(PSID_LOG_RESET, "fordblks %zu\n", mi.fordblks);
    PSID_log(PSID_LOG_RESET, "keepcost %zu\n", mi.keepcost);
    PSID_log(PSID_LOG_RESET, "====================\n");
#else
    struct mallinfo mi = mallinfo();

    PSID_log(PSID_LOG_RESET, "%s:\n", __func__);
    PSID_log(PSID_LOG_RESET, "arena    %d\n", mi.arena);
    PSID_log(PSID_LOG_RESET, "ordblks  %d\n", mi.ordblks);
    PSID_log(PSID_LOG_RESET, "hblks    %d\n", mi.hblks);
    PSID_log(PSID_LOG_RESET, "hblkhd   %d\n", mi.hblkhd);
    PSID_log(PSID_LOG_RESET, "uordblks %d\n", mi.uordblks);
    PSID_log(PSID_LOG_RESET, "fordblks %d\n", mi.fordblks);
    PSID_log(PSID_LOG_RESET, "keepcost %d\n", mi.keepcost);
    PSID_log(PSID_LOG_RESET, "====================\n");
#endif
}

/**
 * @brief Handling of SIGUSR2
 *
 * Print some info on memory usage and try to trim it.
 *
 * @param fd Dummy file-descriptor. Ignored.
 *
 * @param info Dummy pointer to extra info. Ignored.
 *
 * @return Always returns 0
 */
static int handleSIGUSR2(int fd, void *info)
{
    struct signalfd_siginfo sigInfo;

    /* Ignore data available on fd */
    if (read(fd, &sigInfo, sizeof(sigInfo)) < 0) {
	PSID_warn(-1, errno, "%s: read()", __func__);
    }

    printMallocInfo();
    malloc_trim(0);
    printMallocInfo();

    return 0;
}

/**
 * @brief Handling of most signals
 *
 * Print some info on received signal and possibly shutdown more or less
 * gracefully.
 *
 * @param fd File descriptor providing info on the received signal
 *
 * @param info Dummy pointer to extra info. Ignored.
 *
 * @return Always returns 0
 */
static int handleSignals(int fd, void *info)
{
    struct signalfd_siginfo sigInfo;
    ssize_t got = read(fd, &sigInfo, sizeof(sigInfo));

    if (got < 0) {
	int eno = errno;
	PSID_warn(-1, errno, "%s: read()", __func__);
	Selector_remove(fd);
	errno = eno;
	return -1;
    } else if (!got) {
	PSID_log(-1, "%s: fd closed", __func__);
	close(fd);
	Selector_remove(fd);
    } else {
	uint32_t sig = sigInfo.ssi_signo;
	switch (sig) {
	case SIGPIPE:   /* write on a pipe with no one to read it */
	    /* Ignore silently */
	    break;
	case SIGQUIT:   /* (*) quit, generated from terminal special char */
	case SIGTSTP:   /* (@) interactive stop */
	case SIGCONT:   /* (!) continue if stopped */
	case SIGVTALRM: /* virtual time alarm (see setitimer) */
	case SIGPROF:   /* profiling time alarm (see setitimer) */
	case SIGWINCH:  /* (+) window size changed */
	    PSID_log(-1, "Received signal '%s'. Continue\n", strsignal(sig));
	    break;
	case SIGABRT:
	case SIGSEGV:
	case SIGILL:    /* illegal instruction (not reset when caught) */
	case SIGFPE:    /* floating point exception */
	    PSID_log(-1, "Received signal '%s'. Shut down hardly.\n",
		     strsignal(sig));
	    PSID_finalizeLogs();
	    exit(-1);
	    break;
	default:
	    PSID_log(-1, "Received signal '%s'. Shut down\n", strsignal(sig));
	    if (!(PSID_getDaemonState() & PSID_STATE_SHUTDOWN)) PSID_shutdown();
	    break;
	}
    }

    return 0;
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
    sigset_t set;

    PSID_prepareSigs(handleSignals);

    /* Some signals have to be handled separately */
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);
    PSID_initSignalFD(&set, handleSIGCHLD);

    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    PSID_initSignalFD(&set, handleSIGUSR1);

    sigemptyset(&set);
    sigaddset(&set, SIGUSR2);
    PSID_initSignalFD(&set, handleSIGUSR2);
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
    PSID_log(-1, "RPM Version %s\n", PSC_getVersionStr());
    PSID_log(-1, "Protocol Version %d\n", PSProtocolVersion);
    PSID_log(-1, "Daemon-Protocol Version %d\n", PSDaemonProtocolVersion);
    PSID_log(-1, " (c) ParTec AG, Munich (www.par-tec.com)\n");

    return;
}

void PSID_clearMem(bool aggressive)
{
    if (aggressive) PSIDtask_clearMem();
    PSsignal_gc();
    PSrsrvtn_clearMem();

    PSIDRDP_clearMem();
    PSIDclient_clearMem();
    /* This one has to wait until PSIDRDP and PSIDclient are cleaned up */
    PSIDMsgbuf_clearMem();

    /* This one has to wait until PSIDclient is cleaned up */
    PSIDFlwCntrl_clearMem();
    PSIDcomm_clearMem();

    if (aggressive) PSIDnodes_clearMem();
    RDP_clearMem();

    /* Now call all cleanup functions registered by plugins */
    PSIDhook_call(PSIDHOOK_CLEARMEM, &aggressive);

    malloc_trim(0);
}

/**
 * @brief Print version info.
 *
 * Print version info of the current psid.c revision number to stderr.
 *
 * @return No return value.
 */
static void printVersion(void)
{
    fprintf(stderr, "psid %s\n", PSC_getVersionStr());
}

#define FORKMAGIC 4711

int main(int argc, const char *argv[])
{
    poptContext optCon;   /* context for parsing command-line options */

    int rc, version = 0, debugMask = 0, pipeFD[2] = {-1, -1}, magic = FORKMAGIC;
    char *logdest = NULL, *configfile = "/etc/parastation.conf";
    FILE *logfile = NULL;

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

    /* Store arguments for later modification in forwarders, etc. */
    PSID_argc = argc;
    PSID_argv = argv;

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
	openlog("psid", LOG_PID|LOG_CONS, LOG_DAEMON);
    }
    PSID_initLogs(logfile);

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

    /* Save some space in order to modify the cmdline later on */
    PSC_saveTitleSpace(PSID_argc, PSID_argv, 1);

    if (logfile != stderr && logfile != stdout) {
	/* Daemonize only if neither stdout nor stderr is used for logging */
	if (pipe(pipeFD) < 0) {
	    PSID_exit(errno, "unable to create pipe");
	}

	/* Start as daemon */
	switch (fork()) {
	case -1:
	    PSID_exit(errno, "unable to fork server process");
	    break;
	case 0: /* I'm the child (and running further) */
	    close (pipeFD[0]);
	    break;
	default: /* I'm the parent and exiting */
	    close (pipeFD[1]);

	    /* Wait for child's magic data */
	    rc = read(pipeFD[0], &magic, sizeof(magic));
	    if (rc != sizeof(magic) || magic != (FORKMAGIC)) return -1;

	    return 0;
       }
    }

#define _PATH_TTY "/dev/tty"
    /* First disconnect from the old controlling tty. */
    int fd = open(_PATH_TTY, O_RDWR | O_NOCTTY);
    if (fd >= 0) {
	if (ioctl(fd, TIOCNOTTY, NULL)) {
	    PSID_warn(-1, errno, "%s: ioctl(TIOCNOTTY)", __func__);
	}
	close(fd);
    }


    /*
     * Disable stdin,stdout,stderr and install dummy replacement
     * Take care if stdout/stderr is used for logging
     */
    int dummy_fd = open("/dev/null", O_WRONLY , 0);
    dup2(dummy_fd, STDIN_FILENO);
    if (logfile != stdout) dup2(dummy_fd, STDOUT_FILENO);
    if (logfile != stderr) dup2(dummy_fd, STDERR_FILENO);
    close(dummy_fd);

    /* Forget about inherited window sizes */
    unsetenv("LINES");
    unsetenv("COLUMNS");

    if (debugMask) {
	PSID_setDebugMask(debugMask);
	PSC_setDebugMask(debugMask);
	PSID_log(-1, "Debugging mode (mask 0x%x) enabled\n", debugMask);
    }

    /* Init the Selector facility as soon as possible */
    if (!Selector_isInitialized()) Selector_init(logfile);
    PSID_registerLoopAct(Selector_gc);

    /* Initialize timer facility explicitely to ensure correct logging */
    if (!Timer_isInitialized()) Timer_init(logfile);

    /*
     * Create the Local Service Port as early as possible. Actual
     * handling is enabled later. This gives psiadmin the chance to
     * connect. Additionally, this will guarantee exclusiveness
     */
    PSID_createMasterSock(PSmasterSocketName);

    PSID_checkMaxPID();

    /* read the config file */
    PSID_readConfigFile(logfile, configfile);
    /* Now we can rely on the config structure */

    {
	in_addr_t addr;

	PSID_log(-1, "My ID is %d\n", PSC_getMyID());

	addr = PSIDnodes_getAddr(PSC_getMyID());
	PSID_log(-1, "My IP is %s\n", inet_ntoa(*(struct in_addr *) &addr));
    }

    if (!logfile && PSID_config->logDest!=LOG_DAEMON) {
	PSID_log(-1, "Changing logging dest from LOG_DAEMON to %s\n",
		 PSID_config->logDest==LOG_KERN ? "LOG_KERN":
		 PSID_config->logDest==LOG_LOCAL0 ? "LOG_LOCAL0" :
		 PSID_config->logDest==LOG_LOCAL1 ? "LOG_LOCAL1" :
		 PSID_config->logDest==LOG_LOCAL2 ? "LOG_LOCAL2" :
		 PSID_config->logDest==LOG_LOCAL3 ? "LOG_LOCAL3" :
		 PSID_config->logDest==LOG_LOCAL4 ? "LOG_LOCAL4" :
		 PSID_config->logDest==LOG_LOCAL5 ? "LOG_LOCAL5" :
		 PSID_config->logDest==LOG_LOCAL6 ? "LOG_LOCAL6" :
		 PSID_config->logDest==LOG_LOCAL7 ? "LOG_LOCAL7" :
		 "UNKNOWN");
	closelog();

	openlog("psid", LOG_PID|LOG_CONS, PSID_config->logDest);
	printWelcome();
    }

    /* call startupScript, if any */
    if (PSID_config->startupScript && *PSID_config->startupScript) {
	int ret = PSID_execScript(PSID_config->startupScript, NULL, NULL,
				  NULL, NULL);

	if (ret > 1) {
	    PSID_log(-1, "startup script '%s' failed. Exiting...\n",
		     PSID_config->startupScript);
	    PSID_finalizeLogs();
	    exit(1);
	}
    }

    /* Setup handling of signals */
    initSigHandlers();

    if (PSID_config->coreDir) {
	if (chdir(PSID_config->coreDir) < 0) {
	    PSID_warn(-1, errno, "Unable to chdir() to coreDirectory '%s'",
		      PSID_config->coreDir);
	}
    }

    PSIDnodes_setHWStatus(PSC_getMyID(), 0);
    PSIDnodes_setKillDelay(PSC_getMyID(), PSID_config->killDelay);

    /* Bring node up with correct numbers of CPUs */
    declareNodeAlive(PSC_getMyID(), PSIDhw_getCores(), PSIDhw_getHWthreads(),
		     PSProtocolVersion, PSDaemonProtocolVersion);

    /* Initialize timeouts, etc. */
    PSID_initStarttime();

    /* initialize various modules */
    PSIDcomm_init(true);  /* Must be first since it enables message
			   * handlers and droppers */
    initRDPMsgs();
    PSIDclient_init();
    initState();
    initOptions();
    initStatus();
    initSignal();
    PSIDspawn_init();
    PSIDsession_init();
    initPartition();
    PSIDhw_init();
    PSIDscripts_init();
    initAccount();
    initInfo();
    initEnvironment();
    /* Plugins shall be last since they use most of the ones before */
    initPlugins(logfile);

    /* Now we start all the hardware -- this might include the accounter */
    PSID_log(PSID_LOG_HW, "%s: starting up the hardware\n", __func__);
    PSID_startAllHW();

    /*
     * Prepare hostlist to initialize RDP and MCast
     */
    in_addr_t *hostlist = malloc(PSC_getNrOfNodes() * sizeof(*hostlist));
    if (!hostlist) PSID_exit(errno, "Failed to get memory for hostlist");

    for (int i = 0; i < PSC_getNrOfNodes(); i++) {
	hostlist[i] = PSIDnodes_getAddr(i);
    }

    if (PSID_config->useMCast) {
	/* Initialize MCast */
	int MCastSock = initMCast(PSC_getNrOfNodes(),
				  PSID_config->MCastGroup,
				  PSID_config->MCastPort,
				  logfile, hostlist,
				  PSC_getMyID(), MCastCallBack);
	if (MCastSock < 0) PSID_exit(errno, "Error while trying initMCast");

	setDeadLimitMCast(PSID_config->deadInterval);

	PSID_log(-1, "MCast and ");
    } else {
	setStatusTimeout(PSID_config->statusTimeout);
	setMaxStatBCast(PSID_config->statusBroadcasts);
	setDeadLimit(PSID_config->deadLimit);
	setTmOutRDP(PSID_config->RDPTimeout);
    }

    /* Initialize RDP */
    int RDPSocket = RDP_init(PSC_getNrOfNodes(),
			     PSIDnodes_getAddr(PSC_getMyID()),
			     PSID_config->RDPPort, logfile, hostlist,
			     PSIDRDP_handleMsg, RDPCallBack);
    if (RDPSocket < 0) PSID_exit(errno, "Error while trying to initialize RDP");

    PSID_log(-1, "RDP (%d) initialized.\n", RDPSocket);

    free(hostlist);

    /* Now start to listen for clients */
    PSID_enableMasterSock();

    /* Once RDP and the master socket are ready parents might be released */
    if (pipeFD[1] > -1) {
	if (write(pipeFD[1], &magic, sizeof(magic)) <= 0) {
	    /* We don't care */
	}
	close(pipeFD[1]);
    }

    PSID_log(-1, "SelectTime=%d sec    DeadInterval=%d\n",
	     PSID_config->selectTime, PSID_config->deadInterval);

    /* Trigger status stuff, if necessary */
    if (PSID_config->useMCast) {
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
	int res = Swait(PSID_config->selectTime * 1000);

	if (res < 0) PSID_warn(-1, errno, "Error while Swait()");

	/* Handle actions registered to main-loop */
	PSID_handleLoopActions();
    }
}
