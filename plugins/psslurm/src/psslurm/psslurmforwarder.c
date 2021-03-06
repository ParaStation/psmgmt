/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
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
#include "psslurmfwcomm.h"
#ifdef HAVE_SPANK
#include "psslurmspank.h"
#endif

#include "pluginpty.h"
#include "pluginmalloc.h"
#include "pluginhelper.h"
#include "pluginpartition.h"
#include "pluginforwarder.h"
#include "pluginstrv.h"
#include "pscio.h"
#include "selector.h"
#include "psprotocolenv.h"
#include "psaccounthandles.h"
#include "pspmihandles.h"
#include "pslog.h"
#include "psidhook.h"
#include "psidsignal.h"
#include "psipartition.h"
#include "psidnodes.h"

#include "psslurmforwarder.h"

#define X11_AUTH_CMD "/usr/bin/xauth"

#define MPIEXEC_BINARY BINDIR "/mpiexec"

/** Allocation structure of the forwarder */
Alloc_t *fwAlloc = NULL;

/** Job structure of the forwarder */
Job_t *fwJob = NULL;

/** Step structure of the forwarder */
Step_t *fwStep = NULL;

/** Task structure of the forwarder */
PStask_t *fwTask = NULL;

static int termJobJail(void *info)
{
    Job_t *job = info;

    setJailEnv(&job->env, job->username, NULL, &(job->hwthreads));
    return PSIDhook_call(PSIDHOOK_JAIL_TERM, &job->fwdata->cPid);
}

static int cbTermJail(int fd, PSID_scriptCBInfo_t *info)
{
    int32_t exit = 0;
    char errMsg[1024];
    size_t errLen;

    bool ret = getScriptCBdata(fd, info, &exit, errMsg, sizeof(errMsg),&errLen);
    if (!ret) {
	mlog("%s: getting jail term script callback data failed\n", __func__);
	ufree(info);
	return 0;
    }

    if (exit != PSIDHOOK_NOFUNC && exit != 0) {
	mlog("%s: jail script failed with exit status %i\n", __func__, exit);
	mlog("%s: %s\n", __func__, errMsg);
    }

    ufree(info);
    return 0;
}

static int jobCallback(int32_t exit_status, Forwarder_Data_t *fw)
{
    Job_t *job = fw->userData;
    Alloc_t *alloc = findAlloc(job->jobid);

    mlog("%s: job '%u' finished, exit %i / %i\n", __func__, job->jobid,
	 exit_status, fw->chldExitStatus);
    if (!findJobById(job->jobid)) {
	mlog("%s: job '%u' not found\n", __func__, job->jobid);
	return 0;
    }

    /* terminate cgroup */
    PSID_execFunc(termJobJail, NULL, cbTermJail, job);

    /* make sure all processes are gone */
    signalStepsByJobid(job->jobid, SIGKILL, 0);
    signalTasks(job->jobid, job->uid, &job->tasks, SIGKILL, -1);
    killForwarderByJobid(job->jobid);

    job->state = JOB_COMPLETE;
    fdbg(PSSLURM_LOG_JOB, "job %u in %s\n", job->jobid,strJobState(job->state));

    /* get exit status of child */
    int eStatus = fw->exitRcvd ? fw->chldExitStatus : fw->hookExitCode;

    /* job aborted due to node failure */
    if (alloc && alloc->nodeFail) eStatus = 9;

    flog("job %u finished, exit %i\n", job->jobid, eStatus);

    sendJobExit(job, eStatus);

    psAccountDelJob(PSC_getTID(-1, fw->cPid));

    if (!alloc) {
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
	flog("starting epilogue for allocation %u\n", alloc->id);
	fdbg(PSSLURM_LOG_JOB, "job %u in %s\n", job->jobid,
	     strJobState(job->state));
	startEpilogue(alloc);
    }

    job->fwdata = NULL;
    return 0;
}

static int termStepJail(void *info)
{
    Step_t *step = info;

    setJailEnv(&step->env, step->username,
	    &(step->nodeinfos[step->localNodeId].stepHWthreads),
	    &(step->nodeinfos[step->localNodeId].jobHWthreads));
    return PSIDhook_call(PSIDHOOK_JAIL_TERM, &step->fwdata->cPid);
}

static int stepFollowerCB(int32_t exit_status, Forwarder_Data_t *fw)
{
    Step_t *step = fw->userData;

    /* validate step pointer */
    if (!verifyStepPtr(step)) {
	flog("Invalid step pointer %p\n", step);
	return 0;
    }

    /* terminate cgroup */
    PSID_execFunc(termStepJail, NULL, cbTermJail, step);

    /* send launch error if local processes failed to start */
    unsigned int taskCount = countRegTasks(&step->tasks);
    if (taskCount != step->globalTaskIdsLen[step->localNodeId]) {
	sendLaunchTasksFailed(step, step->localNodeId, SLURM_ERROR);
    }

    /* send task exit to srun processes */
    sendTaskExit(step, NULL, NULL);

    step->fwdata = NULL;

    flog("%s tid %s finished\n", strStepID(step), PSC_printTID(fw->tid));

    step->state = JOB_COMPLETE;
    fdbg(PSSLURM_LOG_JOB, "%s in %s\n", strStepID(step),
	 strJobState(step->state));

    /* test if we were waiting only for this step to finish */
    Alloc_t *alloc = findAlloc(step->jobid);
    if (!findJobById(step->jobid) && alloc && alloc->state == A_RUNNING
	&& alloc->terminate) {
	/* run epilogue now */
	flog("starting epilogue for %s\n", strStepID(step));
	startEpilogue(alloc);
    }

    if (step->stepid == SLURM_INTERACTIVE_STEP) {
	deleteStep(step->jobid, step->stepid);
    }

    return 0;
}

