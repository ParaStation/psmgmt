/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#define _GNU_SOURCE
#include "psslurmforwarder.h"

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pty.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utime.h>
#include <fenv.h>

#include "list.h"
#include "pscommon.h"
#include "pscio.h"
#include "psdaemonprotocol.h"
#include "psenv.h"
#include "pslog.h"
#include "psprotocolenv.h"
#include "psreservation.h"
#include "psserial.h"
#include "selector.h"

#include "pluginconfig.h"
#include "pluginhelper.h"
#include "pluginmalloc.h"
#include "pluginpartition.h"
#include "pluginpty.h"
#include "psidcomm.h"
#include "psidhook.h"
#include "psidscripts.h"
#include "psidtask.h"

#include "pamservice_handles.h"
#include "peloguehandles.h"
#include "psaccounthandles.h"
#include "pspmihandles.h"
#include "pspmixhandles.h"

#include "psslurm.h"
#include "psslurmcomm.h"
#include "psslurmconfig.h"
#include "psslurmcontainer.h"
#include "psslurmfwcomm.h"
#include "psslurmio.h"
#include "psslurmjobcred.h"
#include "psslurmlimits.h"
#include "psslurmlog.h"
#include "psslurmmsg.h"
#include "psslurmmultiprog.h"
#include "psslurmpelogue.h"
#include "psslurmpin.h"
#include "psslurmproto.h"
#include "psslurmpscomm.h"
#include "psslurmspawn.h"
#include "psslurmtasks.h"
#include "psslurmgres.h"
#include "slurmcommon.h"
#include "slurmerrno.h"
#ifdef HAVE_SPANK
#include "psslurmspank.h"
#endif

#define X11_AUTH_CMD "/usr/bin/xauth"

#define MPIEXEC_BINARY BINDIR "/mpiexec"

/** Allocation structure of the forwarder */
static Alloc_t *fwAlloc;

/** Job structure of the forwarder */
static Job_t *fwJob;

/** Step structure of the forwarder */
static Step_t *fwStep;

/** Task structure of the forwarder */
static PStask_t *fwTask;

static int termJobJail(void *info)
{
    Job_t *job = info;

    setJailEnv(job->env, job->username, NULL, job->hwthreads,
	       &job->gresList, GRES_CRED_JOB, job->cred, job->localNodeId);
    return PSIDhook_call(PSIDHOOK_JAIL_TERM, &job->fwdata->cPid);
}

static void cbTermJail(int exit, bool tmdOut, int iofd, void *info)
{
    char errMsg[1024];
    size_t errLen;

    bool ret = getScriptCBdata(iofd, errMsg, sizeof(errMsg),&errLen);
    if (!ret) {
	flog("getting jail term script callback data failed\n");
	return;
    }

    if (exit != PSIDHOOK_NOFUNC && exit != 0) {
	flog("jail script failed with exit status %i\n", exit);
	flog("%s\n", errMsg);
    }
}

/**
 * @brief Consolidate pluginforwarder exit status of
 * various hooks
 *
 * @param fw Structure of the pluginforwarder holding exit codes
 *
 * @return Returns the first non zero exit status of a hook or
 * 0 on success
 */
static int32_t getFWExitCode(Forwarder_Data_t *fw)
{
    if (!fw->codeRcvd) return 0;

    if (fw->rcHookFWInit) {
	return fw->rcHookFWInit;
    } else if (fw->rcHookJailChild) {
	return fw->rcHookJailChild;
    } else if (fw->rcCmdSwitchUser) {
	return fw->rcCmdSwitchUser;
    } else if (fw->rcHookFWInitUser) {
	return fw->rcHookFWInitUser;
    } else if (fw->rcHookFWLoop) {
	return fw->rcHookFWLoop;
    } else if (fw->rcHookFWFinalize) {
	return fw->rcHookFWFinalize;
    }

    return 0;
}

static void jobCallback(int32_t exit_status, Forwarder_Data_t *fw)
{
    Job_t *job = fw->userData;
    Alloc_t *alloc = Alloc_find(job->jobid);


    flog("job '%u' finished, exit %i / %i\n", job->jobid, exit_status,
	 fw->chldExitStatus);
    if (!Job_findById(job->jobid)) {
	flog("job '%u' not found\n", job->jobid);
	return;
    }

    /* terminate cgroup */
    PSID_execFunc(termJobJail, NULL, cbTermJail, NULL, job);

    /* make sure all processes are gone */
    Step_signalByJobid(job->jobid, SIGKILL, 0);
    signalTasks(job->jobid, NO_VAL, job->uid, &job->tasks, SIGKILL, -1);
    Job_killForwarder(job->jobid);

    job->state = JOB_COMPLETE;
    fdbg(PSSLURM_LOG_JOB, "job %u in %s\n", job->jobid,Job_strState(job->state));

    /* get exit status of child */
    int32_t fwExitCode = getFWExitCode(fw);
    int eStatus = fwExitCode ? fwExitCode : fw->chldExitStatus;

    /* job aborted due to node failure */
    if (alloc && alloc->nodeFail) eStatus = 9;

    flog("job %u finished, exit %i\n", job->jobid, eStatus);

    sendJobExit(job, eStatus);

    psAccountDelJob(PSC_getTID(-1, fw->cPid));

    if (!alloc) {
	flog("allocation for job %u not found\n", job->jobid);
	Job_delete(job);
	return;
    }

    if (pluginShutdown) {
	/* shutdown in progress, hence we skip the epilogue */
	uint32_t allocID = alloc->id;
	Alloc_delete(alloc);
	sendEpilogueComplete(allocID, SLURM_SUCCESS);
    } else if (alloc->terminate) {
	/* run epilogue now */
	flog("starting epilogue for allocation %u\n", alloc->id);
	fdbg(PSSLURM_LOG_JOB, "job %u in %s\n", job->jobid,
	     Job_strState(job->state));
	startPElogue(alloc, PELOGUE_EPILOGUE);
    }

    job->fwdata = NULL;
}

static int termStepJail(void *info)
{
    Step_t *step = info;

    setJailEnv(step->env, step->username,
	       step->nodeinfos[step->localNodeId].stepHWthreads,
	       step->nodeinfos[step->localNodeId].jobHWthreads,
	       &step->gresList, GRES_CRED_STEP, step->cred, step->credID);
    return PSIDhook_call(PSIDHOOK_JAIL_TERM, &step->fwdata->cPid);
}

