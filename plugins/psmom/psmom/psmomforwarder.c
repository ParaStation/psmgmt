/*
 * ParaStation
 *
 * Copyright (C) 2010-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/types.h>
#include <grp.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <pty.h>
#include <pwd.h>
#include <wordexp.h>
#include <syslog.h>
#include <sys/resource.h>

#include "psidutil.h"
#include "psidhook.h"
#include "pscommon.h"
#include "selector.h"
#include "timer.h"
#include "psipartition.h"

#include "pluginhelper.h"
#include "pluginmalloc.h"
#include "pluginlog.h"
#include "pluginpty.h"

#include "psaccounthandles.h"

#include "psmom.h"
#include "psmomspawn.h"
#include "psmomlog.h"
#include "psmomscript.h"
#include "psmomconfig.h"
#include "psmomspawn.h"
#include "psmomcomm.h"
#include "psmomlocalcomm.h"
#include "psmomconv.h"
#include "psmomsignal.h"
#include "psmominteractive.h"
#include "psmomenv.h"

#ifdef PAM_DEVEL_AVAIL
  #include <security/pam_appl.h>
#endif

#include "psmomforwarder.h"

/** pid of the running forwarder child */
static pid_t forwarder_child_pid = -1;

/** session id of the running forwarder child */
static pid_t forwarder_child_sid = -1;

/** process id of a backup child */
static pid_t backup_pid = -1;

/** timeout flag set to true if the walltime limit is reached */
static bool job_timeout = 0;

/** flag for sigchild */
static bool sigChild = false;

/** flag which will be set to 1 if the first kill phase started */
static bool killAllChildren = 0;

/** flag which indicates of a SIGKILL was already send */
static bool sentHardKill = 0;

/** type of the forwarder */
static int forwarder_type;

/** set to the jobid the forwarder belongs to */
static char *jobid = NULL;

/** the walltime timeout in seconds, set to -1 if no timeout is enforced */
static long timeout = -1;

/** unix pipe to recognize signals in sleeping in select() */
static int signalFD[2];

/** control channel between forwarder and child */
static int controlFDs[2];

/** communication handle conneted to the local psmom for std output */
static ComHandle_t *com = NULL;

char *fwType2Str(int type)
{
    switch (type) {
	case FORWARDER_COPY:
	    return "copy";
	case FORWARDER_INTER:
	    return "interactive";
	case FORWARDER_PELOGUE:
	    return "pelogue";
	case FORWARDER_JOBSCRIPT:
	    return "jobscript";
    }
    return NULL;
}

/**
 * @brief Forward child information to the main psmom.
 *
 * Read the SID from the started child and forward different information to the
 * main psmom process.
 *
 * @return No return value.
 */
static void doForwarderChildStart(void)
{
    /* close childs control fd */
    close(controlFDs[1]);

    /* read sid */
    if (doRead(controlFDs[0], &forwarder_child_sid, sizeof(pid_t))
	 != sizeof(pid_t)) {
	mlog("%s: reading childs sid failed\n", __func__);
	kill(SIGKILL, forwarder_child_pid);
    }

    /* Jail all my children */
    PSIDhook_call(PSIDHOOK_JAIL_CHILD, &forwarder_child_pid);

    /* send header */
    WriteDigit(com, CMD_LOCAL_CHILD_START);
    WriteString(com, jobid);

    /* pid of the forwarder itself */
    WriteDigit(com, getpid());

    /* pid of the forwarders child */
    WriteDigit(com, forwarder_child_pid);

    /* sid of the forwarders child */
    WriteDigit(com, forwarder_child_sid);

    /* type of the forwarder */
    WriteDigit(com, forwarder_type);

    /* walltime timeout */
    WriteDigit(com, timeout);

    wDoSend(com);
}

static void verifyTempDir(void)
{
    struct stat st;
    char *dir = getConfValueC(&config, "DIR_TEMP");

    if (dir && stat(dir, &st) != 0) {
	mwarn(errno, "%s: stat for TEMP DIR '%s' failed : ", __func__, dir);
    }
}

/**
 * @brief Initialize a child process.
 *
 * Initialize a child process and move it to its own session.
 *
 * @return No return value.
 */
static void initChild(void)
{
    /* close forwarders control fd */
    close(controlFDs[0]);

    /* close connection to local psmom */
    close(com->socket);

    /* needed or ioctl(TIOCSCTTY) will fail! */
    if ((forwarder_child_sid = setsid()) == -1) {
	mlog("%s: setsid() failed\n", __func__);
	exit(1);
    }

    /* send sid to forwarder */
    if (doWrite(controlFDs[1], &forwarder_child_sid, sizeof(pid_t))
	!= sizeof(pid_t)) {
	mlog("%s: failed writing childs sid\n", __func__);
	exit(1);
    }
}

void killForwarderChild(char *reason)
{
    int obitTime = 3;

    switch (forwarder_type) {
	case FORWARDER_INTER:
	case FORWARDER_JOBSCRIPT:
	    obitTime = getConfValueI(&config, "TIME_OBIT");
	    break;
	case FORWARDER_PELOGUE:
	    obitTime = getConfValueI(&config, "TIMEOUT_PE_GRACE");
	    break;
	case FORWARDER_COPY:
	    obitTime = 3;
    }

    if (forwarder_child_sid < 1) {
	mlog("%s: invalid child sid '%i'\n", __func__, forwarder_child_sid);
	exit(1);
    }

    if (reason) {
	mlog("signal 'SIGTERM' to sid '%i' job '%s' grace time '%i'"
		" sec, reason: %s\n", forwarder_child_sid, jobid, obitTime,
		reason);
    }

    if (psAccountSignalSession(forwarder_child_sid, SIGTERM) > 0) {
	killAllChildren = 1;
	alarm(obitTime);
    }
}

/**
 * @brief Forward accounting data and exit status to main psmom.
 *
 * @param rusg The rusage struct of the child return by wait4().
 *
 * @param status The exit status of the child.
 *
 * @return No return value.
 */
static void sendForwarderChildExit(struct rusage *rusg, int status)
{
    uint64_t cputime;

    /* wait for all children to exit */
    if (psAccountSignalSession(forwarder_child_sid, SIGTERM) > 0) {
	if (!killAllChildren) killForwarderChild(NULL);
	sleep(2);

	while (psAccountSignalSession(forwarder_child_sid, SIGTERM) > 0) {
	    if (sentHardKill) break;
	    sleep(2);
	}
    }

    WriteDigit(com, CMD_LOCAL_CHILD_EXIT);
    WriteString(com, jobid);
    WriteDigit(com, forwarder_type);
    WriteDigit(com, status);
    WriteDigit(com, forwarder_child_pid);

    cputime = rusg->ru_utime.tv_sec + 1.0e-6 * rusg->ru_utime.tv_usec +
		rusg->ru_stime.tv_sec + 1.0e-6 * rusg->ru_stime.tv_usec;

    wWrite(com, (char *) &cputime, sizeof(uint64_t));

    wDoSend(com);
}

/**
 * @brief Forward a signal received from the main psmom.
 *
 * @return No return value.
 */
static void handleLocalSignal(void)
{
    unsigned int sig;

    ReadDigitUI(com, &sig);
    mlog("%s: signal '%s (%i)' to '%i' and all children\n",
		__func__, signal2String(sig), sig, forwarder_child_pid);
    psAccountSignalSession(forwarder_child_sid, sig);
}

/**
 * @brief Say "hi" to main psmom process or it will kill us.
 *
 * @return No return value.
 */
static void sendForwarderHello(void)
{
    if (!com) {
	fprintf(stderr, "%s: communication handle invalid\n", __func__);
	return;
    }

    WriteDigit(com, CMD_LOCAL_HELLO);
    WriteDigit(com, forwarder_type);
    WriteString(com, jobid);
    wDoSend(com);
}

/**
 * @brief Tell the main psmom that forking failed.
 *
 * @return No return value.
 */
