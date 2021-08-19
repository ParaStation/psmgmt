/*
 * ParaStation
 *
 * Copyright (C) 2010-2018 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <inttypes.h>

#include "pluginhelper.h"
#include "pluginmalloc.h"
#include "timer.h"
#include "selector.h"
#include "psidtask.h"
#include "psidpartition.h"

#include "psaccounthandles.h"

#include "psmomlog.h"
#include "psmomconfig.h"
#include "psmomproto.h"
#include "psmomscript.h"
#include "psmomkvs.h"
#include "psmomjobinfo.h"
#include "psmomsignal.h"

#include "psmomjob.h"

/** List of all known (local) jobs */
static LIST_HEAD(jobList);

/** Timer id for obitting jobs */
int jobObitTimerID = -1;

#define JOB_HISTORY_SIZE 10
#define JOB_HISTORY_ID_LEN 20

static char jobHistory[JOB_HISTORY_SIZE][JOB_HISTORY_ID_LEN];

static int jobHistIndex = 0;

void setJobObitTimer(Job_t *job)
{
    struct timeval Timer = {0,0};
    int Time;

    job->state = JOB_WAIT_OBIT;

    /* timer already in place */
    if (jobObitTimerID != -1) return;

    Time = getConfValueI(&config, "TIME_OBIT_RESEND");
    Timer.tv_sec = Time;
    if ((jobObitTimerID = Timer_register(&Timer, obitWaitingJobs)) == -1) {
	mlog("%s: registering job obit timer failed\n", __func__);
	return;
    }
}

void obitWaitingJobs(void)
{
    list_t *j;
    int err_count = 0;

    if (list_empty(&jobList)) {
	/* no jobs, nothing to do here */
	Timer_remove(jobObitTimerID);
	jobObitTimerID = -1;
	return;
    };

    list_for_each(j, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);

	if (job->state == JOB_WAIT_OBIT) {

	    /* request additional job information from the server */
	    if (job->recovered && job->recoverTrack <= 3 &&
		!(getJobDetail(&job->data, "Variable_List", NULL))) {

		if (!requestJobInformation(job) && job->recoverTrack == 3) {
		    mlog("%s: requesting recover info for job '%s' failed.\n",
			 __func__, job->id);
		}

		job->recoverTrack++;
		err_count++;
		continue;
	    }

	    mdbg(PSMOM_LOG_VERBOSE, "%s: resending job obit for '%s'\n",
		__func__, job->id);

	    /* try to obit the job */
	    if (sendTMJobTermination(job)) err_count++;
	}
    }

    if (err_count == 0) {
	/* all job obit msgs have been set out */
	Timer_remove(jobObitTimerID);
	jobObitTimerID = -1;
    }
}

int countJobs()
{
    int count=0;
    list_t *j;
    list_for_each(j, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);

	if (job->recovered == 0) count++;
    }

    return count;
}

char *getJobString()
{
    char *jStr = NULL;
    list_t *j;
    list_for_each(j, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);

	if (!jStr) {
	    jStr = ustrdup(job->id);
	    if (!jStr) {
		mlog("%s: out of memory!\n", __func__);
		exit(1);
	    }
	} else {
	    jStr = urealloc(jStr, strlen(jStr) + strlen(job->id) + 2);
	    strcat(jStr, " ");
	    strcat(jStr, job->id);
	}
    }

    return jStr;
}

/**
 * @brief Main function to search in the job structure.
 *
 * @return Returns a pointer to the job or 0 if the
 * job was not found.
 */
static Job_t *findJob(char *id, char *user, pid_t pid, JobState_t state,
		      char *shortId, pid_t logger)
{
    list_t *j;
    list_for_each(j, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);

	if (id && !strcmp(job->id, id)) return job;

	if (user && !strcmp(job->user, user)) {
	    if (!state || state == job->state) return job;
	}

	if (pid > -1 && job->pid == pid) return job;

	if (shortId) {
	    size_t slen1, slen2, slen;
	    char *sjob = strchr(job->id, '.');

	    if (!sjob) continue;
	    slen1 = strlen(job->id) - strlen(sjob);
	    slen2 = strlen(shortId);

	    slen = (slen2 > slen1) ? slen2 : slen1;

	    if (!strncmp(job->id, shortId, slen)) return job;
	}

	if (logger > -1 && logger == job->mpiexec) return job;
    }

    return NULL;
}

