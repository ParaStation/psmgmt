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
#include "psidutil.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/signalfd.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include "list.h"
#include "pscommon.h"
#include "psdaemonprotocol.h"
#include "pslog.h"

#include "psidnodes.h"
#include "psidclient.h"
#include "psidtask.h"

config_t *PSID_config = NULL;

logger_t *PSID_logger;

int PSID_argc;
const char **PSID_argv;

/* Wrapper functions for logging */
void PSID_initLogs(FILE *logfile)
{
    PSID_logger = logger_init(logfile ? "PSID" : NULL, logfile);
    if (!PSID_logger) {
	if (logfile) {
	    fprintf(logfile, "%s: failed to initialize logger\n", __func__);
	} else {
	    syslog(LOG_CRIT, "%s: failed to initialize logger", __func__);
	}
	exit(1);
    }
    PSC_initLog(logfile);
}

int32_t PSID_getDebugMask(void)
{
    return logger_getMask(PSID_logger);
}

void PSID_setDebugMask(int32_t mask)
{
    logger_setMask(PSID_logger, mask);
}

void PSID_finalizeLogs(void)
{
    PSC_finalizeLog();
    logger_finalize(PSID_logger);
    PSID_logger = NULL;
}

bool PSID_blockSig(int sig, bool block)
{
    sigset_t set, oldset;

    sigemptyset(&set);
    sigaddset(&set, sig);

    if (sigprocmask(block ? SIG_BLOCK : SIG_UNBLOCK, &set, &oldset)) {
	PSID_flog("sigprocmask()\n");
    }

    return sigismember(&oldset, sig);
}

int PSID_initSignalFD(sigset_t *set, Selector_CB_t handler)
{
    int sigFD;

    if (sigprocmask(SIG_BLOCK, set, NULL) < 0) {
	PSID_exit(errno, "%s(%p): sigprocmask()", __func__, handler);
    }

    sigFD = signalfd(-1, set, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sigFD < 0) {
	PSID_exit(errno, "%s(%p): signalfd()", __func__, handler);
    }

    if (Selector_register(sigFD, handler, NULL) < 0) {
	PSID_exit(errno, "%s(%p): Selector_register()", __func__, handler);
    }

    return sigFD;
}

static sigset_t handledSet;

void PSID_prepareSigs(Selector_CB_t handler)
{
    struct rlimit rlimit;

    sigemptyset(&handledSet);
    sigaddset(&handledSet, SIGINT);
    sigaddset(&handledSet, SIGQUIT);
    sigaddset(&handledSet, SIGILL);
    sigaddset(&handledSet, SIGTRAP);
    sigaddset(&handledSet, SIGPIPE);
    sigaddset(&handledSet, SIGTERM);
    sigaddset(&handledSet, SIGCONT);
    sigaddset(&handledSet, SIGTSTP);
    sigaddset(&handledSet, SIGTTIN);
    sigaddset(&handledSet, SIGTTOU);
    sigaddset(&handledSet, SIGURG);
    sigaddset(&handledSet, SIGXCPU);
    sigaddset(&handledSet, SIGXFSZ);
    sigaddset(&handledSet, SIGVTALRM);
    sigaddset(&handledSet, SIGPROF);
    sigaddset(&handledSet, SIGWINCH);
    sigaddset(&handledSet, SIGIO);
    sigaddset(&handledSet, SIGSYS);
#ifdef SIGINFO
    sigaddset(&handledSet, SIGINFO);
#endif
    /* Catch SIGABRT, SIGSEGV, SIGBUS, and SIGFPE only if core dumps
     * are suppressed */
    getrlimit(RLIMIT_CORE, &rlimit);
    if (!rlimit.rlim_cur) {
	sigaddset(&handledSet, SIGABRT);
	sigaddset(&handledSet, SIGSEGV);
	sigaddset(&handledSet, SIGBUS);
	sigaddset(&handledSet, SIGFPE);
    }

    PSID_initSignalFD(&handledSet, handler);

    /* In addition, ignore SIGHUP */
    PSC_setSigHandler(SIGHUP, SIG_IGN);
}

void PSID_resetSigs(void)
{
    /* Add further signals, but keep SIGCHLD out of the game */
    sigaddset(&handledSet, SIGUSR1);
    sigaddset(&handledSet, SIGUSR2);

    if (sigprocmask(SIG_UNBLOCK, &handledSet, NULL) < 0) {
	PSID_fwarn(errno, "sigprocmask()");
    }

    /* Also reset SIGHUP */
    PSC_setSigHandler(SIGHUP, SIG_DFL);
}