static void sendForkFailed(void)
{
    if (!com) {
	fprintf(stderr, "%s: communication handle invalid\n", __func__);
	return;
    }

    WriteDigit(com, CMD_LOCAL_FORK_FAILED);
    wDoSend(com);
}

/**
 * @brief Reset changed signal mask and hanlder.
 *
 * @return No return value.
 */
static void resetSignalHandling(void)
{
    /* restore sighandler */
    PSC_setSigHandler(SIGALRM, SIG_DFL);
    PSC_setSigHandler(SIGTERM, SIG_DFL);
    PSC_setSigHandler(SIGCHLD, SIG_DFL);
    PSC_setSigHandler(SIGPIPE, SIG_DFL);

    /* unblock blocked signals */
    blockSignal(SIGCHLD, 0);
    blockSignal(SIGALRM, 0);
}

/**
 * @brief Make sure all children are dead and request the psmom to close the
 * main connection.
 *
 * @return No return value.
 */
static void forwarderExit(void)
{
    /* reset possible alarms */
    alarm(0);

    /* make sure all children are dead */
    psAccountSignalSession(forwarder_child_sid, SIGKILL);

    /* restore sighandler */
    resetSignalHandling();

    /* request connection close */
    if (!com) {
	fprintf(stderr, "%s: communication handle invalid\n", __func__);
    } else {
	WriteDigit(com, CMD_LOCAL_CLOSE);
	wDoSend(com);
    }
}

/**
 * @brief Stop sleeping in select().
 *
 * @return No return value.
 */
static void stopForwarderLoop(void)
{
    int res = 1;

    if (doWrite(signalFD[0], &res, sizeof(res)) != sizeof(res)) {
	mlog("%s: writing to signalFD failed : %s\n", __func__,
		strerror(errno));
	exit(1);
    }
}

/**
 * @brief Handle various signals.
 *
 * @param sig The signal to handle.
 *
 * @return No return value.
 */
static void signalHandler(int sig)
{
    static char errmsg[] = "\r\npsmom: job timeout reached, terminating.\n\n\r";

    switch (sig) {
	case SIGTERM:
	    /* kill the child */
	    killForwarderChild("received SIGTERM");
	    break;
	case SIGALRM:
	    /* reset possible alarms */
	    alarm(0);

	    if (killAllChildren) {
		/* second kill phase, do it the hard way now */
		mlog("signal 'SIGKILL' to sid '%i' job" " '%s'\n",
			forwarder_child_sid, jobid);
		psAccountSignalSession(forwarder_child_sid, SIGKILL);
		killAllChildren = 0;
		sentHardKill = 1;
	    } else {
		if (forwarder_type == FORWARDER_INTER) {
		    writeQsubMessage(errmsg, strlen(errmsg));
		}

		killForwarderChild("timeout");

		job_timeout = 1;
	    }
	    break;
	case SIGCHLD:
	    sigChild = true;
	    stopForwarderLoop();
	    break;
	case SIGPIPE:
	    mlog("%s: got sigpipe\n", __func__);
	    break;
	default:
	    mdbg(PSMOM_LOG_LOCAL, "%s: got signal '%i'\n",
		__func__, sig);
	    break;
    }
}

/**
 * @brief Main loop for all forwarders.
 *
 * @return No return value.
 */
static void forwarderLoop(void)
{
    /* set timeout */
    if (timeout > 0) {
	alarm(timeout);
    }

    /* enable signals again */
    blockSignal(SIGCHLD, 0);
    blockSignal(SIGALRM, 0);

    while (!sigChild) {
	/* check for really short jobs */
	if (Swait(-1) < 0) {
	    if (errno && errno != EINTR) mwarn(errno, "%s: Swait()", __func__);
	}
    }
}

/**
 * @brief Main message handler.
 *
 * @param fd Not used, required by selector facility.
 *
 * @param info Not used, required by selector facility.
 *
 * @return Always returns 0.
 */
static int handleMainMessage(int fd, void *info)
{
    unsigned int cmd;

    if (ReadDigitUI(com, &cmd) < 0) {
	if (Selector_isRegistered(com->socket)) Selector_remove(com->socket);
	wClose(com);

	com = openLocalConnection();
	if (com) Selector_register(com->socket, handleMainMessage, NULL);

	mlog("%s: reconnected broken connection to main psmom\n", __func__);
	return 0;
    }
    switch (cmd) {
	case CMD_LOCAL_SIGNAL:
	    handleLocalSignal();
	    break;
	case CMD_LOCAL_QSUB_OUT:
	    handle_Local_Qsub_Out(com);
	    break;
	default:
	    mlog("%s: invalid cmd '%i'\n", __func__, cmd);
    }
    return 0;
}

/**
 * @brief Signal handler to break out of Sselect() when a signal arrives.
 *
 * @param fd The file descriptor were to wakup messages was written to.
 *
 * @param info Not used, required by selector facility.
 *
 * @return Always return 1 to stop Sselect().
 */
static int handleSignalFd(int fd, void *info)
{
    int res;

    doRead(fd, &res, sizeof(res));
    Selector_startOver();
    return 1;
}

/**
 * @brief Initialize a forwarder and establish a connection to main psmom.
 *
 * @param forwarderType The type of the forwarder.
 *
 * @param jobname The name of assosiated job.
 *
 * @return Returns 0 on success and 1 on error.
 */
static int initForwarder(int forwarderType, char *jobname)
{
    isMaster = 0;
    char pTitle[50];

    Selector_init(NULL);
    Timer_init(NULL);

    /* overwrite proc title */
    snprintf(pTitle, sizeof(pTitle), "psmom-%s-fw: %s",
		fwType2Str(forwarderType), jobname);
    PSC_setProcTitle(PSID_argc, PSID_argv, pTitle, 0);

    /* Reset connection to syslog */
    closelog();
    initLogger(NULL, NULL);
    openlog("psmomfw", LOG_PID|LOG_CONS, PSID_config->logDest);

    /* open local control connection back to psmom */
    forwarder_type = forwarderType;
    jobid = jobname;

    com = openLocalConnection();
    if (!com) {
	fprintf(stderr, "%s: open connection to psmom failed\n", __func__);
	return 1;
    }
    Selector_register(com->socket, handleMainMessage, NULL);

    sendForwarderHello();

    blockSignal(SIGCHLD, 1);
    blockSignal(SIGALRM, 1);

    PSC_setSigHandler(SIGALRM, signalHandler);
    PSC_setSigHandler(SIGTERM, signalHandler);
    PSC_setSigHandler(SIGCHLD, signalHandler);
    PSC_setSigHandler(SIGPIPE, signalHandler);

    /* open control fds */
    if (socketpair(PF_UNIX, SOCK_STREAM, 0, controlFDs)<0) {
	fprintf(stderr, "%s: open control socket failed\n", __func__);
	return 1;
    }

    /* open signal control fds */
    if (socketpair(PF_UNIX, SOCK_STREAM, 0, signalFD)<0) {
	fprintf(stderr, "%s: open control socket failed\n", __func__);
	return 1;
    }
    Selector_register(signalFD[0], handleSignalFd, NULL);
    Selector_register(signalFD[1], handleSignalFd, NULL);

    return 0;
}

/**
 * @brief Convert a gWord error to string.
 *
 * @err The gWord error to convert.
 *
 * @return Returns the requested string or NULL on error.
 */
static char *gWordExprError(int err)
{
    switch (err) {
	case WRDE_BADCHAR:
	    return "badchar - unquoted special characters";
	case WRDE_BADVAL:
	    return "badval - undefined shell variable";
	case WRDE_CMDSUB:
	    return "cmdsub - invalid cmd substitution requested";
	case WRDE_NOSPACE:
	    return "nospace - allocate memory failed";
	case WRDE_SYNTAX:
	    return "badsyn - shell syntax error";
	default:
	    return "unknown error code";
    }
}