Job_t *findJobByShortId(char *shortId)
{
    return findJob(NULL, NULL, -1, 0, shortId, -1);
}

Job_t *findJobById(char *id)
{
    return findJob(id, NULL, -1, 0, NULL, -1);
}

Job_t *findJobByUser(char *user, JobState_t state)
{
    return findJob(NULL, user, -1, state, NULL, -1);
}

Job_t *findJobByPid(pid_t pid)
{
    return findJob(NULL, NULL, pid, 0, NULL, -1);
}

Job_t *findJobByLogger(pid_t logger)
{
    return findJob(NULL, NULL, -1, 0, NULL, logger);
}

Job_t *findJobByCom(ComHandle_t *com, Job_Conn_type_t type)
{
    list_t *j;
    list_for_each(j, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);
	if (findJobConn(job, type, com)) return job;
    }

    return NULL;
}

Job_t *findJobforPID(pid_t pid)
{
    list_t *j;

    if (pid < 0) {
	mlog("%s: got invalid pid %i\n", __func__, pid);
	return NULL;
    }

    list_for_each(j, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);

	/* skip jobs in wrong jobstate */
	if (job->state != JOB_RUNNING) continue;

	if (job->mpiexec == pid) return job;

	if (findJobCookie(job->cookie, pid)) {
	    /* try to find our job cookie in the environment */
	    return job;
	} else if (psAccountIsDescendant(job->pid, pid)) {
	    return job;
	}
    }

    return NULL;
}

char *jobState2String(int state)
{
    switch (state) {
	case JOB_INIT:
	    return "INIT";
	case JOB_QUEUED:
	    return "QUEUED";
	case JOB_PRESTART:
	    return "PRESTART";
	case JOB_RUNNING:
	    return "RUNNING";
	case JOB_PROLOGUE:
	    return "PROLOGUE";
	case JOB_EPILOGUE:
	    return "EPILOGUE";
	case JOB_CANCEL_PROLOGUE:
	    return "CANCEL_PROLOGUE";
	case JOB_CANCEL_EPILOGUE:
	    return "CANCEL_EPILOGUE";
	case JOB_CANCEL_INTERACTIVE:
	    return "CANCEL_INTERACTIVE";
	case JOB_WAIT_OBIT:
	    return "WAIT_OBIT";
	case JOB_EXIT:
	    return "EXIT";
    }
    return NULL;
}

Job_t *addJob(char *jobid, char *server)
{
    char cookie[25], sessid[25];
    char *jobnum;
    Job_t *job = umalloc(sizeof(*job));

    if (!jobid || !server) {
	mlog ("%s: invalid jobid or server\n", __func__);
	return NULL;
    }

    job->id = ustrdup(jobid);
    job->hashname = NULL;
    job->server = ustrdup(server);
    job->jobscript = NULL;
    job->user = NULL;
    job->update = 0;
    job->nrOfNodes = 0;
    job->pid = -1;
    job->sid = -1;
    job->prologueTrack = -1;
    job->epilogueTrack = -1;
    job->prologueExit = 0;
    job->epilogueExit = 0;
    job->state = JOB_INIT;
    job->qsubPort = 0;
    job->jobscriptExit = -1;
    job->mpiexec = -1;
    job->recovered = 0;
    job->recoverTrack = 0;
    job->pwbuf = NULL;
    job->pelogueMonitorId = -1;
    job->pelogueMonStr = NULL;
    job->signalFlag = 0;
    job->nodes = NULL;

    job->res.walltime = 0;
    job->res.r_chour = 0;
    job->res.r_cmin = 0;
    job->res.r_csec = 0;
    job->res.a_chour = 0;
    job->res.a_cmin = 0;
    job->res.a_csec = 0;
    job->res.mem = 0;
    job->res.vmem = 0;

    job->end_time = 0;
    job->start_time = 0;

    job->resDelegate = NULL;

    INIT_LIST_HEAD(&job->data.list);
    INIT_LIST_HEAD(&job->status.list);
    INIT_LIST_HEAD(&job->tasks.list);
    INIT_LIST_HEAD(&job->connections.list);

    /* calc uniq job-cookie */
    snprintf(cookie, sizeof(cookie), "%" PRIu64, (uint64_t) getpid() *
		rand() * time(NULL));
    job->cookie = ustrdup(cookie);

    /* calc uniq session id */
    jobnum = strchr(jobid, '.');
    if (!jobnum) {
	snprintf(sessid, sizeof(sessid), "%10s", cookie);
    } else {
	jobnum[0] = '\0';
	snprintf(sessid, sizeof(sessid), "%s", jobid);
    }
    setEntry(&job->status.list, "session_id", NULL, sessid);
    setEntry(&job->status.list, "resources_used", "mem", "0kb");
    setEntry(&job->status.list, "resources_used", "vmem", "0kb");

    /* add job to job history */
    strncpy(jobHistory[jobHistIndex++], jobid, sizeof(jobHistory[0]));
    if (jobHistIndex >= JOB_HISTORY_SIZE) jobHistIndex = 0;

    list_add_tail(&job->next, &jobList);

    return job;
}

