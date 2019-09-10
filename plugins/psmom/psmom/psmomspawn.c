/*
 * ParaStation
 *
 * Copyright (C) 2010-2019 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <pwd.h>
#include <errno.h>
#include <grp.h>
#include <unistd.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <netdb.h>
#include <sys/wait.h>

#include "timer.h"
#include "psidscripts.h"
#include "pscommon.h"
#include "psprotocol.h"
#include "psnodes.h"
#include "psidcomm.h"

#include "pluginhelper.h"
#include "pluginmalloc.h"

#include "psaccounthandles.h"
#include "pspamhandles.h"

#include "pbsdef.h"
#include "psmomlog.h"
#include "psmomconfig.h"
#include "psmom.h"
#include "psmompscomm.h"
#include "psmomchild.h"
#include "psmomscript.h"
#include "psmomforwarder.h"
#include "psmomproto.h"
#include "psmomconv.h"
#include "psmomenv.h"
#include "psmompbsserver.h"
#include "psmomcollect.h"
#include "psmomacc.h"
#include "psmomkvs.h"

#include "psmomspawn.h"

void psmomSwitchUser(char *username, struct passwd *spasswd, int saveEnv)
{
    char tmp[200], *rootDir;

    /* change to new root (chroot) */
    if ((rootDir = getEnvValue("PBS_O_ROOTDIR"))) {
	if ((chroot(rootDir)) == -1) {
	    mlog("%s: chroot(%s) failed : %s\n", __func__, rootDir,
		    strerror(errno));
	    exit(1);
	}
    }

    char *initDir = getEnvValue("PBS_O_INITDIR");
    char *cwd = initDir ? initDir : spasswd->pw_dir;

    if (!switchUser(spasswd->pw_name, spasswd->pw_uid, spasswd->pw_gid, cwd)) {
	mlog("%s: changing user failed\n", __func__);
	exit(1);
    }

    /* update environment */
    setenv("HOME", spasswd->pw_dir, 1);
    setenv("USER", username, 1);
    setenv("USERNAME", username, 1);
    setenv("LOGNAME", spasswd->pw_name, 1);

    if (saveEnv) {
	snprintf(tmp, sizeof(tmp), "HOME=%s", spasswd->pw_dir);
	addEnv(tmp);
	snprintf(tmp, sizeof(tmp), "USER=%s", username);
	addEnv(tmp);
	snprintf(tmp, sizeof(tmp), "USERNAME=%s", username);
	addEnv(tmp);
	snprintf(tmp, sizeof(tmp), "LOGNAME=%s", spasswd->pw_name);
	addEnv(tmp);
    }
}

static char* findShellInPBS(int login)
{
    char *shellCopy, *l_shell, *shell = NULL;

    /* find shell from pbs_vars to use */

    if ((shellCopy = getEnvValue("PBS_O_SHELL"))) {

	if (strlen(shellCopy) <= 2) {
	    return NULL;
	}

	if (login) {
	    if (!(shell = strrchr(shellCopy, '/'))) {
		return NULL;
	    }
	    shell += 1;
	    l_shell = umalloc(strlen(shell) + 2);
	    strcpy(l_shell, "-");
	    strcat(l_shell, shell);
	    return l_shell;
	}
	return ustrdup(shellCopy);
    }

    return NULL;
}

static char *findShellInPasswd(Job_t *job, int login)
{
    char *shell = NULL, *l_shell;

    /* find shell from passwd entry */
    if (!(shell = job->passwd.pw_shell)) {
	return NULL;
    }

    if (login) {
	if (!(shell = strrchr(shell, '/'))) return NULL;
	shell += 1;
	l_shell = umalloc(strlen(shell) + 2);
	strcpy(l_shell, "-");
	strcat(l_shell, shell);
	return l_shell;
    }
    return ustrdup(shell);
}

