/*
 * ParaStation
 *
 * Copyright (C) 2014-2019 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <grp.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pty.h>
#include <fcntl.h>
#include <syslog.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <utime.h>

#include "psslurm.h"
#include "psslurmalloc.h"
#include "psslurmlog.h"
#include "psslurmlimits.h"
#include "psslurmcomm.h"
#include "psslurmproto.h"
#include "psslurmconfig.h"
#include "psslurmenv.h"
#include "psslurmmultiprog.h"
#include "psslurmio.h"
#include "psslurmpelogue.h"
#include "psslurmpin.h"
#include "psslurmspawn.h"
#include "psslurmpscomm.h"
#include "slurmcommon.h"

#include "pluginpty.h"
#include "pluginmalloc.h"
#include "pluginhelper.h"
#include "pluginpartition.h"
#include "pluginforwarder.h"
#include "pluginstrv.h"
#include "selector.h"
#include "psprotocolenv.h"
#include "psaccounthandles.h"
#include "pspmihandles.h"
#include "pslog.h"
#include "psidhook.h"
#include "psipartition.h"

#include "psslurmforwarder.h"

#define X11_AUTH_CMD "/usr/bin/xauth"

#define MPIEXEC_BINARY BINDIR "/mpiexec"

static int jobCallback(int32_t exit_status, Forwarder_Data_t *fw)
{
    Job_t *job = fw->userData;
    Alloc_t *alloc;

    mlog("%s: job '%u' finished, exit %i / %i\n", __func__, job->jobid,
	 exit_status, fw->estatus);
    if (!findJobById(job->jobid)) {
	mlog("%s: job '%u' not found\n", __func__, job->jobid);
	return 0;
    }

    /* make sure all processes are gone */
    signalStepsByJobid(job->jobid, SIGKILL, 0);
    signalTasks(job->jobid, job->uid, &job->tasks, SIGKILL, -1);
    killForwarderByJobid(job->jobid);

    job->state = JOB_COMPLETE;
    mdbg(PSSLURM_LOG_JOB, "%s: job '%u' in '%s'\n", __func__,
	    job->jobid, strJobState(job->state));
    sendJobExit(job, fw->estatus);
    psAccountDelJob(PSC_getTID(-1, fw->cPid));

    if (!(alloc = findAlloc(job->jobid))) {
	flog("allocation for job %u not found\n", job->jobid);
	deleteJob(job->jobid);
	return 0;
    }

    if (pluginShutdown) {
	/* shutdown in progress, hence we skip the epilogue */
	sendEpilogueComplete(alloc->id, SLURM_SUCCESS);
	deleteAlloc(alloc->id);
    } else if (alloc->terminate) {
	/* run epilogue now */
	mlog("%s: starting epilogue for allocation %u\n", __func__, alloc->id);
	mdbg(PSSLURM_LOG_JOB, "%s: job '%u' in '%s'\n", __func__,
		job->jobid, strJobState(job->state));
	startEpilogue(alloc);
    }

    job->fwdata = NULL;
    return 0;
}

static int stepFWIOcallback(int32_t exit_status, Forwarder_Data_t *fw)
{
    Step_t *step = fw->userData, *tmp;

    /* validate step pointer */
    tmp = findStepByStepId(step->jobid, step->stepid);
    if (!tmp || tmp != step) {
	mlog("%s: step %u:%u not found\n", __func__, step->jobid, step->stepid);
	return 0;
    }

    /* send launch error if local processes failed to start */
    unsigned int taskCount = countRegTasks(&step->tasks);
    if (taskCount != step->globalTaskIdsLen[step->localNodeId]) {
	sendLaunchTasksFailed(step, step->localNodeId, SLURM_ERROR);
    }

    /* send task exit to srun processes */
    sendTaskExit(step, NULL, NULL);

    step->fwdata = NULL;

    mlog("%s: step '%u:%u' finished\n", __func__,
	step->jobid, step->stepid);

    step->state = JOB_COMPLETE;
    mdbg(PSSLURM_LOG_JOB, "%s: step '%u:%u' in '%s'\n", __func__,
	    step->jobid, step->stepid, strJobState(step->state));

    /* test if we were waiting only for this step to finish */
    Alloc_t *alloc = findAlloc(step->jobid);
    if (!findJobById(step->jobid) && alloc && alloc->state == A_RUNNING
	&& alloc->terminate) {
	/* run epilogue now */
	mlog("%s: starting epilogue for step '%u:%u'\n", __func__, step->jobid,
		step->stepid);
	startEpilogue(alloc);
    }

    return 0;
}