static int stepCallback(int32_t exit_status, Forwarder_Data_t *fw)
{
    Step_t *step = fw->userData;

    /* validate step pointer */
    if (!verifyStepPtr(step)) {
	flog("Invalid step pointer %p\n", step);
	return 0;
    }

    Alloc_t *alloc = findAlloc(step->jobid);
    flog("%s in %s finished, exit %i / %i\n", strStepID(step),
	 strJobState(step->state), exit_status, fw->chldExitStatus);

    /* terminate cgroup */
    PSID_execFunc(termStepJail, NULL, cbTermJail, step);

    /* make sure all processes are gone */
    signalStep(step, SIGKILL, 0);
    killChild(PSC_getPID(step->loggerTID), SIGKILL, step->uid);

    freeSlurmMsg(&step->srunIOMsg);

    if (step->state == JOB_PRESTART) {
	/* spawn failed */
	if (fw->codeRcvd && fw->hookExitCode == - ESCRIPT_CHDIR_FAILED) {
	    sendSlurmRC(&step->srunControlMsg, ESCRIPT_CHDIR_FAILED);
	} else {
	    sendSlurmRC(&step->srunControlMsg, SLURM_ERROR);
	}
    } else if (step->state == JOB_SPAWNED) {
	    sendLaunchTasksFailed(step, ALL_NODES, SLURM_ERROR);
    } else {
	/* send task exit to srun processes */
	sendTaskExit(step, NULL, NULL);

	/* send step exit to slurmctld */
	int eStatus = step->exitCode;
	if (eStatus == -1) {
	    eStatus = WIFSIGNALED(fw->chldExitStatus) ?
			WTERMSIG(fw->chldExitStatus) : fw->chldExitStatus;
	}

	/* step aborted due to node failure */
	if (alloc && alloc->nodeFail) eStatus = 9;

	sendStepExit(step, eStatus);

	/* forward exit status to pack follower */
	if (step->packJobid != NO_VAL && step->packNrOfNodes > 1) {
	    if (send_PS_PackExit(step, eStatus) == -1) {
		flog("sending pack exit for %s failed\n", strStepID(step));
	    }
	}
    }

    step->state = JOB_COMPLETE;
    fdbg(PSSLURM_LOG_JOB, "%s in %s\n", strStepID(step),
	 strJobState(step->state));
    psAccountDelJob(PSC_getTID(-1, fw->cPid));

    /* test if we were waiting only for this step to finish */
    if (!findJobById(step->jobid) && alloc && alloc->state == A_RUNNING
	&& alloc->terminate) {
	/* run epilogue now */
	flog("starting epilogue for %s\n", strStepID(step));
	startEpilogue(alloc);
    }

    if (!alloc || step->stepid == SLURM_INTERACTIVE_STEP) {
	deleteStep(step->jobid, step->stepid);
    } else {
	step->fwdata = NULL;
	clearTasks(&step->tasks);
    }

    return 0;
}

static int bcastCallback(int32_t exit_status, Forwarder_Data_t *fw)
{
    BCast_t *bcast = fw->userData;

    sendSlurmRC(&bcast->msg, WEXITSTATUS(fw->chldExitStatus));

    bcast->fwdata = NULL;
    if (bcast->lastBlock) {
	clearBCastByJobid(bcast->jobid);
    }

    return 0;
}

static int setFilePermissions(Job_t *job)
{
    if (!job->jobscript) return 1;

    if (chown(job->jobscript, job->uid, job->gid) == -1) {
	mwarn(errno, "%s: chown(%s, %i, %i)", __func__,
	      job->jobscript, job->uid, job->gid);
	return 1;
    }

    if (chmod(job->jobscript, 0700) == -1) {
	mwarn(errno, "%s: chmod(%s, 0700)", __func__, job->jobscript);
	return 1;
    }

    return 0;
}

static void fwExecBatchJob(Forwarder_Data_t *fwdata, int rerun)
{
    Job_t *job = fwdata->userData;
    char buf[128];

    /* reopen syslog */
    openlog("psid", LOG_PID|LOG_CONS, LOG_DAEMON);
    snprintf(buf, sizeof(buf), "psslurm-job:%u", job->jobid);
    initLogger(buf, NULL);

    setFilePermissions(job);

    PSIDhook_call(PSIDHOOK_PSSLURM_JOB_EXEC, job->username);

    /* set default RLimits */
    setDefaultRlimits();

    /* switch user */
    char *cwd = job->cwd;
    if (getConfValueI(&Config, "CWD_PATTERN") == 1) {
	cwd = IO_replaceJobSymbols(job, job->cwd);
    }
    if (!switchUser(job->username, job->uid, job->gid, cwd)) {
	flog("switching user failed\n");
	exit(1);
    }

    /* redirect output */
    IO_redirectJob(fwdata, job);

    /* setup batch specific environment */
    setJobEnv(job);

    /* set RLimits */
    setRlimitsFromEnv(&job->env, 0);

    /* do exec */
    closelog();
    execve(job->jobscript, job->argv, job->env.vars);
    int err = errno;

    /* execve() failed */
    fprintf(stderr, "%s: execve(%s): %s\n", __func__, job->argv[0],
	    strerror(err));
    openlog("psid", LOG_PID|LOG_CONS, LOG_DAEMON);
    snprintf(buf, sizeof(buf), "psslurm-job:%u", job->jobid);
    initLogger(buf, NULL);
    mwarn(err, "%s: execve(%s)", __func__, job->argv[0]);
    exit(err);
}

Step_t * __findStepByEnv(char **environ, uint32_t *jobid_out,
			 uint32_t *stepid_out, bool isAdmin,
			 const char *func, const int line)
{
    uint32_t jobid = 0, stepid = SLURM_BATCH_SCRIPT;

    if (!environ) {
	mlog("%s: invalid environ pointer from '%s:%i'\n", __func__,
		func, line);
	return NULL;
    }

    int count = 0;
    char *ptr = environ[count++];
    while (ptr) {
	if (!strncmp(ptr, "SLURM_STEPID=", 13)) {
	    sscanf(ptr+13, "%u", &stepid);
	}
	if (!strncmp(ptr, "SLURM_JOBID=", 12)) {
	    sscanf(ptr+12, "%u", &jobid);
	}
	ptr = environ[count++];
    }

    if (jobid_out) *jobid_out = jobid;
    if (stepid_out) *stepid_out = stepid;

    Step_t *step = findStepByStepId(jobid, stepid);
    if (!step) {
	if (!isAdmin) {
	    mlog("%s: step '%u:%u' not found for '%s:%i'\n", __func__,
		 jobid, stepid, func, line);
	}
    }

    return step;
}