char *findUserShell(Job_t *job, int login, int noblock)
{
    #define DEFAULT_SHELL "/bin/sh"
    #define DEFAULT_LOGIN_SHELL	"-sh"

    char *shell = NULL;

    if ((shell = findShellInPBS(login))) {
	return shell;
    }

    if (!noblock) {
	if ((shell = findShellInPasswd(job, login))) {
	    return shell;
	}
    }

    if (login) {
	return ustrdup(DEFAULT_LOGIN_SHELL);
    }
    return ustrdup(DEFAULT_SHELL);
}

void setOutputFDs(char *outlog, char *errlog, int stdinFD, int logFD)
{
    struct rlimit rl;
    int i;

    /* Close all open file descriptors, except stdin/out/err */
    if ((getrlimit(RLIMIT_NOFILE, &rl)) < 0) {
	mlog("%s: can't get file limit 'RLIMIT_NOFILE' : %s\n", __func__,
		strerror(errno));
	exit(1);
    }
    if (rl.rlim_max == RLIM_INFINITY) {
	rl.rlim_max = 1024;
    }
    for (i=3; i < (int) rl.rlim_max; i++) {
	if (i == stdinFD) continue;
	if (logFD == i) continue;
	close(i);
    }

    /* connect stdout/err to file */
    if ((freopen(errlog, "a+", stderr)) == NULL) {
	mlog("%s: freopen(stderr) failed file '%s' : %s\n", __func__,
		errlog, strerror(errno));
	exit(1);
    }
    if (freopen(outlog, "a+", stdout) == NULL) {
	mlog("%s: freopen(stdout) failed file '%s' : %s\n", __func__,
		outlog, strerror(errno));
	exit(1);
    }

    /* redirect stdin */
    if ((dup2(stdinFD, STDIN_FILENO)) == -1) {
	mlog("%s: dup2 (%i) failed : %s\n", __func__, stdinFD,
		strerror(errno));
    }
    close(stdinFD);
}

/**
 * @brief Convert a rlimit string to its integer representation.
 *
 * @param limit The limit to convert.
 *
 * @return Returns the requested limit or -1 on error.
 */
static int resString2Limit(char *limit)
{
    if (!(strcmp(limit, "RLIMIT_CPU"))) {
	return RLIMIT_CPU;
    } else if (!(strcmp(limit, "RLIMIT_DATA"))) {
	return RLIMIT_DATA;
    } else if (!(strcmp(limit, "RLIMIT_FSIZE"))) {
	return RLIMIT_FSIZE;
    } else if (!(strcmp(limit, "RLIMIT_RSS"))) {
	return RLIMIT_RSS;
    } else if (!(strcmp(limit, "RLIMIT_AS"))) {
	return RLIMIT_AS;
    } else if (!(strcmp(limit, "RLIMIT_NOFILE"))) {
	return RLIMIT_NOFILE;
    } else if (!(strcmp(limit, "RLIMIT_STACK"))) {
	return RLIMIT_STACK;
    } else if (!(strcmp(limit, "RLIMIT_CORE"))) {
	return RLIMIT_CORE;
    } else if (!(strcmp(limit, "RLIMIT_NPROC"))) {
	return RLIMIT_NPROC;
    } else if (!(strcmp(limit, "RLIMIT_MEMLOCK"))) {
	return RLIMIT_MEMLOCK;
    } else if (!(strcmp(limit, "RLIMIT_NICE"))) {
	return RLIMIT_NICE;
    } else if (!(strcmp(limit, "RLIMIT_LOCKS"))) {
	return RLIMIT_LOCKS;
    }

    return -1;
}

/**
 * @brief Set default ulimits from configuration.
 *
 * @param limits A comma separated string with limits to set.
 *
 * @param soft Flag which should be set to 1 for setting soft limits and
 * 0 for setting hard limits.
 *
 * @return No return value.
 */