/**
* @brief Create a new task.
*
* New tasks are used to keep track of processes spawn by
* the user via the TM interface.
*
* Not yet fully implemented.
*
* @return No return value.
*/
Task_t *createTask(Job_t *job)
{
    Task_t *task = umalloc(sizeof(*task));

    task->pid = 0;
    task->argc = 0;
    task->event = 0;
    task->nodeNr = 0;
    task->env = NULL;

    list_add_tail(&task->list, &job->tasks.list);

    return task;
}

char *getJobDetail(Data_Entry_t *data, char *name, char *resource)
{
    struct list_head *pos;
    Data_Entry_t *next;

    if (!data || list_empty(&data->list)|| !name) return NULL;

    list_for_each(pos, &data->list) {
	if ((next = list_entry(pos, Data_Entry_t, list)) == NULL) {
	    break;
	}
	if (next->name && !(strcmp(next->name, name))) {
	    if (!resource && !next->resource) return next->value;
	    if (!resource) continue;
	    if ((next->resource && !(strcmp(next->resource, resource))) ||
		(!next->resource && strlen(resource) == 0)) {
		return next->value;
	    }
	}
    }
    return NULL;
}

int getJobDetailGlue(Data_Entry_t *data, char *name, char *buf, int buflen)
{
    struct list_head *pos;
    Data_Entry_t *next;
    int count, point = 0;

    buf[0] = '\0';

    if (!data || list_empty(&data->list)|| !name) return 0;

    list_for_each(pos, &data->list) {
	if ((next = list_entry(pos, Data_Entry_t, list)) == NULL) {
	    break;
	}
	if (next->name && !strcmp(next->name, name)) {
	    count = point + strlen(buf) + strlen(next->name) + 1 +
		strlen(next->value) + 1;
	    if (count > buflen) {
		return 0;
	    }
	    if (point) {
		strcat(buf, ",");
	    } else {
		point = 1;
	    }
	    strcat(buf, next->resource);
	    strcat(buf, "=");
	    strcat(buf, next->value);
	}
    }
    return 1;
}

static void closeAllJobConnections(Job_t *job)
{
    list_t *c, *tmp;
    list_for_each_safe(c, tmp, &job->connections.list) {
	Job_Conn_t *con = list_entry(c, Job_Conn_t, list);
	if (!strcmp(job->id, con->jobid)) closeJobConn(con);
    }
}

static void doDelete(Job_t *job)
{
    /* make sure pelogue timeout monitoring is gone */
    removePELogueTimeout(job);

    /* close all connections associated with this job */
    closeAllJobConnections(job);

    if (job->id) {
	ufree(job->id);
	job->id = NULL;
    }
    if (job->user) {
	ufree(job->user);
	job->user = NULL;
    }
    if (job->hashname) {
	ufree(job->hashname);
	job->hashname = NULL;
    }
    if (job->server) {
	ufree(job->server);
	job->server = NULL;
    }
    if (job->jobscript) {
	ufree(job->jobscript);
	job->jobscript = NULL;
    }
    if (job->cookie) {
	ufree(job->cookie);
	job->cookie = NULL;
    }
    if (job->pwbuf) ufree(job->pwbuf);
    if (job->nodes) ufree(job->nodes);

    if (job->resDelegate) {
	list_t *t;
	if (job->resDelegate->partition) {
	    send_TASKDEAD(job->resDelegate->tid);
	}
	/* Cleanup all references to this delegate */
	list_for_each(t, &managedTasks) {
	    PStask_t *task = list_entry(t, PStask_t, next);
	    if (task->delegate == job->resDelegate) task->delegate = NULL;
	}
	job->resDelegate->deleted = true;
    }

    clearDataList(&job->status.list);
    clearDataList(&job->data.list);

    /* delete tasks when TM interface is correct implemented
     * and tasks are possible */

    list_del(&job->next);
    ufree(job);
}