static int stepCallback(int32_t exit_status, Forwarder_Data_t *fw)
{
    Step_t *step = fw->userData, *tmp;

    /* validate step pointer */
    tmp = findStepByStepId(step->jobid, step->stepid);
    if (!tmp || tmp != step) {
	mlog("%s: step %u:%u not found\n", __func__, step->jobid, step->stepid);
	return 0;
    }

    mlog("%s: step %u:%u state '%s' finished, exit %i / %i\n", __func__,
	 step->jobid, step->stepid, strJobState(step->state), exit_status,
	 fw->estatus);

    /* make sure all processes are gone */
    signalStep(step, SIGKILL, 0);
    killChild(PSC_getPID(step->loggerTID), SIGKILL);

    freeSlurmMsg(&step->srunIOMsg);

    if (step->state == JOB_PRESTART) {
	/* spawn failed */
	if (fw->codeRcvd && fw->ecode == - ESCRIPT_CHDIR_FAILED) {
	    sendSlurmRC(&step->srunControlMsg, ESCRIPT_CHDIR_FAILED);
	} else {
	    sendSlurmRC(&step->srunControlMsg, SLURM_ERROR);
	}
    } else if (step->state == JOB_SPAWNED) {
	    sendLaunchTasksFailed(step, ALL_NODES, SLURM_ERROR);
    } else {
	/* send task exit to srun processes */
	sendTaskExit(step, NULL, NULL);

	if (step->exitCode != -1) {
	    sendStepExit(step, step->exitCode);
	} else {
	    if (WIFSIGNALED(fw->estatus)) {
		sendStepExit(step, WTERMSIG(fw->estatus));
	    } else {
		sendStepExit(step, fw->estatus);
	    }
	}
    }

    step->state = JOB_COMPLETE;
    mdbg(PSSLURM_LOG_JOB, "%s: step '%u:%u' in '%s'\n", __func__,
	    step->jobid, step->stepid, strJobState(step->state));
    psAccountDelJob(PSC_getTID(-1, fw->cPid));

    /* test if we were waiting only for this step to finish */
    Alloc_t *alloc = findAlloc(step->jobid);
    if (!findJobById(step->jobid) && alloc && alloc->state == A_RUNNING
	&& alloc->terminate) {
	/* run epilogue now */
	mlog("%s: starting epilogue for step '%u:%u'\n", __func__, step->jobid,
		step->stepid);
	startEpilogue(alloc);
    }

    if (!alloc) {
	deleteStep(step->jobid, step->stepid);
    } else {
	step->fwdata = NULL;
    }

    return 0;
}

static int bcastCallback(int32_t exit_status, Forwarder_Data_t *fw)
{
    BCast_t *bcast = fw->userData;

    sendSlurmRC(&bcast->msg, WEXITSTATUS(fw->estatus));

    bcast->fwdata = NULL;
    if (bcast->lastBlock) {
	clearBCastByJobid(bcast->jobid);
    }

    return 0;
}

void switchUser(char *username, uid_t uid, gid_t gid, char *cwd)
{
    pid_t pid = getpid();

    /* jail child into cgroup */
    PSIDhook_call(PSIDHOOK_JAIL_CHILD, &pid);

    /* remove psslurm group memberships */
    if ((setgroups(0, NULL)) == -1) {
	mlog("%s: setgroups(0) failed : %s\n", __func__, strerror(errno));
	exit(1);
    }

    /* set supplementary groups */
    if ((initgroups(username, gid)) < 0) {
	mlog("%s: initgroups() failed : %s\n", __func__, strerror(errno));
	exit(1);
    }

    /* change the gid */
    if ((setgid(gid)) < 0) {
	mlog("%s: setgid(%i) failed : %s\n", __func__, gid,
		strerror(errno));
	exit(1);
    }

    /* change the uid */
    if ((setuid(uid)) < 0) {
	mlog("%s: setuid(%i) failed : %s\n", __func__, uid,
		strerror(errno));
	exit(1);
    }

    /* re-enable capability to create coredumps */
    prctl(PR_SET_DUMPABLE, 1);

    /* change to job working directory */
    if (cwd && (chdir(cwd)) == -1) {
	mlog("%s: chdir to '%s' failed : %s\n", __func__, cwd, strerror(errno));
	exit(1);
    }
}

static void execBatchJob(Forwarder_Data_t *fwdata, int rerun)
{
    Job_t *job = fwdata->userData;
    char buf[128];

    /* reopen syslog */
    openlog("psid", LOG_PID|LOG_CONS, LOG_DAEMON);
    snprintf(buf, sizeof(buf), "psslurm-job:%u", job->jobid);
    initLogger(buf, NULL);

    setFilePermissions(job);

    /* set default rlimits */
    setDefaultRlimits();

    /* switch user */
    switchUser(job->username, job->uid, job->gid, job->cwd);

    /* redirect output */
    redirectJobOutput(job);

    /* setup batch specific env */
    setJobEnv(job);

    /* set rlimits */
    setRlimitsFromEnv(&job->env, 0);

    /* do exec */
    closelog();
    execve(job->jobscript, job->argv, job->env.vars);
    int err = errno;

    /* execve failed */
    fprintf(stderr, "%s: execve %s failed: %s\n", __func__, job->argv[0],
	    strerror(err));
    openlog("psid", LOG_PID|LOG_CONS, LOG_DAEMON);
    snprintf(buf, sizeof(buf), "psslurm-job:%u", job->jobid);
    initLogger(buf, NULL);
    mwarn(err, "%s: execve %s failed: ", __func__, job->argv[0]);
    exit(err);
}