/**
 * @brief Perform word expansion.
 *
 * The expanded words must be freed using wordfree().
 *
 * @param Buffer to save the expanded words.
 *
 * @param words The words to be expanded.
 *
 * @return Returns 0 on success and 1 on error.
 */
static int expandWords(wordexp_t *fExpr, char *words)
{
    static int envInit = 0;

    /* set environment var so wordexp() can parse them */
    if (!envInit) {
	setEnvVars();
	envInit = 1;
    }

    if (!wordexp(words, fExpr, WRDE_NOCMD | WRDE_UNDEF)) {
	if (fExpr->we_wordc > 1) {
	    wordfree(fExpr);
	    return 1;
	}
	return 0;
    }

    mlog("%s: wordexp failed(%i): %s\n", __func__, errno,
	    gWordExprError(errno));
    if (errno == WRDE_NOSPACE) wordfree(fExpr);

    return 1;
}

static bool rewriteCopyVisitor(char *key, char *value, const void *info)
{
    char *dest = *(char **)info, *reEnd = strchr(value, ' ');
    size_t lenStart, lenEnd;

    if (strcmp(key, "COPY_REWRITE")) return false;

    if (!reEnd) {
	mlog("%s: invalid rewrite option '%s'\n", __func__, value);
	return false;
    }
    reEnd++;

    lenEnd = strlen(reEnd);
    lenStart = strlen(value) - (lenEnd + 1);

    if (!strncmp(value, dest, lenStart)) {
	char *tmp = dest + lenStart;
	char *newDest = umalloc(lenEnd + strlen(tmp) + 1);

	strcpy(newDest, reEnd);
	strcat(newDest, tmp);
	newDest[lenEnd + strlen(tmp)] = '\0';
	mdbg(PSMOM_LOG_VERBOSE, "rewrite: dest:%s to new dest:%s\n",
	     dest, newDest);
	*(char **)info = newDest;
	return true;
    }
    return false;
}

/**
 * @brief Rewrite the copy destination when request in configuration.
 *
 * @param dest The destination string to rewrite.
 *
 * @returns The new destination or NULL if the destination did not change.
 */
static char *rewriteCopyDest(char *dest)
{
    char *myDest = dest;

    if (traverseConfig(&config, rewriteCopyVisitor, &myDest)) return myDest;

    return NULL;
}

/**
 * @brief Copy a single file in a new process using the configured copy command.
 *
 * @param src The source file to copy.
 *
 * @param dest The destination to copy the file to.
 *
 * @return Returns the exit status of the copy process.
 */
static int copyFile(char *src, char *dest, char *opt)
{
    char *cmd_cp, *opt_cp;
    int cp_status = -1, cp_wstat;
    struct rusage cp_rusage;

    cmd_cp = getConfValueC(&config, "CMD_COPY");
    if (!opt) {
	opt_cp = getConfValueC(&config, "OPT_COPY");
    } else {
	opt_cp = opt;
    }

    pid_t pid = fork();
    if (pid == -1) {
	mwarn(errno, "%s: fork() failed", __func__);
	sendForkFailed();
	exit(1);
    } else if (pid == 0) {
	execl(cmd_cp, cmd_cp, opt_cp, src, dest, NULL);

	/* never reached */
	exit(1);
    } else {
	/* wait for cp to finish */
	if (wait4(pid, &cp_wstat, 0, &cp_rusage) == -1) {
	    cp_status =  1;
	} else {
	    if (WIFEXITED(cp_wstat)) {
		cp_status = WEXITSTATUS(cp_wstat);
	    } else if (WIFSIGNALED(cp_wstat)) {
		cp_status = WTERMSIG(cp_wstat) + 0x100;
	    } else {
		cp_status = 1;
	    }
	}
    }
    return cp_status;
}

int execCopyForwarder(void *info)
{
    char *spoolDir;
    int status, wstat;
    struct rusage rusage;
    Copy_Data_t *data;

    if (!(data = (Copy_Data_t *) info)) {
	mlog("%s: invalid data\n", __func__);
	return 1;
    }

    if (initForwarder(FORWARDER_COPY, data->jobid)) return 1;

    Job_t *job = findJobById(data->jobid);
    if (!job) {
	fprintf(stderr, "%s: job for jobid '%s' not found\n", __func__,
		data->jobid);
	return 1;
    }

    timeout = getConfValueL(&config, "TIMEOUT_COPY");

    if ((forwarder_child_pid = fork()) < 0) {
	fprintf(stdout, "%s: unable to fork copy process : %s\n", __func__,
		strerror(errno));
	sendForkFailed();
	return 1;
    } else if (forwarder_child_pid == 0) {
	int i;
	Copy_Data_files_t **files;
	wordexp_t fExpr;

	files = data->files;

	/*  become session leader */
	initChild();

	/* set PBS environment vars */
	setupPBSEnv(job, 0);
	setEnvVars();

	/* switch to user */
	psmomSwitchUser(job->user, &job->passwd, 1);

	/* get some config options */
	spoolDir = getConfValueC(&config, "DIR_SPOOL");

	/* copy all files */
	for (i=0; i<data->count; i++) {
	    char src[256], *s, *dest, *d;
	    int cp_status;

	    /* setup source file */
	    s = files[i]->local;
	    if (!expandWords(&fExpr, s)) {
		s = fExpr.we_wordv[0];
	    } else {
		mlog("%s: src file '%s' can not be copied\n", __func__,
			files[i]->local);
		continue;
	    }
	    snprintf(src, sizeof(src), "%s/%s", spoolDir, s);

	    /* setup destination file */
	    if (!(dest = strchr(files[i]->remote, ':'))) {
		dest = files[i]->remote;
	    } else {
		if (strlen(dest) <= 1) {
		    char tmp[256];
		    snprintf(tmp, sizeof(tmp), "%s/%s",
				DEFAULT_DIR_JOB_UNDELIVERED, s);
		    mlog("%s: dest invalid, saving file '%s' in '%s'\n",
			    __func__, src, tmp);
		    if (rename(src, tmp) == -1) {
			mlog("%s: saving output to undelivered failed : %s\n",
				__func__, strerror(errno));
		    }
		    continue;
		}
		dest++;
	    }

	    /* rewrite the destination */
	    d = rewriteCopyDest(dest);
	    if (d) dest = d;

	    /* expand environment args */
	    if (!expandWords(&fExpr, dest)) {
		dest = fExpr.we_wordv[0];
	    } else {
		char tmp[512];
		snprintf(tmp, sizeof(tmp), "%s/%s", DEFAULT_DIR_JOB_UNDELIVERED,
			    src);
		mlog("%s: save file '%s' in '%s'\n", __func__, src, tmp);
		if (rename(src, tmp) == -1) {
		    mlog("%s: saving output to undelivered failed : %s\n",
			    __func__, strerror(errno));
		}
		continue;
	    }

	    /* skip invalid destinations */
	    if (!strcmp(dest, "/dev/null")) continue;

	    //mlog("%s: local:%s dest:%s\n", __func__, src, dest);

	    /* do the copy in a new process */
	    if ((cp_status = copyFile(src, dest, NULL)) != 0) {
		char tmp[256];
		snprintf(tmp, sizeof(tmp), "%s/%s",
			DEFAULT_DIR_JOB_UNDELIVERED, s);
		mlog("%s: copy failed: (%i), saving file '%s' in '%s'\n",
			__func__, cp_status, src, tmp);
		if (rename(src, tmp) == -1) {
		    mlog("%s: saving output to undelivered failed : %s\n",
			    __func__, strerror(errno));
		}
	    }
	}
	exit(0);
    }

    /* forward info to main daemon */
    doForwarderChildStart();

    forwarderLoop();

    if (wait4(forwarder_child_pid, &wstat, 0, &rusage) == -1) {
	fprintf(stderr, "%s: waitpid for %d failed\n", __func__,
	    forwarder_child_pid);
	status =  1;
    } else {
	if (WIFEXITED(wstat)) {
	    status = WEXITSTATUS(wstat);
	} else if (WIFSIGNALED(wstat)) {
	    status = WTERMSIG(wstat) + 0x100;
	} else {
	    status = 1;
	}
    }
    alarm(0);

    /* check for timeout */
    if (job_timeout) {
	fprintf(stderr, "%s: copy process timed out (%li sec)\n", __func__,
	    timeout);
	status = -4;
    }

    /* cleanup */
    forwarderExit();

    return status;
}

