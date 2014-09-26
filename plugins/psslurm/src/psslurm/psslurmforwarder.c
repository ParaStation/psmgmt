/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <grp.h>
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

#include "psslurmlog.h"
#include "psslurmlimits.h"
#include "psslurmcomm.h"
#include "psslurmproto.h"
#include "psslurmenv.h"
#include "slurmcommon.h"
#include "pluginpty.h"
#include "psprotocolenv.h"

#include "pluginmalloc.h"
#include "pluginforwarder.h"
#include "selector.h"

#include "psaccfunc.h"

#include "psslurmforwarder.h"

#define SERIAL_MODE 0


int jobCallback(int32_t exit_status, char *errMsg, size_t errLen, void *data)
{
    Forwarder_Data_t *fwdata = data;
    Job_t *job = fwdata->userData;

    mlog("%s: job '%s' finished, exit '%u'\n", __func__, job->id,
	    exit_status);
    if (!findJobById(job->jobid)) {
	mlog("%s: job '%u' not found\n", __func__, job->jobid);
	return 0;
    }

    /* make sure all processes are gone */
    signalTasks(job->uid, &job->tasks, SIGKILL, -1);

    job->state = JOB_COMPLETE;
    sendJobExit(job, exit_status);
    psAccountUnregisterJob(fwdata->childPid);

    /* run epilogue now */
    if (job->terminate) {
	mlog("%s: starting epilogue for job '%u'\n", __func__, job->jobid);
	job->state = JOB_EPILOGUE;
	startPElogue(job->jobid, job->uid, job->gid, job->nrOfNodes, job->nodes,
		    job->env, job->envc, 0, 0);
    }

    return 0;
}

int stepCallback(int32_t exit_status, char *errMsg, size_t errLen, void *data)
{
    Forwarder_Data_t *fwdata = data;
    Step_t *step = fwdata->userData;

    if (errLen >0) {
	mlog("%s: %s", __func__, errMsg);
    }

    mlog("%s: step '%u:%u' state '%u' finished, exit '%u'\n", __func__,
	step->jobid, step->stepid, step->state, exit_status);

    if (!findStepById(step->jobid, step->stepid)) {
	mlog("%s: step '%u:%u' not found\n", __func__, step->jobid,
		step->stepid);
	return 0;
    }

    /* make sure all processes are gone */
    signalStep(step, SIGKILL);

    if (step->srunIOSock != -1) {
	if (Selector_isRegistered(step->srunIOSock)) {
	    Selector_remove(step->srunIOSock);
	}
	close(step->srunIOSock);
	step->srunIOSock = -1;
    }

    if (!SERIAL_MODE && step->state == JOB_PRESTART) {
	/* spawn failed */
	sendSlurmRC(step->srunControlSock, SLURM_ERROR, step);
    } else {
	if (step->exitCode != 0) {
	    sendTaskExit(step, step->exitCode);
	    sendStepExit(step, step->exitCode);
	} else {
	    if (WIFSIGNALED(exit_status)) {
		sendTaskExit(step, WTERMSIG(exit_status));
		sendStepExit(step, WTERMSIG(exit_status));
	    } else {
		sendTaskExit(step, exit_status);
		sendStepExit(step, exit_status);
	    }
	}
    }

    step->state = JOB_COMPLETE;
    psAccountUnregisterJob(fwdata->childPid);

    /* run epilogue now */
    if (step->terminate) {
	mlog("%s: starting epilogue for step '%u:%u'\n", __func__, step->jobid,
		step->stepid);
	step->state = JOB_EPILOGUE;
	startPElogue(step->jobid, step->uid, step->gid, step->nrOfNodes,
			step->nodes, step->env, step->envc, 1, 0);
    }

    return 0;
}