/**
 * Find step structure by using the values of SLURM_STEPID and SLURM_JOBID
 * in the passed environment. If NULL is passed as environment or one of the
 * variables is not found, the values used are 0 as job id and
 * SLURM_BATCH_SCRIPT as step id.
 *
 * jobid_out and stepid_out are set to the used values if not NULL.
 */
#define findStepByEnv(environ, jobid_out, stepid_out, isAdmin) \
	    __findStepByEnv(environ, jobid_out, stepid_out, isAdmin, \
			    __func__, __LINE__)
static Step_t * __findStepByEnv(char **environ, uint32_t *jobid_out,
				uint32_t *stepid_out, bool isAdmin,
				const char *func, const int line) {
    int count = 0;
    char *ptr;
    uint32_t jobid = 0, stepid = SLURM_BATCH_SCRIPT;
    Step_t *step;

    if (!environ) {
	mlog("%s: invalid environ pointer from '%s:%i'\n", __func__,
		func, line);
	return NULL;
    }

    ptr = environ[count++];
    while (ptr) {
	if (!(strncmp(ptr, "SLURM_STEPID=", 13))) {
	    sscanf(ptr+13, "%u", &stepid);
	}
	if (!(strncmp(ptr, "SLURM_JOBID=", 12))) {
	    sscanf(ptr+12, "%u", &jobid);
	}
	ptr = environ[count++];
    }

    if (jobid_out) *jobid_out = jobid;
    if (stepid_out) *stepid_out = stepid;

    if (!(step = findStepByStepId(jobid, stepid))) {
	if (!isAdmin) {
	    mlog("%s: step '%u:%u' not found for '%s:%i'\n", __func__,
		 jobid, stepid, func, line);
	}
    }

    return step;
}

int handleForwarderInit(void * data)
{
    PStask_t *task = data;
    Step_t *step;
    int status;
    pid_t child = PSC_getPID(task->tid);
    bool isAdmin;

    if (task->rank <0 || task->group != TG_ANY) return 0;
    isAdmin = isPSAdminUser(task->uid, task->gid);

    if ((step = findStepByEnv(task->environ, NULL, NULL, isAdmin))) {

	initSpawnFacility(step);

	if (step->taskFlags & LAUNCH_PARALLEL_DEBUG) {
	    waitpid(child, &status, WUNTRACED);
	    if (!WIFSTOPPED(status)) {
		mlog("%s: child '%i' not stopped\n", __func__, child);
	    } else {
		if ((killChild(child, SIGSTOP)) == -1) {
		    mwarn(errno, "%s: kill(%i) failed: ", __func__, child);
		}
		if ((ptrace(PTRACE_DETACH, child, 0, 0)) == -1) {
		    mwarn(errno, "%s: ptrace(PTRACE_DETACH) failed: ",
			    __func__);
		}
	    }
	}
    } else {
	if (!isAdmin) {
	    mlog("%s: rank '%i' failed to find my step\n",
		 __func__, task->rank);
	}
    }

    /* override spawn task filling function in pspmi */
    psPmiSetFillSpawnTaskFunction(fillSpawnTaskWithSrun);

    return 0;
}

int handleForwarderClientStatus(void * data)
{
    PStask_t *task = data;
    Step_t *step;
    pid_t childpid;
    char *argv[2];
    char buffer[4096], *taskEpilogue;
    int status, grace;
    size_t i;
    time_t t;
    bool isAdmin;

    if (task->rank <0 || task->group != TG_ANY) return 0;
    isAdmin = isPSAdminUser(task->uid, task->gid);

    if (!(step = findStepByEnv(task->environ, NULL, NULL, isAdmin))) {
	if (!isAdmin) {
	    mlog("%s: rank '%i' failed to find my step\n",
		    __func__, task->rank);
	}
	return 0;
    }

    if (task->rank < 0) return 0;

    if (!step->taskEpilog || *(step->taskEpilog) == '\0') return 0;

    taskEpilogue = step->taskEpilog;

    /* handle relative paths */
    if (taskEpilogue[0] != '/') {
	snprintf(buffer, 4096, "%s/%s", step->cwd, taskEpilogue);
	taskEpilogue = buffer;
    }

    if ((childpid = fork()) < 0) {
	mlog("%s: fork failed\n", __func__);
	return 0;
    }

    if (childpid == 0) {
	/* This is the child */

	setpgrp();

	setDefaultRlimits();

	setStepEnv(step);

	errno = 0;

	if (access(taskEpilogue, R_OK | X_OK) < 0) {
	    mwarn(errno, "task epilogue '%s' not accessable", taskEpilogue);
	    exit(-1);
	}

	for (i = 0; i < step->env.cnt; i++) {
	    putenv(step->env.vars[i]);
	}

	setRankEnv(task->rank, step);

	if (chdir(step->cwd) != 0) {
	    mwarn(errno, "cannot change to working direktory '%s'", step->cwd);
	}

	argv[0] = taskEpilogue;
	argv[1] = NULL;

	/* execute task epilogue */
	mlog("%s: starting task epilogue '%s' for rank %u of job %u\n",
	    __func__, taskEpilogue, task->rank, step->jobid);

	execvp(argv[0], argv);
	mwarn(errno, "%s: exec for task epilogue '%s' failed for rank %u of job"
	      " %u", __func__, taskEpilogue, task->rank, step->jobid);
	exit(-1);
    }

    /* This is the parent */

    t = time(NULL);
    grace = getConfValueI(&SlurmConfig, "KillWait");

    while(1) {
	if ((time(NULL) - t) > 5) killpg(childpid, SIGTERM);
	if ((time(NULL) - t) > (5 + grace)) killpg(childpid, SIGKILL);
	usleep(100000);
	if(waitpid(childpid, &status, WNOHANG) < 0) {
	    if (errno == EINTR) continue;
	    killpg(childpid, SIGKILL);
	    break;
	}
    }

    return 0;
}

