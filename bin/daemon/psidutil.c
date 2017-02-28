/*
 * ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "pscommon.h"
#include "logging.h"
#include "selector.h"
#include "config_parsing.h"
#include "psdaemonprotocol.h"
#include "pslog.h"

#include "psidnodes.h"
#include "psidcomm.h"
#include "psidclient.h"
#include "psidtask.h"

#include "psidutil.h"

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

int PSID_blockSig(int block, int sig)
{
    sigset_t set, oldset;

    sigemptyset(&set);
    sigaddset(&set, sig);

    if (sigprocmask(block ? SIG_BLOCK : SIG_UNBLOCK, &set, &oldset)) {
	PSID_log(-1, "%s: sigprocmask()\n", __func__);
    }

    return sigismember(&oldset, sig);
}

void PSID_resetSigs(void)
{
    signal(SIGHUP   ,SIG_DFL);
    signal(SIGALRM  ,SIG_DFL);
    signal(SIGINT   ,SIG_DFL);
    signal(SIGQUIT  ,SIG_DFL);
    signal(SIGILL   ,SIG_DFL);
    signal(SIGTRAP  ,SIG_DFL);
    signal(SIGABRT  ,SIG_DFL);
    signal(SIGIOT   ,SIG_DFL);
    signal(SIGBUS   ,SIG_DFL);
    signal(SIGFPE   ,SIG_DFL);
    signal(SIGUSR1  ,SIG_DFL);
    signal(SIGSEGV  ,SIG_DFL);
    signal(SIGUSR2  ,SIG_DFL);
    signal(SIGPIPE  ,SIG_DFL);
    signal(SIGTERM  ,SIG_DFL);
    signal(SIGCONT  ,SIG_DFL);
    signal(SIGTSTP  ,SIG_DFL);
    signal(SIGTTIN  ,SIG_DFL);
    signal(SIGTTOU  ,SIG_DFL);
    signal(SIGURG   ,SIG_DFL);
    signal(SIGXCPU  ,SIG_DFL);
    signal(SIGXFSZ  ,SIG_DFL);
    signal(SIGVTALRM,SIG_DFL);
    signal(SIGPROF  ,SIG_DFL);
    signal(SIGWINCH ,SIG_DFL);
    signal(SIGIO    ,SIG_DFL);
#if defined(__alpha)
    /* Linux on Alpha*/
    signal( SIGSYS  ,SIG_DFL);
    signal( SIGINFO ,SIG_DFL);
#else
    signal(SIGSTKFLT,SIG_DFL);
#endif

    PSID_blockSig(0, SIGUSR1);
    PSID_blockSig(0, SIGUSR2);
}

/* Reading and basic handling of the configuration */
void PSID_readConfigFile(FILE* logfile, char *configfile)
{
    /* Parse the configfile */
    PSID_config = parseConfig(logfile, PSID_getDebugMask(), configfile);
    if (! PSID_config) {
#ifdef BUILD_WITHOUT_PSCONFIG
	PSID_log(-1, "%s: parsing of <%s> failed\n", __func__, configfile);
#else
	PSID_log(-1, "%s: reading configuration from psconfig failed\n",
		 __func__);
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
	PSID_log(-1, "%s: Node not configured\n", __func__);
	PSID_finalizeLogs();
	exit(1);
    }

    PSC_setNrOfNodes(PSIDnodes_getMaxID()+1);
    PSC_setDaemonFlag(1); /* To get the correct result from PSC_getMyTID() */
}

int PSID_writeall(int fd, const void *buf, size_t count)
{
    int len;
    char *cbuf = (char *)buf;
    size_t c = count;

    while (c > 0) {
	len = write(fd, cbuf, c);
	if (len < 0) {
	    if ((errno == EINTR) || (errno == EAGAIN))
		continue;
	    else
		return -1;
	}
	c -= len;
	cbuf += len;
    }

    return count;
}

int PSID_readall(int fd, void *buf, size_t count)
{
    int len;
    char *cbuf = (char *)buf;
    size_t c = count;

    while (c > 0) {
	len = read(fd, cbuf, c);
	if (len < 0) {
	    if ((errno == EINTR) || (errno == EAGAIN))
		continue;
	    else
		return -1;
	} else if (len == 0) {
	    return count-c;
	}
	c -= len;
	cbuf += len;
    }

    return count;
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
    struct linger linger;
    socklen_t size;
    int ssock;

    if (fd != masterSock) {
	PSID_log(-1, "%s: wrong fd %d, %d expected\n",
		 __func__, fd, masterSock);
	return -1;
    }

    PSID_log(PSID_LOG_CLIENT | PSID_LOG_VERB,
	     "%s: accepting new connection\n", __func__);

    ssock = accept(fd, NULL, 0);
    if (ssock < 0) {
	PSID_warn(-1, errno, "%s: error while accept", __func__);
	return -1;
    }

    PSIDclient_register(ssock, -1, NULL);

    PSID_log(PSID_LOG_CLIENT | PSID_LOG_VERB,
	     "%s: new socket is %d\n", __func__, ssock);

    size = sizeof(linger);
    getsockopt(ssock, SOL_SOCKET, SO_LINGER, &linger, &size);

    PSID_log(PSID_LOG_VERB, "%s: linger was (%d,%d), setting it to (1,1)\n",
	     __func__, linger.l_onoff, linger.l_linger);

    linger.l_onoff=1;
    linger.l_linger=1;
    size = sizeof(linger);
    setsockopt(ssock, SOL_SOCKET, SO_LINGER, &linger, size);

    return 0;
}