static int setPermissions(Job_t *job)
{
    if (!job->jobscript) return 1;

    if ((chown(job->jobscript, job->uid, job->gid)) == -1) {
	mlog("%s: chown(%i:%i) '%s' failed : %s\n", __func__,
		job->uid, job->gid, job->jobscript,
		strerror(errno));
	return 1;
    }

    if ((chmod(job->jobscript, 0700)) == -1) {
	mlog("%s: chmod 0700 on '%s' failed : %s\n", __func__,
		job->jobscript, strerror(errno));
	return 1;
    }

    return 0;
}

static char *replaceStepSymbols(Step_t *step, int rank, char *path)
{
    char *hostname;
    Job_t *job;
    uint32_t i, arrayJobId = 0, nodeid = 0;
    PSnodes_ID_t myID;

    hostname = getConfValueC(&Config, "SLURM_HOSTNAME");
    if ((job = findJobById(step->jobid))) {
	arrayJobId = job->arrayJobId;
    }

    myID = PSC_getMyID();
    for (i=0; i<step->nrOfNodes; i++) {
	if (step->nodes[i] == myID) {
	    nodeid = i;
	    break;
	}
    }

    return replaceSymbols(step->jobid, step->stepid, hostname, nodeid,
			    step->username, arrayJobId, rank, path);
}

static char *replaceJobSymbols(Job_t *job, char *path)
{
    return replaceSymbols(job->jobid, SLURM_BATCH_SCRIPT, job->hostname,
			    0, job->username, job->arrayJobId, 0, path);
}

/*
 *  step replace symbols
 *
 * %A     Job array's master job allocation number.
 * %a     Job array ID (index) number.
 * %J     jobid.stepid of the running job. (e.g. "128.0")
 * %j     jobid of the running job.
 * %s     stepid of the running job.
 * %N     short hostname. This will create a separate IO file per node.
 * %n     Node identifier relative to current job (e.g. "0" is the first node
 *	    of the running job) This will create a separate IO file per node.
 * %t     task identifier (rank) relative to current job. This will
 *	    create a separate IO file per task.
 * %u     User name.
*/
char *replaceSymbols(uint32_t jobid, uint32_t stepid, char *hostname,
			int nodeid, char *username, uint32_t arrayJobId,
			int rank, char *path)
{
    char *next, *ptr, symbol, *buf = NULL;
    char tmp[1024];
    size_t len, bufSize = 0;
    int saved = 0;

    ptr = path;
    if (!(next = strchr(ptr, '%'))) {
	return ustrdup(path);
    }

    while (next) {
	symbol = *(next+1);
	len = next - ptr;

	strn2Buf(ptr, len, &buf, &bufSize);

	switch (symbol) {
	    case 'A':
		/* TODO */
		break;
	    case 'a':
		snprintf(tmp, sizeof(tmp), "%u", arrayJobId);
		str2Buf(tmp, &buf, &bufSize);
		saved = 1;
		break;
	    case 'J':
		snprintf(tmp, sizeof(tmp), "%u.%u", jobid, stepid);
		saved = 1;
		break;
	    case 'j':
		snprintf(tmp, sizeof(tmp), "%u", jobid);
		str2Buf(tmp, &buf, &bufSize);
		saved = 1;
		break;
	    case 's':
		snprintf(tmp, sizeof(tmp), "%u", stepid);
		str2Buf(tmp, &buf, &bufSize);
		saved = 1;
		break;
	    case 'N':
		str2Buf(hostname, &buf, &bufSize);
		saved = 1;
		break;
	    case 'n':
		snprintf(tmp, sizeof(tmp), "%u", nodeid);
		str2Buf(tmp, &buf, &bufSize);
		saved = 1;
		break;
	    case 't':
		snprintf(tmp, sizeof(tmp), "%u", rank);
		str2Buf(tmp, &buf, &bufSize);
		saved = 1;
		break;
	    case 'u':
		str2Buf(username, &buf, &bufSize);
		saved = 1;
		break;
	}

	if (!saved) {
	    strn2Buf(next, 2, &buf, &bufSize);
	}

	saved = 0;
	ptr = next+2;
	next = strchr(ptr, '%');
    }
    str2Buf(ptr, &buf, &bufSize);

    mlog("%s: orig '%s' result: '%s'\n", __func__, path, buf);

    return buf;
}