static void setDefaultResLimits(char *limits, int soft)
{
    char *cp_limits, *toksave, *next, *lname, *lvalue, *tmp;
    const char delim[] = ",";
    int iLimit, iValue;
    struct rlimit limit;

    cp_limits = ustrdup(limits);
    next = strtok_r(cp_limits, delim, &toksave);

    while (next) {
	if (!(tmp = strchr(next, '='))) {
	    mlog("%s: invalid rlimit '%s'\n", __func__, next);
	    next = strtok_r(NULL, delim, &toksave);
	    continue;
	}
	tmp[0] = '\0';
	lvalue = tmp + 1;
	lname = next;

	if ((iLimit = resString2Limit(lname)) == -1) {
	    mlog("%s: invalid rlimit name '%s'\n", __func__, lname);
	    next = strtok_r(NULL, delim, &toksave);
	    continue;
	}

	if ((sscanf(lvalue, "%i", &iValue)) != 1) {
	    mlog("%s: invalid rlimit value '%s'\n", __func__, lvalue);
	    next = strtok_r(NULL, delim, &toksave);
	    continue;
	}

	if ((getrlimit(iLimit, &limit)) < 0) {
	    mlog("%s: getting rlimit failed :  '%s'\n", __func__,
		    strerror(errno));
	    limit.rlim_cur = limit.rlim_max = iValue;
	} else {
	    if (soft) {
		limit.rlim_cur = iValue;
	    } else {
		limit.rlim_max = iValue;
		if (limit.rlim_cur > limit.rlim_max) {
		    limit.rlim_cur = iValue;
		}
	    }
	}

	if ((setrlimit(iLimit, &limit)) == -1) {
	    mlog("%s: failed setting rlimit '%s' soft '%zu' hard '%zu' : "
		    "%s\n", __func__, lname, limit.rlim_cur, limit.rlim_max,
		    strerror(errno));
	}
	next = strtok_r(NULL, delim, &toksave);
    }

    ufree(cp_limits);
}

static unsigned long sizeToBytes(char *string)
{
    unsigned long size;
    char suf[11];
    int s_word = sizeof(int);

    struct {
	char *format;
	uint64_t mult;
    } conf_table[] = {
	{ "b",  1 },
	{ "kb", 1024 },
	{ "mb", 1024 * 1024 },
	{ "gb", 1024 * 1024 * 1024 },
	{ "w",  s_word },
	{ "kw", s_word * 1024 },
	{ "mw", s_word * 1024 * 1024 },
	{ "gw", (uint64_t) s_word * 1024 * 1024 * 1024 },
	{ "", 0 },
    }, *ptr = conf_table;


    if ((sscanf(string, "%lu%10s", &size, suf)) > 2) return 0;

    while (ptr->mult !=  0) {
	if (!(strcmp(ptr->format, suf))) return ptr->mult * size;
	ptr++;
    }

    return 0;
}

static int strToInt(char *string)
{
    int num;

    if ((sscanf(string, "%d", &num)) != 1) return 0;

    return num;
}