/* Reading and basic handling of the configuration */
void PSID_readConfigFile(FILE* logfile, char *configfile)
{
    /* Parse the configfile */
    PSID_config = parseConfig(logfile, PSID_getDebugMask(), configfile);
    if (! PSID_config) {
#ifdef BUILD_WITHOUT_PSCONFIG
	PSID_flog("parsing of <%s> failed\n", configfile);
#else
	PSID_flog("reading configuration from psconfig failed\n");
#endif

	PSID_finalizeLogs();
	exit(1);
    }
    PSID_config->logfile = logfile;

    /* Set correct debugging mask if given in config-file */
    if (PSID_config->logMask && !PSID_getDebugMask()) {
	PSID_setDebugMask(PSID_config->logMask);
	PSC_setDebugMask(PSID_config->logMask);
    }

    /* Try to find out if node is configured */
    if (PSC_getMyID() == -1) {
	PSID_flog("node not configured\n");
	PSID_finalizeLogs();
	exit(1);
    }

    PSC_setNrOfNodes(PSIDnodes_getMaxID()+1);
    PSC_setDaemonFlag(true); /* To get the correct result from PSC_getMyTID() */
}

static bool mixedProto = false;

bool PSID_mixedProto(void)
{
    return mixedProto;
}

void PSID_setMixedProto(bool mixed)
{
    mixedProto = mixed;
}

/**
 * Master socket (type UNIX) for clients to connect. Set up within @ref
 * setupMasterSock().
 */
static int masterSock = -1;

/**
 * @brief Handle master socket
 *
 * Handle the master socket @a fd, i.e. accept requests for connecting
 * the daemon from new clients. This will only register the new
 * client's file-descriptor. The client has to send a
 * PSP_CD_CLIENTCONNECT message in order to actually get accepted by
 * the daemon.
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
static int handleMasterSock(int fd, void *info)
{
    if (fd != masterSock) {
	PSID_flog("wrong fd %d (%d expected)\n", fd, masterSock);
	return -1;
    }

    PSID_fdbg(PSID_LOG_CLIENT | PSID_LOG_VERB, "accepting new connection\n");

    int ssock = accept(fd, NULL, 0);
    if (ssock < 0) {
	PSID_fwarn(errno, "error while accept");
	return -1;
    }

    PSIDclient_register(ssock, -1, NULL);

    PSID_fdbg(PSID_LOG_CLIENT | PSID_LOG_VERB, "new socket is %d\n", ssock);

    struct linger linger;
    socklen_t size = sizeof(linger);
    getsockopt(ssock, SOL_SOCKET, SO_LINGER, &linger, &size);

    PSID_fdbg(PSID_LOG_VERB, "linger was (%d,%d), setting it to (1,1)\n",
	      linger.l_onoff, linger.l_linger);

    linger.l_onoff=1;
    linger.l_linger=1;
    size = sizeof(linger);
    setsockopt(ssock, SOL_SOCKET, SO_LINGER, &linger, size);

    return 0;
}

void PSID_createMasterSock(char *sName)
{
    struct sockaddr_un sa;

    masterSock = socket(PF_UNIX, SOCK_STREAM, 0);
    if (masterSock < 0) {
	PSID_exit(errno, "Unable to create socket for Local Service Port");
    }

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    if (sName[0] == '\0') {
	sa.sun_path[0] = '\0';
	sName++;
	memcpy(sa.sun_path + 1, sName, MIN(sizeof(sa.sun_path) - 1,
					   strlen(sName) + 1));
    } else {
	memcpy(sa.sun_path, sName, MIN(sizeof(sa.sun_path), strlen(sName) + 1));
    }

    /* bind the socket to the right address */
    if (bind(masterSock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
	PSID_exit(errno, "Daemon already running? ");
    }

    if (listen(masterSock, 20) < 0) {
	PSID_exit(errno, "Error while trying to listen");
    }

    PSID_log("Local Service Port (%d) created.\n", masterSock);
}

void PSID_enableMasterSock(void)
{
    if (masterSock < 0) {
	PSID_flog("Local Service Port not yet opened\n");
	PSID_createMasterSock(PSmasterSocketName);
    }
    if (!Selector_isInitialized()) {
	PSID_flog("Local Service Port needs running Selector\n");
	PSID_finalizeLogs();
	exit(-1);
    }
    Selector_register(masterSock, handleMasterSock, NULL);

    PSID_log("Local Service Port (%d) enabled.\n", masterSock);
}

void PSID_disableMasterSock(void)
{
    if (masterSock == -1) {
	PSID_flog("master sock already down\n");
	return;
    }

    Selector_remove(masterSock);
}