static char *addCwd(char *cwd, char *path)
{
    char *buf = NULL;
    size_t bufSize = 0;

    if (path[0] == '/' || path[0] == '.') {
	return path;
    }

    str2Buf(cwd, &buf, &bufSize);
    str2Buf("/", &buf, &bufSize);
    str2Buf(path, &buf, &bufSize);
    ufree(path);

    return buf;
}

int getAppendFlags(uint8_t appendMode)
{
    int flags = 0;

    if (!appendMode) {
	/* TODO: use default of configuration JobFileAppend */
	flags |= O_CREAT|O_WRONLY|O_TRUNC|O_APPEND;
    } else if (appendMode == OPEN_MODE_APPEND) {
	flags |= O_CREAT|O_WRONLY|O_APPEND;
    } else {
	flags |= O_CREAT|O_WRONLY|O_TRUNC|O_APPEND;
    }

    return flags;
}

static void redirectJobOutput(Job_t *job)
{
    char *outFile, *errFile, *inFile;
    int fd, flags = 0;

    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    close(STDIN_FILENO);

    flags = getAppendFlags(job->appendMode);

    /* stdout */
    if (!(strlen(job->stdOut))) {
	outFile = addCwd(job->cwd, replaceJobSymbols(job, "slurm-%j.out"));
    } else {
	outFile = addCwd(job->cwd, replaceJobSymbols(job, job->stdOut));
    }

    if ((fd = open(outFile, flags, 0666)) == -1) {
	mwarn(errno, "%s: open stdout '%s' failed :", __func__, outFile);
	exit(1);
    }
    if ((dup2(fd, STDOUT_FILENO)) == -1) {
	mwarn(errno, "%s: dup2(%i) '%s' failed :", __func__, fd, outFile);
	exit(1);
    }

    /* stderr */
    if (!(strlen(job->stdErr))) {
	errFile = addCwd(job->cwd, replaceJobSymbols(job, "slurm-%j.out"));
    } else {
	errFile = addCwd(job->cwd, replaceJobSymbols(job, job->stdErr));
    }

    if (strlen(job->stdErr)) {
	if ((fd = open(errFile, flags, 0666)) == -1) {
	    mwarn(errno, "%s: open stderr '%s' failed :", __func__, errFile);
	    exit(1);
	}
    }
    if ((dup2(fd, STDERR_FILENO)) == -1) {
	mwarn(errno, "%s: dup2(%i) '%s' failed :", __func__, fd, errFile);
	exit(1);
    }

    ufree(errFile);
    ufree(outFile);

    /* stdin */
    if (!(strlen(job->stdIn))) {
	inFile = ustrdup("/dev/null");
    } else {
	inFile = addCwd(job->cwd, replaceJobSymbols(job, job->stdIn));
    }
    if ((fd = open(inFile, O_RDONLY)) == -1) {
	mwarn(errno, "%s: open stdin '%s' failed :", __func__, inFile);
	exit(1);
    }
    if ((dup2(fd, STDIN_FILENO)) == -1) {
	mwarn(errno, "%s: dup2(%i) '%s' failed :", __func__, fd, inFile);
	exit(1);
    }
    ufree(inFile);
}

static void switchUser(char *username, uid_t uid, gid_t gid, char *cwd)
{
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

    /* change to job working directory */
    if ((chdir(cwd)) == -1) {
	mlog("%s: chdir to '%s' failed : %s\n", __func__, cwd, strerror(errno));
	exit(1);
    }
}

static void execBatchJob(void *data, int rerun)
{
    Forwarder_Data_t *fwdata = data;
    Job_t *job = fwdata->userData;

    mlog("%s: exec job '%s'\n", __func__, job->id);

    setPermissions(job);

    /* TODO pinning, waiting for #2049 */

    /* switch user */
    switchUser(job->username, job->uid, job->gid, job->cwd);

    /* redirect output */
    redirectJobOutput(job);

    /* setup batch specific env */
    setBatchEnv(job);

    /* set rlimits */
    setRlimitsFromEnv(&job->env, &job->envc, 0);

    /* do exec */
    closelog();
    execve(job->jobscript, job->argv, job->env);
}