static void stepFollowerCB(int32_t exit_status, Forwarder_Data_t *fw)
{
    Step_t *step = fw->userData;

    /* validate step pointer */
    if (!Step_verifyPtr(step)) {
	flog("Invalid step pointer %p\n", step);
	return;
    }

    /* terminate cgroup */
    PSID_execFunc(termStepJail, NULL, cbTermJail, NULL, step);

    /* send launch error if local processes failed to start */
    unsigned int taskCount = countRegTasks(&step->tasks);
    if (taskCount != step->globalTaskIdsLen[step->localNodeId]) {
	uint32_t rc = step->termAfterFWmsg != NO_VAL ?
		      step->termAfterFWmsg : (uint32_t) SLURM_ERROR;
	sendLaunchTasksFailed(step, step->localNodeId, rc);
    }

    /* send task exit to srun processes */
    sendTaskExit(step, NULL, NULL);

    if (step->nodes[0] == PSC_getMyID()) {;
	/* send step exit to slurmctld */
	int eStatus = step->exitCode;
	if (eStatus == -1) {
	    eStatus = WIFSIGNALED(fw->chldExitStatus) ?
			WTERMSIG(fw->chldExitStatus) : fw->chldExitStatus;
	}

	/* step aborted due to node failure */
	Alloc_t *alloc = Alloc_find(step->jobid);
	if (alloc && alloc->nodeFail) eStatus = 9;

	sendStepExit(step, eStatus);
    }

    step->fwdata = NULL;

    flog("%s tid %s finished\n", Step_strID(step), PSC_printTID(fw->tid));

    step->state = JOB_COMPLETE;
    fdbg(PSSLURM_LOG_JOB, "%s in %s\n", Step_strID(step),
	 Job_strState(step->state));

    /* test if we were waiting only for this step to finish */
    Alloc_t *alloc = Alloc_find(step->jobid);
    if (!Job_findById(step->jobid) && alloc && alloc->terminate &&
	(alloc->state == A_RUNNING || alloc->state == A_PROLOGUE_FINISH)) {
	/* run epilogue now */
	flog("starting epilogue for %s\n", Step_strID(step));
	startPElogue(alloc, PELOGUE_EPILOGUE);
    }

    if (step->stepid == SLURM_INTERACTIVE_STEP) Step_destroy(step);
}

static void stepCallback(int32_t exit_status, Forwarder_Data_t *fw)
{
    Step_t *step = fw->userData;

    /* validate step pointer */
    if (!Step_verifyPtr(step)) {
	flog("Invalid step pointer %p\n", step);
	return;
    }
    if (step->nrOfNodes > 1) stopStepFollower(step);

    Alloc_t *alloc = Alloc_find(step->jobid);
    flog("%s in %s finished, exit %i / %i\n", Step_strID(step),
	 Job_strState(step->state), exit_status, fw->chldExitStatus);

    /* terminate cgroup */
    PSID_execFunc(termStepJail, NULL, cbTermJail, NULL, step);

    /* make sure all processes are gone */
    Step_signal(step, SIGKILL, 0);
    if (step->fwdata->cPid > 0) killChild(step->fwdata->cPid, SIGKILL, step->uid);

    clearSlurmMsg(&step->srunIOMsg);

    if (step->state == JOB_PRESTART) {
	/* spawn failed */
	uint32_t rc = (uint32_t) SLURM_ERROR;

	if (fw->rcType == RC_HOOK_FW_INIT) {
	    switch (fw->rcHook) {
	    case -ESCRIPT_CHDIR_FAILED:
		rc = ESCRIPT_CHDIR_FAILED;
		break;
	    case -ESLURMD_EXECVE_FAILED:
		rc = ESLURMD_EXECVE_FAILED;
		break;
	    }
	}

	if (rc == (uint32_t) SLURM_ERROR && step->termAfterFWmsg != NO_VAL) {
	    rc = step->termAfterFWmsg;
	}

	sendSlurmRC(&step->srunControlMsg, rc);
    } else if (step->state == JOB_SPAWNED) {
	sendLaunchTasksFailed(step, ALL_NODES, ESLURMD_EXECVE_FAILED);
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
	if (step->packStepCount > 1 && step->packNrOfNodes > 1
	    && send_PS_PackExit(step, eStatus) == -1) {
	    flog("sending pack exit for %s failed\n", Step_strID(step));
	}
    }

    fdbg(PSSLURM_LOG_JOB, "%s in %s => JOB_COMPLETE\n", Step_strID(step),
	 Job_strState(step->state));
    step->state = JOB_COMPLETE;
    PStask_ID_t jobTID = fw->cPid < 1 ? fw->tid : PSC_getTID(-1, fw->cPid);
    psAccountDelJob(jobTID);

    /* test if we were waiting only for this step to finish */
    if (!Job_findById(step->jobid) && alloc && alloc->terminate &&
	(alloc->state == A_RUNNING || alloc->state == A_PROLOGUE_FINISH)) {
	/* run epilogue now */
	flog("starting epilogue for %s\n", Step_strID(step));
	startPElogue(alloc, PELOGUE_EPILOGUE);
    }

    if (!alloc || step->stepid == SLURM_INTERACTIVE_STEP) {
	Step_destroy(step);
    } else {
	step->fwdata = NULL;
	clearTasks(&step->tasks);
    }
}

static void bcastCallback(int32_t exit_status, Forwarder_Data_t *fw)
{
    BCast_t *bcast = fw->userData;

    sendSlurmRC(&bcast->msg, WEXITSTATUS(fw->chldExitStatus));

    bcast->fwdata = NULL;
    if (bcast->flags & BCAST_LAST_BLOCK) BCast_clearByJobid(bcast->jobid);
}

static int setFilePermissions(Job_t *job)
{
    if (!job->jobscript) return 1;

    if (chown(job->jobscript, job->uid, job->gid) == -1) {
	fwarn(errno, "chown(%s, %i, %i)", job->jobscript, job->uid, job->gid);
	return 1;
    }

    if (chmod(job->jobscript, 0700) == -1) {
	fwarn(errno, "chmod(%s, 0700)", job->jobscript);
	return 1;
    }

    return 0;
}

static void fwExecBatchJob(Forwarder_Data_t *fwdata, int rerun)
{
    Job_t *job = fwdata->userData;

    char buf[128];
    snprintf(buf, sizeof(buf), "psslurm-job:%u", job->jobid);
    reOpenSyslog(buf, &psslurmlogger);

    setFilePermissions(job);

    if (pamserviceOpenSession) pamserviceOpenSession(job->username);

#ifdef HAVE_SPANK
    struct spank_handle spank = {
	.task = NULL,
	.alloc = Alloc_find(job->jobid),
	.job = job,
	.step = NULL,
	.hook = SPANK_TASK_INIT_PRIVILEGED,
	.envSet = NULL,
	.envUnset = NULL
    };
    SpankCallHook(&spank);
#endif

    /* set RLimits from configuration */
    setDefaultRlimits();

    /* switch user */
    char *cwd = job->cwd;
    if (getConfValueI(Config, "CWD_PATTERN") == 1) {
	cwd = IO_replaceJobSymbols(job, job->cwd);
    }
    if (!switchUser(job->username, job->uid, job->gid)) {
	flog("switching user failed\n");
	exit(1);
    }
    if (!switchCwd(cwd)) {
	flog("switching working directory failed\n");
	exit(1);
    }

    /* redirect output */
    IO_redirectJob(fwdata, job);

#ifdef HAVE_SPANK
    spank.hook = SPANK_TASK_INIT;
    SpankCallHook(&spank);
#endif

    /* setup batch specific environment */
    setJobEnv(job);

    /* initialize container */
    if (job->ct) Container_jobInit(job);

    /* set RLimits */
    setRlimitsFromEnv(job->env, false);

    /* reset FPE exceptions mask */
    if (getConfValueI(Config, "ENABLE_FPE_EXCEPTION") &&
	oldExceptions != -1) {
	if (feenableexcept(oldExceptions) == -1) {
	    flog("warning: failed to reset exception mask\n");
	}
    }

    /* start jobscript in container, does *not* return */
    if (job->ct) Container_run(job->ct);

    /* do exec */
    closelog();
    execve(job->jobscript, strvGetArray(job->argV), envGetArray(job->env));
    int err = errno;

    /* execve() failed */
    fprintf(stderr, "%s: execve(%s): %s\n", __func__, strvGet(job->argV, 0),
	    strerror(err));

    snprintf(buf, sizeof(buf), "psslurm-job:%u", job->jobid);
    reOpenSyslog(buf, &psslurmlogger);
    fwarn(err, "execve(%s)", strvGet(job->argV, 0));
    exit(err);
}

