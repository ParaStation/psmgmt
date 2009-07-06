/*
 *               ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2009 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
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

#include "psidnodes.h"
#include "psidamd.h"
#include "psidintel.h"
#include "psidppc.h"
#include "psidcomm.h"
#include "psidclient.h"

#include "psidutil.h"

config_t *config = NULL;

logger_t *PSID_logger;

/* Wrapper functions for logging */
void PSID_initLog(FILE *logfile)
{
    PSID_logger = logger_init(logfile ? "PSID" : NULL, logfile);
}

int32_t PSID_getDebugMask(void)
{
    return logger_getMask(PSID_logger);
}

void PSID_setDebugMask(int32_t mask)
{
    logger_setMask(PSID_logger, mask);
}

void PSID_blockSig(int block, int sig)
{
    sigset_t set;

    sigemptyset(&set);
    sigaddset(&set, sig);

    if (sigprocmask(block ? SIG_BLOCK : SIG_UNBLOCK, &set, NULL)) {
	PSID_log(-1, "%s: sigprocmask()\n", __func__);
    }
}

/* Reading and basic handling of the configuration */
void PSID_readConfigFile(FILE* logfile, char *configfile)
{
    /* Parse the configfile */
    config = parseConfig(logfile, PSID_getDebugMask(), configfile);
    if (! config) {
	PSID_log(-1, "%s: parsing of <%s> failed\n", __func__, configfile);
	exit(1);
    }
    config->logfile = logfile;

    /* Set correct debugging mask if given in config-file */
    if (config->logMask && !PSID_getDebugMask()) {
	PSID_setDebugMask(config->logMask);
	PSC_setDebugMask(config->logMask);
    }

    /* Try to find out if node is configured */
    if (PSC_getMyID() == -1) {
	PSID_log(-1, "%s: Node not configured\n", __func__);
	exit(1);
    }

    PSC_setNrOfNodes(PSIDnodes_getNum());
    PSC_setDaemonFlag(1); /* To get the correct result from PSC_getMyTID() */
}

long PSID_getVirtCPUs(void)
{
    return sysconf(_SC_NPROCESSORS_CONF);
}

long PSID_getPhysCPUs(void)
{
    long virtCPUs = PSID_getVirtCPUs(), physCPUs;

    PSID_log(PSID_LOG_VERB, "%s: got %ld virtual CPUs\n", __func__, virtCPUs);

    if (PSID_GenuineIntel()) {
	physCPUs = PSID_getPhysCPUs_IA32();
    } else if (PSID_AuthenticAMD()) {
	physCPUs = PSID_getPhysCPUs_AMD();
    } else if (PSID_PPC()) {
	physCPUs = PSID_getPhysCPUs_PPC();
    } else {
	/* generic case (assume no SMT) */
	PSID_log(-1, "%s: Generic case for # of physical CPUs.\n", __func__);
	physCPUs = virtCPUs;
    }

    return physCPUs;
}

#define LOCKFILENAME "/var/lock/subsys/parastation"

int PSID_lockFD = -1;

void PSID_getLock(void)
{
    PSID_lockFD = open(LOCKFILENAME, O_CREAT, 0600);
    if (PSID_lockFD<0) {
	PSID_warn(-1, errno, "%s: Unable to open lockfile '%s'",
		  __func__, LOCKFILENAME);
	exit (1);
    }

    if (chmod(LOCKFILENAME, S_IRWXU | S_IRGRP | S_IROTH)) {
	PSID_warn(-1, errno, "%s: chmod()", __func__);
	exit (1);
    }

    if (flock(PSID_lockFD, LOCK_EX | LOCK_NB)) {
	PSID_warn(-1, errno, "%s: Unable to get lock", __func__);
	exit (1);
    }
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
 * This function will return 1 in order the make the calling Sselect()
 * function return. This enables the daemon's main-loop to update the
 * PSID_readfds set of file-descriptors passed to the Sselect()
 * function of the selector facility.
 *
 * @param fd The file-descriptor from which the new connection might
 * be accepted.
 *
 * @param info Extra info. Currently ignored.
 *
 * @return On success 1 is returned. Otherwise -1 is returned and
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

    if (ssock >= FD_SETSIZE) {
	PSID_log(-1, "%s: error while accept(), ssock %d out of mask\n",
		 __func__, ssock);
	close(ssock);
	return -1;
    }

    registerClient(ssock, -1, NULL);
    FD_SET(ssock, &PSID_readfds);

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

    return 1; /* return 1 to allow main-loop updating PSID_readfds */
}