static void redirectIORank(Step_t *step, int rank)
{
    char *ptr, *next, *outFile, *errFile, *inFile;
    int flags = 0, fd, needReplace = 0;

    flags = getAppendFlags(step->appendMode);

    if (strlen(step->stdOut) > 0) {
	needReplace = 0;
	ptr = step->stdOut;
	while ((next = strchr(ptr, '%'))) {
	    if (next[1] == 't') {
		needReplace = 1;
		break;
	    }
	    ptr = next+1;
	}

	if (needReplace) {
	    ptr = replaceStepSymbols(step, rank, step->stdOut);
	    outFile = addCwd(step->cwd, ptr);

	    if ((fd = open(outFile, flags, 0666)) == -1) {
		mwarn(errno, "%s: open(%s) failed: ", __func__, outFile);
		exit(1);
	    }
	    close(STDOUT_FILENO);
	    if ((dup2(fd, STDOUT_FILENO)) == -1) {
		mwarn(errno, "%s: stdout dup2(%u) failed: ", __func__, fd);
		exit(1);
	    }
	}
    }

    if (strlen(step->stdErr) > 0) {
	needReplace = 0;
	ptr = step->stdErr;
	while ((next = strchr(ptr, '%'))) {
	    if (next[1] == 't') {
		needReplace = 1;
		break;
	    }
	    ptr = next+1;
	}

	if (needReplace) {
	    ptr = replaceStepSymbols(step, rank, step->stdErr);
	    errFile = addCwd(step->cwd, ptr);

	    if ((fd = open(errFile, flags, 0666)) == -1) {
		mwarn(errno, "%s: open(%s) failed: ", __func__, errFile);
		exit(1);
	    }
	    close(STDERR_FILENO);
	    if ((dup2(fd, STDERR_FILENO)) == -1) {
		mwarn(errno, "%s: stdout dup2(%u) failed: ", __func__, fd);
		exit(1);
	    }
	}
    }

    if (strlen(step->stdIn) > 0) {
	needReplace = 0;
	ptr = step->stdIn;
	while ((next = strchr(ptr, '%'))) {
	    if (next[1] == 't') {
		needReplace = 1;
		break;
	    }
	    ptr = next+1;
	}

	if (needReplace) {
	    ptr = replaceStepSymbols(step, rank, step->stdIn);
	    inFile = addCwd(step->cwd, ptr);

	    if ((fd = open(inFile, O_RDONLY)) == -1) {
		mwarn(errno, "%s: open(%s) failed: ", __func__, inFile);
		exit(1);
	    }
	    close(STDIN_FILENO);
	    if ((dup2(fd, STDIN_FILENO)) == -1) {
		mwarn(errno, "%s: stdout dup2(%u) failed: ", __func__, fd);
		exit(1);
	    }
	}
    }
}

int handleForwarderInit(void * data)
{
    PStask_t *task = data;
    Step_t *step;
    int count = 0, status;
    char *ptr, *next;
    uint32_t jobid = 0, stepid = SLURM_BATCH_SCRIPT;
    pid_t child = PSC_getPID(task->tid);

    if (task->rank <0) return 0;

    ptr = task->environ[count++];
    while (ptr) {
	if (!(strncmp(ptr, "SLURM_STEPID=", 13))) {
	    sscanf(ptr+13, "%u", &stepid);
	}
	if (!(strncmp(ptr, "SLURM_JOBID=", 12))) {
	    sscanf(ptr+12, "%u", &jobid);
	}
	ptr = task->environ[count++];
    }

    if ((step = findStepById(jobid, stepid))) {
	if (strlen(step->stdIn) >0) {
	    ptr = step->stdIn;
	    while ((next = strchr(ptr, '%'))) {
		if (next[1] == 't') {
		    mlog("%s: closing child stdin\n", __func__);
		    close(task->stdin_fd);
		    task->stdin_fd = -1;
		    break;
		}
		ptr = next+1;
	    }
	}

	if (step->taskFlags & TASK_PARALLEL_DEBUG) {

	    waitpid(child, &status, WUNTRACED);
	    if (!WIFSTOPPED(status)) {
		mlog("%s: child '%i' not stopped\n", __func__, child);
	    } else {
		if ((kill(child, SIGSTOP)) == -1) {
		    mwarn(errno, "%s: kill(%i) failed: ", __func__, child);
		}
		if ((ptrace(PTRACE_DETACH, child, 0, 0)) == -1) {
		    mwarn(errno, "%s: ptrace(PTRACE_DETACH) failed: ", __func__);
		}
	    }
	}
    }

    return 0;
}