/**
 * @brief Prepare the environment and execute a PElog script.
 *
 * @param prologue If true we start a prologue script, else start an
 *	epilogue script.
 *
 *  @param data Pointer to the data structure the PElog script belongs to.
 *
 *  @param root If true we start the script as root, else we start a user
 *	PElog script.
 */
static void startPElogue(int prologue, PElogue_Data_t *data, char *filename,
			    int root)
{
    char *argv[100], exitCode[50];
    int argc = 0;

    /**
     * prologue cmdline args
     *
     * ($1) PBS_JOBID	    : 1262.michi-ng.localdomain
     * ($2) PBS_USER	    : rauh
     * ($3) PBS_GROUP	    : users
     * ($4) PBS_JOBNAME	    : mom_test
     * ($5) PBS_LIMITS	    : neednodes=1:ppn=2,nodes=1:ppn=2,walltime=00:00:10
     * ($6) PBS_QUEUE	    : batch
     * ($7) PBS_ACCOUNT     :
     */

    /**
     * prologue env vars
     *
     * PBS_MSHOST=michi-ng
     * PBS_RESOURCE_NODES=1:ppn=2
     * PATH=/bin:/usr/bin
     * PWD=/var/spool/torque/mom_priv
     * PBS_NODENUM=0
     * SHLVL=1
     * PBS_NODEFILE=/var/spool/torque/aux//1265.michi-ng.localdomain
     * _=/bin/env
    */

    /**
     * epilogue cmdline args
     *
     * ($1)  PBS_JOBID	    : 1264.michi-ng.localdomain
     * ($2)  PBS_USER	    : rauh
     * ($3)  PBS_GROUP	    : users
     * ($4)  PBS_JOBNAME    : mom_test
     * ($5)  PBS_SESSION_ID : 31813
     * ($6)  PBS_LIMITS	    : neednodes=1:ppn=2,nodes=1:ppn=2,walltime=00:00:10
     * ($7)  PBS_RESOURCES  : cput=00:00:05,mem=23700kb,vmem=36720kb,walltime=00:00:05
     * ($8)  PBS_QUEUE	    : batch
     * ($9)  PBS_ACCOUNT    :
     * ($10) PBS_EXIT	    : 0
     */

    /**
     * epilogue env vars
     *
     * PBS_MSHOST=michi-ng
     * PBS_RESOURCE_NODES=1:ppn=2
     * PATH=/bin:/usr/bin
     * PWD=/var/spool/torque/mom_priv
     * PBS_NODENUM=0
     * SHLVL=1
     * PBS_NODEFILE=/var/spool/torque/aux//1266.michi-ng.localdomain
     * _=/bin/env
     */

    /* prepare argv */
    argv[argc++] = filename;

    /* arg1: jobid */
    argv[argc++] = ustrdup(data->jobid);
    setenv("PBS_JOBID", data->jobid, 1);

    /* arg2: user */
    argv[argc++] = ustrdup(data->user);
    setenv("PBS_USER", data->user, 1);

    /* arg3: group */
    argv[argc++] = ustrdup(data->group);
    setenv("PBS_GROUP", data->group, 1);

    /* arg4: jobname */
    argv[argc++] = ustrdup(data->jobname);
    setenv("PBS_JOBNAME", data->jobname, 1);

    if (!prologue) {
	/* additional epilogue args */

	/* arg5: sessionid */
	argv[argc++] = ustrdup(data->sessid);
	setenv("PBS_SESSION_ID", data->sessid, 1);

	/* arg6: resource limits */
	argv[argc++] = ustrdup(data->limits);
	setenv("PBS_LIMITS", data->limits, 1);

	/* arg7: resources used */
	argv[argc++] = ustrdup(data->resources_used);
	setenv("PBS_RESOURCES", data->resources_used, 1);

	/* arg8: queue */
	argv[argc++] = ustrdup(data->queue);
	setenv("PBS_QUEUE", data->queue, 1);

	/* arg9: account (not set) */
	argv[argc++] = ustrdup("");
	setenv("PBS_ACCOUNT", "", 1);

	/* arg10: exit code */
	snprintf(exitCode, sizeof(exitCode), "%d", data->exit);
	argv[argc++] = ustrdup(exitCode);
	setenv("PBS_EXIT", exitCode, 1);

    } else {
	/* additional prologue args */

	/* arg5: resource limits */
	argv[argc++] = ustrdup(data->limits);
	setenv("PBS_LIMITS", data->limits, 1);

	/* arg6: queue */
	argv[argc++] = ustrdup(data->queue);
	setenv("PBS_QUEUE", data->queue, 1);

	/* arg7: account (not set) */
	argv[argc++] = ustrdup("");
	setenv("PBS_ACCOUNT", "", 1);
    }

    /* close argv */
    argv[argc++] = NULL;

    /* switch to user */
    if (!root) {
	struct passwd *spasswd;

	if (!(spasswd = getpwnam(data->user))) {
	    mlog("%s: getpwnam(%s) failed\n", __func__, data->user);
	    exit(1);
	}
	psmomSwitchUser(data->user, spasswd, 0);
    } else {
	setenv("USER", "root", 1);
	setenv("USERNAME", "root", 1);
	setenv("LOGNAME", "root", 1);
	setenv("HOME", rootHome, 1);
    }

    /* set tmp directory */
    if (data->tmpDir) {
	setenv("TMPDIR", data->tmpDir, 1);
    }

    /* set gpu info */
    if (data->gpus) {
	setenv("PBS_GPUS", data->gpus, 1);
    }

    /* set psmom var */
    setenv("PSMOM", "1", 1);

    /* restore signals */
    resetSignalHandling();

    /* start prologue script */
    execvp(argv[0], argv);
}

/**
 * @brief Prepare and spawn the child process.
 *
 * @param job The job to spawn the process for.
 *
 * @param argv Pointer to the argument list.
 *
 * @param argc Number of arguments.
 *
 * @return No return value.
 */
static void doSpawn(Job_t *job, char **argv, int argc)
{
    char **env = NULL;
    int envc;
    char *pShell, *end, *login, *shell, tmp[100];
    struct stat sb;

    /* open shell */
    char *ptr = getJobDetail(&job->data, "Shell_Path_List", NULL);
    if (ptr) {
	pShell = ustrdup(ptr);

	/* throw away host def */
	if ((end = strchr(pShell, '@'))) {
	    end[0] = '\0';
	}

	/* throw away other shells for other hosts */
	if ((end = strchr(pShell, ','))) {
	    end[0] = '\0';
	}

	/* lets see if we find the shell */
	if (stat(pShell, &sb) == -1) {
	    mlog("psmom: stat of shell '%s' failed, aborting"
		    " job\n\r", pShell);
	    exit(1);
	}

	shell = ustrdup(pShell);

	if ((login = strrchr(pShell, '/'))) {
	    login++;
	    snprintf(tmp, sizeof(tmp), "-%s", login);
	} else {
	    snprintf(tmp, sizeof(tmp), "-%s", pShell);
	}
	argv[argc++] = ustrdup(tmp);
    } else {
	/* no shell defined, use users default shell */
	shell = findUserShell(job, 0, 0);
	argv[argc++] = findUserShell(job, 1, 0);
    }

    /* set shell environment var */
    snprintf(tmp, sizeof(tmp), "SHELL=%s", shell);
    addEnv(tmp);

    /* prevent mpiexec from resolving the nodelist */
    snprintf(tmp, sizeof(tmp), "%s=1", ENV_PSID_BATCH);
    addEnv(tmp);

    /* overwrite commando to execute qsub -x */
    ptr = getJobDetail(&job->data, "inter_cmd", NULL);
    if (ptr) {
	argv[argc++] = ustrdup("-c");
	argv[argc++] = ustrdup(ptr);
    }

    env = getEnvArray(&envc);
    argv[argc++] = NULL;

    /* restore sighandler */
    resetSignalHandling();

    /* start the interactive shell */
    execve(shell, argv, env);

    /* never reached */
    exit(1);
}