void PSID_createMasterSock(void)
{
    struct sockaddr_un sa;

    masterSock = socket(PF_UNIX, SOCK_STREAM, 0);

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, PSmasterSocketName, sizeof(sa.sun_path));

    /*
     * bind the socket to the right address
     */
    unlink(PSmasterSocketName);
    if (bind(masterSock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
	PSID_exit(errno, "Daemon already running?");
    }
    chmod(sa.sun_path, S_IRWXU | S_IRWXG | S_IRWXO);

    if (listen(masterSock, 20) < 0) {
	PSID_exit(errno, "Error while trying to listen");
    }

    PSID_log(-1, "Local Service Port (%d) created.\n", masterSock);
}

void PSID_enableMasterSock(void)
{
    if (masterSock < 0) {
	PSID_log(-1, "%s: Local Service Port not yet opened\n", __func__);
	PSID_createMasterSock();
    }
    if (!Selector_isInitialized()) {
	PSID_log(-1, "%s: Local Service Port needs running Selector\n",
		 __func__);
	exit(-1);
    }
    Selector_register(masterSock, handleMasterSock, NULL);

    PSID_log(-1, "Local Service Port (%d) enabled.\n", masterSock);
}

void PSID_shutdownMasterSock(void)
{
    char *action = NULL;

    if (masterSock == -1) {
	PSID_log(-1, "%s: master sock already down\n", __func__);
	return;
    }

    Selector_remove(masterSock);

    if (shutdown(masterSock, SHUT_RDWR)) {
	action = "shutdown()";
	goto exit;
    }
    if (close(masterSock)) {
	action = "close()";
	goto exit;
    }

exit:
    if (unlink(PSmasterSocketName)) {
	action = "unlink()";
    }
    if (action) PSID_warn(-1, errno, "%s: %s", __func__, action);
    masterSock = -1;
}

/* /\** */
/*  * @brief Write complete buffer. */
/*  * */
/*  * Write the complete buffer @a buf of size @a count to the file */
/*  * descriptor @a fd. Even if one or more trials to write to @a fd */
/*  * fails due to e.g. timeouts, further writing attempts are made until */
/*  * either a fatal error occurred or the whole buffer is sent. */
/*  * */
/*  * @param fd The file descriptor to send the buffer to. */
/*  * */
/*  * @param buf The buffer to send. */
/*  * */
/*  * @param count The number of bytes within @a buf to send. */
/*  * */
/*  * @return Upon success the number of bytes sent is returned, */
/*  * i.e. usually this is @a count. Otherwise -1 is returned. */
/*  *\/ */
/* static size_t writeall(int fd, const void *buf, size_t count) */
/* { */
/*     int len; */
/*     char *cbuf = (char *)buf; */
/*     size_t c = count; */

/*     while (c > 0) { */
/* 	len = write(fd, cbuf, c); */
/* 	if (len < 0) { */
/* 	    if ((errno == EINTR) || (errno == EAGAIN)) */
/* 		continue; */
/* 	    else */
/* 		return -1; */
/* 	} */
/* 	c -= len; */
/* 	cbuf += len; */
/*     } */

/*     return count; */
/* } */

/* /\** */
/*  * @brief Read complete buffer. */
/*  * */
/*  * Read the complete buffer @a buf of size @a count from the file */
/*  * descriptor @a fd. Even if one or more trials to read to @a fd fails */
/*  * due to e.g. timeouts, further reading attempts are made until */
/*  * either a fatal error occurred, an EOF is received or the whole */
/*  * buffer is read. */
/*  * */
/*  * @param fd The file descriptor to read the buffer from. */
/*  * */
/*  * @param buf The buffer to read. */
/*  * */
/*  * @param count The maximum number of bytes to read. */
/*  * */
/*  * @return Upon success the number of bytes read is returned, */
/*  * i.e. usually this is @a count if no EOF occurred. Otherwise -1 is */
/*  * returned. */
/*  *\/ */
/* static size_t readall(int fd, void *buf, size_t count) */
/* { */
/*     int len; */
/*     char *cbuf = (char *)buf; */
/*     size_t c = count; */

/*     while (c > 0) { */
/* 	len = read(fd, cbuf, c); */
/* 	if (len < 0) { */
/* 	    if ((errno == EINTR) || (errno == EAGAIN)) */
/* 		continue; */
/* 	    else */
/* 		return -1; */
/* 	} else if (len == 0) { */
/* 	    return count-c; */
/* 	} */
/* 	c -= len; */
/* 	cbuf += len; */
/*     } */

/*     return count; */
/* } */

/* static int defaultHandler(int fd, void *info) */
/* { */
/*     return 0; */
/* } */