bool deleteJob(char *jobid)
{
    Job_t *job = findJobById(jobid);

    if (!job) return false;

    doDelete(job);
    return true;
}

void clearJobList()
{
    list_t *j, *tmp;
    list_for_each_safe(j, tmp, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);
	doDelete(job);
    }
}

Job_Node_List_t *findJobNodeEntry(Job_t *job, PSnodes_ID_t id)
{
    int i;

    if (!job->nodes) return NULL;

    for (i=0; i<job->nrOfUniqueNodes; i++) {
	if (job->nodes[i].id == id) return &job->nodes[i];
    }
    return NULL;
}

int setNodeInfos(Job_t *job)
{
    char *nodeStr, *next, *toksave, *value, *tmp;
    const char delimiter[] = "+\0";
    int nodeCount = 0, uniqueNodeCount = 0;
    int i, nrOfNodes = PSC_getNrOfNodes(), x = 0;
    bool *allNodes;
    PSnodes_ID_t nextNodeID;

    if (!(tmp = getJobDetail(&job->data, "exec_host", ""))) {
	mlog("%s: exec_host not found\n", __func__);
	return 0;
    }
    nodeStr = ustrdup(tmp);

    /* init tracking array */
    allNodes = umalloc(sizeof(bool *) * (nrOfNodes + 1) +
	sizeof(bool) * nrOfNodes);
    for (i=0; i<nrOfNodes; i++) { allNodes[i] = false; };

    next = strtok_r(nodeStr, delimiter, &toksave);
    while (next) {
	if ((value = strchr(next,'/'))) {
	    value[0] = '\0';
	    if ((nextNodeID = getNodeIDbyName(next)) == -1) {
		mlog("%s: getting id for node '%s' failed\n", __func__, next);
	    }
	    if (!allNodes[nextNodeID]) {
		allNodes[nextNodeID] = true;
		uniqueNodeCount++;
	    }
	    nodeCount++;
	}
	next = strtok_r(NULL, delimiter, &toksave);
    }

    job->nodes = umalloc(sizeof(Job_Node_List_t *) * uniqueNodeCount +
			    sizeof(Job_Node_List_t) * uniqueNodeCount);

    /* set the uniq nodelist for the job  */
    for (i=0; i<nrOfNodes; i++) {
	if (allNodes[i] == 1) {
	    job->nodes[x].id = i;
	    job->nodes[x].prologue = -1;
	    job->nodes[x].epilogue = -1;
	    x++;
	}
    }

    ufree(nodeStr);
    ufree(allNodes);

    if (nodeCount <= 0 || uniqueNodeCount <= 0) {
	mlog("%s: invalid nodelist from server: nodeCount '%i' "
		"uniqNodeCount '%i'\n", __func__, nodeCount, uniqueNodeCount);
	return 0;
    }

    job->nrOfNodes = nodeCount;
    job->nrOfUniqueNodes = uniqueNodeCount;

    stat_numNodes += uniqueNodeCount;
    stat_numProcs += nodeCount;

    return 1;
}

Job_Conn_t *addJobConn(Job_t *job, ComHandle_t *com, Job_Conn_type_t type)
{
    Job_Conn_t *con = umalloc(sizeof(*con));

    con->com = com;
    con->sock = com->socket;
    con->cType = com->type;
    con->type = type;
    con->comForward = NULL;
    con->sockForward = -1;
    con->jobid = ustrdup(job->id);

    if (!com->jobid) com->jobid = ustrdup(job->id);

    list_add_tail(&con->list, &job->connections.list);

    return con;
}

void addJobConnF(Job_Conn_t *con, ComHandle_t *com)
{
    con->comForward = com;
    con->sockForward = com->socket;
    con->cfType = com->type;

    if (!com->jobid) com->jobid = ustrdup(con->com->jobid);
}