void setResourceLimits(Job_t *job)
{
    struct list_head *pos;
    struct rlimit limit;
    Data_Entry_t *next, *data;
    char *limits = getConfValueC(&config, "RLIMITS_HARD");

    /* set default hard rlimits */
    if (limits) setDefaultResLimits(limits, 0);

    /* set default soft rlimits */
    limits = getConfValueC(&config, "RLIMITS_SOFT");
    if (limits) setDefaultResLimits(limits, 1);

    data = &job->data;

    /* loop through all limits in the job */
    if ((data && !list_empty(&data->list))) {
	list_for_each(pos, &data->list) {
	    if (!(next = list_entry(pos, Data_Entry_t, list))) break;

	    if (!(strcmp(next->name, "Resource_List"))) {

		mdbg(PSMOM_LOG_VERBOSE, "%s: %s to %s\n", __func__,
			next->resource, next->value);

		if (!(strcmp(next->resource, "pcput")) ||
			!(strcmp(next->resource, "cput"))) {
		    /* max cpu time per process */
		    limit.rlim_cur = limit.rlim_max =
			stringTimeToSec(next->value);
		    if ((setrlimit(RLIMIT_CPU, &limit)) == 1) {
			mwarn(errno, "%s: limit cpu time failed", __func__);
		    }
		} else if (!(strcmp(next->resource, "file"))) {
		    /* max file size */
		    limit.rlim_cur = limit.rlim_max = sizeToBytes(next->value);
		    if ((setrlimit(RLIMIT_FSIZE, &limit)) == 1) {
			mwarn(errno, "%s: limit file size failed", __func__);
		    }
		} else if (!(strcmp(next->resource, "pmem")) ||
				!(strcmp(next->resource, "mem"))) {
		    /* max physical memory */
		    limit.rlim_cur = limit.rlim_max = sizeToBytes(next->value);
		    if ((setrlimit(RLIMIT_DATA, &limit)) == -1) {
			mwarn(errno, "%s: limit physical memory failed",
			    __func__);
		    }

		    if ((setrlimit(RLIMIT_RSS, &limit)) == -1) {
			mwarn(errno, "%s: limit physical memory failed",
			    __func__);
		    }
		} else if (!(strcmp(next->resource, "pvmem")) ||
				!(strcmp(next->resource, "vmem"))) {
		    /* max virtual memory */
		    limit.rlim_cur = limit.rlim_max = sizeToBytes(next->value);
		    if ((setrlimit(RLIMIT_AS, &limit)) == -1) {
			mwarn(errno, "%s: limit virtual memory failed",
			    __func__);
		    }
		} else if (!(strcmp(next->resource, "nice"))) {
		    /* nice level */
		    if ((nice(strToInt(next->value))) == -1 ) {
			mlog("%s: error setting nice value\n", __func__);
		    }
		} else if (!(strcmp(next->resource, "walltime"))) {
		    /* ignore, already done with the child tracking */
		} else if (!(strcmp(next->resource, "nodes"))) {
		    /* ignore nodes, no real limit */
		} else if (!(strcmp(next->resource, "neednodes"))) {
		    /* ignore neednodes, no real limit */
		} else if (!(strcmp(next->resource, "nodect"))) {
		    /* ignore nodect, no real limit */
		} else if (!(strcmp(next->resource, "signal"))) {
		    /* ignore signal, no real limit */
		} else if (!(strcmp(next->resource, "depend"))) {
		    /* ignore signal, no real limit */
		} else {
		    mdbg(PSMOM_LOG_WARN, "%s: limit not supported:%s val:%s\n",
			__func__, next->resource, next->value);
		}
	    }
	}
    }
}

int sendPElogueStart(Job_t *job, bool prologue)
{
    char *jobUserName, *group, *limits, *queue, *jobtype;
    char buf[300], *res_used = NULL, *gpu = NULL;
    PS_SendDB_t data;
    int32_t timeout, type, i;

    if (prologue) {
	timeout = getConfValueI(&config, "TIMEOUT_PROLOGUE");
	type = PSP_PSMOM_PROLOGUE_START;
    } else {
	timeout = getConfValueI(&config, "TIMEOUT_EPILOGUE");
	type = PSP_PSMOM_EPILOGUE_START;
    }

    /* add PElogue data structure */
    initFragBuffer(&data, PSP_CC_PLUG_PSMOM, type);
    for (i=0; i<job->nrOfUniqueNodes; i++) {
	setFragDest(&data, PSC_getTID(job->nodes[i].id, 0));
    }

    /* add job hashname */
    addStringToMsg(job->hashname, &data);

    /* add user name */
    addStringToMsg(job->user, &data);

    /* add job name */
    addStringToMsg(job->id, &data);

    /* add start time */
    addTimeToMsg(job->start_time, &data);

    /* add users job name */
    if (!(jobUserName = getJobDetail(&job->data, "Job_Name", NULL))) {
	mlog("%s: can't find job_name for job '%s'\n", __func__, job->id);
	return 0;
    }
    addStringToMsg(getJobDetail(&job->data, "Job_Name", NULL), &data);

    /* add user name */
    addStringToMsg(job->user, &data);

    /* add group */
    if (!(group = getJobDetail(&job->data, "egroup", NULL))) {
	mlog("%s: can't find group for job '%s'\n", __func__, job->id);
	return 0;
    }
    addStringToMsg(group, &data);

    /* add resource limits */
    if (getJobDetailGlue(&job->data, "Resource_List", buf, sizeof(buf))) {
	limits = buf;
    } else {
	limits = NULL;
    }
    addStringToMsg(limits, &data);

    /* add queue */
    queue = getJobDetail(&job->data, "queue", NULL);
    addStringToMsg(queue, &data);

    /* add timeout */
    addInt32ToMsg(timeout, &data);

    /* add session id */
    snprintf(buf, sizeof(buf), "%d", job->sid);
    addStringToMsg(buf, &data);

    /* add jobtype (nameExt) */
    jobtype = getJobDetail(&job->data, "jobtype", NULL);
    addStringToMsg(jobtype, &data);

    /* add used resources */
    if (!prologue) {
	updateJobInfo(job);
	if (getJobDetailGlue(&job->status, "resources_used", buf,
	    sizeof(buf))) {
	    res_used = buf;
	}
    }
    addStringToMsg(res_used, &data);

    /* add exit status */
    addInt32ToMsg(job->jobscriptExit, &data);

    /* add gpu infos */
    gpu = getJobDetail(&job->data, "exec_gpus", NULL);
    addStringToMsg(gpu, &data);

    /* add PBS server */
    addStringToMsg(job->server, &data);

    /* send the message to all hosts in the job */
    sendFragMsg(&data);

    return 1;
}