int handleExecClient(void * data)
{
    PStask_t *task = data;
    Step_t *step;
    int i, count = 0, fd;
    char *ptr;
    uint32_t jobid = 0, stepid = SLURM_BATCH_SCRIPT;

    if (task->rank <0) return 0;

    /* clear environment */
    for (i=0; PSP_rlimitEnv[i].envName; i++) {
	unsetenv(PSP_rlimitEnv[i].envName);
    }
    unsetenv("__PSI_UMASK");
    unsetenv("__PSI_RAW_IO");
    unsetenv("PSI_SSH_INTERACTIVE");
    unsetenv("PSI_LOGGER_RAW_MODE");

    /* redirect stdin to /dev/null for all ranks > 0 to /dev/null */
    ptr = task->environ[count++];
    while (ptr) {
	if (!(strncmp(ptr, "SLURM_PTY_WIN_ROW", 17))) {
	    if (task->rank >0) {
		close(STDIN_FILENO);
		fd = open("/dev/null", O_RDONLY);
		dup2(fd, STDIN_FILENO);
	    }
	}

	if (!(strncmp(ptr, "SLURM_STEPID=", 13))) {
	    sscanf(ptr+13, "%u", &stepid);
	}
	if (!(strncmp(ptr, "SLURM_JOBID=", 12))) {
	    sscanf(ptr+12, "%u", &jobid);
	}

	ptr = task->environ[count++];
    }

    if ((step = findStepById(jobid, stepid))) {
	redirectIORank(step, task->rank);

	/* stop child after exec */
	if (step->taskFlags & TASK_PARALLEL_DEBUG) {
	    if ((ptrace(PTRACE_TRACEME, 0, 0, 0)) == -1) {
		mwarn(errno, "%s: ptrace() failed: ", __func__);
		exit(1);
	    }
	}

	setRankEnv(step);
    }

    return 0;
}