ComHandle_t *getJobCom(Job_t *job, Job_Conn_type_t type)
{
    Job_Conn_t *con = findJobConn(job, type, NULL);

    if (!con) return NULL;

    if (!isValidComHandle(con->com)) {
	mlog("%s: removing invalid job con handle\n", __func__);
	closeJobConn(con);
	return NULL;
    }

    return con->com;
}

Job_Conn_t *getJobConn(Job_t *job, Job_Conn_type_t type)
{
    return findJobConn(job, type, NULL);
}

Job_Conn_t *getJobConnByCom(ComHandle_t *com, Job_Conn_type_t type)
{
    list_t *j;
    list_for_each(j, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);
	Job_Conn_t *con = findJobConn(job, type, com);
	if (con) return con;
    }

    return NULL;
}

Job_Conn_t *findJobConn(Job_t *job, Job_Conn_type_t type, ComHandle_t *com)
{
    list_t *c, *tmp;
    list_for_each_safe(c, tmp, &job->connections.list) {
	Job_Conn_t *con = list_entry(c, Job_Conn_t, list);

	if (!isValidComHandle(con->com)) {
	    mlog("%s: removing invalid con handle\n", __func__);
	    list_del(&con->list);
	    ufree(con);
	    continue;
	}
	if (!con->jobid) continue;
	if (!!strcmp(con->jobid, job->id)) continue;

	if (com == NULL && con->type == type) return con;
	if (con->com == com && con->type == type) return con;
	if (con->comForward == com && con->type == type) return con;
    }
    return NULL;
}

void closeJobConn(Job_Conn_t *con)
{
    if (con->jobid) ufree(con->jobid);

    list_del(&con->list);
    ufree(con);
}

int findJobCookie(char *cookie, pid_t pid)
{
    char buf[200];
    char *ptr, *line = NULL;
    FILE *fd;
    size_t len = 0;
    int found = 0;

    if (!cookie) {
	mlog("%s: invalid job cookie\n", __func__);
	return 0;
    }

    snprintf(buf, sizeof(buf), "/proc/%i/environ", pid);

    if ((fd = fopen(buf,"r")) == NULL) {
	mlog("%s: open '%s' failed\n", __func__, buf);
	return 0;
    }

    while (getdelim(&line, &len, '\0', fd) != -1) {
	if (!strncmp("PBS_JOBCOOKIE=", line, 14)) {
	    ptr = line + 14;
	    if (!strcmp(ptr, cookie)) {
		found = 1;
		break;
	    }
	    /*
	    mlog("%s: job cookie invalid, user: '%s' psmom: '%s'\n", __func__,
		ptr, cookie);
	    */
	    break;
	}
    }

    if (line) ufree(line);
    fclose(fd);
    if (found) return 1;
    return 0;
}

int isJobIDinHistory(char *jobid)
{
    int i;

    for (i=0; i<JOB_HISTORY_SIZE; i++) {
	if (!(strncmp(jobid, jobHistory[i], JOB_HISTORY_ID_LEN))) return 1;
    }
    return 0;
}

int hasRunningJobs(char *user)
{
    /* search in normal jobs */
    if ((findJobByUser(user, JOB_RUNNING))) {
	//mlog("%s: user has running local job\n", __func__);
	return 1;
    }

    /* search in remote jobs */
    if ((findJobInfoByUser(user))) {
	//mlog("%s: user has running remote job\n", __func__);
	return 1;
    }

    return 0;
}