static int callbackCopyScript(int fd, PSID_scriptCBInfo_t *info)
{
    int32_t exitCode;
    char errMsg[300] = { '\0' };
    Copy_Data_t *data;
    Job_t *job;
    Child_t *child;
    ComHandle_t *com;
    size_t errLen;

    /* fetch error msg and exit status */
    if ((getScriptCBData(fd, info, &exitCode, errMsg, sizeof(errMsg),
	    &errLen))) {
	mlog("%s: invalid scriptcb data\n", __func__);
	return 1;
    }

    data = (Copy_Data_t *) info->info;
    com = data->com;

    if (errMsg[0] != '\0' && strlen(errMsg) > 0) {
	mlog("%s", errMsg);
    }

    if ((job = findJobById(data->jobid))) {
	int i;

	if (!(child = findChildByJobid(job->id, PSMOM_CHILD_COPY))) {
	    mlog("%s: finding child '%s' failed\n", __func__, job->id);
	} else {
	    if (!(deleteChild(child->pid))) {
		mlog("%s: deleting child '%s' failed\n", __func__, job->id);
	    }
	}

	/* ufree memory */
	for (i=0; i<data->count; i++) {
	    ufree(data->files[i]->local);
	    ufree(data->files[i]->remote);
	    ufree(data->files[i]);
	}
	ufree(data->files);
	ufree(data->jobid);
	ufree(data->hashname);
	ufree(data->jobowner);
	ufree(data->execuser);
	ufree(data->execgroup);
	ufree(data);

	/* reset process information */
	job->pid = -1;
	job->sid = -1;
    } else {
	mlog("%s: finding job '%s' for copy data failed\n", __func__,
		data->jobid);
    }
    /* malloced by psid */
    ufree(info);

    if (!exitCode) {
	/* copy ok */
	WriteTM(com, 0);
	WriteDigit(com, 0);
	WriteDigit(com, 1);
	wDoSend(com);

	return 0;
    } else {
	/* copy failed */
	mlog("%s: copy forwarder exit '%i'\n", __func__, exitCode);
	send_TM_Error(com, PBSE_NOCOPYFILE, "Job files not copied", 1);

	/* PBS Server will not tell us to delete the job. So we have to do it on
	 * our own. */
	if (job) jobCleanup(job, 1);
    }
    return 0;
}

void afterJobCleanup(char *user)
{
    if (!hasRunningJobs(user)) {
	/* find all leftover user daemons and warn/kill them */
	int killDaemons = getConfValueI(&config, "KILL_USER_DAEMONS");
	int warnDaemons = getConfValueI(&config, "WARN_USER_DAEMONS");
	struct passwd *spasswd;

	if (killDaemons || warnDaemons) {
	    if (!(spasswd = getpwnam(user))) {
		mlog("%s: getpwnam failed for '%s' failed\n", __func__, user);
	    } else {
		psAccountFindDaemonProcs(spasswd->pw_uid, killDaemons,
		    warnDaemons);
	    }
	}
    }
}