static void execInteractiveJob(void *data, int rerun)
{
    /* TODO use correct path */
#define MPIEXEC_BINARY "/opt/parastation/bin/mpiexec"

    Forwarder_Data_t *fwdata = data;
    Step_t *step = fwdata->userData;
    int argc = 0;
    unsigned int i;
    char **argv, buf[128], *tty_name;
    char *cols = NULL, *rows = NULL;
    struct winsize ws;

    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    close(STDIN_FILENO);

    if (step->pty) {
	tty_name = ttyname(fwdata->stdOut[0]);
	close(fwdata->stdOut[1]);

	pty_setowner(step->uid, step->gid, tty_name);
	pty_make_controlling_tty(&fwdata->stdOut[0], tty_name);

	cols = getValueFromEnv(step->env, step->envc, "SLURM_PTY_WIN_COL");
	rows = getValueFromEnv(step->env, step->envc, "SLURM_PTY_WIN_ROW");

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
	if (!step->userManagedIO) {
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
	    if ((dup2(fwdata->stdIn[0], STDIN_FILENO)) == -1) {
		mwarn(errno, "%s: stdin dup2(%u) failed: ",
			__func__, fwdata->stdIn[0]);
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

    /* switch user */
    switchUser(step->username, step->uid, step->gid, step->cwd);

    /* build mpiexec argv */
    argv = umalloc((step->argc + 20 + 1) * sizeof(char *));

    if (!SERIAL_MODE) {
	argv[argc++] = ustrdup(MPIEXEC_BINARY);
	//argv[argc++] = ustrdup("-v");
	/* number of processes */
	argv[argc++] = ustrdup("-np");
	snprintf(buf, sizeof(buf), "%u", step->np);
	argv[argc++] = ustrdup(buf);
	/* export all environment variables */
	argv[argc++] = ustrdup("-x");

	/* interactive mode */
	if (step->pty) argv[argc++] = ustrdup("-i");
	/* label output */
	if (step->labelIO) argv[argc++] = ustrdup("-l");
    }

    /*
    argv[argc++] = ustrdup("-u");
    argv[argc++] = ustrdup(buffer);
    */

    /* executable and arguments */
    for (i=0; i<step->argc; i++) {
	argv[argc++] = step->argv[i];
    }
    argv[argc] = NULL;

    /* setup task specific env */
    setTaskEnv(step);

    mlog("%s: exec job '%u:%u' mypid '%u'\n", __func__, step->jobid,
	    step->stepid, getpid());

    /* set rlimits */
    setRlimitsFromEnv(&step->env, &step->envc, 1);

    /* start mpiexec to spawn the parallel job */
    closelog();
    execve(argv[0], argv, step->env);
}

static void redirectStepIO(Forwarder_Data_t *fwdata, Step_t *step)
{
    char *outFile = NULL, *errFile = NULL, *inFile;
    int flags = 0;

    flags = getAppendFlags(step->appendMode);

    /* need to create pipes as user, or the permission to /dev/stdX
     *  will be denied */
    if (seteuid(step->uid) == -1) {
	mwarn(errno, "%s: seteuid(%i) failed: ", __func__, step->uid);
	return;
    }

    /* stdout */
    if (step->stdOut && strlen(step->stdOut) > 0) {
	outFile = addCwd(step->cwd, replaceStepSymbols(step, 0, step->stdOut));

	fwdata->stdOut[0] = -1;
	if ((fwdata->stdOut[1] = open(outFile, flags, 0666)) == -1) {
	    mwarn(errno, "%s: open stdout '%s' failed :", __func__, outFile);
	}
	mlog("%s: outfile: '%s' fd '%i'\n", __func__, outFile,
		fwdata->stdOut[1]);
    } else {
	if ((pipe(fwdata->stdOut)) == -1) {
	    mlog("%s: create stdout pipe failed\n", __func__);
	    return;
	}
	/*
	mlog("%s: stdout pipe '%i:%i'\n", __func__, fwdata->stdOut[0],
		fwdata->stdOut[1]);
	*/
    }

    /* stderr */
    if (step->stdErr && strlen(step->stdErr) > 0) {
	errFile = addCwd(step->cwd, replaceStepSymbols(step, 0, step->stdErr));

	fwdata->stdErr[0] = -1;
	if (outFile && !(strcmp(outFile, errFile))) {
	    fwdata->stdErr[1] = fwdata->stdOut[1];
	} else {
	    if ((fwdata->stdErr[1] = open(errFile, flags, 0666)) == -1) {
		mwarn(errno, "%s: open stderr '%s' failed :",
			__func__, errFile);
	    }
	}
	/*
	mlog("%s: errfile: '%s' fd '%i'\n", __func__, errFile,
		fwdata->stdErr[1]);
	*/
    } else if (step->stdOut && strlen(step->stdOut) > 0) {
	fwdata->stdErr[0] = -1;
	fwdata->stdErr[1] = fwdata->stdOut[1];
	mlog("%s: errfile: '%s' fd '%i'\n", __func__, outFile,
		fwdata->stdErr[1]);
    } else {
	if ((pipe(fwdata->stdErr)) == -1) {
	    mlog("%s: create stderr pipe failed\n", __func__);
	    return;
	}
	/*
	mlog("%s: stderr pipe '%i:%i'\n", __func__, fwdata->stdErr[0],
		fwdata->stdErr[1]);
	*/
    }

    /* stdin */
    if (step->stdIn && strlen(step->stdIn) > 0) {
	inFile = addCwd(step->cwd, replaceStepSymbols(step, 0, step->stdIn));

	fwdata->stdIn[1] = -1;
	if ((fwdata->stdIn[0] = open(inFile, O_RDONLY)) == -1) {
	    mwarn(errno, "%s: open stdin '%s' failed :",
		    __func__, inFile);
	}
	mlog("%s: infile: '%s' fd '%i'\n", __func__, inFile,
		fwdata->stdIn[0]);
    } else {
	if ((pipe(fwdata->stdIn)) == -1) {
	    mlog("%s: create stdin pipe failed\n", __func__);
	    return;
	}
	/*
	mlog("%s: stdin pipe '%i:%i'\n", __func__, fwdata->stdIn[0],
		fwdata->stdIn[1]);
	*/
    }

    if (seteuid(0) == -1) {
	mwarn(errno, "%s: seteuid(0) failed: ", __func__);
    };
}

void stepForwarderInit(void *data)
{
    Forwarder_Data_t *fwdata = data;
    Step_t *step = fwdata->userData;
    step->fwdata = fwdata;

   /* open stderr/stdout/stdin fds */
    if (step->pty) {
	/* open pty */
	if ((openpty(&fwdata->stdOut[1], &fwdata->stdOut[0],
			NULL, NULL, NULL)) == -1) {
	    mlog("%s: openpty() failed\n", __func__);
	    return;
	}
    } else {
	/* user will take care of I/O handling */
	if (step->userManagedIO) return;

	redirectStepIO(fwdata, step);
    }
}

int handleUserOE(int sock, void *data)
{
    static char buf[1024];
    Forwarder_Data_t *fwdata = data;
    Step_t *step = fwdata->userData;
    int32_t size, ret;
    uint16_t type;

    if (step->pty) {
	type = (sock == fwdata->stdOut[1]) ? SLURM_IO_STDOUT : SLURM_IO_STDERR;
    } else {
	type = (sock == fwdata->stdOut[0]) ? SLURM_IO_STDOUT : SLURM_IO_STDERR;
    }

    if ((size = doRead(sock, buf, sizeof(buf) - 1)) <= 0) {
	mlog("%s: connection(%i) closed: %s\n", __func__, sock,
		type == SLURM_IO_STDOUT ? "stdout" : "stderr");
	Selector_remove(sock);
	close(sock);
    }

    /*
    mlog("%s: sock '%i' forward '%s' size '%u'\n", __func__, sock,
	    type == SLURM_IO_STDOUT ? "stdout" : "stderr", size);
    */

    /* eof to srun */
    if (size <0) size = 0;
    if (size >0) buf[size] = '\0';

    //if (size>0) mlog("%s: '%s'", __func__, buf);

    /* forward data to srun, size of 0 means EOF for stream */
    if ((ret = srunSendIO(type, step, buf, size)) != (size + 10)) {
	mwarn(errno, "%s: sending IO failed: size:%i ret:%i ", __func__,
		(size +10), ret);
    }

    return 0;
}

void stepForwarderLoop(void *data)
{
    Forwarder_Data_t *fwdata = data;
    Step_t *step = fwdata->userData;

    /* user will take care of I/O handling */
    if (step->userManagedIO) return;

    if (!step->IOPort) {
	mlog("%s: no IO Ports\n", __func__);
	/* TODO: kill step ? */
	return;
    }

    if (!srunOpenIOConnection(step)) {
	mlog("%s: srun connect failed\n", __func__);
	return;
    }

    if (step->pty) {
	/* open additiional pty connection to srun */
	if ((srunOpenPTY(step)) < 0) {
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
	close(fwdata->stdOut[1]);
	close(fwdata->stdErr[1]);
	close(fwdata->stdIn[0]);
    }

    if (SERIAL_MODE) {
	/* set pid info for our only child */
	step->tidsLen = 1;
	step->tids = umalloc(sizeof(uint32_t));
	step->tids[0] = PSC_getTID(PSC_getMyID(), fwdata->childPid);

	/* forward info to waiting srun */
	sendTaskPids(step);
    }
}

#define CMD_PRINT_CHILD_MSG 100

void printChildMessage(Forwarder_Data_t *fwdata, char *msg, int error)
{
    PS_DataBuffer_t data = { .buf = NULL };

    /* can happen, if forwarder is already gone */
    if (!fwdata) return;

    addInt32ToMsg(CMD_PRINT_CHILD_MSG, &data);
    addUint8ToMsg(error, &data);
    addStringToMsg(msg, &data);

    doWriteP(fwdata->controlSocket, data.buf, data.bufUsed);
    ufree(data.buf);
}

static int stepForwarderMsg(void *data, char *ptr, int32_t cmd)
{
    Forwarder_Data_t *fwdata = data;
    Step_t *step = fwdata->userData;
    uint8_t error;
    char *msg;

    if (cmd == CMD_PRINT_CHILD_MSG) {
	getUint8(&ptr, &error);
	msg = getStringM(&ptr);

	if (error && !step->pty) {
	    srunSendIO(SLURM_IO_STDERR, step, msg, strlen(msg));
	} else {
	    srunSendIO(SLURM_IO_STDOUT, step, msg, strlen(msg));
	}
	ufree(msg);
	return 1;
    }
    return 0;
}

static void handleChildStart(void *data, pid_t fw, pid_t childPid,
				pid_t childSid)
{
    psAccountRegisterJob(childPid, NULL);
}

void execUserStep(Step_t *step)
{
    Forwarder_Data_t *fwdata;
    char jobid[100];
    char fname[300];
    int grace;

    getConfValueI(&SlurmConfig, "KillWait", &grace);
    mlog("%s: grace %u\n", __func__, grace);
    if (grace < 3) grace = 30;

    fwdata = getNewForwarderData();

    snprintf(jobid, sizeof(jobid), "%u.%u", step->jobid, step->stepid);
    snprintf(fname, sizeof(fname), "psslurm-step:%s", jobid);
    fwdata->pTitle = ustrdup(fname);

    fwdata->jobid = ustrdup(jobid);
    fwdata->userData = step;
    fwdata->graceTime = grace;
    fwdata->killSession = psAccountsendSignal2Session;
    fwdata->callback = stepCallback;
    fwdata->childFunc = execInteractiveJob;
    fwdata->hookForwarderLoop = stepForwarderLoop;
    fwdata->hookForwarderInit = stepForwarderInit;
    fwdata->hookHandleMsg = stepForwarderMsg;
    fwdata->hookChildStart = handleChildStart;

    if ((startForwarder(fwdata)) != 0) {
	mlog("%s: starting forwarder for job '%u' failed\n", __func__,
		step->stepid);
    }
    step->fwdata = fwdata;
    if (SERIAL_MODE) {
	sendSlurmRC(step->srunControlSock, SLURM_SUCCESS, step);
    }
}

void execUserJob(Job_t *job)
{
    Forwarder_Data_t *fwdata;
    char fname[300];
    int grace;

    getConfValueI(&SlurmConfig, "KillWait", &grace);
    if (grace < 3) grace = 30;

    fwdata = getNewForwarderData();

    snprintf(fname, sizeof(fname), "psslurm-job:%s", job->id);
    fwdata->pTitle = ustrdup(fname);

    fwdata->jobid = ustrdup(job->id);
    fwdata->userData = job;
    fwdata->graceTime = grace;
    fwdata->killSession = psAccountsendSignal2Session;
    fwdata->callback = jobCallback;
    fwdata->childFunc = execBatchJob;
    //fwdata->hookHandleMsg = handleForwarderMsg;
    fwdata->hookChildStart = handleChildStart;

    if ((startForwarder(fwdata)) != 0) {
	mlog("%s: starting forwarder for job '%s' failed\n", __func__, job->id);
    }

    job->state = JOB_RUNNING;
    job->fwdata = fwdata;
}