char *listJobs(char *buf, size_t *bufSize)
{
    char line[256];
    list_t *j;

    if (list_empty(&jobList)) {
	return str2Buf("\nCurrently no jobs.\n", &buf, bufSize);
    }

    snprintf(line, sizeof(line), "\n%26s %8s %8s %6s %10s %10s %20s %20s\n",
	     "JobId", "State", "Procs", "Nodes", "User", "TaskID",
	     "Startttime", "Timeout");
    str2Buf(line, &buf, bufSize);

    list_for_each(j, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);
	char start[50], timeout[32], logger[15], procs[8], nodes[6];
	struct tm *ts;
	long secTimeout;

	/* format start time */
	ts = localtime(&job->start_time);
	strftime(start, sizeof(start), "%Y-%m-%d %H:%M:%S", ts);

	/* format timeout */
	secTimeout = stringTimeToSec(getJobDetail(&job->data, "Resource_List",
		    "walltime"));

	formatTimeout(job->start_time, secTimeout, timeout, sizeof(timeout));

	if (job->mpiexec == -1) {
	    strncpy(logger, "-", sizeof(logger));
	} else {
	    snprintf(logger, sizeof(logger), "[%i:%i]", PSC_getID(-1),
			job->mpiexec);
	}

	snprintf(procs, sizeof(procs), "%i", job->nrOfNodes);
	snprintf(nodes, sizeof(nodes), "%i", job->nrOfUniqueNodes);

	snprintf(line, sizeof(line), "%26s %8s %8s %6s %10s %10s %20s %20s\n",
		 job->id, jobState2String(job->state), procs, nodes, job->user,
		 logger, start, timeout);

	str2Buf(line, &buf, bufSize);
    }

    return buf;
}

bool showAllowedJobPid(pid_t pid, pid_t sid, PStask_ID_t psAccLogger,
		       char **reason)
{
    list_t *j;
    list_for_each(j, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);

	/* skip jobs in wrong jobstate */
	if (job->state == JOB_CANCEL_PROLOGUE ||
	    job->state == JOB_CANCEL_EPILOGUE ||
	    job->state == JOB_CANCEL_INTERACTIVE ||
	    job->state == JOB_WAIT_OBIT ||
	    job->state == JOB_QUEUED) continue;

	/* is a child of the jobscript? */
	if (job->pid == pid) {
	    *reason = "LOCAL_PID";
	    return true;
	}

	/* try sid of jobscript child */
	if (sid > 0 && job->sid == sid) {
	    *reason = "LOCAL_SID";
	    return true;
	}

	/* check if the child is from a known logger */
	if (psAccLogger == PSC_getTID(-1, job->mpiexec)) {
	    *reason = "LOCAL_LOGGER";
	    return true;
	}

	/* try to find the job cookie in the environment */
	if (findJobCookie(job->cookie, pid)) {
	    *reason = "LOCAL_ENV";
	    return true;
	}
    }

    return false;
}

void cleanJobByNode(PSnodes_ID_t id)
{
    const char *hname = getHostnameByNodeId(id);
    list_t *j, *tmp;
    list_for_each_safe(j, tmp, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);
	int i;

	if (job->state != JOB_PROLOGUE && job->state != JOB_EPILOGUE
	    && job->state != JOB_RUNNING  && job->state != JOB_CANCEL_PROLOGUE
	    && job->state != JOB_CANCEL_EPILOGUE) continue;

	for (i=0; i<job->nrOfUniqueNodes; i++) {
	    if (job->nodes[i].id != id) continue;

	    mlog("%s: node %s(%i) died: job '%s' jstate '%s' affected \n",
		 __func__, hname, id, job->id, jobState2String(job->state));

	    /* tell the PBS server that the node is down */
	    if (hname) setPBSNodeState(job->server, NULL, "down", hname);

	    if (job->state == JOB_PROLOGUE ||
		job->state == JOB_EPILOGUE ||
		job->state == JOB_CANCEL_PROLOGUE ||
		job->state == JOB_CANCEL_EPILOGUE) {

		/* stop pelogue scripts on all nodes */
		signalPElogue(job, "SIGTERM", "node down");
		stopPElogueExecution(job);
	    } else if (job->state == JOB_RUNNING) {
		char *ft = getJobDetail(&job->data, "fault_tolerant", NULL);
		if (ft && (!strcmp(ft, "True") || !strcmp(ft, "true"))) {
		    continue;
		} else {
		    /* kill the job */
		    mlog("%s: job '%s' is not fault tolerant, killing job\n",
			 __func__, job->id);
		    signalJob(job, SIGTERM, "node down");
		    signalJob(job, SIGKILL, "node down");
		}
	    }
	}
    }
}

bool signalAllJobs(int signal, char *reason)
{
    bool ret = true;
    list_t *j;
    list_for_each(j, &jobList) {
	Job_t *job = list_entry(j, Job_t, next);
	bool tmp = signalJob(job, signal, reason);
	if (!tmp) ret = tmp;
    }

    return ret;
}