/**
 * @brief Start a new PAM session.
 *
 * This will allow using various pam modules (e.g. pam_limits.so to
 * set correct ulimits).
 *
 * @param user The username to start the session for.
 *
 * @return No return value.
 */
static void startPAMSession(const char *user)
{
#ifdef PAM_DEVEL_AVAIL

    const char service_name[] = "psmom";
    const struct pam_conv conversation;
    int ret, disabled;
    pam_handle_t *pamh;

    disabled = getConfValueI(&config, "DISABLE_PAM");
    if (disabled) return;

    if ((ret = pam_start(service_name, user, &conversation, &pamh))
	    != PAM_SUCCESS) {
	mlog("%s: starting PAM failed : %s\n", __func__,
		pam_strerror(pamh, ret));
	exit(1);
    }

    if ((ret = pam_open_session(pamh, PAM_SILENT)) != PAM_SUCCESS) {
	mlog("%s: open PAM session failed : %s\n", __func__,
		pam_strerror(pamh, ret));
	exit(1);
    }

    if ((ret = pam_end(pamh, ret)) != PAM_SUCCESS) {
	mlog("%s: ending PAM failed : %s\n", __func__,
		pam_strerror(pamh, ret));
	return;
    }

#endif
}

int execInterForwarder(void *info)
{
    static char errmsg[] = "\r\npsmom: prologue execution failed, "
			    "aborting job\n\n\r";
    int status, wstat, stderrfds[2], controlPro[2];
    int x11Port = -1, prologue_exit = -1;
    struct rusage rusage;
    char *x11Cookie;
    Inter_Data_t *data;
    Job_t *job = (Job_t *)info;

    if (!job) {
	fprintf(stderr, "%s: invalid job id\n", __func__);
	return 1;
    }

    if (initForwarder(FORWARDER_INTER, job->id)) return 1;

    timeout = stringTimeToSec(getJobDetail(&job->data, "Resource_List",
	"walltime"));

    data = umalloc(sizeof(Inter_Data_t));

    /* set PBS environment vars */
    setupPBSEnv(job, 1);

    /* open pty */
    if (openpty(&stderrfds[0], &stderrfds[1], NULL, NULL, NULL) == -1) {
	fprintf(stderr, "%s: openpty() failed\n", __func__);
	return 1;
    }

    /* connect to waiting qsub */
    if (!initQsubConnection(job, data, stderrfds[0])) {
	return 1;
    }

    /* prepare x11 support */
    if ((x11Cookie = getJobDetail(&job->data, "forward_x11", ""))) {
	if (initX11Forwarding1(job, &x11Port)) return 1;
    }

    /* open control fds */
    if (socketpair(PF_UNIX, SOCK_STREAM, 0, controlPro)<0) {
	fprintf(stderr, "%s: open control socket failed\n", __func__);
	return 1;
    }

    if ((forwarder_child_pid = fork()) < 0) {
	fprintf(stdout, "%s: unable to fork interactive job : %s\n", __func__,
		strerror(errno));
	sendForkFailed();
	return 1;
    } else if (forwarder_child_pid == 0) {
	/* the interactive child */
	char *argv[50], tmp[QSUB_DATA_SIZE], dis[100];
	char *tty_name;
	int argc = 0, fd;

	tty_name = ttyname(stderrfds[1]);
	close(stderrfds[0]);

	/*  become session leader */
	initChild();

	/* wait till prologue finished */
	close(controlPro[0]);
	doRead(controlPro[1], &prologue_exit, sizeof(int));

	if (prologue_exit != 0) {
	    mlog("%s: progloue exit not 0: %i\n", __func__, prologue_exit);
	    exit(0);
	}

	/* prepare the pty */
	if (!pty_setowner(job->passwd.pw_uid, job->passwd.pw_gid, tty_name)) {
	    mlog("setting pty owner failed\n");
	    exit(1);

	}
	if (!pty_make_controlling_tty(&stderrfds[1], tty_name)) {
	    mlog("initialize controlling tty failed\n");
	    exit(1);
	}

	/* setup term options */
	setTermOptions(data->termcontrol, stderrfds[1]);

	/* setup pty */
	setWindowSize(data->windowsize, stderrfds[1]);

	/* setup terminal type */
	snprintf(tmp, sizeof(tmp), "TERM=%s",
		 (strlen(data->termtype) < 6) ? "xterm" : data->termtype + 5);
	addEnv(tmp);

	/* close all fd */
	for (fd=0; fd<getdtablesize(); fd++) {
	    if (fd == stderrfds[1]) continue;
	    if (com->socket == fd) continue;
	    close(fd);
	}

	/* setup stdin/stdout/stderr */
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
	dup2(stderrfds[1], STDIN_FILENO);
	dup2(stderrfds[1], STDOUT_FILENO);
	dup2(stderrfds[1], STDERR_FILENO);

	close(stderrfds[1]);

	/* start a PAM session */
	startPAMSession(job->user);

	/* set resource limits */
	setResourceLimits(job);

	/* switch to user */
	psmomSwitchUser(job->user, &job->passwd, 1);

	/* init x11 forwarding */
	if (x11Cookie && x11Port != -1) {
	    initX11Forwarding2(job, x11Cookie, x11Port, dis, sizeof(dis));
	    if (dis[0] != '\0') addEnv(dis);
	}

	verifyTempDir();

	/* do the actual spawn */
	doSpawn(job, argv, argc);
    }

    /* forward info to main daemon */
    doForwarderChildStart();

    /* wait till prologue is finished */
    if (doRead(com->socket, &prologue_exit, sizeof(prologue_exit))
	!= sizeof(prologue_exit)) {

	kill(forwarder_child_pid, SIGKILL);
	fprintf(stderr, "%s: reading prologue exit status failed (%i): %s\n",
		__func__, errno, strerror(errno));
	return 1;
    }

    if (prologue_exit != 0) {
	/* the prologue has run into an error. So we abort the job */
	if (prologue_exit == 2) {
	    writeQsubMessage(errmsg, strlen(errmsg));
	    sleep(1);
	}
	forwarderExit();
	return 0;
    } else {
	if (doWrite(controlPro[0], &prologue_exit, sizeof(int))
	    != sizeof(int)) {
	    fprintf(stderr, "%s: write to interactive child failed\n",
		    __func__);
	    kill(forwarder_child_pid, SIGKILL);
	    return 1;
	}
    }
    close(controlFDs[0]);
    close(controlFDs[1]);

    /* set input/output channels */
    close(stderrfds[1]);
    setSocketNonBlocking(stderrfds[0]);

    /* start forwarding data between local terminal and qsub */
    enableQsubConnection();

    forwarderLoop();

    if (wait4(forwarder_child_pid, &wstat, 0, &rusage) == -1) {
	fprintf(stderr, "%s: waitpid for %d failed\n", __func__,
		forwarder_child_pid);
	status = 1;
    } else {
	if (WIFEXITED(wstat)) {
	    status = WEXITSTATUS(wstat);
	} else if (WIFSIGNALED(wstat)) {
	    status = WTERMSIG(wstat) + 0x100;
	} else {
	    status = 1;
	}
    }
    alarm(0);

    closeQsubConnection();

    /* check for timeout */
    if (job_timeout) {
	fprintf(stdout, "%s: interactive job '%s' timed out (%s)\n", __func__,
	    job->id, getJobDetail(&job->data, "Resource_List", "walltime"));
    }

    sendForwarderChildExit(&rusage, status);

    /* cleanup */
    forwarderExit();

    return 0;
}

