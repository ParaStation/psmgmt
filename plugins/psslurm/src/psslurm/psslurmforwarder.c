/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
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
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utime.h>
#include <fenv.h>

#include "list.h"
#include "pscommon.h"
#include "pscio.h"
#include "psenv.h"
#include "pslog.h"
#include "psprotocol.h"
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

#include "peloguehandles.h"
#include "psaccounthandles.h"
#include "pspmihandles.h"

#include "psslurm.h"
#include "psslurmcomm.h"
#include "psslurmconfig.h"
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
#include "slurmcommon.h"
#include "slurmerrno.h"
#ifdef HAVE_SPANK
#include "psslurmspank.h"
#endif

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

    setJailEnv(&job->env, job->username, NULL, &(job->hwthreads),
	       &job->gresList, job->cred, job->localNodeId);
    return PSIDhook_call(PSIDHOOK_JAIL_TERM, &job->fwdata->cPid);
}

static void cbTermJail(int exit, bool tmdOut, int iofd, void *info)
{
    char errMsg[1024];
    size_t errLen;

    bool ret = getScriptCBdata(iofd, errMsg, sizeof(errMsg),&errLen);
    if (!ret) {
	flog("%s: getting jail term script callback data failed\n", __func__);
	return;
    }

    if (exit != PSIDHOOK_NOFUNC && exit != 0) {
	flog("jail script failed with exit status %i\n", exit);
	flog("%s\n", errMsg);
    }
}