int handleExecClient(void *data)
{
    PStask_t *task = data;

    if (task->rank <0) return 0;

    setDefaultRlimits();

    return 0;
}

int handleExecClientUser(void *data)
{
    PStask_t *task = data;
    Step_t *step;
    int i;
    uint32_t jobid = 0;
    bool isAdmin;

    if (task->rank <0 || task->group != TG_ANY) return 0;
    isAdmin = isPSAdminUser(task->uid, task->gid);

    /* unset MALLOC_CHECK_ set by psslurm */
    unsetenv("MALLOC_CHECK_");

    if ((step = findStepByEnv(task->environ, &jobid, NULL, isAdmin))) {
	/* set supplementary groups */
	if (step->gidsLen) {
	    setgroups(step->gidsLen, step->gids);
	}

	if (!(redirectIORank(step, task->rank))) return -1;

	/* stop child after exec */
	if (step->taskFlags & LAUNCH_PARALLEL_DEBUG) {
	    if ((ptrace(PTRACE_TRACEME, 0, 0, 0)) == -1) {
		mwarn(errno, "%s: ptrace() failed: ", __func__);
		return -1;
	    }
	}

	setRankEnv(task->rank, step);

	handleTaskPrologue(step->taskProlog, task->rank, jobid,
		PSC_getPID(task->tid), step->cwd);

	doMemBind(step, task);
	verboseMemPinningOutput(step, task);
    } else {
	if (!isAdmin) {
	    mlog("%s: rank '%i' failed to find my step\n",
		 __func__, task->rank);
	}
    }

    /* clean up environment */
    for (i=0; PSP_rlimitEnv[i].envName; i++) {
	unsetenv(PSP_rlimitEnv[i].envName);
    }

    unsetenv("__PSI_UMASK");
    unsetenv("__PSI_RAW_IO");
    unsetenv("PSI_SSH_INTERACTIVE");
    unsetenv("PSI_LOGGER_RAW_MODE");
    unsetenv("__PSI_LOGGER_UNBUFFERED");
    unsetenv("__MPIEXEC_DIST_START");
    unsetenv("MPIEXEC_VERBOSE");

    return 0;
}

static void initX11Forward(Step_t *step)
{
    char *cookie, *proto, *screen, *port, *host, *home;
    char display[100];
    char xauthCmd[200], x11Auth[100];
    FILE *fp;
    int iport;

    cookie = envGet(&step->spankenv, "X11_COOKIE");
    proto = envGet(&step->spankenv, "X11_PROTO");
    screen = envGet(&step->spankenv, "X11_SCREEN");
    port = envGet(&step->spankenv, "X11_PORT");
    host = envGet(&step->env, "SLURM_SUBMIT_HOST");
    home = envGet(&step->env, "HOME");
    iport = atoi(port);
    iport -= 6000;

    snprintf(display, sizeof(display), "%s:%i.%s", host, iport, screen);
    envSet(&step->env, "DISPLAY", display);

    snprintf(x11Auth, sizeof(x11Auth), "%s:%i.%s", host, iport, screen);
    snprintf(xauthCmd, sizeof(xauthCmd), "%s -q -", X11_AUTH_CMD);

    /* xauth needs the correct HOME */
    setenv("HOME", home, 1);

    if ((fp = popen(xauthCmd, "w")) != NULL) {
	fprintf(fp, "remove %s\n", x11Auth);
	fprintf(fp, "add %s %s %s\n", x11Auth, proto, cookie);
	pclose(fp);
    } else {
	mlog("%s: open xauth '%s' failed\n", __func__, X11_AUTH_CMD);
	envUnset(&step->env, "DISPLAY");
    }
}