/**
 * @brief Start a timeout script which can log more detailed information.
 *
 * @param script The timeout script to spawn.
 *
 * @param data Pointer to the pelogue data.
 *
 * @param pelogue The pro/epilogue script name that timed out.
 *
 * @return No return value.
 */
static void spawnTimeoutScript(char *script, PElogue_Data_t *data,
				char *pelogue)
{
    char *argv[1];

    pid_t pid = fork();
    if (pid == -1) {
	mwarn(errno, "%s: fork() failed", __func__);
	sendForkFailed();
	return;
    } else if (pid == 0) {
	int fd;

	/* redirect stdout and stderr */
	dup2(open("/dev/null", 0), STDOUT_FILENO);
	dup2(open("/dev/null", 0), STDERR_FILENO);

	/* close all fd */
	for (fd=0; fd<getdtablesize(); fd++) {
	    close(fd);
	}

	/* reset signals */
	PSC_setSigHandler(SIGALRM, SIG_DFL);
	PSC_setSigHandler(SIGTERM, SIG_DFL);
	PSC_setSigHandler(SIGCHLD, SIG_DFL);
	PSC_setSigHandler(SIGPIPE, SIG_DFL);

	/* setup env */
	setenv("PBS_JOBID", data->jobid, 1);
	setenv("PBS_USER", data->user, 1);
	setenv("PBS_GROUP", data->group, 1);
	setenv("PBS_JOBNAME", data->jobname, 1);
	setenv("PELOGUE_SCRIPT", pelogue, 1);

	/* spawn the script */
	argv[0] = NULL;
	execv(script, argv);

	/* never reached */
	exit(0);
    }
}

/**
 * @brief Execute a pelogue script and wait for it to finish.
 *
 * @param data A pointer to the PElogue_Data structure.
 *
 * @param filename The filename of the script to spawn.
 *
 * @param root Flag which should be set to 1 if the script will run as root,
 * otherwise to 0.
 *
 * @return Returns 0 on success or on exit code indicating the error.
 */
static int runPElogueScript(PElogue_Data_t *data, char *filename, int root)
{
    int status = -1, fstat, wstat;
    struct rusage rusage;
    char buf[300], *peType, *timeoutScript;

    peType = data->prologue ? "prologue" : "epilogue";
    snprintf(buf, sizeof(buf), "%s/%s", data->dirScripts, filename);

    if ((fstat = checkPELogueFileStats(buf, root)) == 1) {

	mdbg(PSMOM_LOG_PELOGUE, "%s: executing '%s'\n", __func__, filename);

	if ((forwarder_child_pid = fork()) < 0) {
	    fprintf(stdout, "%s: unable to fork %s process : %s\n",
		    __func__, peType, strerror(errno));
	    sendForkFailed();
	    return -3;
	} else if (forwarder_child_pid == 0) {
	    int fd;

	    /*  become session leader */
	    initChild();

	    /* redirect stdout and stderr */
	    dup2(open("/dev/null", 0), STDOUT_FILENO);
	    dup2(open("/dev/null", 0), STDERR_FILENO);

	    /* close all fd */
	    for (fd=0; fd<getdtablesize(); fd++) {
		if (fd == STDOUT_FILENO || fd == STDERR_FILENO) continue;
		close(fd);
	    }

	    /* reset signals */
	    PSC_setSigHandler(SIGALRM, SIG_DFL);
	    PSC_setSigHandler(SIGTERM, SIG_DFL);
	    PSC_setSigHandler(SIGCHLD, SIG_DFL);

	    startPElogue(data->prologue, data, buf, root);

	    /* never reached */
	    exit(1);
	}

	/* forward info to main daemon */
	doForwarderChildStart();

	forwarderLoop();

	if (wait4(forwarder_child_pid, &wstat, 0, &rusage) == -1) {
	    fprintf(stdout, "waitpid for %d failed\n", forwarder_child_pid);
	    return -3;
	}
	alarm(0);

	if (WIFEXITED(wstat)) {
	    status = WEXITSTATUS(wstat);
	} else if (WIFSIGNALED(wstat)) {
	    status = WTERMSIG(wstat) + 0x100;
	} else {
	    status = 1;
	}

	if (job_timeout) {
	    fprintf(stdout, "'%s' as '%s' timed out\n", filename,
		    root ? "root" : "user");

	    /* start timeout script */
	    timeoutScript = getConfValueC(&config, "TIMEOUT_SCRIPT");
	    if (timeoutScript) {
		spawnTimeoutScript(timeoutScript, data, filename);
	    }
	    return -4;
	}

	if (status != 0) {
	    fprintf(stdout, "'%s' as '%s' failed, exit:%d\n", filename,
		    root ? "root" : "user", status);
	    return status;
	}
    } else if (fstat == -1) {
	/* if the prologue file not exists, all is okay */
	return 0;
    } else {
	fprintf(stdout, "permisson error for '%s' as '%s'\n", filename,
		root ? "root" : "user");
	return -1;
    }
    return 0;
}

/**
 * @brief Setup the path and filename to a pelogue script.
 *
 * @param script The name of the pelogue script.
 *
 * @param data A pointer to the PElogue_Data structure.
 *
 * @param buf The buffer which will receive the requested absolute filename.
 *
 * @param bufSize The size of the buffer.
 *
 * @return No return value.
 */
static void setPElogueName(char *script, PElogue_Data_t *data, char *buf,
    size_t bufSize)
{
    struct stat sb;
    char path[300], *par = "\0";

    if (!data->frontend) par = ".parallel";

    if (!strlen(data->nameExt)) {
	snprintf(buf, bufSize, "%s%s", script, par);
	return;
    }

    /* use extended script name */
    snprintf(buf, bufSize, "%s%s.%s", script, par, data->nameExt);
    snprintf(path, sizeof(path), "%s/%s", data->dirScripts, buf);

    if (stat(path, &sb) != -1) return;

    /* if the choosen script does not exist, fallback to standard script */
    snprintf(buf, bufSize, "%s%s", script, par);
}

int execPElogueForwarder(void *info)
{
    int ret;
    PElogue_Data_t *data;
    char name[50];

    data = (PElogue_Data_t *) info;

    /* set timeout from mother superior */
    timeout = data->timeout;

    if (initForwarder(FORWARDER_PELOGUE, data->jobid)) return 1;

    if (data->prologue) {
	mdbg(PSMOM_LOG_PELOGUE, "Running prologue script(s) [max %li sec]\n",
		timeout);

	setPElogueName("prologue", data, name, sizeof(name));

	/* prologue (root) */
	if (!(ret = runPElogueScript(data, name, 1))) {

	    /* prologue.user (user) */
	    setPElogueName("prologue.user", data, name, sizeof(name));
	    ret = runPElogueScript(data, name, 0);
	}
	mdbg(PSMOM_LOG_PELOGUE, "Running prologue script(s) finished\n");
    } else {
	mdbg(PSMOM_LOG_PELOGUE, "Running epilogue script(s) [max %li sec]\n",
		timeout);

	/* epilogue (root) */
	setPElogueName("epilogue", data, name, sizeof(name));
	if (!(ret = runPElogueScript(data, name, 1))) {

	    /* epilogue.user (user) */
	    setPElogueName("epilogue.user", data, name, sizeof(name));
	    ret = runPElogueScript(data, name, 0);
	}
	mdbg(PSMOM_LOG_PELOGUE, "Running epilogue script(s) finished\n");
    }

    /* cleanup */
    forwarderExit();

    return ret;
}