void PSID_shutdownMasterSock(void)
{
    if (masterSock == -1) {
	PSID_flog("master sock already down\n");
	return;
    }

    /* Just in case, this has not yet happened */
    if (Selector_isRegistered(masterSock)) Selector_remove(masterSock);

    if (close(masterSock) < 0) PSID_fwarn(errno, "close()");

    masterSock = -1;
}

#define PID_FILE "/proc/sys/kernel/pid_max"

void PSID_checkMaxPID(void)
{
    FILE *maxPIDFile = fopen(PID_FILE,"r");
    unsigned int maxPID;
    int ret;

    if (!maxPIDFile) {
	PSID_fwarn(errno, "cannot open file '%s'", PID_FILE);
	return;
    }

    ret = fscanf(maxPIDFile, "%u", &maxPID);
    if (ret == EOF) {
	PSID_fwarn(ferror(maxPIDFile) ? errno : 0,
		   "unable to determine maximum PID");
	goto end;
    }

    PSID_fdbg(PSID_LOG_VERB, "pid_max is %d\n", maxPID);

    if (maxPID > 65536) {
	PSID_exit(EINVAL, "%s: cannot handle PIDs larger than 16 bit."
		  " Please fix setting in '%s'", __func__, PID_FILE);
    }

end:
    fclose(maxPIDFile);
    return;
}

static time_t startTime = -1;

void PSID_initStarttime(void)
{
    if (startTime == -1) startTime = time(NULL);
}

time_t PSID_getStarttime(void)
{
    return startTime;
}

void PSID_dumpMsg(DDMsg_t *msg)
{
    PSID_fdbg(PSID_LOG_MSGDUMP, "type %s sender %s",
	     PSDaemonP_printMsg(msg->type), PSC_printTID(msg->sender));
    PSID_dbg(PSID_LOG_MSGDUMP, " destination %s size %d",
	     PSC_printTID(msg->dest), msg->len);

    switch (msg->type) {
    case PSP_CC_MSG:
	PSID_dbg(PSID_LOG_MSGDUMP, " type %d", ((PSLog_Msg_t *)msg)->type);
	break;
    default:
	break;
    }

    PSID_dbg(PSID_LOG_MSGDUMP, "\n");
}

bool PSID_checkPrivilege(PStask_ID_t sender)
{
    if (PSC_getID(sender) != PSC_getMyID()) {
	/* No info on remote task, lets assumed it's privileged */
	return true;
    }

    if (!PSC_getPID(sender)) {
	/* Sender is a daemon itself and, thus, privileged */
	return true;
    }

    PStask_t *senderTask = PStasklist_find(&managedTasks, sender);

    if (!senderTask) {
	PSID_flog("sender %s not among managed tasks\n", PSC_printTID(sender));
	return false;
    }

    if (senderTask->uid && senderTask->gid
	&& !PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_ADMUSER,
			       (PSIDnodes_guid_t){.u=senderTask->uid})
	&& !PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_ADMGROUP,
			       (PSIDnodes_guid_t){.g=senderTask->gid})) {
	PSID_flog("sender %s not privileged\n", PSC_printTID(sender));
	return false;
    }

    return true;
}

/** All main-loop actions registered */
static LIST_HEAD(loopActions);

/** Structure holding all information concerning a loop action */
typedef struct {
    list_t next;               /**< Used to put into @ref loopActions */
    PSID_loopAction_t *action; /**< Actual loop-action to be called */
} action_t;

int PSID_registerLoopAct(PSID_loopAction_t action)
{
    action_t *newAction = malloc(sizeof(*newAction));

    if (!newAction) return -1;

    newAction->action = action;

    list_add_tail(&newAction->next, &loopActions);

    return 0;
}

int PSID_unregisterLoopAct(PSID_loopAction_t action)
{
    list_t *a;

    list_for_each(a, &loopActions) {
	action_t *curAct = list_entry(a, action_t, next);

	if (curAct->action == action) {
	    list_del(&curAct->next);
	    free(curAct);

	    return 0;
	}
    }

    errno = ENOKEY;
    return -1;
}

void PSID_handleLoopActions(void)
{
    list_t *a, *tmp;

    list_for_each_safe(a, tmp, &loopActions) {
	action_t *action = list_entry(a, action_t, next);

	if (action->action) action->action();
    }
}

void PSID_adjustLoginUID(uid_t uid)
{
    FILE *fd;
    char fileName[128];
    struct stat sbuf;

    snprintf(fileName, sizeof(fileName), "/proc/%i/loginuid", getpid());
    if (stat(fileName, &sbuf) == -1) return;

    fd = fopen(fileName,"w");
    if (!fd) {
	PSID_flog("open '%s' failed\n", fileName);
	return;
    }
    fprintf(fd, "%d", uid);
    fclose(fd);
}