static void setupStepIO(Forwarder_Data_t *fwdata, Step_t *step)
{
    char *tty_name, *cols = NULL, *rows = NULL;
    struct winsize ws;

    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    close(STDIN_FILENO);

    if (step->taskFlags & LAUNCH_PTY) {
	/* setup pty */
	tty_name = ttyname(fwdata->stdOut[0]);
	close(fwdata->stdOut[1]);

	pty_setowner(step->uid, step->gid, tty_name);
	pty_make_controlling_tty(&fwdata->stdOut[0], tty_name);

	cols = envGet(&step->env, "SLURM_PTY_WIN_COL");
	rows = envGet(&step->env, "SLURM_PTY_WIN_ROW");

	if (cols && rows) {
	    ws.ws_col = atoi(cols);
	    ws.ws_row = atoi(rows);
	    if (ioctl(step->stdOut[0], TIOCSWINSZ, &ws)) {
		mwarn(errno, "%s: ioctl(TIOCSWINSZ) %s:%s failed: ",
			__func__, cols, rows);
		exit(1);
	    }
	}

	/* redirect stdout/stderr/stdin to pty */
	if ((dup2(fwdata->stdOut[0], STDOUT_FILENO)) == -1) {
	    mwarn(errno, "%s: stdout dup2(%u) failed: ",
		    __func__, fwdata->stdOut[0]);
	    exit(1);
	}
	if ((dup2(fwdata->stdOut[0], STDERR_FILENO)) == -1) {
	    mwarn(errno, "%s: stderr dup2(%u) failed: ",
		    __func__, fwdata->stdOut[0]);
	    exit(1);
	}
	if ((dup2(fwdata->stdOut[0], STDIN_FILENO)) == -1) {
	    mwarn(errno, "%s: stdin dup2(%u) failed: ",
		    __func__, fwdata->stdOut[0]);
	    exit(1);
	}
    } else {
	if (!(step->taskFlags & LAUNCH_USER_MANAGED_IO)) {
	    if ((dup2(fwdata->stdOut[1], STDOUT_FILENO)) == -1) {
		mwarn(errno, "%s: stdout dup2(%u) failed: ",
			__func__, fwdata->stdOut[0]);
		exit(1);
	    }
	    if ((dup2(fwdata->stdErr[1], STDERR_FILENO)) == -1) {
		mwarn(errno, "%s: stderr dup2(%u) failed: ",
			__func__, fwdata->stdErr[0]);
		exit(1);
	    }
	    if (step->stdInRank == -1 && step->stdIn &&
	       strlen(step->stdIn) > 0) {
		/* input is redirected from file and not connected to psidfw! */

		int fd;
		if ((fd = open("/dev/null", O_RDONLY)) == -1) {
		    mwarn(errno, "%s: open /dev/null failed :", __func__);
		    exit(1);
		}
		if ((dup2(fd, STDIN_FILENO)) == -1) {
		    mwarn(errno, "%s: dup2(%i) '/dev/null' failed :",
			    __func__, fd);
		    exit(1);
		}
	    } else {
		if ((dup2(fwdata->stdIn[0], STDIN_FILENO)) == -1) {
		    mwarn(errno, "%s: stdin dup2(%u) failed: ",
			    __func__, fwdata->stdIn[0]);
		    exit(1);
		}
	    }
	}

	/* close obsolete fds */
	close(fwdata->stdIn[0]);
	close(fwdata->stdOut[0]);
	close(fwdata->stdErr[0]);
	close(fwdata->stdIn[1]);
	close(fwdata->stdOut[1]);
	close(fwdata->stdErr[1]);
    }
}

static bool isPMIdisabled(Step_t *step)
{
    char *val;

    if ((val = envGet(&step->env, "SLURM_MPI_TYPE"))) {
	if (!strcmp(val, "none")) return true;
    }
    return false;
}

static void debugMpiexecStart(char **argv, char **env)
{
    int i = 0;

    mlog("%s:", __func__);
    do {
	mlog(" %s", argv[i++]);
    } while (argv[i]);
    mlog("\n");

    for (i=0; env[i]; i++) {
	mlog("%s: env[%i] '%s'\n", __func__, i, env[i]);
    }
}

/**
 * @brief Build up mpiexec argument vector
 *
 * @param fwdata Forwarder data of the step
 *
 * @param argV Argument vector to build
 *
 * @param PMIdisabled Flag to signal if PMI is used
 */
static void buildMpiexecArgs(Forwarder_Data_t *fwdata, strv_t *argV,
			     bool PMIdisabled)
{
    Step_t *step = fwdata->userData;
    char buf[128];
    uint32_t i;

    strvInit(argV, NULL, 0);

    if (getenv("PMI_SPAWNED")) {
	char *tmpStr = getenv("__PSI_MPIEXEC_KVSPROVIDER");
	if (tmpStr) {
	    strvAdd(argV, ustrdup(tmpStr));
	} else {
	    strvAdd(argV, ustrdup(PKGLIBEXECDIR "/kvsprovider"));
	}
    } else {
	strvAdd(argV, ustrdup(MPIEXEC_BINARY));
    }

    /* always export all environment variables */
    strvAdd(argV, ustrdup("-x"));

    /* interactive mode */
    if (step->taskFlags & LAUNCH_PTY) strvAdd(argV, ustrdup("-i"));
    /* label output */
    if (step->taskFlags & LAUNCH_LABEL_IO) strvAdd(argV, ustrdup("-l"));
    /* PMI layer support */
    if (PMIdisabled) strvAdd(argV, ustrdup("--pmidisable"));

    if (step->taskFlags & LAUNCH_MULTI_PROG) {
	setupArgsFromMultiProg(step, fwdata, argV);
    } else {
	if (step->packJobid == NO_VAL) {
	    /* number of processes */
	    strvAdd(argV, ustrdup("-np"));
	    snprintf(buf, sizeof(buf), "%u", step->np);
	    strvAdd(argV, ustrdup(buf));

	    /* threads per processes */
	    strvAdd(argV, ustrdup("-tpp"));
	    snprintf(buf, sizeof(buf), "%u", step->numHwThreads/step->np);
	    strvAdd(argV, ustrdup(buf));

	    /* executable and arguments */
	    for (i=0; i<step->argc; i++) {
		strvAdd(argV, step->argv[i]);
	    }
	} else {
	    /* executables from job pack */
	    int64_t last, offset = -1;
	    uint32_t index = -1;
	    for (i=0; i<step->numPackInfo; i++) {
		/* find next pack task array index */
		last = offset;
		if (!findPackIndex(step, last, &offset, &index)) {
		    flog("calculating task index %u for step %u:%u failed\n", i,
			 step->jobid, step->stepid);
		    exit(1);
		}

		if (i) strvAdd(argV, ":");

		/* number of processes */
		strvAdd(argV, ustrdup("-np"));
		snprintf(buf, sizeof(buf), "%u", step->packInfo[index].np);
		strvAdd(argV, ustrdup(buf));

		/* threads per processes */
		uint16_t tpp = step->packInfo[index].numHwThreads /
				step->packInfo[index].np;
		strvAdd(argV, ustrdup("-tpp"));
		snprintf(buf, sizeof(buf), "%u", tpp);
		strvAdd(argV, ustrdup(buf));

		/* executable and arguments */
		uint32_t z;
		for (z=0; z<step->packInfo[index].argc; z++) {
		    strvAdd(argV, step->packInfo[index].argv[z]);
		}
	    }
	}
    }
}