int spawnCopyScript(Copy_Data_t *data)
{
    pid_t pid;

    if ((pid = PSID_execFunc(execCopyForwarder, NULL, callbackCopyScript,
			     data)) == -1) {
	mlog("%s: exec copy script failed\n", __func__);
	handleFailedSpawn();
	return 1;
    }

    addChild(pid, PSMOM_CHILD_COPY, data->jobid);
    mdbg(PSMOM_LOG_PROCESS, "%s: copy [%i] for job '%s' "
	"started\n", __func__, pid, data->jobid);

    return 0;
}

static int callbackJob(int fd, PSID_scriptCBInfo_t *info)
{
    char errMsg[300] = { '\0' };
    int32_t status = -1;
    Job_t *job_info, *job;
    Child_t *child;
    int childType;
    size_t errLen;

    /* fetch error msg and exit status */
    if ((getScriptCBData(fd, info, &status, errMsg, sizeof(errMsg), &errLen))) {
	mlog("%s: invalid cb data\n", __func__);
	return 1;
    }

    if (errMsg[0] != '\0' && strlen(errMsg) > 0) {
	mlog("%s", errMsg);
    }

    job_info = (Job_t *) info->info;
    if (!(job = findJobById(job_info->id))) {
	mlog("%s job not found\n", __func__);
	return 1;
    }
    /* malloced by psid */
    ufree(info);

    if (status != 0) {
	mlog("%s: forwarder exit '%i', child exit '%i'\n", __func__, status,
		job->jobscriptExit);
    }

    /* if job was already running but failed, let it exit with 1 */
    if (job->jobscriptExit == -1) {
	if (job->state == JOB_RUNNING) {
	    job->jobscriptExit = 1;
	}
    }

    if (job->jobscriptExit != 0 || status != 0) {
	if (job->qsubPort) {
	    stat_failedInterJobs++;
	} else {
	    stat_failedBatchJobs++;
	}
    } else {
	if (job->qsubPort) {
	    stat_successInterJobs++;
	} else {
	    stat_successBatchJobs++;
	}
    }

    /* un-register the forwarder child */
    childType = (job->qsubPort) ? PSMOM_CHILD_INTERACTIVE :
		 PSMOM_CHILD_JOBSCRIPT;

    if (!(child = findChildByJobid(job->id, childType))) {
	mlog("%s: finding child '%s' failed\n", __func__, job->id);
    } else {
	if (!(deleteChild(child->pid))) {
	    mlog("%s: deleting child '%s' failed\n", __func__, job->id);
	}
    }

    /* if the job is interactive and the prologue failed
     * we abort here */
    if (job->qsubPort && job->state == JOB_CANCEL_PROLOGUE) {
	sendTMJobTermination(job);
	return 0;
    }

    /* get accounting info from the psaccount plugin */
    fetchAccInfo(job);

    /* stop accounting of the job */
    psAccountUnregisterJob(job->pid);

    /* tell other nodes the job is finished */
    sendJobInfo(job, 0);

    /* cleanup leftover ssh/daemon processes */
    psPamDeleteUser(job->user, job->id);
    afterJobCleanup(job->user);

    /* reset process information */
    job->pid = job->sid = -1;

    if (doShutdown) {
	/* we are shutting down so we don't run epilogue scripts */
	job->end_time = time(NULL);
	job->state = JOB_EXIT;

	sendTMJobTermination(job);
	return 0;
    }

    /* if the prologue is still running but connection to qsub failed,
     * we cancel the prologue and abort here */
    if (job->qsubPort && job->state == JOB_PROLOGUE) {
	job->state = JOB_CANCEL_INTERACTIVE;

	/* cancel the prologue scripts */
	signalPElogue(job, "SIGTERM", "qsub connection failed");
	return 0;
    }

    /* start the epilogue script(s) */
    job->epilogueTrack = job->nrOfUniqueNodes;
    job->state = JOB_EPILOGUE;
    sendPElogueStart(job, false);
    monitorPELogueTimeout(job);

    return 0;
}