/* int PSID_execHook(char *script, PSID_hookEnv_t env, PSID_hookCB_t cb, */
/* 		  void *info) */
/* { */
/*     int controlfds[2], iofds[2]; */
/*     int ret, result; */
/*     pid_t pid; */

/*     /\* Don't SEGFAULT *\/ */
/*     if (!script) { */
/* 	PSID_log(-1, "%s: script is NULL\n", __func__); */
/* 	return -1; */
/*     } */

/*     /\* create a control channel in order to observe the script *\/ */
/*     if (pipe(controlfds)<0) { */
/* 	PSID_warn(-1, errno, "%s: pipe(controlfds)", __func__); */
/* 	return -1; */
/*     } */

/*     /\* create a io channel in order to get script's output *\/ */
/*     if (pipe(iofds)<0) { */
/* 	PSID_warn(-1, errno, "%s: pipe(iofds)", __func__); */
/* 	return -1; */
/*     } */

/*     if (!(pid=fork())) { */
/* 	/\* This part calls the script and returns results to the parent *\/ */
/* 	int fd; */
/* 	char *command; */

/* 	for (fd=0; fd<getdtablesize(); fd++) { */
/* 	    if (fd != controlfds[1] && fd != iofds[1]) close(fd); */
/* 	} */

/* 	/\* setup the environment *\/ */
/* 	if (env) env(info); */

/* 	while (*script==' ' || *script=='\t') script++; */
/* 	if (*script != '/') { */
/* 	    char *dir = PSC_lookupInstalldir(NULL); */

/* 	    if (!dir) dir = ""; */

/* 	    command = PSC_concat(dir, "/", script, NULL); */
/* 	} else { */
/* 	    command = strdup(script); */
/* 	} */

/* 	/\* redirect stdout and stderr *\/ */
/* 	dup2(iofds[1], STDOUT_FILENO); */
/* 	dup2(iofds[1], STDERR_FILENO); */
/* 	close(iofds[1]); */

/* 	{ */
/* 	    char *dir = PSC_lookupInstalldir(NULL); */

/* 	    if (dir && (chdir(dir)<0)) { */
/* 		fprintf(stderr, "%s: cannot change to directory '%s'", */
/* 			__func__, dir); */
/* 		exit(0); */
/* 	    } */
/* 	} */

/* 	ret = system(command); */

/* 	/\* Send results to controlling daemon *\/ */
/* 	if (ret < 0) { */
/* 	    PSID_warn(-1, errno, "%s: system(%s)", __func__, command); */
/* 	} else { */
/* 	    ret = WEXITSTATUS(ret); */
/* 	} */
/* 	writeall(controlfds[1], &ret, sizeof(ret)); */

/* 	free(command); */
/* 	exit(0); */
/*     } */

/*     /\* This part receives results from the script *\/ */

/*     /\* save errno in case of error *\/ */
/*     ret = errno; */

/*     close(controlfds[1]); */
/*     close(iofds[1]); */

/*     /\* check if fork() was successful *\/ */
/*     if (pid == -1) { */
/* 	char *errstr = strerror(ret); */

/* 	close(controlfds[0]); */
/* 	close(iofds[0]); */

/* 	snprintf(scriptOut, sizeof(scriptOut), "%s: fork(): %s\n", */
/* 		 __func__, errstr ? errstr : "UNKNOWN"); */

/* 	return -1; */
/*     } */

/*     ret = readall(iofds[0], scriptOut, sizeof(scriptOut)); */
/*     /\* Discard further output *\/ */
/*     close(iofds[0]); */

/*     if (ret == sizeof(scriptOut)) { */
/* 	strcpy(&scriptOut[sizeof(scriptOut)-4], "..."); */
/*     } else if (ret<0) { */
/* 	snprintf(scriptOut, sizeof(scriptOut), */
/* 		 "%s: read(iofd) failed : %s", __func__, strerror(errno)); */
/* 	return -1; */
/*     } else { */
/* 	scriptOut[ret]='\0'; */
/*     } */

/*     ret = readall(controlfds[0], &result, sizeof(result)); */
/*     close(controlfds[0]); */

/*     if (!ret) { */
/* 	/\* control channel closed without telling result of system() call. *\/ */
/* 	snprintf(scriptOut, sizeof(scriptOut), "%s: no answer\n", __func__); */
/* 	return -1; */
/*     } else if (ret<0) { */
/* 	snprintf(scriptOut, sizeof(scriptOut), */
/* 		 "%s: read(controlfd) failed : %s", __func__, strerror(errno)); */
/* 	return -1; */
/*     } */

/*     return result; */
/* } */