static void execJobStep(Forwarder_Data_t *fwdata, int rerun)
{
    Step_t *step = fwdata->userData;
    strv_t argV;
    char buf[128];
    bool PMIdisabled = isPMIdisabled(step);
    int32_t oldMask = psslurmlogger->mask;

    /* reopen syslog */
    openlog("psid", LOG_PID|LOG_CONS, LOG_DAEMON);
    snprintf(buf, sizeof(buf), "psslurm-step:%u.%u", step->jobid, step->stepid);
    initLogger(buf, NULL);
    maskLogger(oldMask);

    /* setup standard I/O and pty */
    setupStepIO(fwdata, step);

    /* set default rlimits */
    setDefaultRlimits();

    /* switch user */
    switchUser(step->username, step->uid, step->gid, step->cwd);

    /* build mpiexec argument vector */
    buildMpiexecArgs(fwdata, &argV, PMIdisabled);

    /* setup step specific env */
    setStepEnv(step);

    /* setup x11 forwarding */
    if (step->x11forward) initX11Forward(step);

    mlog("%s: exec step '%u:%u' mypid '%u'\n", __func__, step->jobid,
	    step->stepid, getpid());

    /* set rlimits */
    setRlimitsFromEnv(&step->env, 1);

    /* remove environment variables not evaluted by mpiexec */
    removeUserVars(&step->env, PMIdisabled);

    if (psslurmlogger->mask & PSSLURM_LOG_PROCESS) {
	debugMpiexecStart(argV.strings, step->env.vars);
    }

    /* start mpiexec to spawn the parallel job */
    closelog();
    execve(argV.strings[0], argV.strings, step->env.vars);
    int err = errno;

    /* execve failed */
    fprintf(stderr, "%s: execve %s failed: %s\n", __func__, argV.strings[0],
	    strerror(err));
    openlog("psid", LOG_PID|LOG_CONS, LOG_DAEMON);
    snprintf(buf, sizeof(buf), "psslurm-step:%u.%u", step->jobid, step->stepid);
    initLogger(buf, NULL);
    mwarn(err, "%s: execve %s failed: ", __func__, argV.strings[0]);
    exit(err);
}

static int stepForwarderInit(Forwarder_Data_t *fwdata)
{
    Step_t *step = fwdata->userData;
    step->fwdata = fwdata;

    /* check if we can change working dir */
    if (step->cwd && chdir(step->cwd) == -1) {
	mlog("%s: chdir to '%s' failed : %s\n", __func__, step->cwd,
		strerror(errno));
	return - ESCRIPT_CHDIR_FAILED;
    }

    /* open stderr/stdout/stdin fds */
    if (step->taskFlags & LAUNCH_PTY) {
	/* open pty */
	if ((openpty(&fwdata->stdOut[1], &fwdata->stdOut[0],
			NULL, NULL, NULL)) == -1) {
	    mlog("%s: openpty() failed\n", __func__);
	    return -1;
	}
    } else {
	/* user will take care of I/O handling */
	if (step->taskFlags & LAUNCH_USER_MANAGED_IO) return 1;

	redirectStepIO(fwdata, step);
    }
    return 1;
}