static void initFwPtr(PStask_t *task)
{
    if (fwStep || !task) return;

    bool isAdmin = isPSAdminUser(task->uid, task->gid);
    uint32_t jobid, stepid;
    Step_t *step = Step_findByEnv(task->env, &jobid, &stepid);

    if (step) {
	fwStep = step;
	fwJob = Job_findById(jobid);
	fwAlloc = Alloc_find(jobid);
    } else if (!isAdmin) {
	Step_t s = { .jobid = jobid, .stepid = stepid };
	flog("%s not found\n", Step_strID(&s));
    }

    fwTask = task;
}

int handleForwarderInitPriv(void *data)
{
    PStask_t *task = data;

    if (task->rank < 0 || task->group != TG_ANY) return 0;
    initFwPtr(task);

#ifdef HAVE_SPANK
    struct spank_handle spank = {
	.task = fwTask,
	.alloc = fwAlloc,
	.job = fwJob,
	.step = fwStep,
	.hook = SPANK_TASK_POST_FORK,
	.envSet = NULL,
	.envUnset = NULL,
	.spankEnv = fwStep ? fwStep->spankenv : NULL
    };
    SpankInitOpt(&spank);
    SpankCallHook(&spank);
#endif

    return 0;
}

int handleForwarderInit(void *data)
{
    PStask_t *task = data;

    if (task->rank < 0 || task->group != TG_ANY) return 0;

    initFwPtr(task);
    if (fwStep) {
	initSpawnFacility(fwStep);

	if (fwStep->taskFlags & LAUNCH_PARALLEL_DEBUG) {
	    pid_t child = PSC_getPID(task->tid);
	    int status;

	    waitpid(child, &status, WUNTRACED);
	    if (!WIFSTOPPED(status)) {
		flog("child '%i' not stopped\n", child);
	    } else {
		if (killChild(child, SIGSTOP, task->uid) == -1) {
		    fwarn(errno, "kill(%i)", child);
		}
		if (ptrace(PTRACE_DETACH, child, 0, 0) == -1) {
		    fwarn(errno, "ptrace(PTRACE_DETACH)");
		}
	    }
	}
    } else if (!isPSAdminUser(task->uid, task->gid)) {
	flog("rank %d (global %d) failed to find my step\n", task->jobRank,
	     task->rank);
    }

    /* override spawn task filling function in pspmi */
    psPmiSetFillSpawnTaskFunction(fillSpawnTaskWithSrun);

    /* override spawn task filling function in pspmix */
    psPmixSetFillSpawnTaskFunction(fillSpawnTaskWithSrun);

    return 0;
}

int handleForwarderClientStatus(void * data)
{
    PStask_t *task = data;

    if (task->rank < 0 || task->group != TG_ANY) return IDLE;

    initFwPtr(task);
    if (!fwStep) {
	if (!isPSAdminUser(task->uid, task->gid)) {
	    flog("rank %d (global %d) failed to find my step\n", task->jobRank,
		 task->rank);
	}
	return IDLE;
    }

    startTaskEpilogue(fwStep, task);

    return IDLE;
}

int handleHookExecFW(void *data)
{
    PStask_t *task = data;

    initFwPtr(task);

    if (fwStep) {
	if (!fwStep->nodeinfos) {
	    flog("missing nodeinfos: unable to set jail environment\n");
	    return -1;
	}

	/* set threads bitmaps env */
	setJailEnv(NULL, NULL,
		   fwStep->nodeinfos[fwStep->localNodeId].stepHWthreads,
		   fwStep->nodeinfos[fwStep->localNodeId].jobHWthreads,
		   &fwStep->gresList, GRES_CRED_STEP, fwStep->cred,
		   fwStep->credID);
    }

    return 0;
}

int handleExecClient(void *data)
{
    PStask_t *task = data;

    if (!task) return -1;
    /* skip service tasks */
    if (task->rank < 0) return 0;

    setDefaultRlimits();
    initFwPtr(task);

#ifdef HAVE_SPANK
    struct spank_handle spank = {
	.task = fwTask,
	.alloc = fwAlloc,
	.job = fwJob,
	.step = fwStep,
	.hook = SPANK_TASK_INIT_PRIVILEGED,
	.envSet = NULL,
	.envUnset = NULL,
	.spankEnv = fwStep ? fwStep->spankenv : NULL
    };
    SpankInitOpt(&spank);
    SpankCallHook(&spank);
#endif

    return 0;
}

int handleExecClientPrep(void *data)
{
    PStask_t *task = data;

    if (!task) return -1;
    if (task->rank < 0 || task->group != TG_ANY) return 0;

    /* unset MALLOC_CHECK_ set by psslurm */
    unsetenv("MALLOC_CHECK_");

    initFwPtr(task);
    if (fwStep) {
	/* set supplementary groups */
	if (fwStep->gidsLen) {
	    setgroups(fwStep->gidsLen, fwStep->gids);
	}

	if (!IO_redirectRank(fwStep, task->jobRank)) return -1;

	setRankEnv(task->jobRank, fwStep);

	/* adjust BCast executable name */
	char *exe = strvGet(task->argV, 0);
	char *newExe = BCast_adjustExe(exe, fwStep->jobid, fwStep->stepid);
	if (!newExe) {
	    flog("failed to adjust BCast exe %s\n", exe);
	} else {
	    if (newExe != exe) {
		strvReplace(task->argV, 0, newExe);
		free(newExe);
	    }
	}
    } else {
	if (!isPSAdminUser(task->uid, task->gid)) {
	    flog("rank %d (global %d) failed to find my step\n", task->jobRank,
		 task->rank);
	}
    }

#ifdef HAVE_SPANK
    struct spank_handle spank = {
	.task = fwTask,
	.alloc = fwAlloc,
	.job = fwJob,
	.step = fwStep,
	.hook = SPANK_TASK_INIT,
	.envSet = NULL,
	.envUnset = NULL
    };
    SpankCallHook(&spank);
#endif

    return 0;
}