void PSID_createMasterSock(char *sockname)
{
    struct sockaddr_un sa;

    masterSock = socket(PF_UNIX, SOCK_STREAM, 0);
    if (masterSock < 0) {
	PSID_exit(errno, "Unable to create socket for Local Service Port");
    }

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    if (sockname[0] == '\0') {
	sa.sun_path[0] = '\0';
	strncpy(sa.sun_path+1, sockname+1, sizeof(sa.sun_path)-1);
    } else {
	strncpy(sa.sun_path, sockname, sizeof(sa.sun_path));
    }

    /* bind the socket to the right address */
    if (bind(masterSock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
	PSID_exit(errno, "Daemon already running? ");
    }

    if (listen(masterSock, 20) < 0) {
	PSID_exit(errno, "Error while trying to listen");
    }

    PSID_log(-1, "Local Service Port (%d) created.\n", masterSock);
}

void PSID_enableMasterSock(void)
{
    if (masterSock < 0) {
	PSID_log(-1, "%s: Local Service Port not yet opened\n", __func__);
	PSID_createMasterSock(PSmasterSocketName);
    }
    if (!Selector_isInitialized()) {
	PSID_log(-1, "%s: Local Service Port needs running Selector\n",
		 __func__);
	PSID_finalizeLogs();
	exit(-1);
    }
    Selector_register(masterSock, handleMasterSock, NULL);

    PSID_log(-1, "Local Service Port (%d) enabled.\n", masterSock);
}

void PSID_disableMasterSock(void)
{
    if (masterSock == -1) {
	PSID_log(-1, "%s: master sock already down\n", __func__);
	return;
    }

    Selector_remove(masterSock);
}

void PSID_shutdownMasterSock(void)
{
    if (masterSock == -1) {
	PSID_log(-1, "%s: master sock already down\n", __func__);
	return;
    }

    /* Just in case, this has not yet happened */
    if (Selector_isRegistered(masterSock)) Selector_remove(masterSock);

    if (close(masterSock)) {
	PSID_warn(-1, errno, "%s: close()", __func__);
    }

    masterSock = -1;
}

#define PID_FILE "/proc/sys/kernel/pid_max"

void PSID_checkMaxPID(void)
{
    FILE *maxPIDFile = fopen(PID_FILE,"r");
    unsigned int maxPID;
    int ret;

    if (!maxPIDFile) {
	PSID_warn(-1, errno, "%s: cannot open file '%s'", __func__, PID_FILE);
	return;
    }

    ret = fscanf(maxPIDFile, "%u", &maxPID);
    if (ret == EOF) {
	PSID_warn(-1, ferror(maxPIDFile) ? errno : 0,
		  "%s: unable to determine maximum PID", __func__);
	goto end;
    }

    PSID_log(PSID_LOG_VERB, "%s: pid_max is %d\n", __func__, maxPID);

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
    PSID_log(PSID_LOG_MSGDUMP, "%s: type %s sender %s", __func__,
	     PSDaemonP_printMsg(msg->type), PSC_printTID(msg->sender));
    PSID_log(PSID_LOG_MSGDUMP, " destination %s size %d",
	     PSC_printTID(msg->dest), msg->len);

    switch (msg->type) {
    case PSP_CC_MSG:
	PSID_log(PSID_LOG_MSGDUMP, " type %d", ((PSLog_Msg_t *)msg)->type);
	break;
    default:
	break;
    }

    PSID_log(PSID_LOG_MSGDUMP, "\n");
}

int PSID_checkPrivilege(PStask_ID_t sender)
{
    if (PSC_getID(sender) != PSC_getMyID()) {
	/* No info on remote task, lets assumed it's privileged */
	return 1;
    }

    if (!PSC_getPID(sender)) {
	/* Sender is a daemon itself and, thus, privileged */
	return 1;
    }

    PStask_t *senderTask = PStasklist_find(&managedTasks, sender);

    if (!senderTask) return 0;

    if (senderTask->uid && senderTask->gid
	&& !PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_ADMUSER,
			       (PSIDnodes_guid_t){.u=senderTask->uid})
	&& !PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_ADMGROUP,
			       (PSIDnodes_guid_t){.g=senderTask->gid})) {
	return 0;
    }

    return 1;
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
	PSID_log(-1, "%s: open '%s' failed\n", __func__, fileName);
	return;
    }
    fprintf(fd, "%d", uid);
    fclose(fd);
}