static void jobCallback(int32_t exit_status, Forwarder_Data_t *fw)
{
    Job_t *job = fw->userData;
    Alloc_t *alloc = Alloc_find(job->jobid);

    mlog("%s: job '%u' finished, exit %i / %i\n", __func__, job->jobid,
	 exit_status, fw->chldExitStatus);
    if (!Job_findById(job->jobid)) {
	mlog("%s: job '%u' not found\n", __func__, job->jobid);
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
    int eStatus = fw->exitRcvd ? fw->chldExitStatus : fw->hookExitCode;

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
	Alloc_delete(alloc->id);
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

    setJailEnv(&step->env, step->username,
	    &(step->nodeinfos[step->localNodeId].stepHWthreads),
	    &(step->nodeinfos[step->localNodeId].jobHWthreads),
	    &step->gresList, step->cred, step->localNodeId);
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
    stopStepFollower(step);

    Alloc_t *alloc = Alloc_find(step->jobid);
    flog("%s in %s finished, exit %i / %i\n", Step_strID(step),
	 Job_strState(step->state), exit_status, fw->chldExitStatus);

    /* terminate cgroup */
    PSID_execFunc(termStepJail, NULL, cbTermJail, NULL, step);

    /* make sure all processes are gone */
    Step_signal(step, SIGKILL, 0);
    if (step->fwdata->cPid > 0) killChild(step->fwdata->cPid, SIGKILL, step->uid);

    freeSlurmMsg(&step->srunIOMsg);

    if (step->state == JOB_PRESTART) {
	/* spawn failed */
	uint32_t rc = (uint32_t) SLURM_ERROR;

	if (fw->codeRcvd) {
	    switch (fw->hookExitCode) {
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
	if (step->packJobid != NO_VAL && step->packNrOfNodes > 1) {
	    if (send_PS_PackExit(step, eStatus) == -1) {
		flog("sending pack exit for %s failed\n", Step_strID(step));
	    }
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

    /* set default RLimits */
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
    spank.hook = SPANK_TASK_POST_FORK;
    SpankCallHook(&spank);

    spank.hook = SPANK_TASK_INIT;
    SpankCallHook(&spank);
#endif

    /* setup batch specific environment */
    setJobEnv(job);

    /* set RLimits */
    setRlimitsFromEnv(&job->env, 0);

    /* reset FPE exceptions mask */
    if (getConfValueI(Config, "ENABLE_FPE_EXCEPTION") &&
	oldExceptions != -1) {
	if (feenableexcept(oldExceptions) == -1) {
	    flog("warning: failed to reset exception mask\n");
	}
    }

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

static void initFwPtr(PStask_t *task)
{
    if (fwStep || !task) return;

    bool isAdmin = isPSAdminUser(task->uid, task->gid);
    uint32_t jobid, stepid;
    Step_t *step = Step_findByEnv(task->environ, &jobid, &stepid);

    if (step) {
	fwStep = step;
	fwJob = Job_findById(jobid);
	fwAlloc = Alloc_find(jobid);
    } else if (!isAdmin) {
	flog("step '%u:%u' not found\n", jobid, stepid);
    }

    fwTask = task;
}

int handleForwarderInit(void * data)
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
	    flog("rank %d (global %d) failed to find my step\n", task->jobRank,
		 task->rank);
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
	.hook = SPANK_TASK_POST_FORK,
	.envSet = NULL,
	.envUnset = NULL
    };
    SpankInitOpt(&spank);
    SpankCallHook(&spank);
#endif

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
		   &(fwStep->nodeinfos[fwStep->localNodeId].stepHWthreads),
		   &(fwStep->nodeinfos[fwStep->localNodeId].jobHWthreads),
		   &fwStep->gresList, fwStep->cred, fwStep->localNodeId);
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
	.envUnset = NULL
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
	char *newExe = BCast_adjustExe(task->argv[0], fwStep->jobid,
				       fwStep->stepid);
	if (!newExe) {
	    flog("failed to adjust BCast exe %s\n", task->argv[0]);
	} else {
	    if (newExe != task->argv[0]) free(task->argv[0]);
	    task->argv[0] = newExe;
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
		mwarn(errno, "%s: ptrace()", __func__);
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
		.len = offsetof(PSLog_Msg_t, buf) },
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

static void debugMpiexecStart(char **argv, char **env)
{
    mlog("%s:", __func__);
    for (int i = 0; argv[i]; i++) mlog(" %s", argv[i]);
    mlog("\n");

    for (int i = 0; env[i]; i++) {
	mlog("%s: env[%i] '%s'\n", __func__, i, env[i]);
    }
}

void buildStartArgv(Forwarder_Data_t *fwData, strv_t *argV, pmi_type_t pmiType)
{
    Step_t *step = fwData->userData;
    char buf[128];

    strvInit(argV, NULL, 0);

    if (envGet(&step->env, "PMI_SPAWNED")) {
	char *tmpStr = envGet(&step->env, "__PSI_MPIEXEC_KVSPROVIDER");
	if (tmpStr) {
	    strvAdd(argV, tmpStr);
	} else {
	    strvAdd(argV, PKGLIBEXECDIR "/kvsprovider");
	}
    } else {
	strvAdd(argV, MPIEXEC_BINARY);
    }

    /* always export all environment variables */
    strvAdd(argV, "-x");

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
	if (step->packJobid == NO_VAL) {
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
    snprintf(buf, sizeof(buf), "psslurm-%s", Step_strID(step));
    initLogger(buf, NULL);
    maskLogger(oldMask);

    /* setup standard I/O and PTY */
    setupStepIO(fwdata, step);

    /* set default RLimits */
    setDefaultRlimits();

    /* switch user */
    if (!switchUser(step->username, step->uid, step->gid)) {
	flog("switching user failed\n");
    }

    char *cwd = step->cwd;
    if (getConfValueI(Config, "CWD_PATTERN") == 1) {
	cwd = IO_replaceStepSymbols(step, 0, step->cwd);
    }

    char *pwd = envGet(&step->env, "PWD");
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

    /* build mpiexec et al. argument vector */
    buildStartArgv(fwdata, &argV, pmi_type);

    /* setup step specific environment */
    setStepEnv(step);

    /* setup X11 forwarding */
    if (step->x11forward) initX11Forward(step);

    fdbg(PSSLURM_LOG_JOB, "exec %s via %s mypid %u\n", Step_strID(step),
	 argV.strings[0], getpid());

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
    snprintf(buf, sizeof(buf), "psslurm-%s", Step_strID(step));
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

    /* free unused memory */
    Step_deleteAll(step);

    initSerial(0, sendMsg);
    setJailEnv(&step->env, step->username,
	    &(step->nodeinfos[step->localNodeId].stepHWthreads),
	    &(step->nodeinfos[step->localNodeId].jobHWthreads),
	    &step->gresList, step->cred, step->localNodeId);

#ifdef HAVE_SPANK
    struct spank_handle spank = {
	.task = NULL,
	.alloc = Alloc_find(step->jobid),
	.job = Job_findById(step->jobid),
	.step = step,
	.hook = SPANK_INIT,
	.envSet = fwCMD_setEnv,
	.envUnset = fwCMD_unsetEnv
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
	if (slurmProto < SLURM_23_02_PROTO_VERSION) {
	    return - ESCRIPT_CHDIR_FAILED;
	}

	char buf[512];
	snprintf(buf, sizeof(buf), "psslurm: chdir(%s) failed: %s, "
		 "using /tmp\n", step->cwd, strerror(errno));
	queueFwMsg(&step->fwMsgQueue, buf, strlen(buf), STDERR, 0);

	if (chdir("/tmp") == -1) {
	    mwarn(errno, "%s: chdir(/tmp) for uid %u gid %u", __func__,
		  step->uid, step->gid);
	    return - ESLURMD_EXECVE_FAILED;
	}

	ufree(step->cwd);
	step->cwd = ustrdup("/tmp");
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

    /* inform the mother psid */
    fwCMD_initComplete(step);

    /* setup I/O channels solely to send an error message, so prevent
     * any execution of child tasks */
    if (step->termAfterFWmsg != NO_VAL) {
	if (fwdata->childFunc) fwdata->childFunc = NULL;
	fwdata->childRerun = 1;
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

    /* print queued messages if any */
    list_t *t;
    list_for_each(t, &step->fwMsgQueue) {
	FwUserMsgBuf_t *buf = list_entry(t, FwUserMsgBuf_t, next);
	IO_printStepMsg(fwdata, buf->msg, buf->msgLen, buf->rank, buf->type);
    }

    if (step->termAfterFWmsg != NO_VAL) {
	flog("terminating forwarder for %s after sending user message\n",
	     Step_strID(step));
	exit(1);
    }
}

static void stepFinalize(Forwarder_Data_t *fwdata)
{
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
	 Step_strID(step), step->srunControlMsg.sock);
    sendSlurmRC(&step->srunControlMsg, SLURM_SUCCESS);
    step->state = JOB_SPAWNED;
}

bool execStepLeader(Step_t *step)
{
    int grace = getConfValueI(SlurmConfig, "KillWait");
    if (grace < 3) grace = 30;

    step->spawned = envGet(&step->env, "PMI_SPAWNED");

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
	char *env = envGet(&step->env, "__PSSLURM_SPAWN_PTID");
	if (env) fwdata->pTid = atoi(env);
	env = envGet(&step->env, "__PSSLURM_SPAWN_LTID");
	if (env) fwdata->loggerTid = atoi(env);
	env = envGet(&step->env, "__PSSLURM_SPAWN_RANK");
	if (env) fwdata->rank = atoi(env);
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
	setNodeOffline(&step->env, step->jobid,
		       getConfValueC(Config, "SLURM_HOSTNAME"), msg);
	ForwarderData_delete(fwdata);
	return false;
    }

    flog("%s tid %s started\n", Step_strID(step), PSC_printTID(fwdata->tid));
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

    /* print queued messages if any */
    list_t *t;
    list_for_each(t, &job->fwMsgQueue) {
	FwUserMsgBuf_t *buf = list_entry(t, FwUserMsgBuf_t, next);
	IO_printJobMsg(fwdata, buf->msg, buf->msgLen, buf->type);
    }

    if (job->termAfterFWmsg) {
	flog("terminating forwarder for job %u after sending user message\n",
	     job->jobid);
	exit(1);
    }

    if (switchEffectiveUser("root", 0, 0) == -1) {
	mlog("%s: switching effective user failed\n", __func__);
	exit(1);
    }
}

static int jobForwarderInit(Forwarder_Data_t *fwdata)
{
    Job_t *job = fwdata->userData;
    Job_deleteAll(job);
    Step_deleteAll(NULL);

    PSIDhook_call(PSIDHOOK_PSSLURM_JOB_FWINIT, job->username);

#ifdef HAVE_SPANK
    struct spank_handle spank = {
	.task = NULL,
	.alloc = Alloc_find(job->jobid),
	.job = job,
	.step = NULL,
	.hook = SPANK_INIT,
	.envSet = NULL,
	.envUnset = NULL
    };
    SpankCallHook(&spank);

    SpankInitOpt(&spank);

    spank.hook = SPANK_INIT_POST_OPT;
    SpankCallHook(&spank);

    if (switchEffectiveUser(job->username, job->uid, job->gid) == -1) {
	flog("switching effective user to %s failed\n", job->username);
	exit(1);
    }

    spank.hook = SPANK_USER_INIT;
    SpankCallHook(&spank);

    if (switchEffectiveUser("root", 0, 0) == -1) {
	flog("switching effective user to root failed\n");
	exit(1);
    }

#endif

    setJailEnv(&job->env, job->username, NULL, &(job->hwthreads),
	       &job->gresList, job->cred, job->localNodeId);

    /* setup I/O channels solely to send an error message, so prevent
     * any execution of child tasks */
    if (job->termAfterFWmsg) {
	if (fwdata->childFunc) fwdata->childFunc = NULL;
	fwdata->childRerun = 1;
    }

    return IO_openJobPipes(fwdata);
}

static void jobForwarderFin(Forwarder_Data_t *fwdata)
{
    Job_t *job = fwdata->userData;
    job->exitCode = fwdata->chldExitStatus;

    PSIDhook_call(PSIDHOOK_PSSLURM_JOB_FWFIN, job->username);

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
	setNodeOffline(&job->env, job->jobid,
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
	mwarn(eno, "%s: open(%s)", __func__, destFile);
	exit(eno);
    }

    /* write the file */
    if (PSCio_sendF(fd, bcast->block, bcast->blockLen) == -1) {
	int eno = errno;
	mwarn(eno, "%s: write(%s)", __func__, destFile);
	exit(eno);
    }

    /* set permissions */
    if (bcast->flags & BCAST_LAST_BLOCK) {
	if (fchmod(fd, bcast->modes & 0700) == -1) {
	    int eno = errno;
	    mwarn(eno, "%s: chmod(%s)", __func__, destFile);
	    exit(eno);
	}
	if (fchown(fd, bcast->uid, bcast->gid) == -1) {
	    int eno = errno;
	    mwarn(eno, "%s: chown(%s)", __func__, destFile);
	    exit(eno);
	}
	if (bcast->atime) {
	    times.actime  = bcast->atime;
	    times.modtime = bcast->mtime;
	    if (utime(destFile, &times)) {
		int eno = errno;
		mwarn(eno, "%s: utime(%s)", __func__, destFile);
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

    setJailEnv(bcast->env, bcast->username, NULL, &(bcast->hwthreads), NULL,
	       NULL, 0);

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

    /* print queued messages if any */
    list_t *t;
    list_for_each(t, &step->fwMsgQueue) {
	FwUserMsgBuf_t *buf = list_entry(t, FwUserMsgBuf_t, next);
	IO_printStepMsg(fwdata, buf->msg, buf->msgLen, buf->rank, buf->type);
    }

    if (step->termAfterFWmsg != NO_VAL) {
	flog("terminating forwarder for %s after sending user message\n",
	     Step_strID(step));
	exit(1);
    }

    if (switchEffectiveUser("root", 0, 0) == -1) {
	mlog("%s: switching effective user failed\n", __func__);
	exit(1);
    }
}

static int stepFollowerFWinit(Forwarder_Data_t *fwdata)
{
    initSerial(0, sendMsg);

    Step_t *step = fwdata->userData;
    Step_deleteAll(step);

    setJailEnv(&step->env, step->username,
	    &(step->nodeinfos[step->localNodeId].stepHWthreads),
	    &(step->nodeinfos[step->localNodeId].jobHWthreads),
	    &step->gresList, step->cred, step->localNodeId);

#ifdef HAVE_SPANK

    struct spank_handle spank = {
	.task = NULL,
	.alloc = Alloc_find(step->jobid),
	.job = Job_findById(step->jobid),
	.step = step,
	.hook = SPANK_INIT,
	.envSet = fwCMD_setEnv,
	.envUnset = fwCMD_unsetEnv
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

    /* inform the mother psid */
    fwCMD_initComplete(step);

    return 1;
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
	setNodeOffline(&step->env, step->jobid,
		       getConfValueC(Config, "SLURM_HOSTNAME"), msg);
	ForwarderData_delete(fwdata);
	return false;
    }

    flog("%s tid %s started\n", Step_strID(step), PSC_printTID(fwdata->tid));
    step->fwdata = fwdata;

    return true;
}

int handleFwRes(void * data)
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

static void fwExecEpiFin(Forwarder_Data_t *fwdata, int rerun)
{
    Alloc_t *alloc = fwdata->userData;
    char *argv[2];
    char buf[1024], script[1024];
    char *dirScripts = getConfValueC(Config, "DIR_SCRIPTS");

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

static void epiFinCallback(int32_t exit_status, Forwarder_Data_t *fwdata)
{
    Alloc_t *alloc = fwdata->userData;

    fdbg(PSSLURM_LOG_PELOG, "exit_status: %i fw-chldExitStatus %i\n",
	 exit_status, fwdata->chldExitStatus);

    if (alloc->terminate) {
	uint32_t allocID = alloc->id;
	Alloc_delete(alloc->id);
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