void spawnJobScript(Job_t *job)
{
    pid_t pid = PSID_execFunc(execJobscriptForwarder, NULL, callbackJob, job);

    if (pid == -1) {
	mlog("%s: exec jobscript script failed\n", __func__);
	job->state = JOB_EXIT;
	job->jobscriptExit = job->prologueExit;
	sendTMJobTermination(job);
	handleFailedSpawn();
    } else {
	mdbg(PSMOM_LOG_JOB, "batch job '%s' user '%s' np '%i' is starting\n",
	     job->id, job->user, job->nrOfNodes);

	/* register child */
	addChild(pid, PSMOM_CHILD_JOBSCRIPT, job->id);
	stat_batchJobs++;
	mdbg(PSMOM_LOG_PROCESS, "%s: jobscript [%i] for job '%s' "
		"started\n", __func__, pid, job->id);

	job->state = JOB_PRESTART;

	/* tell other nodes the job is starting */
	sendJobInfo(job, 1);
    }
}

void startInteractiveJob(Job_t *job, ComHandle_t *com)
{
    int pid = 0, exit = 0;

    mdbg(PSMOM_LOG_JOB, "interactive job '%s' user '%s' np %i is starting\n",
	 job->id, job->user, job->nrOfNodes);

    wWrite(com, &exit, sizeof(exit));

    job->state = JOB_PRESTART;

    if (wDoSend(com) < 0) {
	mlog("%s: writing prologue exit status failed, killing job '%s'\n",
		__func__, job->id);

	/* kill the waiting forwarder */
	stopInteractiveJob(job);
	return;
    }

    job->state = JOB_RUNNING;
    psPamSetState(job->user, job->id, PSPAM_STATE_JOB);

    /* tell other nodes the job is starting */
    sendJobInfo(job, 1);

    mdbg(PSMOM_LOG_PROCESS, "%s: interactive job started [%i] name '%s'\n"
	, __func__, pid, job->id);

    return;
}

void stopInteractiveJob(Job_t *job)
{
    int err = 2, ret;
    ComHandle_t *forwarder_com;
    Child_t *child;

    if (!(child = findChildByJobid(job->id, PSMOM_CHILD_INTERACTIVE))) {
	mlog("%s interactive forwarder not found\n", __func__);
	return;
    }

    /* release waiting forwarder */
    if (!(forwarder_com = getJobCom(job, JOB_CON_FORWARD))) {
	mlog("%s: invalid forwarder com handle\n", __func__);
	kill(child->pid, SIGTERM);
    } else {
	while(1) {
	    if ((ret = write(forwarder_com->socket, &err,
			    sizeof(err))) != sizeof(err)) {
		if (ret == -1 && errno == EINTR) continue;

		mlog("%s: writing error status failed : %s\n", __func__,
			strerror(errno));
		kill(child->pid, SIGTERM);
		break;
	    } else {
		break;
	    }
	}
    }

    /* remark: the forwarder callback will send the job termination */
}

int spawnInteractiveJob(Job_t *job)
{
    pid_t pid;

    /* do the actual spawn */
    if ((pid = PSID_execFunc(execInterForwarder, NULL, callbackJob,
	    job)) == -1) {
	mlog("%s: exec interactive job script failed\n", __func__);
	handleFailedSpawn();
	return 1;
    }

    /* set job infos */
    addChild(pid, PSMOM_CHILD_INTERACTIVE, job->id);
    stat_interJobs++;

    /* set job state */
    job->state = JOB_PRESTART;
    return 0;
}

void handleFailedSpawn()
{
    list_t *pos;
    Server_t *serv;
    struct tm *ts;
    time_t now;
    char buf[32], note[128];

    now = time(NULL);
    ts = localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", ts);
    snprintf(note, sizeof(note), "psmom - %s - spawn/fork failed, possible "
		"memory problem", buf);

    mlog("%s: setting myself offline\n", __func__);

    list_for_each(pos, &ServerList.list) {
	if ((serv = list_entry(pos, Server_t, list)) == NULL) break;

	setPBSNodeOffline(serv->addr, NULL, note);
    }
}