static void initFwPtr(PStask_t *task)
{
    if (fwStep) return;

    bool isAdmin = isPSAdminUser(task->uid, task->gid);
    uint32_t jobid = 0;
    Step_t *step = findStepByEnv(task->environ, &jobid, NULL, isAdmin);

    if (step) {
	fwStep = step;
	fwJob = findJobById(jobid);
	fwAlloc = findAlloc(jobid);
    }
    fwTask = task;
}

int handleForwarderInit(void * data)
{
    PStask_t *task = data;

    if (task->rank <0 || task->group != TG_ANY) return 0;

    initFwPtr(task);
    if (fwStep) {
	initSpawnFacility(fwStep);

	if (fwStep->taskFlags & LAUNCH_PARALLEL_DEBUG) {
	    pid_t child = PSC_getPID(task->tid);
	    int status;

	    waitpid(child, &status, WUNTRACED);
	    if (!WIFSTOPPED(status)) {
		mlog("%s: child '%i' not stopped\n", __func__, child);
	    } else {
		if (killChild(child, SIGSTOP, task->uid) == -1) {
		    mwarn(errno, "%s: kill(%i)", __func__, child);
		}
		if (ptrace(PTRACE_DETACH, child, 0, 0) == -1) {
		    mwarn(errno, "%s: ptrace(PTRACE_DETACH)", __func__);
		}
	    }
	}
    } else {
	if (!isPSAdminUser(task->uid, task->gid)) {
	    flog("rank %i failed to find my step\n", task->rank);
	}
    }

    /* override spawn task filling function in pspmi */
    psPmiSetFillSpawnTaskFunction(fillSpawnTaskWithSrun);

#ifdef HAVE_SPANK
    struct spank_handle spank = {
	.task = fwTask,
	.alloc = fwAlloc,
	.job = fwJob,
	.step = fwStep,
	.hook = SPANK_TASK_POST_FORK
    };
    SpankCallHook(&spank);
#endif

    return 0;
}

int handleForwarderClientStatus(void * data)
{
    PStask_t *task = data;

    if (task->rank <0 || task->group != TG_ANY) return 0;

    initFwPtr(task);
    if (!fwStep) {
	if (!isPSAdminUser(task->uid, task->gid)) {
	    flog("rank %i failed to find my step\n", task->rank);
	}
	return 0;
    }

    startTaskEpilogue(fwStep, task);

    return 0;
}

int handleHookExecFW(void *data)
{
    PStask_t *task = data;

    initFwPtr(task);

    if (fwStep) {

	/* set threads bitmaps env */
	setJailEnv(NULL, NULL,
		&(fwStep->nodeinfos[fwStep->localNodeId].stepHWthreads),
		&(fwStep->nodeinfos[fwStep->localNodeId].jobHWthreads));
    }

    return 0;
}

int handleExecClient(void *data)
{
    PStask_t *task = data;

    if (!task) return -1;
    /* skip service tasks */
    if (task->rank <0) return 0;

    setDefaultRlimits();
    initFwPtr(task);

#ifdef HAVE_SPANK
    struct spank_handle spank = {
	.task = fwTask,
	.alloc = fwAlloc,
	.job = fwJob,
	.step = fwStep,
	.hook = SPANK_TASK_INIT_PRIVILEGED
    };
    SpankCallHook(&spank);
#endif

    return 0;
}

int handleExecClientPrep(void *data)
{
    PStask_t *task = data;

    if (!task) return -1;
    if (task->rank <0 || task->group != TG_ANY) return 0;

    /* unset MALLOC_CHECK_ set by psslurm */
    unsetenv("MALLOC_CHECK_");

    initFwPtr(task);
    if (fwStep) {
	/* set supplementary groups */
	if (fwStep->gidsLen) {
	    setgroups(fwStep->gidsLen, fwStep->gids);
	}

	if (!IO_redirectRank(fwStep, task->rank)) return -1;

	setRankEnv(task->rank, fwStep);
    } else {
	if (!isPSAdminUser(task->uid, task->gid)) {
	    flog("rank %i failed to find my step\n", task->rank);
	}
    }

#ifdef HAVE_SPANK
    struct spank_handle spank = {
	.task = fwTask,
	.alloc = fwAlloc,
	.job = fwJob,
	.step = fwStep,
	.hook = SPANK_TASK_INIT
    };
    SpankCallHook(&spank);
#endif

    return 0;
}

int handleExecClientUser(void *data)
{
    PStask_t *task = data;

    if (!task) return -1;
    if (task->rank <0 || task->group != TG_ANY) return 0;

    initFwPtr(task);
    if (fwStep) {
	/* stop child after exec */
	if (fwStep->taskFlags & LAUNCH_PARALLEL_DEBUG) {
	    if (ptrace(PTRACE_TRACEME, 0, 0, 0) == -1) {
		mwarn(errno, "%s: ptrace()", __func__);
		return -1;
	    }
	}

	doMemBind(fwStep, task);
	verboseMemPinningOutput(fwStep, task);
    } else {
	if (!isPSAdminUser(task->uid, task->gid)) {
	    flog("rank %i failed to find my step\n", task->rank);
	}
    }

    /* clean up environment */
    for (int i = 0; PSP_rlimitEnv[i].envName; i++) {
	unsetenv(PSP_rlimitEnv[i].envName);
    }

    unsetenv("__PSI_UMASK");
    unsetenv("__PSI_RAW_IO");
    unsetenv("PSI_SSH_INTERACTIVE");
    unsetenv("PSI_LOGGER_RAW_MODE");
    unsetenv("__PSI_LOGGER_UNBUFFERED");
    unsetenv("__MPIEXEC_DIST_START");
    unsetenv("MPIEXEC_VERBOSE");

    if (fwStep) startTaskPrologue(fwStep, task);

    return 0;
}