static void stepForwarderLoop(Forwarder_Data_t *fwdata)
{
    Step_t *step = fwdata->userData;

    initStepIO(step);

    /* user will take care of I/O handling */
    if (step->taskFlags & LAUNCH_USER_MANAGED_IO) return;

    if (!step->IOPort) {
	mlog("%s: no IO Ports\n", __func__);
	return;
    }

    if (srunOpenIOConnection(step, step->cred->sig) == -1) {
	mlog("%s: open srun I/O connection failed\n", __func__);
	return;
    }

    if (step->taskFlags & LAUNCH_PTY) {
	/* open additiional pty connection to srun */
	if ((srunOpenPTYConnection(step)) < 0) {
	    /* Not working with current srun 14.03 anyway */
	}

	Selector_register(fwdata->stdOut[1], handleUserOE, fwdata);
	close(fwdata->stdOut[0]);
    } else {
	if (fwdata->stdOut[0] > -1) {
	    Selector_register(fwdata->stdOut[0], handleUserOE, fwdata);
	}
	if (fwdata->stdErr[0] > -1) {
	    Selector_register(fwdata->stdErr[0], handleUserOE, fwdata);
	}

	if (step->stdOutOpt == IO_SRUN) close(fwdata->stdOut[1]);
	if (step->stdErrOpt == IO_SRUN) close(fwdata->stdErr[1]);
	close(fwdata->stdIn[0]);
    }
}

static void handleChildStartJob(Forwarder_Data_t *fwdata, pid_t fw,
				pid_t childPid, pid_t childSid)
{
    psAccountRegisterJob(childPid, NULL);
}

static void handleChildStartStep(Forwarder_Data_t *fwdata, pid_t fw,
				 pid_t childPid, pid_t childSid)
{
    Step_t *step = fwdata->userData;

    psAccountRegisterJob(childPid, NULL);

    /* say ok to srun if mpiexec could be spawned */
    mlog("%s: send launch success for step '%u:%u' to srun sock '%u'\n",
	    __func__, step->jobid, step->stepid, step->srunControlMsg.sock);
    sendSlurmRC(&step->srunControlMsg, SLURM_SUCCESS);
    step->state = JOB_SPAWNED;
}

int execUserStep(Step_t *step)
{
    Forwarder_Data_t *fwdata;
    char jobid[100];
    char fname[300];
    int grace;

    grace = getConfValueI(&SlurmConfig, "KillWait");
    mlog("%s: %u:%u grace %u\n", __func__, step->jobid, step->stepid, grace);
    if (grace < 3) grace = 30;

    fwdata = ForwarderData_new();

    snprintf(jobid, sizeof(jobid), "%u.%u", step->jobid, step->stepid);
    snprintf(fname, sizeof(fname), "psslurm-step:%s", jobid);
    fwdata->pTitle = ustrdup(fname);

    fwdata->jobID = ustrdup(jobid);
    fwdata->userData = step;
    fwdata->graceTime = grace;
    fwdata->accounted = true;
    fwdata->killSession = psAccountSignalSession;
    fwdata->callback = stepCallback;
    fwdata->childFunc = execJobStep;
    fwdata->hookLoop = stepForwarderLoop;
    fwdata->hookFWInit = stepForwarderInit;
    fwdata->handleMthrMsg = stepForwarderMsg;
    fwdata->handleFwMsg = hookFWmsg;
    fwdata->hookChild = handleChildStartStep;
    fwdata->hookFinalize = stepFinalize;

    if (!startForwarder(fwdata)) {
	char msg[128];

	snprintf(msg, sizeof(msg), "starting forwarder for step '%u:%u' "
		 "failed\n", step->jobid, step->stepid);
	flog(msg);
	setNodeOffline(&step->env, step->jobid,
		       getConfValueC(&Config, "SLURM_HOSTNAME"), msg);
	return 0;
    }
    step->fwdata = fwdata;
    return 1;
}

bool execUserJob(Job_t *job)
{
    Forwarder_Data_t *fwdata;
    char fname[300];
    int grace;

    grace = getConfValueI(&SlurmConfig, "KillWait");
    if (grace < 3) grace = 30;

    fwdata = ForwarderData_new();

    snprintf(fname, sizeof(fname), "psslurm-job:%u", job->jobid);
    fwdata->pTitle = ustrdup(fname);

    fwdata->jobID = ustrdup(strJobID(job->jobid));
    fwdata->userData = job;
    fwdata->graceTime = grace;
    fwdata->accounted = true;
    fwdata->killSession = psAccountSignalSession;
    fwdata->callback = jobCallback;
    fwdata->childFunc = execBatchJob;
    fwdata->hookChild = handleChildStartJob;

    if (!startForwarder(fwdata)) {
	char msg[128];

	snprintf(msg, sizeof(msg), "starting forwarder for job '%u' failed\n",
		 job->jobid);
	flog(msg);
	setNodeOffline(&job->env, job->jobid,
		       getConfValueC(&Config, "SLURM_HOSTNAME"), msg);
	return false;
    }

    job->state = JOB_RUNNING;
    mdbg(PSSLURM_LOG_JOB, "%s: job '%u' in '%s'\n", __func__,
	    job->jobid, strJobState(job->state));
    job->fwdata = fwdata;
    return true;
}