int handleExecClientUser(void *data)
{
    PStask_t *task = data;

    if (!task) return -1;
    if (task->rank < 0 || task->group != TG_ANY) return 0;

    initFwPtr(task);
    if (fwStep) {
	/* stop child after exec */
	if (fwStep->taskFlags & LAUNCH_PARALLEL_DEBUG) {
	    if (ptrace(PTRACE_TRACEME, 0, 0, 0) == -1) {
		fwarn(errno, "ptrace()");
		return -1;
	    }
	}

	doMemBind(fwStep, task);
	verboseMemPinningOutput(fwStep, task);
    } else {
	if (!isPSAdminUser(task->uid, task->gid)) {
	    flog("rank %d (global %d) failed to find my step\n", task->jobRank,
		 task->rank);
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
    unsetenv("__PSI_NO_MEMBIND");

    extern char **environ;
    for (int i=0; environ[i] != NULL; i++) {
	while (environ[i] && !strncmp(environ[i], "__PSJAIL_", 9)) {
	    char *val = strchr(environ[i], '=');
	    if (val) {
		char *key = strndup(environ[i], val - environ[i]);
		if (key) unsetenv(key);
		free(key);
	    } else {
		break;
	    }
	}
    }

    /* reset FPE exceptions mask */
    if (getConfValueI(Config, "ENABLE_FPE_EXCEPTION") &&
	oldExceptions != -1) {
	if (feenableexcept(oldExceptions) == -1) {
	    flog("warning: failed to reset exception mask\n");
	}
    }

    if (fwStep) startTaskPrologue(fwStep, task);

    return 0;
}

int handleExecClientExec(void *data)
{
    PStask_t *task = data;

    if (!task) return -1;
    if (task->rank < 0 || task->group != TG_ANY) return 0;

    if (fwStep && fwStep->ct) {
	Container_taskInit(fwStep->ct, task, (fwStep->taskFlags & LAUNCH_PTY));
	/* exec task in container, function will *not* return!
	 * every initialization should be finished beforehand */
	Container_run(fwStep->ct);
    }

    return 0;
}

int handleLastChildGone(void *data)
{
    PStask_t *task = data;
    if (!task) return -1;

    if (task->group == TG_KVS && task->ptid != task->loggertid
	&& task->forwarder) {
	/* kvsprovider of a PMI_spawn()ed job => send SERV_EXT to forwarder */
	PSLog_Msg_t msg = {
	    .header = {
		.type = PSP_CC_MSG,
		.sender = PSC_getTID(-1,0),
		.dest = task->forwarder->tid,
		.len = PSLog_headerSize },
	    .version = 2,
	.type = SERV_EXT,
	.sender = -1 };

	sendMsg(&msg);
	fdbg(PSSLURM_LOG_JOB, "stop KVSprovider %s\n", PSC_printTID(task->tid));
    }
    return 0;
}

int handleResReleased(void *data)
{
    PSrsrvtn_t *res = data;
    if (!res) return -1;

    PStask_t *task = PStasklist_find(&managedTasks, res->task);
    if (!task) {
	flog("task %s not found\n", PSC_printTID(res->task));
	return -1;
    }

    Forwarder_Data_t *fw = PStask_infoGet(task, TASKINFO_FORWARDER);
    if (!fw || fw->callback != stepCallback) {
	fdbg(PSSLURM_LOG_JOB, "%s not step forwarder\n", PSC_printTID(task->tid));
	return 0;
    }

    Step_t *step = fw->userData;
    if (!step || !step->spawned) {
	fdbg(PSSLURM_LOG_JOB, "step %s not spawned\n", Step_strID(step));
	return 0;
    }

    flog("shutdown step forwarder %s %s\n", PSC_printTID(task->tid),
	 Step_strID(step));
    shutdownForwarder(fw);
    return 0;
}

static void initX11Forward(Step_t *step)
{
    char srunIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &step->srun.sin_addr, srunIP, INET_ADDRSTRLEN);

    int port = atoi(envGet(step->spankenv, "X11_PORT"));
    port -= 6000;

    char *screen = envGet(step->spankenv, "X11_SCREEN");
    char authStr[128], xauthCmd[256];

    snprintf(authStr, sizeof(authStr), "%s:%i.%s", srunIP, port, screen);
    envSet(step->env, "DISPLAY", authStr);

    /* xauth needs the correct HOME */
    setenv("HOME", envGet(step->env, "HOME"), 1);

    snprintf(xauthCmd, sizeof(xauthCmd), "%s -q -", X11_AUTH_CMD);
    FILE *fp = popen(xauthCmd, "w");
    if (fp != NULL) {
	char *proto = envGet(step->spankenv, "X11_PROTO");
	char *cookie = envGet(step->spankenv, "X11_COOKIE");
	fprintf(fp, "remove %s\n", authStr);
	fprintf(fp, "add %s %s %s\n", authStr, proto, cookie);
	pclose(fp);
    } else {
	flog("open xauth '%s' failed\n", X11_AUTH_CMD);
	envUnset(step->env, "DISPLAY");
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

	cols = envGet(step->env, "SLURM_PTY_WIN_COL");
	rows = envGet(step->env, "SLURM_PTY_WIN_ROW");

	if (cols && rows) {
	    ws.ws_col = atoi(cols);
	    ws.ws_row = atoi(rows);
	    if (ioctl(fwdata->stdOut[0], TIOCSWINSZ, &ws)) {
		fwarn(errno, "ioctl(TIOCSWINSZ, %s:%s)", cols, rows);
		exit(1);
	    }
	}

	/* redirect stdout/stderr/stdin to PTY */
	if (dup2(fwdata->stdOut[0], STDOUT_FILENO) == -1) {
	    fwarn(errno, "dup2(%u/stdout)", fwdata->stdOut[0]);
	    exit(1);
	}
	if (dup2(fwdata->stdOut[0], STDERR_FILENO) == -1) {
	    fwarn(errno, "dup2(%u/stderr)", fwdata->stdOut[0]);
	    exit(1);
	}
	if (dup2(fwdata->stdOut[0], STDIN_FILENO) == -1) {
	    fwarn(errno, "dup2(%u/stdin)", fwdata->stdOut[0]);
	    exit(1);
	}
    } else {
	if (dup2(fwdata->stdOut[1], STDOUT_FILENO) == -1) {
	    fwarn(errno, "dup2(%u/stdout)", fwdata->stdOut[0]);
	    exit(1);
	}
	if (dup2(fwdata->stdErr[1], STDERR_FILENO) == -1) {
	    fwarn(errno, "dup2(%u/stderr)", fwdata->stdErr[0]);
	    exit(1);
	}
	if (step->stdInRank == -1 && step->stdIn &&
	   strlen(step->stdIn) > 0) {
	    /* input is redirected from file and not connected to psidfw! */

	    int fd = open("/dev/null", O_RDONLY);
	    if (fd == -1) {
		fwarn(errno, "open(/dev/null)");
		exit(1);
	    }
	    if (dup2(fd, STDIN_FILENO) == -1) {
		fwarn(errno, "dup2(%i=/dev/null)", fd);
		exit(1);
	    }
	} else {
	    if (dup2(fwdata->stdIn[0], STDIN_FILENO) == -1) {
		fwarn(errno, "dup2(%u/stdin)", fwdata->stdIn[0]);
		exit(1);
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

static void debugMpiexecStart(char **argv, char **env)
{
    flog(" ");
    for (int i = 0; argv[i]; i++) mlog(" %s", argv[i]);
    mlog("\n");

    for (int i = 0; env[i]; i++) {
	flog("env[%i] '%s'\n", i, env[i]);
    }
}

strv_t buildStartArgv(Forwarder_Data_t *fwData, pmi_type_t pmiType)
{
    Step_t *step = fwData->userData;
    char buf[128];

    strv_t argV = strvNew(NULL);

    if (step->spawned) {
	char *tmpStr = envGet(step->env, "__PSI_MPIEXEC_KVSPROVIDER");
	if (tmpStr) {
	    strvAdd(argV, tmpStr);
	} else {
	    strvAdd(argV, PKGLIBEXECDIR "/kvsprovider");
	}
    } else {
	strvAdd(argV, MPIEXEC_BINARY);
    }

    /* always export all environment variables */
    strvAdd(argV, "-genvall");

    /* interactive mode */
    if (step->taskFlags & LAUNCH_PTY) strvAdd(argV, "-i");
    /* label output */
    if (step->taskFlags & LAUNCH_LABEL_IO) strvAdd(argV, "-l");

    /* choose PMI layer, default is MPICH's Simple PMI */
    switch (pmiType) {
	case PMI_TYPE_NONE:
	    strvAdd(argV, "--pmidisable");
	    break;
	case PMI_TYPE_PMIX:
	    strvAdd(argV, "--pmix");
	    break;
	case PMI_TYPE_DEFAULT:
	default:
	    break;
    }

    if (step->taskFlags & LAUNCH_MULTI_PROG) {
	setupArgsFromMultiProg(step, fwData, argV);
    } else {
	char *cwd = step->cwd;
	if (getConfValueI(Config, "CWD_PATTERN") == 1) {
	    cwd = IO_replaceStepSymbols(step, 0, step->cwd);
	}
	char *pwd = envGet(step->env, "PWD");
	if (pwd) {
	    char *rpath = realpath(pwd, NULL);
	    if (rpath && !strcmp(rpath, cwd)) {
		/* use pwd over cwd if realpath is identical */
		cwd = pwd;
	    }
	    free(rpath);
	}
	strvAdd(argV, "--gwdir");
	strvAdd(argV, cwd);

	if (step->packStepCount == 1) {
	    /* number of processes */
	    strvAdd(argV, "-np");
	    snprintf(buf, sizeof(buf), "%u", step->np);
	    strvAdd(argV, buf);

	    /* threads per processes */
	    /* NOTE: Actually it is unnecessary to pass tpp since it is
	     * ignored by psslurm reservation generation replacement code */
	    strvAdd(argV, "-tpp");
	    snprintf(buf, sizeof(buf), "%u", step->tpp);
	    strvAdd(argV, buf);

	    char *pset = envGet(step->env, "SLURM_SPANK_PSET_0");
	    if (pset) {
		strvAdd(argV, "--pset");
		strvAdd(argV, pset);
	    }

	    /* executable and arguments */
	    strvAppend(argV, step->argV);
	} else {
	    /* executables from job pack */
	    list_t *c;
	    bool first = true;
	    size_t c_id = 0;
	    list_for_each(c, &step->jobCompInfos) {
		JobCompInfo_t *cur = list_entry(c, JobCompInfo_t, next);

		if (!first) strvAdd(argV, ":");
		first = false;

		/* number of processes */
		strvAdd(argV, "-np");
		snprintf(buf, sizeof(buf), "%u", cur->np);
		strvAdd(argV, buf);

		/* threads per processes */
		/* NOTE: Actually it is unnecessary to pass tpp since it is
		 * ignored by psslurm reservation generation replacement code */
		uint16_t tpp = cur->tpp;
		strvAdd(argV, "-tpp");
		snprintf(buf, sizeof(buf), "%u", tpp);
		strvAdd(argV, buf);

		char key[32];
		sprintf(key, "SLURM_SPANK_PSET_%zi", c_id);
		char *pset = envGet(step->env, key);
		if (pset) {
		    strvAdd(argV, "--pset");
		    strvAdd(argV, pset);
		}

		/* executable and arguments */
		strvAppend(argV, cur->argV);

		c_id++;
	    }
	}
    }
    return argV;
}

static void fwExecStep(Forwarder_Data_t *fwdata, int rerun)
{
    Step_t *step = fwdata->userData;
    pmi_type_t pmi_type;

    /* reopen syslog */
    char buf[128];
    snprintf(buf, sizeof(buf), "psslurm-%s", Step_strID(step));
    reOpenSyslog(buf, &psslurmlogger);

    if (pamserviceOpenSession) pamserviceOpenSession(step->username);

    /* setup standard I/O and PTY */
    setupStepIO(fwdata, step);

    /* set RLimits from configuration */
    setDefaultRlimits();

    /* switch user */
    if (!switchUser(step->username, step->uid, step->gid)) {
	flog("switching user failed\n");
    }

    char *cwd = step->cwd;
    if (getConfValueI(Config, "CWD_PATTERN") == 1) {
	cwd = IO_replaceStepSymbols(step, 0, step->cwd);
    }

    char *pwd = envGet(step->env, "PWD");
    if (pwd) {
	char *rpath = realpath(pwd, NULL);
	if (rpath && !strcmp(rpath, cwd)) {
	    /* use pwd over cwd if realpath is identical */
	    cwd = pwd;
	}
	free(rpath);
    }

    if (!switchCwd(cwd)) {
	flog("switching working directory failed\n");
	exit(1);
    }

    /* decide which PMI type to use */
    pmi_type = getPMIType(step);

    /* set PSSLURM_PMI_TYPE in forwarder environment for potential respawns */
    setPMITypeEnv(pmi_type);

    /* build mpiexec et al. argument vector */
    strv_t argV = buildStartArgv(fwdata, pmi_type);
    char **argvP = strvStealArray(argV);

    /* setup step specific environment */
    setStepEnv(step);

    /* setup X11 forwarding */
    if (step->x11forward) initX11Forward(step);

    fdbg(PSSLURM_LOG_JOB, "exec %s via %s mypid %u\n", Step_strID(step),
	 argvP[0], getpid());

    /* set RLimits */
    setRlimitsFromEnv(step->env, true);

    /* remove environment variables not evaluated by mpiexec */
    removeUserVars(step->env, pmi_type);

    if (mset(PSSLURM_LOG_PROCESS)) {
	debugMpiexecStart(argvP, envGetArray(step->env));
    }

    /* start mpiexec to spawn the parallel job */
    closelog();
    execve(argvP[0], argvP, envGetArray(step->env));
    int err = errno;

    /* execve() failed */
    fprintf(stderr, "%s: execve %s: %s\n", __func__, argvP[0], strerror(err));
    snprintf(buf, sizeof(buf), "psslurm-%s", Step_strID(step));
    reOpenSyslog(buf, &psslurmlogger);
    fwarn(err, "execve(%s)", argvP[0]);
    exit(err);
}

/* HACK HACK HACK this must be called eary before other messages appear */
static int getNextServiceRank(Forwarder_Data_t *fwData)
{
    /* get next service rank from logger */
    if (PSLog_write(fwData->loggerTid, SERV_RNK, NULL, 0) < 0) {
	fwarn(errno, "rank %d: PSLog_write()", fwData->rank);
	return 0;
    }

    int rank = 0;
    while (true) {
	PSLog_Msg_t msg;

	int ret = PSLog_read(&msg, NULL);
	if (ret == -1) {
	    fwarn(errno, "rank %d: PSLog_read()", fwData->rank);
	} else if (msg.header.type != PSP_CC_MSG) {
	    flog("rank %d: unexpected message type %s ", fwData->rank,
		 PSDaemonP_printMsg(msg.header.type));
	    mlog(" from %s", PSC_printTID(msg.header.sender));
	    mlog(" while fetching from %s\n", PSC_printTID(fwData->loggerTid));
	} else if (msg.type != SERV_RNK) {
	    flog("rank %d: unexpected log message type %s", fwData->rank,
		 PSLog_printMsgType(msg.type));
	    mlog(" from %s", PSC_printTID(msg.header.sender));
	    mlog(" while fetching from %s\n", PSC_printTID(fwData->loggerTid));
	} else {
	    rank = *(int32_t *)&msg.buf;
	    break;
	}
    }

    return rank;
}

static int stepForwarderInit(Forwarder_Data_t *fwdata)
{
    Step_t *step = fwdata->userData;
    step->fwdata = fwdata;

    /* free unused memory */
    Step_deleteAll(step);

    int serviceRank = step->spawned ? getNextServiceRank(fwdata) : 0;

    initSerial(0, (Send_Msg_Func_t *)sendMsgToMother);

    GRes_Cred_type_t cType = GRES_CRED_STEP;
    if (step->stepid == SLURM_INTERACTIVE_STEP ||
	step->taskFlags & LAUNCH_EXT_LAUNCHER) {
	/* interactive steps should access all allocated
	 * resources */
	cType = GRES_CRED_JOB;
    }

    setJailEnv(step->env, step->username,
	       step->nodeinfos[step->localNodeId].stepHWthreads,
	       step->nodeinfos[step->localNodeId].jobHWthreads,
	       &step->gresList, cType, step->cred, step->credID);

    /* since this might call the jail hook, execute *after* setJailEnv() */
    if (pamserviceStartService) pamserviceStartService(step->username);

#ifdef HAVE_SPANK
    struct spank_handle spank = {
	.task = NULL,
	.alloc = Alloc_find(step->jobid),
	.job = Job_findById(step->jobid),
	.step = step,
	.hook = SPANK_INIT,
	.envSet = fwCMD_setEnv,
	.envUnset = fwCMD_unsetEnv,
	.spankEnv = step ? step->spankenv : NULL
    };
    SpankCallHook(&spank);

    SpankInitOpt(&spank);

    spank.hook = SPANK_INIT_POST_OPT;
    SpankCallHook(&spank);
#endif

    if (!PSC_switchEffectiveUser(step->username, step->uid, step->gid)) {
	flog("switching effective user to '%s' failed\n", step->username);
	return 1;
    }

    /* check if we can change working directory */
    if (step->cwd && chdir(step->cwd) == -1) {
	fwarn(errno, "chdir(%s) for uid %u gid %u",
	      step->cwd, step->uid, step->gid);
	if (slurmProto < SLURM_23_02_PROTO_VERSION) {
	    return - ESCRIPT_CHDIR_FAILED;
	}

	char buf[512];
	snprintf(buf, sizeof(buf), "psslurm: chdir(%s) failed: %s, "
		 "using /tmp\n", step->cwd, strerror(errno));
	queueFwMsg(&step->fwMsgQueue, buf, strlen(buf), STDERR, 0);

	if (chdir("/tmp") == -1) {
	    fwarn(errno, "chdir(/tmp) for uid %u gid %u", step->uid, step->gid);
	    return - ESLURMD_EXECVE_FAILED;
	}

	ufree(step->cwd);
	step->cwd = ustrdup("/tmp");
    }

    /* redirect stdout/stderr/stdin */
    if (!(step->taskFlags & LAUNCH_PTY)) {
	IO_redirectStep(fwdata, step);
	IO_openStepPipes(fwdata, step);
    }

#ifdef HAVE_SPANK
    spank.hook = SPANK_USER_INIT;
    SpankCallHook(&spank);
#endif

    if (!PSC_switchEffectiveUser("root", 0, 0)) {
	flog("switching effective user to root failed\n");
	return 1;
    }

    /* open stderr/stdout/stdin fds */
    if (step->taskFlags & LAUNCH_PTY) {
	/* open PTY */
	if (openpty(&fwdata->stdOut[1], &fwdata->stdOut[0],
		    NULL, NULL, NULL) == -1) {
	    fwarn(errno, "openpty()");
	    return 1;
	}
    }

    /* inform the mother psid */
    fwCMD_initComplete(step, serviceRank);

    /* setup I/O channels solely to send an error message, so prevent
     * any execution of child tasks */
    if (step->termAfterFWmsg != NO_VAL) {
	if (fwdata->childFunc) fwdata->childFunc = NULL;
	fwdata->childRerun = 1;
    }

    return 0;
}

static int stepForwarderLoop(Forwarder_Data_t *fwdata)
{
    Step_t *step = fwdata->userData;

    IO_init();

    if (!step->IOPort) {
	flog("no I/O ports available\n");
	return 1;
    }

    if (srunOpenIOConnection(step, step->cred->sig) == -1) {
	flog("open srun I/O connection failed\n");
	return 1;
    }

    if (step->taskFlags & LAUNCH_PTY) {
	/* open additional PTY connection to srun */
	if (srunOpenPTYConnection(step) < 0) {
	    flog("open srun pty connection failed\n");
	    return 1;
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

    /* print queued messages if any */
    list_t *t;
    list_for_each(t, &step->fwMsgQueue) {
	FwUserMsgBuf_t *buf = list_entry(t, FwUserMsgBuf_t, next);
	IO_printStepMsg(fwdata, buf->msg, buf->msgLen, buf->rank, buf->type);
    }

    if (step->termAfterFWmsg != NO_VAL) {
	flog("terminating forwarder for %s after sending user message\n",
	     Step_strID(step));
	return 1;
    }

    return 0;
}

static int stepFinalize(Forwarder_Data_t *fwdata)
{
    if (pamserviceStopService) pamserviceStopService();

    IO_finalize(fwdata);

#ifdef HAVE_SPANK
    Step_t *step = fwdata->userData;
    struct spank_handle spank = {
	.task = NULL,
	.alloc = Alloc_find(step->jobid),
	.job = Job_findById(step->jobid),
	.step = step,
	.hook = SPANK_EXIT,
	.envSet = NULL,
	.envUnset = NULL
    };

    /* set environment variables from user */
    for (char **e = envGetArray(step->env); e && *e; e++) putenv(*e);

    SpankCallHook(&spank);
#endif

    return 0;
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
    if (!Step_verifyPtr(step)) {
	flog("Invalid step pointer %p\n", step);
	return;
    }
    psAccountRegisterJob(childPid, NULL);

    /* return launch success to waiting srun since mpiexec could be spawned */
    flog("launch success for %s to srun sock '%u'\n",
	 Step_strID(step), step->srunControlMsg.sock);
    sendSlurmRC(&step->srunControlMsg, SLURM_SUCCESS);
    step->state = JOB_SPAWNED;
}

bool execStepLeader(Step_t *step)
{
    int grace = getConfValueI(SlurmConfig, "KillWait");
    if (grace < 3) grace = 30;

    step->spawned = envGet(step->env, "PMI_SPAWNED")
			|| envGet(step->env, "PMIX_SPAWNID");

    Forwarder_Data_t *fwdata = ForwarderData_new();
    char jobStr[32], titleStr[64];
    snprintf(jobStr, sizeof(jobStr), "%u.%u", step->jobid, step->stepid);
    snprintf(titleStr, sizeof(titleStr), "psslurm-step:%s", jobStr);
    fwdata->pTitle = ustrdup(titleStr);
    fwdata->jobID = ustrdup(jobStr);
    fwdata->userData = step;
    fwdata->graceTime = grace;
    fwdata->accounted = true;
    if (step->spawned) {
	char *env = envGet(step->env, "__PSSLURM_SPAWN_PTID");
	if (env && sscanf(env, "%ld", &fwdata->pTid) != 1) {
	    flog("unable to determine parent TID from '%s'\n", env);
	}
	env = envGet(step->env, "__PSSLURM_SPAWN_LTID");
	if (env && sscanf(env, "%ld", &fwdata->loggerTid) != 1) {
	    flog("unable to determine logger TID from '%s'\n", env);
	}
	env = envGet(step->env, "__PSSLURM_STEP_RANK");
	if (env && sscanf(env, "%d", &fwdata->rank) != 1) {
	    flog("unable to determine rank from '%s'\n", env);
	}
	fwdata->omitSPAWNSUCCESS = true;
    }
    fwdata->jailChild = true;
    fwdata->killSession = psAccountSignalSession;
    fwdata->callback = stepCallback;
    if (!step->spawned) fwdata->childFunc = fwExecStep;
    fwdata->hookFWInit = stepForwarderInit;
    fwdata->hookLoop = stepForwarderLoop;
    fwdata->hookChild = handleChildStartStep;
    fwdata->hookFinalize = stepFinalize;
    fwdata->handleMthrMsg = fwCMD_handleMthrStepMsg;
    fwdata->handleFwMsg = fwCMD_handleFwStepMsg;

    if (!startForwarder(fwdata)) {
	char msg[128];

	snprintf(msg, sizeof(msg), "starting forwarder for %s failed\n",
		 Step_strID(step));
	flog("%s", msg);
	setNodeOffline(step->env, step->jobid,
		       getConfValueC(Config, "SLURM_HOSTNAME"), msg);
	ForwarderData_delete(fwdata);
	return false;
    }

    flog("%s tid %s started\n", Step_strID(step), PSC_printTID(fwdata->tid));
    step->fwdata = fwdata;
    return true;
}

static int handleJobLoop(Forwarder_Data_t *fwdata)
{
    Job_t *job = fwdata->userData;

    if (!PSC_switchEffectiveUser(job->username, job->uid, job->gid)) {
	flog("switching effective user to '%s' failed\n", job->username);
	return 1;
    }

    IO_openJobIOfiles(fwdata);

    /* print queued messages if any */
    list_t *t;
    list_for_each(t, &job->fwMsgQueue) {
	FwUserMsgBuf_t *buf = list_entry(t, FwUserMsgBuf_t, next);
	IO_printJobMsg(fwdata, buf->msg, buf->msgLen, buf->type);
    }

    if (job->termAfterFWmsg) {
	flog("terminating forwarder for job %u after sending user message\n",
	     job->jobid);
	return 1;
    }

    if (!PSC_switchEffectiveUser("root", 0, 0)) {
	flog("switching effective user to root failed\n");
	return 1;
    }

#ifdef HAVE_SPANK
    struct spank_handle spank = {
	.task = NULL,
	.alloc = Alloc_find(job->jobid),
	.job = job,
	.step = NULL,
	.hook = SPANK_TASK_POST_FORK,
	.envSet = NULL,
	.envUnset = NULL
    };

    if (SpankCallHook(&spank) < 0) {
	flog("SPANK_TASK_POST_FORK failed, terminating job %u\n", job->jobid);
	/* terminate possible started child */
	kill(-fwdata->cPid, SIGKILL);
	return 1;
    }
#endif

    return 0;
}

static int jobForwarderInit(Forwarder_Data_t *fwdata)
{
    Job_t *job = fwdata->userData;
    Job_deleteAll(job);
    Step_deleteAll(NULL);

    if (pamserviceStartService) pamserviceStartService(job->username);

#ifdef HAVE_SPANK
    struct spank_handle spank = {
	.task = NULL,
	.alloc = Alloc_find(job->jobid),
	.job = job,
	.step = NULL,
	.hook = SPANK_INIT,
	.envSet = NULL,
	.envUnset = NULL,
	.spankEnv = job ? job->spankenv : NULL
    };
    SpankCallHook(&spank);

    SpankInitOpt(&spank);

    spank.hook = SPANK_INIT_POST_OPT;
    SpankCallHook(&spank);

    if (!PSC_switchEffectiveUser(job->username, job->uid, job->gid)) {
	flog("switching effective user to '%s' failed\n", job->username);
	return 1;
    }

    spank.hook = SPANK_USER_INIT;
    SpankCallHook(&spank);

    if (!PSC_switchEffectiveUser("root", 0, 0)) {
	flog("switching effective user to root failed\n");
	return 1;
    }

#endif

    setJailEnv(job->env, job->username, NULL, job->hwthreads, &job->gresList,
	       GRES_CRED_JOB, job->cred, job->localNodeId);

    /* setup I/O channels solely to send an error message, so prevent
     * any execution of child tasks */
    if (job->termAfterFWmsg) {
	if (fwdata->childFunc) fwdata->childFunc = NULL;
	fwdata->childRerun = 1;
    }

    IO_initJobFilenames(fwdata);

    return IO_openJobPipes(fwdata);
}

static int jobForwarderFin(Forwarder_Data_t *fwdata)
{
    Job_t *job = fwdata->userData;
    job->exitCode = fwdata->chldExitStatus;

    if (pamserviceStopService) pamserviceStopService();

#ifdef HAVE_SPANK
    struct spank_handle spank = {
	.task = NULL,
	.alloc = Alloc_find(job->jobid),
	.job = job,
	.step = NULL,
	.hook = SPANK_TASK_EXIT,
	.envSet = NULL,
	.envUnset = NULL
    };
    SpankCallHook(&spank);

    spank.hook = SPANK_EXIT;
    SpankCallHook(&spank);
#endif

    /* stop and destroy container */
    if (job->ct) Container_stop(job->ct);

    return 0;
}

bool execBatchJob(Job_t *job)
{
    if (job->state == JOB_RUNNING) {
	flog("error: job %u has already been started\n", job->jobid);
	return false;
    }

    if (job->mother) {
	flog("error: job %u has to be executed on the mother superior node %i\n",
	     job->jobid, PSC_getID(job->mother));
	return false;
    }

    int grace = getConfValueI(SlurmConfig, "KillWait");
    if (grace < 3) grace = 30;

    char fname[300];
    snprintf(fname, sizeof(fname), "psslurm-job:%u", job->jobid);

    Forwarder_Data_t *fwdata = ForwarderData_new();
    fwdata->pTitle = ustrdup(fname);
    fwdata->jobID = ustrdup(Job_strID(job->jobid));
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
	setNodeOffline(job->env, job->jobid,
		       getConfValueC(Config, "SLURM_HOSTNAME"), msg);
	ForwarderData_delete(fwdata);
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

    /* open the file */
    int wFlags = O_WRONLY;
    if (bcast->blockNumber == 1) {
	wFlags |= O_CREAT;
	if (bcast->flags & BCAST_FORCE) {
	    wFlags |= O_TRUNC;
	} else {
	    wFlags |= O_EXCL;
	}
    } else {
	wFlags |= O_APPEND;
    }

    char *destFile = BCast_adjustExe(bcast->fileName, bcast->jobid,
				     bcast->stepid);
    if (!destFile) {
	flog("adjusting bcast destination file %s failed\n", bcast->fileName);
	exit(1);
    }

    int fd = open(destFile, wFlags, 0700);
    if (fd == -1) {
	int eno = errno;
	fwarn(eno, "open(%s)", destFile);
	exit(eno);
    }

    /* write the file */
    if (PSCio_sendF(fd, bcast->block, bcast->blockLen) == -1) {
	int eno = errno;
	fwarn(eno, "write(%s)", destFile);
	exit(eno);
    }

    /* set permissions */
    if (bcast->flags & BCAST_LAST_BLOCK) {
	if (fchmod(fd, bcast->modes & 0700) == -1) {
	    int eno = errno;
	    fwarn(eno, "chmod(%s)", destFile);
	    exit(eno);
	}
	if (fchown(fd, bcast->uid, bcast->gid) == -1) {
	    int eno = errno;
	    fwarn(eno, "chown(%s)", destFile);
	    exit(eno);
	}
	if (bcast->atime) {
	    times.actime  = bcast->atime;
	    times.modtime = bcast->mtime;
	    if (utime(destFile, &times)) {
		int eno = errno;
		fwarn(eno, "utime(%s)", destFile);
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

    setJailEnv(bcast->env, bcast->username, NULL, bcast->hwthreads, NULL,
	       GRES_CRED_JOB, NULL, 0);

    return 0;
}

bool execBCast(BCast_t *bcast)
{
    int grace = getConfValueI(SlurmConfig, "KillWait");
    if (grace < 3) grace = 30;

    char jobid[100], fname[300];
    snprintf(jobid, sizeof(jobid), "%u", bcast->jobid);
    snprintf(fname, sizeof(fname), "psslurm-bcast:%s", jobid);

    Forwarder_Data_t *fwdata = ForwarderData_new();
    fwdata->pTitle = ustrdup(fname);
    fwdata->jobID = ustrdup(jobid);
    fwdata->userData = bcast;
    fwdata->userName = strdup(bcast->username);
    fwdata->uID = bcast->uid;
    fwdata->gID = bcast->gid;
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
		       getConfValueC(Config, "SLURM_HOSTNAME"), msg);
	ForwarderData_delete(fwdata);
	return false;
    }

    bcast->fwdata = fwdata;
    return true;
}

static int stepFollowerFWloop(Forwarder_Data_t *fwdata)
{
    Step_t *step = fwdata->userData;
    step->fwdata = fwdata;

    IO_init();

    if (!step->IOPort) {
	flog("no I/O Ports\n");
	return 1;
    }

    if (srunOpenIOConnection(step, step->cred->sig) == -1) {
	flog("open srun I/O connection failed\n");
	return 1;
    }

    if (!PSC_switchEffectiveUser(step->username, step->uid, step->gid)) {
	flog("switching effective user to '%s' failed\n", step->username);
	return 1;
    }

    IO_redirectStep(fwdata, step);

    /* print queued messages if any */
    list_t *t;
    list_for_each(t, &step->fwMsgQueue) {
	FwUserMsgBuf_t *buf = list_entry(t, FwUserMsgBuf_t, next);
	IO_printStepMsg(fwdata, buf->msg, buf->msgLen, buf->rank, buf->type);
    }

    if (step->termAfterFWmsg != NO_VAL) {
	flog("terminating forwarder for %s after sending user message\n",
	     Step_strID(step));
	return 1;
    }

    if (!PSC_switchEffectiveUser("root", 0, 0)) {
	flog("switching effective user to root failed\n");
	return 1;
    }
    return 0;
}

static int stepFollowerFWinit(Forwarder_Data_t *fwdata)
{
    initSerial(0, (Send_Msg_Func_t *)sendMsgToMother);

    Step_t *step = fwdata->userData;
    Step_deleteAll(step);

    GRes_Cred_type_t cType = GRES_CRED_STEP;
    if (step->stepid == SLURM_INTERACTIVE_STEP ||
	step->taskFlags & LAUNCH_EXT_LAUNCHER) {
	/* interactive steps should access all allocated
	 * resources */
	cType = GRES_CRED_JOB;
    }

    setJailEnv(step->env, step->username,
	       step->nodeinfos[step->localNodeId].stepHWthreads,
	       step->nodeinfos[step->localNodeId].jobHWthreads,
	       &step->gresList, cType, step->cred, step->credID);

    /* since this might call the jail hook, execute *after* setJailEnv() */
    if (pamserviceStartService) pamserviceStartService(step->username);

#ifdef HAVE_SPANK

    struct spank_handle spank = {
	.task = NULL,
	.alloc = Alloc_find(step->jobid),
	.job = Job_findById(step->jobid),
	.step = step,
	.hook = SPANK_INIT,
	.envSet = fwCMD_setEnv,
	.envUnset = fwCMD_unsetEnv,
	.spankEnv = step ? step->spankenv : NULL
    };
    SpankCallHook(&spank);

    SpankInitOpt(&spank);

    spank.hook = SPANK_INIT_POST_OPT;
    SpankCallHook(&spank);

    if (!PSC_switchEffectiveUser(step->username, step->uid, step->gid)) {
	flog("switching effective user to '%s' failed\n", step->username);
	return 1;
    }

    spank.hook = SPANK_USER_INIT;
    SpankCallHook(&spank);

    if (!PSC_switchEffectiveUser("root", 0, 0)) {
	flog("switching effective user to root failed\n");
	return 1;
    }

#endif

    /* inform the mother psid */
    fwCMD_initComplete(step, 0);

    return 0;
}

bool execStepFollower(Step_t *step)
{
    int grace = getConfValueI(SlurmConfig, "KillWait");
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
		 Step_strID(step));
	flog("%s", msg);
	setNodeOffline(step->env, step->jobid,
		       getConfValueC(Config, "SLURM_HOSTNAME"), msg);
	ForwarderData_delete(fwdata);
	return false;
    }

    flog("%s tid %s started\n", Step_strID(step), PSC_printTID(fwdata->tid));
    step->fwdata = fwdata;

    return true;
}

int handleFwResPriv(void * data)
{
#ifdef HAVE_SPANK
    if (fwStep && fwTask && fwTask->rank >= 0) {
	fwStep->exitCode = *(int *) data;

	struct spank_handle spank = {
	    .task = fwTask,
	    .alloc = fwAlloc,
	    .job = fwJob,
	    .step = fwStep,
	    .hook = SPANK_TASK_EXIT,
	    .envSet = NULL,
	    .envUnset = NULL
	};
	SpankCallHook(&spank);
    }
#endif
    return 0;
}

int handleFwRes(void * data)
{
    /* kill and remove container */
    if (fwStep && fwTask && fwTask->rank >= 0) {
	if (fwStep->ct) Container_stop(fwStep->ct);
    }

    return 0;
}

static void fwExecEpiFin(Forwarder_Data_t *fwdata, int rerun)
{
    Alloc_t *alloc = fwdata->userData;

    errno = 0;
    psPelogueCallPE("psslurm", PELOGUE_ACTION_EPILOGUE_FINALIZE, alloc->env);
    int eno = errno;

    /* execve() failed */
    fprintf(stderr, "%s: psPelogueCallPE(PELOGUE_ACTION_EPILOGUE_FINALIZE)"
	    " failed", __func__);
    if (eno) fprintf(stderr, ": %s", strerror(eno));
    fprintf(stderr, "\n");

    openlog("psid", LOG_PID | LOG_CONS, LOG_DAEMON);
    char buf[1024];
    snprintf(buf, sizeof(buf), "psslurm-epifin: %u", alloc->id);
    reOpenSyslog(buf, &psslurmlogger);
    flog("psPelogueCallPE(PELOGUE_ACTION_EPILOGUE_FINALIZE) failed");
    if (eno) {
	mwarn(eno, " ");
    } else {
	mlog("\n");
    }

    exit(eno ? eno : -1);
}

static void epiFinCallback(int32_t exit_status, Forwarder_Data_t *fwdata)
{
    Alloc_t *alloc = fwdata->userData;
    if (!Alloc_verifyPtr(alloc)) {
	flog("Invalid allocation pointer %p\n", alloc);
	return;
    }

    fdbg(PSSLURM_LOG_PELOG, "exit_status: %i fw-chldExitStatus %i\n",
	 exit_status, fwdata->chldExitStatus);

    if (alloc->terminate) {
	uint32_t allocID = alloc->id;
	Alloc_delete(alloc);
	sendEpilogueComplete(allocID, SLURM_SUCCESS);
    }
}

bool execEpilogueFin(Alloc_t *alloc)
{
    int grace = getConfValueI(SlurmConfig, "KillWait");
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
	ForwarderData_delete(fwdata);
	return false;
    }

    return true;
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet:*/