static void initX11Forward(Step_t *step)
{
    char srunIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(step->srun.sin_addr.s_addr), srunIP, INET_ADDRSTRLEN);

    int port = atoi(envGet(&step->spankenv, "X11_PORT"));
    port -= 6000;

    char *screen = envGet(&step->spankenv, "X11_SCREEN");
    char authStr[128], xauthCmd[256];

    snprintf(authStr, sizeof(authStr), "%s:%i.%s", srunIP, port, screen);
    envSet(&step->env, "DISPLAY", authStr);

    /* xauth needs the correct HOME */
    setenv("HOME", envGet(&step->env, "HOME"), 1);

    snprintf(xauthCmd, sizeof(xauthCmd), "%s -q -", X11_AUTH_CMD);
    FILE *fp = popen(xauthCmd, "w");
    if (fp != NULL) {
	char *proto = envGet(&step->spankenv, "X11_PROTO");
	char *cookie = envGet(&step->spankenv, "X11_COOKIE");
	fprintf(fp, "remove %s\n", authStr);
	fprintf(fp, "add %s %s %s\n", authStr, proto, cookie);
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
	/* setup PTY */
	tty_name = ttyname(fwdata->stdOut[0]);
	close(fwdata->stdOut[1]);

	if (!pty_setowner(step->uid, step->gid, tty_name)) {
	    flog("setting pty owner failed\n");
	    exit(1);
	}
	if (!pty_make_controlling_tty(&fwdata->stdOut[0], tty_name)) {
	    flog("initialize controlling tty failed\n");
	    exit(1);
	}

	cols = envGet(&step->env, "SLURM_PTY_WIN_COL");
	rows = envGet(&step->env, "SLURM_PTY_WIN_ROW");

	if (cols && rows) {
	    ws.ws_col = atoi(cols);
	    ws.ws_row = atoi(rows);
	    if (ioctl(fwdata->stdOut[0], TIOCSWINSZ, &ws)) {
		mwarn(errno, "%s: ioctl(TIOCSWINSZ, %s:%s)",
		      __func__, cols, rows);
		exit(1);
	    }
	}

	/* redirect stdout/stderr/stdin to PTY */
	if (dup2(fwdata->stdOut[0], STDOUT_FILENO) == -1) {
	    mwarn(errno, "%s: dup2(%u/stdout)", __func__, fwdata->stdOut[0]);
	    exit(1);
	}
	if (dup2(fwdata->stdOut[0], STDERR_FILENO) == -1) {
	    mwarn(errno, "%s: dup2(%u/stderr)", __func__, fwdata->stdOut[0]);
	    exit(1);
	}
	if (dup2(fwdata->stdOut[0], STDIN_FILENO) == -1) {
	    mwarn(errno, "%s: dup2(%u/stdin)", __func__, fwdata->stdOut[0]);
	    exit(1);
	}
    } else {
	if (!(step->taskFlags & LAUNCH_USER_MANAGED_IO)) {
	    if (dup2(fwdata->stdOut[1], STDOUT_FILENO) == -1) {
		mwarn(errno, "%s: dup2(%u/stdout)",
		      __func__, fwdata->stdOut[0]);
		exit(1);
	    }
	    if (dup2(fwdata->stdErr[1], STDERR_FILENO) == -1) {
		mwarn(errno, "%s: dup2(%u/stderr)",
		      __func__, fwdata->stdErr[0]);
		exit(1);
	    }
	    if (step->stdInRank == -1 && step->stdIn &&
	       strlen(step->stdIn) > 0) {
		/* input is redirected from file and not connected to psidfw! */

		int fd = open("/dev/null", O_RDONLY);
		if (fd == -1) {
		    mwarn(errno, "%s: open(/dev/null)", __func__);
		    exit(1);
		}
		if (dup2(fd, STDIN_FILENO) == -1) {
		    mwarn(errno, "%s: dup2(%i=/dev/null)", __func__, fd);
		    exit(1);
		}
	    } else {
		if (dup2(fwdata->stdIn[0], STDIN_FILENO) == -1) {
		    mwarn(errno, "%s: dup2(%u/stdin)",
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

/* choose PMI layer, default is MPICH's Simple PMI */
static pmi_type_t getPMIType(Step_t *step)
{
    /* for interactive steps, ignore pmi type and use none */
    if (step->stepid == SLURM_INTERACTIVE_STEP) {
	flog("interactive step detected, using PMI type 'none'\n");
	return PMI_TYPE_NONE;
    }

    /* PSSLURM_PMI_TYPE can be used to choose PMI environment to be set up */
    char *pmi = envGet(&step->env, "PSSLURM_PMI_TYPE");
    if (!pmi) {
	/* if PSSLURM_PMI_TYPE is not set and srun is called with --mpi=none,
	 *  do not setup any pmi environment */
	pmi = envGet(&step->env, "SLURM_MPI_TYPE");
	if (pmi && !strcmp(pmi, "none")) {
	    flog("%s SLURM_MPI_TYPE set to 'none'\n", strStepID(step));
	    return PMI_TYPE_NONE;
	}
	return PMI_TYPE_DEFAULT;
    }

    flog("%s PSSLURM_PMI_TYPE set to '%s'\n", strStepID(step), pmi);
    if (!strcmp(pmi, "none")) return PMI_TYPE_NONE;
    if (!strcmp(pmi, "pmix")) return PMI_TYPE_PMIX;

    /* if PSSLURM_PMI_TYPE is set to anything else, use default */
    return PMI_TYPE_DEFAULT;
}

static void debugMpiexecStart(char **argv, char **env)
{
    mlog("%s:", __func__);
    for (int i = 0; argv[i]; i++) mlog(" %s", argv[i]);
    mlog("\n");

    for (int i = 0; env[i]; i++) {
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
				pmi_type_t pmi_type)
{
    Step_t *step = fwdata->userData;
    char buf[128];

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

    /* choose PMI layer, default is MPICH's Simple PMI */
    switch (pmi_type) {
	case PMI_TYPE_NONE:
	    strvAdd(argV, ustrdup("--pmidisable"));
	    break;
	case PMI_TYPE_PMIX:
	    strvAdd(argV, ustrdup("--pmix"));
	    break;
	case PMI_TYPE_DEFAULT:
	default:
	    break;
    }

    if (step->taskFlags & LAUNCH_MULTI_PROG) {
	setupArgsFromMultiProg(step, fwdata, argV);
    } else {
	if (step->packJobid == NO_VAL) {
	    /* number of processes */
	    strvAdd(argV, ustrdup("-np"));
	    snprintf(buf, sizeof(buf), "%u", step->np);
	    strvAdd(argV, ustrdup(buf));

	    /* threads per processes */
	    /* NOTE: Actually it is unnecessary to pass tpp since it is
	     * ignored by psslurm reservation generation replacement code */
	    strvAdd(argV, ustrdup("-tpp"));
	    snprintf(buf, sizeof(buf), "%u", step->tpp);
	    strvAdd(argV, ustrdup(buf));

	    /* executable and arguments */
	    for (uint32_t i = 0; i < step->argc; i++) {
		strvAdd(argV, step->argv[i]);
	    }
	} else {
	    /* executables from job pack */
	    list_t *c;
	    bool first = true;
	    list_for_each(c, &step->jobCompInfos) {
		JobCompInfo_t *cur = list_entry(c, JobCompInfo_t, next);

		if (!first) strvAdd(argV, ":");
		first = false;

		/* number of processes */
		strvAdd(argV, ustrdup("-np"));
		snprintf(buf, sizeof(buf), "%u", cur->np);
		strvAdd(argV, ustrdup(buf));

		/* threads per processes */
		/* NOTE: Actually it is unnecessary to pass tpp since it is
		 * ignored by psslurm reservation generation replacement code */
		uint16_t tpp = cur->tpp;
		strvAdd(argV, ustrdup("-tpp"));
		snprintf(buf, sizeof(buf), "%u", tpp);
		strvAdd(argV, ustrdup(buf));

		/* executable and arguments */
		for (uint32_t j = 0; j < cur->argc; j++) {
		    strvAdd(argV, cur->argv[j]);
		}
	    }
	}
    }
}

static void fwExecStep(Forwarder_Data_t *fwdata, int rerun)
{
    Step_t *step = fwdata->userData;
    strv_t argV;
    char buf[128];
    pmi_type_t pmi_type;
    int32_t oldMask = psslurmlogger->mask;

    /* reopen syslog */
    openlog("psid", LOG_PID|LOG_CONS, LOG_DAEMON);
    snprintf(buf, sizeof(buf), "psslurm-%s", strStepID(step));
    initLogger(buf, NULL);
    maskLogger(oldMask);

    /* setup standard I/O and PTY */
    setupStepIO(fwdata, step);

    /* set default RLimits */
    setDefaultRlimits();

    /* switch user */
    char *cwd = step->cwd;
    if (getConfValueI(&Config, "CWD_PATTERN") == 1) {
	cwd = IO_replaceStepSymbols(step, 0, step->cwd);
    }
    if (!switchUser(step->username, step->uid, step->gid, cwd)) {
	flog("switching user failed\n");
	exit(1);
    }

    /* decide which PMI type to use */
    pmi_type = getPMIType(step);

    /* build mpiexec argument vector */
    buildMpiexecArgs(fwdata, &argV, pmi_type);

    /* setup step specific environment */
    setStepEnv(step);

    /* setup X11 forwarding */
    if (step->x11forward) initX11Forward(step);

    flog("exec %s mypid %u\n", strStepID(step), getpid());

    /* set RLimits */
    setRlimitsFromEnv(&step->env, 1);

    /* remove environment variables not evaluated by mpiexec */
    removeUserVars(&step->env, pmi_type);

    if (psslurmlogger->mask & PSSLURM_LOG_PROCESS) {
	debugMpiexecStart(argV.strings, step->env.vars);
    }

    /* start mpiexec to spawn the parallel job */
    closelog();
    execve(argV.strings[0], argV.strings, step->env.vars);
    int err = errno;

    /* execve() failed */
    fprintf(stderr, "%s: execve %s: %s\n", __func__, argV.strings[0],
	    strerror(err));
    openlog("psid", LOG_PID|LOG_CONS, LOG_DAEMON);
    snprintf(buf, sizeof(buf), "psslurm-%s", strStepID(step));
    initLogger(buf, NULL);
    mwarn(err, "%s: execve(%s)", __func__, argV.strings[0]);
    exit(err);
}

static int switchEffectiveUser(char *username, uid_t uid, gid_t gid)
{
    if (uid) {
	/* current user is root, change groups before switching to user */
	/* remove group memberships */
	if (setgroups(0, NULL) == -1) {
	    mwarn(errno, "%s: setgroups(0)", __func__);
	    return -1;
	}

	/* set supplementary groups */
	if (initgroups(username, gid) < 0) {
	    mwarn(errno, "%s: initgroups()", __func__);
	    return -1;
	}
    }

    /* change effective GID */
    if (setegid(gid) < 0) {
	mwarn(errno, "%s: setgid(%i)", __func__, gid);
	return -1;
    }

    /* change effective UID */
    if (seteuid(uid) < 0) {
	mwarn(errno, "%s: setuid(%i)", __func__, uid);
	return -1;
    }

    if (!uid) {
	/* current user was not root, change groups after switching UID */
	/* remove group memberships */
	if (setgroups(0, NULL) == -1) {
	    mwarn(errno, "%s: setgroups(0)", __func__);
	    return -1;
	}

	/* set supplementary groups */
	if (initgroups(username, gid) < 0) {
	    mwarn(errno, "%s: initgroups()", __func__);
	    return -1;
	}
    }

    if (prctl(PR_SET_DUMPABLE, 1) == -1) {
	mwarn(errno, "%s: prctl(PR_SET_DUMPABLE)", __func__);
    }

    return 1;
}

static int stepForwarderInit(Forwarder_Data_t *fwdata)
{
    Step_t *step = fwdata->userData;
    step->fwdata = fwdata;

    initSerial(0, sendMsg);
    setJailEnv(&step->env, step->username,
	    &(step->nodeinfos[step->localNodeId].stepHWthreads),
	    &(step->nodeinfos[step->localNodeId].jobHWthreads));

#ifdef HAVE_SPANK
    struct spank_handle spank = {
	.task = NULL,
	.alloc = findAlloc(step->jobid),
	.job = findJobById(step->jobid),
	.step = step,
	.hook = SPANK_INIT
    };
    SpankCallHook(&spank);

    SpankInitOpt(&spank);

    spank.hook = SPANK_INIT_POST_OPT;
    SpankCallHook(&spank);
#endif

    if (switchEffectiveUser(step->username, step->uid, step->gid) == -1) {
	mlog("%s: switching effective user failed\n", __func__);
	exit(1);
    }

    /* check if we can change working directory */
    if (step->cwd && chdir(step->cwd) == -1) {
	mwarn(errno, "%s: chdir(%s) for uid %u gid %u", __func__,
	      step->cwd, step->uid, step->gid);
	return - ESCRIPT_CHDIR_FAILED;
    }

    /* redirect stdout/stderr/stdin */
    if (!(step->taskFlags & LAUNCH_PTY)
	&& !(step->taskFlags & LAUNCH_USER_MANAGED_IO)) {
	IO_redirectStep(fwdata, step);
	IO_openStepPipes(fwdata, step);
    }

#ifdef HAVE_SPANK
    spank.hook = SPANK_USER_INIT;
    SpankCallHook(&spank);
#endif

    if (switchEffectiveUser("root", 0, 0) == -1) {
	mlog("%s: switching effective user failed\n", __func__);
	exit(1);
    }

    /* open stderr/stdout/stdin fds */
    if (step->taskFlags & LAUNCH_PTY) {
	/* open PTY */
	if (openpty(&fwdata->stdOut[1], &fwdata->stdOut[0],
		    NULL, NULL, NULL) == -1) {
	    mwarn(errno, "%s: openpty()", __func__);
	    return -1;
	}
    }

    return 1;
}

static void stepForwarderLoop(Forwarder_Data_t *fwdata)
{
    Step_t *step = fwdata->userData;

    IO_init();

    /* user will take care of I/O handling */
    if (step->taskFlags & LAUNCH_USER_MANAGED_IO) return;

    if (!step->IOPort) {
	mlog("%s: no I/O ports available\n", __func__);
	return;
    }

    if (srunOpenIOConnection(step, step->cred->sig) == -1) {
	mlog("%s: open srun I/O connection failed\n", __func__);
	return;
    }

    if (step->taskFlags & LAUNCH_PTY) {
	/* open additional PTY connection to srun */
	if (srunOpenPTYConnection(step) < 0) {
	    flog("open srun pty connection failed\n");
	    return;
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

static void stepFinalize(Forwarder_Data_t *fwdata)
{
    IO_finalize(fwdata);

#ifdef HAVE_SPANK
    Step_t *step = fwdata->userData;
    struct spank_handle spank = {
	.task = NULL,
	.alloc = findAlloc(step->jobid),
	.job = findJobById(step->jobid),
	.step = step,
	.hook = SPANK_EXIT
    };

    /* set environment variables from user */
    for (uint32_t i = 0; i < step->env.cnt; i++) {
	putenv(step->env.vars[i]);
    }
    SpankCallHook(&spank);
#endif
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

    /* return launch success to waiting srun since mpiexec could be spawned */
    flog("launch success for %s to srun sock '%u'\n",
	 strStepID(step), step->srunControlMsg.sock);
    sendSlurmRC(&step->srunControlMsg, SLURM_SUCCESS);
    step->state = JOB_SPAWNED;
}

bool execStepLeader(Step_t *step)
{
    int grace = getConfValueI(&SlurmConfig, "KillWait");
    if (grace < 3) grace = 30;

    char jobid[100], fname[300];
    snprintf(jobid, sizeof(jobid), "%u.%u", step->jobid, step->stepid);
    snprintf(fname, sizeof(fname), "psslurm-step:%s", jobid);

    Forwarder_Data_t *fwdata = ForwarderData_new();
    fwdata->pTitle = ustrdup(fname);
    fwdata->jobID = ustrdup(jobid);
    fwdata->userData = step;
    fwdata->graceTime = grace;
    fwdata->accounted = true;
    fwdata->killSession = psAccountSignalSession;
    fwdata->callback = stepCallback;
    fwdata->childFunc = fwExecStep;
    fwdata->hookLoop = stepForwarderLoop;
    fwdata->hookFWInit = stepForwarderInit;
    fwdata->handleMthrMsg = fwCMD_handleMthrStepMsg;
    fwdata->handleFwMsg = fwCMD_handleFwStepMsg;
    fwdata->hookChild = handleChildStartStep;
    fwdata->hookFinalize = stepFinalize;
    fwdata->jailChild = true;

    if (!startForwarder(fwdata)) {
	char msg[128];

	snprintf(msg, sizeof(msg), "starting forwarder for %s failed\n",
		 strStepID(step));
	flog("%s", msg);
	setNodeOffline(&step->env, step->jobid,
		       getConfValueC(&Config, "SLURM_HOSTNAME"), msg);
	return false;
    }

    flog("%s tid %s started\n", strStepID(step), PSC_printTID(fwdata->tid));
    step->fwdata = fwdata;
    return true;
}

void handleJobLoop(Forwarder_Data_t *fwdata)
{
    Job_t *job = fwdata->userData;

    if (switchEffectiveUser(job->username, job->uid, job->gid) == -1) {
	mlog("%s: switching effective user failed\n", __func__);
	exit(1);
    }

    IO_openJobIOfiles(fwdata);

    if (switchEffectiveUser("root", 0, 0) == -1) {
	mlog("%s: switching effective user failed\n", __func__);
	exit(1);
    }
}

static int jobForwarderInit(Forwarder_Data_t *fwdata)
{
    Job_t *job = fwdata->userData;

    PSIDhook_call(PSIDHOOK_PSSLURM_JOB_FWINIT, job->username);

    setJailEnv(&job->env, job->username, NULL, &(job->hwthreads));

    return IO_openJobPipes(fwdata);
}

static void jobForwarderFin(Forwarder_Data_t *fwdata)
{
    Job_t *job = fwdata->userData;

    PSIDhook_call(PSIDHOOK_PSSLURM_JOB_FWFIN, job->username);
}

bool execBatchJob(Job_t *job)
{
    int grace = getConfValueI(&SlurmConfig, "KillWait");
    if (grace < 3) grace = 30;

    char fname[300];
    snprintf(fname, sizeof(fname), "psslurm-job:%u", job->jobid);

    Forwarder_Data_t *fwdata = ForwarderData_new();
    fwdata->pTitle = ustrdup(fname);
    fwdata->jobID = ustrdup(strJobID(job->jobid));
    fwdata->userData = job;
    fwdata->graceTime = grace;
    fwdata->accounted = true;
    fwdata->killSession = psAccountSignalSession;
    fwdata->callback = jobCallback;
    fwdata->childFunc = fwExecBatchJob;
    fwdata->hookChild = handleChildStartJob;
    fwdata->hookFWInit = jobForwarderInit;
    fwdata->hookLoop = handleJobLoop;
    fwdata->handleMthrMsg = fwCMD_handleMthrJobMsg;
    fwdata->hookFinalize = jobForwarderFin;
    fwdata->jailChild = true;

    if (!startForwarder(fwdata)) {
	char msg[128];

	snprintf(msg, sizeof(msg), "starting forwarder for job '%u' failed\n",
		 job->jobid);
	flog("%s", msg);
	setNodeOffline(&job->env, job->jobid,
		       getConfValueC(&Config, "SLURM_HOSTNAME"), msg);
	return false;
    }

    job->state = JOB_RUNNING;
    flog("job %u tid %s started\n", job->jobid, PSC_printTID(fwdata->tid));
    job->fwdata = fwdata;
    return true;
}

static void fwExecBCast(Forwarder_Data_t *fwdata, int rerun)
{
    BCast_t *bcast = fwdata->userData;
    struct utimbuf times;

    if (!switchUser(bcast->username, bcast->uid, bcast->gid, NULL)) {
	flog("switching user failed\n");
	exit(1);
    }

    /* open the file */
    int flags = O_WRONLY;
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

    int fd = open(bcast->fileName, flags, 0700);
    if (fd == -1) {
	int eno = errno;
	mwarn(eno, "%s: open(%s)", __func__, bcast->fileName);
	exit(eno);
    }

    /* write the file */
    if (PSCio_sendF(fd, bcast->block, bcast->blockLen) == -1) {
	int eno = errno;
	mwarn(eno, "%s: write(%s)", __func__, bcast->fileName);
	exit(eno);
    }

    /* set permissions */
    if (bcast->lastBlock) {
	if (fchmod(fd, bcast->modes & 0700) == -1) {
	    int eno = errno;
	    mwarn(eno, "%s: chmod(%s)", __func__, bcast->fileName);
	    exit(eno);
	}
	if (fchown(fd, bcast->uid, bcast->gid) == -1) {
	    int eno = errno;
	    mwarn(eno, "%s: chown(%s)", __func__, bcast->fileName);
	    exit(eno);
	}
	if (bcast->atime) {
	    times.actime  = bcast->atime;
	    times.modtime = bcast->mtime;
	    if (utime(bcast->fileName, &times)) {
		int eno = errno;
		mwarn(eno, "%s: utime(%s)", __func__, bcast->fileName);
		exit(eno);
	    }
	}
    }
    close(fd);

    exit(0);
}

static int initBCastFW(Forwarder_Data_t *fwdata)
{
    BCast_t *bcast = fwdata->userData;

    setJailEnv(bcast->env, bcast->username, NULL, &(bcast->hwthreads));

    return 0;
}

bool execBCast(BCast_t *bcast)
{
    int grace = getConfValueI(&SlurmConfig, "KillWait");
    if (grace < 3) grace = 30;

    char jobid[100], fname[300];
    snprintf(jobid, sizeof(jobid), "%u", bcast->jobid);
    snprintf(fname, sizeof(fname), "psslurm-bcast:%s", jobid);

    Forwarder_Data_t *fwdata = ForwarderData_new();
    fwdata->pTitle = ustrdup(fname);
    fwdata->jobID = ustrdup(jobid);
    fwdata->userData = bcast;
    fwdata->graceTime = grace;
    fwdata->killSession = psAccountSignalSession;
    fwdata->callback = bcastCallback;
    fwdata->childFunc = fwExecBCast;
    fwdata->hookFWInit = initBCastFW;
    fwdata->jailChild = true;

    if (!startForwarder(fwdata)) {
	char msg[128];

	snprintf(msg, sizeof(msg), "starting forwarder for bcast '%u' failed\n",
		 bcast->jobid);
	flog("%s", msg);
	setNodeOffline(bcast->env, bcast->jobid,
		       getConfValueC(&Config, "SLURM_HOSTNAME"), msg);
	return false;
    }

    bcast->fwdata = fwdata;
    return true;
}

static void stepFollowerFWloop(Forwarder_Data_t *fwdata)
{
    Step_t *step = fwdata->userData;
    step->fwdata = fwdata;

    IO_init();

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

    if (switchEffectiveUser(step->username, step->uid, step->gid) == -1) {
	mlog("%s: switching effective user failed\n", __func__);
	exit(1);
    }

    IO_redirectStep(fwdata, step);

    if (switchEffectiveUser("root", 0, 0) == -1) {
	mlog("%s: switching effective user failed\n", __func__);
	exit(1);
    }
}

static int stepFollowerFWinit(Forwarder_Data_t *fwdata)
{
    initSerial(0, sendMsg);

    Step_t *step = fwdata->userData;
    setJailEnv(&step->env, step->username,
	    &(step->nodeinfos[step->localNodeId].stepHWthreads),
	    &(step->nodeinfos[step->localNodeId].jobHWthreads));

#ifdef HAVE_SPANK

    struct spank_handle spank = {
	.task = NULL,
	.alloc = findAlloc(step->jobid),
	.job = findJobById(step->jobid),
	.step = step,
	.hook = SPANK_INIT
    };
    SpankCallHook(&spank);

    SpankInitOpt(&spank);

    spank.hook = SPANK_INIT_POST_OPT;
    SpankCallHook(&spank);

    if (switchEffectiveUser(step->username, step->uid, step->gid) == -1) {
	mlog("%s: switching effective user failed\n", __func__);
	exit(1);
    }

    spank.hook = SPANK_USER_INIT;
    SpankCallHook(&spank);

    if (switchEffectiveUser("root", 0, 0) == -1) {
	mlog("%s: switching effective user failed\n", __func__);
	exit(1);
    }

#endif

    return 1;
}

bool execStepFollower(Step_t *step)
{
    int grace = getConfValueI(&SlurmConfig, "KillWait");
    if (grace < 3) grace = 30;

    char jobid[100], fname[300];
    snprintf(jobid, sizeof(jobid), "%u.%u", step->jobid, step->stepid);
    snprintf(fname, sizeof(fname), "psslurm-step:%s", jobid);

    Forwarder_Data_t *fwdata = ForwarderData_new();
    fwdata->pTitle = ustrdup(fname);
    fwdata->jobID = ustrdup(jobid);
    fwdata->userData = step;
    fwdata->graceTime = grace;
    fwdata->killSession = psAccountSignalSession;
    fwdata->hookLoop = stepFollowerFWloop;
    fwdata->hookFWInit = stepFollowerFWinit;
    fwdata->handleMthrMsg = fwCMD_handleMthrStepMsg;
    fwdata->handleFwMsg = fwCMD_handleFwStepMsg;
    fwdata->callback = stepFollowerCB;
    fwdata->hookFinalize = stepFinalize;
    fwdata->jailChild = true;

    if (!startForwarder(fwdata)) {
	char msg[128];

	snprintf(msg, sizeof(msg), "starting step forwarder for %s failed\n",
		 strStepID(step));
	flog("%s", msg);
	setNodeOffline(&step->env, step->jobid,
		       getConfValueC(&Config, "SLURM_HOSTNAME"), msg);
	return false;
    }

    flog("%s tid %s started\n", strStepID(step), PSC_printTID(fwdata->tid));
    step->fwdata = fwdata;

    return true;
}

int handleFwRes(void * data)
{
#ifdef HAVE_SPANK
    if (fwStep) {
	fwStep->exitCode = *(int *) data;

	struct spank_handle spank = {
	    .task = fwTask,
	    .alloc = fwAlloc,
	    .job = fwJob,
	    .step = fwStep,
	    .hook = SPANK_TASK_EXIT
	};
	SpankCallHook(&spank);
    }
#endif
    return 0;
}

static void fwExecEpiFin(Forwarder_Data_t *fwdata, int rerun)
{
    Alloc_t *alloc = fwdata->userData;
    char *argv[2];
    char buf[1024], script[1024];
    char *dirScripts = getConfValueC(&Config, "DIR_SCRIPTS");

    snprintf(script, sizeof(script), "%s/epilogue.finalize", dirScripts);

    argv[0] = script;
    argv[1] = NULL;
    execve(argv[0], argv, alloc->env.vars);
    int err = errno;

    /* execve() failed */
    fprintf(stderr, "%s: execve(%s): %s\n", __func__, buf, strerror(err));
    openlog("psid", LOG_PID|LOG_CONS, LOG_DAEMON);
    snprintf(buf, sizeof(buf), "psslurm-epifin:%u", alloc->id);
    initLogger(buf, NULL);
    mwarn(err, "%s: execve(%s)", __func__, script);
    exit(err);
}

static int epiFinCallback(int32_t exit_status, Forwarder_Data_t *fwdata)
{
    Alloc_t *alloc = fwdata->userData;

    fdbg(PSSLURM_LOG_PELOG, "exit_status: %i fw-chldExitStatus %i\n",
	 exit_status, fwdata->chldExitStatus);

    if (alloc->terminate) {
	sendEpilogueComplete(alloc->id, 0);
	deleteAlloc(alloc->id);
    }

    return 0;
}

bool execEpilogueFin(Alloc_t *alloc)
{
    int grace = getConfValueI(&SlurmConfig, "KillWait");
    if (grace < 3) grace = 30;

    char jobid[100], fname[300];
    snprintf(jobid, sizeof(jobid), "%u", alloc->id);
    snprintf(fname, sizeof(fname), "psslurm-epifin:%s", jobid);

    Forwarder_Data_t *fwdata = ForwarderData_new();
    fwdata->pTitle = ustrdup(fname);
    fwdata->jobID = ustrdup(jobid);
    fwdata->userData = alloc;
    fwdata->graceTime = grace;
    fwdata->killSession = psAccountSignalSession;
    fwdata->callback = epiFinCallback;
    fwdata->childFunc = fwExecEpiFin;

    if (!startForwarder(fwdata)) {
	flog("starting epilogue finalize forwarder for alloc '%u' failed\n",
	     alloc->id);
	return false;
    }

    return true;
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet:*/