/**
 * @brief Setup the output and error log files for a jobscript
 *
 * @param job Pointer to the job structure.
 *
 * @param outLog Buffer which will receive the absolute path to the output file.
 *
 * @param outLen The size of the output buffer.
 *
 * @param errLog Buffer which will receive the absolute path to the error file.
 *
 * @param The size of the error buffer.
 *
 * @return Returns 1 on success and 0 on error.
 */
static int setOutputStreams(Job_t *job, char *outLog, size_t outLen,
				char *errLog, size_t errLen)
{
    char *seq, *tmp, *spoolDir, *homeDir, *join_path;
    char out[500], error[500];

    /* get dirs */
    spoolDir = getConfValueC(&config, "DIR_SPOOL");
    homeDir = getEnvValue("HOME");
    if (!homeDir) {
	mlog("%s: invalid home dir\n", __func__);
	return 0;
    }

    /* write output/error to users home not spool dir */
    char *keep_files = getJobDetail(&job->data, "Keep_Files", NULL);
    if (!keep_files) {
	mlog("%s: job detail keep_files not found\n", __func__);
	return 0;
    }

    char *jobname = getJobDetail(&job->data, "Job_Name", NULL);
    if (!jobname) {
	mlog("%s: jobname not found\n", __func__);
	return 0;
    }

    seq = ustrdup(job->id);
    if (!(tmp = strchr(seq, '.'))) {
	mlog("%s: invalid hashname '%s'\n", __func__, job->hashname);
	return 0;
    }
    tmp[0] = '\0';

    if (!strcmp(keep_files, "oe") || !strcmp(keep_files, "eo")) {
	snprintf(error, sizeof(error), "%s/%s.e%s", homeDir, jobname, seq);
	snprintf(out, sizeof(out), "%s/%s.o%s", homeDir, jobname, seq);
    } else if (!strcmp(keep_files, "e")) {
	snprintf(error, sizeof(error), "%s/%s.e%s", homeDir, jobname, seq);
	snprintf(out, sizeof(out), "%s/%s.OU", spoolDir, job->hashname);
    } else if (!strcmp(keep_files, "o")) {
	snprintf(out, sizeof(out), "%s/%s.o%s", homeDir, jobname, seq);
	snprintf(error, sizeof(error), "%s/%s.ER", spoolDir, job->hashname);
    } else {
	snprintf(out, sizeof(out), "%s/%s.OU", spoolDir, job->hashname);
	snprintf(error, sizeof(error), "%s/%s.ER", spoolDir, job->hashname);
    }
    if (seq) ufree(seq);

    /* redirect/join stdout and error output */
    if (!(join_path = getJobDetail(&job->data, "Join_Path", NULL))) {
	mlog("%s: job detail join_path not found\n", __func__);
	return 0;
    }

    if (!strcmp(join_path, "oe")) {
	snprintf(outLog, outLen, "%s", out);
	snprintf(errLog, errLen, "%s", out);
    } else if (!strcmp(join_path, "eo")) {
	snprintf(outLog, outLen, "%s", error);
	snprintf(errLog, errLen, "%s", error);
    } else {
	snprintf(outLog, outLen, "%s", out);
	snprintf(errLog, errLen, "%s", error);
    }

    return 1;
}

static void writeAccData(char *errLog, int status)
{
    Data_Entry_t *next;
    struct list_head *pos;
    FILE *errFile;
    size_t count = 0;
    Data_Entry_t accData;

    /* request accounting data */
    WriteDigit(com, CMD_LOCAL_REQUEST_ACCOUNT);
    WriteString(com, jobid);
    wDoSend(com);

    ReadDigitUL(com, &count);
    if (!count) {
	mlog("%s: failed getting account data\n", __func__);
	return;
    }

    INIT_LIST_HEAD(&accData.list);
    ReadDataStruct(com, count, &accData.list, NULL);

    if (list_empty(&accData.list)) return;

    /* write to stderr */
    if ((errFile = fopen(errLog, "a+")) == NULL) {
	mlog("%s: open error log failed\n", __func__);
    } else {
	fprintf(errFile, "\n-- job accounting information --\n");
	fprintf(errFile, "%-10s %i\n", "exit_code", status);
	list_for_each(pos, &accData.list) {
	    if ((next = list_entry(pos, Data_Entry_t, list)) == NULL) break;
	    if (!strcmp(next->name, "resources_used")) {
		fprintf(errFile, "%-10s %s\n", next->resource, next->value);
	    }
	}
	fclose(errFile);
    }
}

static void backupTimeout(int sig)
{
    mlog("%s: killing backup script, reason: timeout\n", __func__);
    kill(backup_pid, SIGKILL);
    alarm(0);
}

static void backupJob(char *backupScript, Job_t *job, char *outLog,
			char *errLog)
{
    int wstat;
    pid_t pid;
    char *localBackupDir, *nodeFiles, *jobFiles, *ptr, *opt, *spoolFiles;
    char *accFiles;
    char dest[400], src[400], start[100];
    struct rusage rusage;
    struct tm *ts;
    size_t len;

    localBackupDir = getConfValueC(&config, "DIR_LOCAL_BACKUP");
    if (!localBackupDir) {
	mlog("%s: no local backup dir given\n", __func__);
	return;
    }

    opt = getConfValueC(&config, "OPT_BACKUP_COPY");
    spoolFiles = getConfValueC(&config, "DIR_SPOOL");
    len = strlen(spoolFiles);

    /* copy stdout */
    if (!strncmp(outLog, spoolFiles, len)) {
	snprintf(dest, sizeof(dest), "%s/%s.OU", localBackupDir, job->hashname);
	setenv("PSMOM_BACKUP_OUTPUT", dest, 1);
	copyFile(outLog, dest, opt);
    } else {
	setenv("PSMOM_BACKUP_OUTPUT", outLog, 1);
    }

    /* copy stderr */
    if (!!strcmp(outLog, errLog)) {
	if (!strncmp(errLog, spoolFiles, len)) {
	    snprintf(dest, sizeof(dest), "%s/%s.ER", localBackupDir,
			job->hashname);
	    setenv("PSMOM_BACKUP_ERROR", dest, 1);
	    copyFile(errLog, dest, opt);
	} else {
	    setenv("PSMOM_BACKUP_ERROR", errLog, 1);
	}
    }

    /* copy nodefile */
    nodeFiles = getConfValueC(&config, "DIR_NODE_FILES");
    snprintf(src, sizeof(src), "%s/%s", nodeFiles, job->hashname);
    snprintf(dest, sizeof(dest), "%s/%s.NODES", localBackupDir, job->hashname);
    setenv("PSMOM_BACKUP_NODES", dest, 1);
    copyFile(src ,dest, opt);

    /* copy accountfile */
    accFiles = getConfValueC(&config, "DIR_JOB_ACCOUNT");
    snprintf(src, sizeof(src), "%s/%s", accFiles, job->hashname);
    snprintf(dest, sizeof(dest), "%s/%s.ACC", localBackupDir, job->hashname);
    setenv("PSMOM_BACKUP_ACCOUNT", dest, 1);
    copyFile(src ,dest, opt);

    /* copy jobscript */
    jobFiles = getConfValueC(&config, "DIR_JOB_FILES");
    snprintf(src, sizeof(src), "%s/%s", jobFiles, job->hashname);
    snprintf(dest, sizeof(dest), "%s/%s.JOBSCRIPT", localBackupDir,
		job->hashname);
    setenv("PSMOM_BACKUP_JOBSCRIPT", dest, 1);
    copyFile(src ,dest, opt);

    /* set the job startime */
    ts = localtime(&job->start_time);
    strftime(start, sizeof(start), "%Y-%m-%d %H:%M:%S", ts);
    setenv("PSMOM_JOBSTART", start, 1);

    pid = fork();
    if (pid == -1) {
	mwarn(errno, "%s: fork() failed", __func__);
	sendForkFailed();
	return;
    } else if (pid == 0) {
	int fd;

	/* close all fd */
	for (fd=0; fd<getdtablesize(); fd++) {
	    close(fd);
	}

	/* reset signals */
	PSC_setSigHandler(SIGALRM, SIG_DFL);
	PSC_setSigHandler(SIGTERM, SIG_DFL);
	PSC_setSigHandler(SIGCHLD, SIG_DFL);
	PSC_setSigHandler(SIGPIPE, SIG_DFL);

	if ((backup_pid = fork()) == -1) {
	    sendForkFailed();
	    mwarn(errno, "%s: forking backup process failed\n", __func__);
	    return;
	} else if (backup_pid == 0) {
	    char *argv[1];

	    /* set job environment vars */
	    setEnvVars();

	    if (freopen("/dev/null", "a+", stdout)) {};
	    if (freopen("/dev/null", "a+", stderr)) {};
	    argv[0] = NULL;

	    execv(backupScript, argv);

	    /* never reached */
	    exit(1);
	} else {
	    /* monitor backup process with a timeout */
	    int bTimeout = getConfValueI(&config, "TIMEOUT_BACKUP");
	    if (bTimeout > 0) {
		PSC_setSigHandler(SIGALRM, backupTimeout);
		alarm(bTimeout);
	    }

	    /* wait for the backup script */
	    if (wait4(backup_pid, &wstat, 0, &rusage) == -1) {
		mlog("%s: waiting for backup process failed\n", __func__);
	    }

	    /* delete leftover copies */
	    ptr = getenv("PSMOM_BACKUP_OUTPUT");
	    if (ptr && !strncmp(outLog, spoolFiles, len)) unlink(ptr);

	    ptr = getenv("PSMOM_BACKUP_ERROR");
	    if (ptr && !strncmp(errLog, spoolFiles, len)) unlink(ptr);

	    ptr = getenv("PSMOM_BACKUP_NODES");
	    if (ptr) unlink(ptr);

	    ptr = getenv("PSMOM_BACKUP_ACCOUNT");
	    if (ptr) unlink(ptr);

	    ptr = getenv("PSMOM_BACKUP_JOBSCRIPT");
	    if (ptr) unlink(ptr);

	    exit(0);
	}
    }
}