static void execBCast(Forwarder_Data_t *fwdata, int rerun)
{
    BCast_t *bcast = fwdata->userData;
    int flags = 0, fd, left, ret, eno;
    struct utimbuf times;
    char *ptr;

    switchUser(bcast->username, bcast->uid, bcast->gid, NULL);
    errno = eno = 0;

    /* open the file */
    flags = O_WRONLY;
    if (bcast->blockNumber == 1) {
	flags |= O_CREAT;
	if (bcast->force) {
	    flags |= O_TRUNC;
	} else {
	    flags |= O_EXCL;
	}
    } else {
	flags |= O_APPEND;
    }

    if ((fd = open(bcast->fileName, flags, 0700)) == -1) {
	eno = errno;
	mwarn(eno, "%s: open '%s' failed :", __func__, bcast->fileName);
	exit(eno);
    }

    /* write the file */
    left = bcast->blockLen;
    ptr = bcast->block;
    while (left > 0) {
	if ((ret = write(fd, ptr, left)) == -1) {
	    eno = errno;
	    if (eno == EINTR || eno == EAGAIN) continue;
	    mwarn(eno, "%s: write '%s' failed :", __func__, bcast->fileName);
	    exit(eno);
	}
	left -= ret;
	ptr += ret;
    }

    /* set permissions */
    if (bcast->lastBlock) {
	if ((fchmod(fd, (bcast->modes & 0700))) == -1) {
	    eno = errno;
	    mwarn(eno, "%s: chmod '%s' failed :", __func__, bcast->fileName);
	    exit(eno);
	}
	if ((fchown(fd, bcast->uid, bcast->gid)) == -1) {
	    eno = errno;
	    mwarn(eno, "%s: chown '%s' failed :", __func__, bcast->fileName);
	    exit(eno);
	}
	if (bcast->atime) {
	    times.actime  = bcast->atime;
	    times.modtime = bcast->mtime;
	    if (utime(bcast->fileName, &times)) {
		eno = errno;
		mwarn(eno, "%s: utime '%s' failed :", __func__,
			bcast->fileName);
		exit(eno);
	    }
	}
    }
    close(fd);

    exit(0);
}

int execUserBCast(BCast_t *bcast)
{
    Forwarder_Data_t *fwdata;
    char jobid[100];
    char fname[300];
    int grace;

    grace = getConfValueI(&SlurmConfig, "KillWait");
    if (grace < 3) grace = 30;

    fwdata = ForwarderData_new();

    snprintf(jobid, sizeof(jobid), "%u", bcast->jobid);
    snprintf(fname, sizeof(fname), "psslurm-bcast:%s", jobid);
    fwdata->pTitle = ustrdup(fname);

    fwdata->jobID = ustrdup(jobid);
    fwdata->userData = bcast;
    fwdata->graceTime = grace;
    fwdata->killSession = psAccountSignalSession;
    fwdata->callback = bcastCallback;
    fwdata->childFunc = execBCast;

    if (!startForwarder(fwdata)) {
	char msg[128];

	snprintf(msg, sizeof(msg), "starting forwarder for bcast '%u' failed\n",
		 bcast->jobid);
	flog(msg);
	setNodeOffline(bcast->env, bcast->jobid,
		       getConfValueC(&Config, "SLURM_HOSTNAME"), msg);
	return 0;
    }

    bcast->fwdata = fwdata;
    return 1;
}

static void stepFWIOloop(Forwarder_Data_t *fwdata)
{
    Step_t *step = fwdata->userData;
    step->fwdata = fwdata;

    initStepIO(step);

    /* user will take care of I/O handling */
    if (step->taskFlags & LAUNCH_USER_MANAGED_IO) return;

    if (!step->IOPort) {
	mlog("%s: no I/O Ports\n", __func__);
	return;
    }

    if (srunOpenIOConnection(step, step->cred->sig) == -1) {
	mlog("%s: open srun I/O connection failed\n", __func__);
	return;
    }

    redirectStepIO2(fwdata, step);
}

int execStepFWIO(Step_t *step)
{
    Forwarder_Data_t *fwdata;
    char jobid[100];
    char fname[300];
    int grace;

    grace = getConfValueI(&SlurmConfig, "KillWait");
    mlog("%s: grace %u\n", __func__, grace);
    if (grace < 3) grace = 30;

    fwdata = ForwarderData_new();

    snprintf(jobid, sizeof(jobid), "%u.%u", step->jobid, step->stepid);
    snprintf(fname, sizeof(fname), "psslurm-step:%s", jobid);
    fwdata->pTitle = ustrdup(fname);

    fwdata->jobID = ustrdup(jobid);
    fwdata->userData = step;
    fwdata->graceTime = grace;
    fwdata->killSession = psAccountSignalSession;
    fwdata->hookLoop = stepFWIOloop;
    fwdata->handleMthrMsg = stepForwarderMsg;
    fwdata->handleFwMsg = hookFWmsg;
    fwdata->callback = stepFWIOcallback;
    fwdata->hookFinalize = stepFinalize;

    if (!startForwarder(fwdata)) {
	char msg[128];

	snprintf(msg, sizeof(msg), "starting I/O forwarder for step '%u:%u' "
		 "failed\n", step->jobid, step->stepid);
	flog(msg);
	setNodeOffline(&step->env, step->jobid,
		       getConfValueC(&Config, "SLURM_HOSTNAME"), msg);
	return 0;
    }
    step->fwdata = fwdata;

    return 1;
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet:*/