int execJobscriptForwarder(void *info)
{
    struct rusage rusage;
    int status, wstat, stdinFD[2], len, logAccount;
    Job_t *job;
    char outLog[500], errLog[500], *backupScript;

    job = (Job_t *) info;

    if (initForwarder(FORWARDER_JOBSCRIPT, job->id)) return 1;

    timeout = stringTimeToSec(getJobDetail(&job->data, "Resource_List",
					   "walltime"));

    mdbg(PSMOM_LOG_VERBOSE, "%s starting user job:%s\n", __func__, job->id);

    /* set jobscript permissions */
    if (chown(job->jobscript, job->passwd.pw_uid, job->passwd.pw_gid) == -1) {
	mlog("%s: chown(%i:%i) '%s' failed : %s\n", __func__,
	     job->passwd.pw_uid, job->passwd.pw_gid, job->jobscript,
	     strerror(errno));
	return 1;
    }

    if (chmod(job->jobscript, 0700) == -1) {
	mlog("%s: chmod 0700 on '%s' failed : %s\n", __func__,
	     job->jobscript, strerror(errno));
	return 1;
    }

    /* open shell stdin fds */
    if (socketpair(PF_UNIX, SOCK_STREAM, 0, stdinFD)<0) {
	fprintf(stderr, "%s: open control socket failed\n", __func__);
	return 1;
    }

    /* set PBS environment vars */
    setupPBSEnv(job, 0);

    /* setup output and error log files for jobscript */
    if (!setOutputStreams(job, outLog, sizeof(outLog), errLog,
			  sizeof(errLog))) {
	return 1;
    }

    /* start jobscript */
    if ((forwarder_child_pid = fork()) < 0) {
	fprintf(stdout, "%s: unable to fork Jobscript process : %s\n", __func__,
		strerror(errno));
	sendForkFailed();
	return -3;
    } else if (forwarder_child_pid == 0) {
	mode_t mask;
	int argc = 0, old_mask;
	char *argv[50];
	char *opt_mask;

	/*  become session leader */
	initChild();

	/* start a PAM session */
	startPAMSession(job->user);

	/* set resource limits */
	setResourceLimits(job);

	/* switch to user */
	psmomSwitchUser(job->user, &job->passwd, 1);

	/* set umask for job output/error files */
	opt_mask = getJobDetail(&job->data, "umask", NULL);
	if (opt_mask) {
	    /* switch to umask choosen by user via qsub -W umask option */
	    if (sscanf(opt_mask, "%i", &mask) != 1) {
		mlog("%s: invalid umask from qsub: %s\n", __func__,
			opt_mask);
		exit(1);
	    }
	    /* make sure copyout is successful */
	    mask = mask & 0377;
	    old_mask = umask(mask);
	} else if ((opt_mask = getConfValueC(&config, "JOB_UMASK"))) {
	    if (opt_mask[0] == '0') {
		if (sscanf(opt_mask, "%o", &mask) != 1) {
		    mlog("%s: invalid umask from config: %s\n", __func__,
			    opt_mask);
		    exit(1);
		}
	    } else {
		if (sscanf(opt_mask, "%i", &mask) != 1) {
		    mlog("%s: invalid umask from config: %s\n", __func__,
			    opt_mask);
		    exit(1);
		}
	    }
	    /* make sure copyout is successful */
	    mask = mask & 0377;
	    old_mask = umask(mask);
	} else {
	    old_mask = umask(0077);
	}

	/* setup file descriptors */
	setOutputFDs(outLog, errLog, stdinFD[1], com->socket);

	/* switch back to users default umask */
	umask(old_mask);

	verifyTempDir();

	/* do the actual spawn */
	doSpawn(job, argv, argc);
    }

    /* write jobname to stdin of login shell */
    close(stdinFD[1]);
    len = strlen(job->jobscript);
    if (doWrite(stdinFD[0], job->jobscript, len) != len) {
	fprintf(stderr, "%s: writing job filename(1) failed\n", __func__);
	kill(SIGKILL, forwarder_child_pid);
	return 1;
    }
    if (doWrite(stdinFD[0], "\n", 1) != 1) {
	fprintf(stderr, "%s: writing job filename(2) failed\n", __func__);
	kill(SIGKILL, forwarder_child_pid);
	return 1;
    }
    close(stdinFD[0]);

    /* forward info to main daemon */
    doForwarderChildStart();

    close(controlFDs[0]);
    close(controlFDs[1]);

    /* wait for the jobscript to exit */
    forwarderLoop();

    if (wait4(forwarder_child_pid, &wstat, 0, &rusage) == -1) {
	fprintf(stdout, "%s: waitpid for %d failed\n", __func__,
		forwarder_child_pid);
	status = -3;
    } else {
	if (WIFEXITED(wstat)) {
	    status = WEXITSTATUS(wstat);
	} else if (WIFSIGNALED(wstat)) {
	    status = WTERMSIG(wstat) + 0x100;
	} else {
	    status = 1;
	}
    }
    alarm(0);

    /* check for timeout */
    if (job_timeout) {
	fprintf(stdout, "%s: batch job '%s' timed out (%s)\n", __func__,
	    job->id, getJobDetail(&job->data, "Resource_List", "walltime"));
    }

    /* write accounting data to stderr */
    logAccount = getConfValueI(&config, "LOG_ACCOUNT_RECORDS");
    if ((logAccount || getEnvValue("PSMOM_LOG_ACCOUNT"))
	&& !getEnvValue("PSMOM_DIS_ACCOUNT")) {
	writeAccData(errLog, status);
    }

    /* backup job files */
    backupScript = getConfValueC(&config, "BACKUP_SCRIPT");
    if (backupScript) backupJob(backupScript, job, outLog, errLog);

    sendForwarderChildExit(&rusage, status);

    /* cleanup */
    forwarderExit();

    return 0;
}
