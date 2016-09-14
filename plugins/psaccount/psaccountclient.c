/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdlib.h>
#include <stdio.h>

#include "pluginmalloc.h"

#include "psaccount.h"
#include "psaccountlog.h"
#include "psaccountproc.h"
#include "psaccountconfig.h"
#include "psaccountcomm.h"

#include "psaccountclient.h"

#define MAX_JOBS_PER_NODE 1024

static LIST_HEAD(clientList);

/* flag to control the global collect mode */
bool globalCollectMode = false;

static const char* clientType2Str(PS_Acct_job_types_t type)
{
    switch(type) {
    case ACC_CHILD_JOBSCRIPT:
	return "JOBSCRIPT";
    case ACC_CHILD_PSIDCHILD:
	return "PSIDCHILD";
    case ACC_CHILD_REMOTE:
	return "REMOTE";
    default:
	return "UNKOWN";
    }
}

Client_t *findClientByTID(PStask_ID_t clientTID)
{
    list_t *pos;
    list_for_each(pos, &clientList) {
	Client_t *client = list_entry(pos, Client_t, next);
	if (client->taskid == clientTID) return client;
    }
    return NULL;
}

Client_t *findClientByPID(pid_t clientPID)
{
    list_t *pos;
    list_for_each(pos, &clientList) {
	Client_t *client = list_entry(pos, Client_t, next);
	if (client->pid == clientPID) return client;
    }
    return NULL;
}

/**
 * @brief Try to find the jobscript for a logger.
 *
 * The logger (mpiexec) process must be a child of a jobscript
 * which was started by the psmom. This functions tries to find
 * the correct jobscript for a logger using the /proc filesystem
 * parent-child realtions.
 *
 * @param The logger TaskID to find the jobscript for.
 *
 * @return On success the found jobscript is returned. On error
 * NULL is returned.
 */
static Client_t *findJobscriptByLogger(PStask_ID_t logger)
{
    list_t *pos;
    list_for_each(pos, &clientList) {
	Client_t *client = list_entry(pos, Client_t, next);

	if (client->type == ACC_CHILD_JOBSCRIPT) {
	    /* check if the jobscript is a parent of logger */
	    if ((isDescendant(client->pid, PSC_getPID(logger)))) {
		client->logger = logger;
		return client;
	    } else {
		/*
		mlog("%s: js %i not parent of logger %i\n", __func__,
			jobscript->pid, PSC_getPID(logger));
		*/
	    }
	}
    }
    return NULL;
}

Client_t *findJobscriptInClients(Job_t *job)
{
    list_t *pos;
    list_for_each(pos, &clientList) {
	Client_t *client = list_entry(pos, Client_t, next);

	if (client->job == job && client->type == ACC_CHILD_PSIDCHILD) {
	    Client_t *js = findJobscriptByLogger(client->logger);
	    if (js) return js;
	}
    }
    return NULL;
}

/************************** Data collection **************************/

/** System's clock granularity. Set via @ref setClockTicks(). */
static int clockTicks = -1;

/**
 * @brief Update clients accounting data
 *
 * Fetch accounting data for the client @a client from the current
 * snapshot of the /proc filesystem.
 *
 * @param client The client for which to update the accounting data
 *
 * @return No return value
 */
static void updateAccountData(Client_t *client)
{
    unsigned long rssnew, vsizenew;
    uint64_t cutime, cstime;
    AccountDataExt_t *accData;
    ProcSnapshot_t *proc = findProcSnapshot(client->pid), pChildren;
    ProcIO_t procIO;
    uint64_t cputime, maxRssMB = 0;
    int64_t diffCputime;

    if (!client->doAccounting) return;

    if (!proc) {
	client->doAccounting = false;
	client->endTime = time(NULL);
	return;
    }

    accData = &client->data;
    if (!accData->session) accData->session = proc->session;
    if (!accData->pgroup) accData->pgroup = proc->pgrp;

    /* get infos for all children  */
    getDescendantData(client->pid, &pChildren);
    rssnew = proc->mem + pChildren.mem;
    vsizenew = proc->vmem + pChildren.vmem;

    /* save cutime and cstime in seconds */
    cutime = (proc->cutime + pChildren.cutime) / clockTicks;
    cstime = (proc->cstime + pChildren.cstime) / clockTicks;

    /* set rss (resident set size) */
    if (rssnew > accData->maxRss) accData->maxRss = rssnew;
    accData->avgRssTotal += rssnew;
    accData->avgRssCount++;

    /* set virtual mem */
    if (vsizenew > accData->maxVsize) accData->maxVsize = vsizenew;
    accData->avgVsizeTotal += vsizenew;
    accData->avgVsizeCount++;

    /* set threads */
    if (proc->threads > accData->maxThreads) {
	accData->maxThreads = proc->threads;
    }
    accData->avgThreadsTotal += proc->threads;
    accData->avgThreadsCount++;

    /* set cutime and cstime */
    cputime = cutime + cstime;
    diffCputime = cputime - (accData->cutime + accData->cstime);
    if (cutime > accData->cutime) accData->cutime = cutime;
    if (cstime > accData->cstime) accData->cstime = cstime;

    /* set major page faults */
    if (proc->majflt > accData->totMajflt) accData->totMajflt = proc->majflt;

    /* read IO statistics */
    readProcIO(client->pid, &procIO);

    /* set total disc read/write */
    if (procIO.diskRead > accData->totDiskRead) {
	accData->totDiskRead = procIO.diskRead;
    }
    if (procIO.diskWrite > accData->totDiskWrite) {
	accData->totDiskWrite = procIO.diskWrite;
    }

    /* set readBytes/writeBytes */
    if (procIO.readBytes > accData->readBytes) {
	accData->readBytes = procIO.readBytes;
    }
    if (procIO.writeBytes > accData->writeBytes) {
	accData->writeBytes = procIO.writeBytes;
    }

    /* calc cpu freq */
    if (diffCputime > 0) {
	accData->cpuWeight = accData->cpuWeight +
	    getCpuFreq(proc->cpu) * diffCputime;
	if (cputime) {
	    accData->cpuFreq = accData->cpuWeight / cputime;
	}
    }
    if (!cputime) accData->cpuFreq = getCpuFreq(proc->cpu);

    maxRssMB = (accData->maxRss * (pageSize / (1024)) / 1024);

    mdbg(PSACC_LOG_COLLECT, "%s: tid '%s' rank '%i' cutime: '%lu' cstime: '%lu'"
	 " session '%i' mem '%lu MB' vmem '%lu MB' threads '%lu' "
	 "majflt '%lu' cpu '%u' cpuFreq '%lu'\n", __func__,
	 PSC_printTID(client->taskid),
	 client->rank, accData->cutime, accData->cstime, accData->session,
	 maxRssMB, accData->maxVsize / (1024 * 1024), accData->maxThreads,
	 accData->totMajflt, proc->cpu, accData->cpuFreq);
}

/************************* Data aggregations *************************/

/**
 * @brief Add client data to data aggregation
 *
 * Add each data item of the client @a client to the corresponding
 * item of the data aggregation @a aggData and store the results into
 * @a aggData.
 *
 * @param srcData Data aggregation to be added
 *
 * @param destData Data aggregation acting as the accumulator
 *
 * @return No return value
 */
void addClientToAggData(Client_t *client, AccountDataExt_t *aggData)
{
    uint64_t maxRss, maxVsize, tmp;
    double dtmp;

    if (!client->data.pageSize) client->data.pageSize = pageSize;

    maxRss =  client->data.maxRss * (client->data.pageSize / 1024);
    maxVsize = client->data.maxVsize / 1024;

    /* sum up for maxima totals */
    aggData->maxThreads += client->data.maxThreads;
    aggData->maxRssTotal += maxRss;
    aggData->maxVsizeTotal += maxVsize;

    /* maxima per client */
    if (aggData->maxThreads < client->data.maxThreads) {
	aggData->maxThreads = client->data.maxThreads;
    }

    if (aggData->maxRss < maxRss) {
	aggData->maxRss = maxRss;
	aggData->taskIds[ACCID_MAX_RSS] = client->taskid;
    }

    if (aggData->maxVsize < maxVsize) {
	aggData->maxVsize = maxVsize;
	aggData->taskIds[ACCID_MAX_VSIZE] = client->taskid;
    }

    /* calculate averages per client if data available */
    if (client->data.avgThreadsCount > 0) {
	aggData->avgThreadsTotal +=
	    client->data.avgThreadsTotal / client->data.avgThreadsCount;
	aggData->avgThreadsCount++;
    }
    if (client->data.avgVsizeCount > 0) {
	aggData->avgVsizeTotal +=
	    client->data.avgVsizeTotal / (client->data.avgVsizeCount * 1024);
	aggData->avgVsizeCount++;
    }
    if (client->data.avgRssCount > 0) {
	aggData->avgRssTotal +=
			(client->data.avgRssTotal * client->data.pageSize)
			    / (client->data.avgRssCount * 1024);
	aggData->avgRssCount++;
    }

    aggData->cutime += client->data.cutime;
    aggData->cstime += client->data.cstime;

    aggData->cputime += client->data.cputime;
    aggData->pageSize = client->data.pageSize;

    aggData->rusage.ru_utime.tv_sec += client->data.rusage.ru_utime.tv_sec;
    aggData->rusage.ru_utime.tv_usec += client->data.rusage.ru_utime.tv_usec;
    aggData->rusage.ru_stime.tv_sec += client->data.rusage.ru_stime.tv_sec;
    aggData->rusage.ru_stime.tv_usec += client->data.rusage.ru_stime.tv_usec;

    /* min cputime */
    tmp = client->data.rusage.ru_utime.tv_sec +
		client->data.rusage.ru_stime.tv_sec;
    if (!aggData->numTasks) {
	aggData->minCputime = tmp;
	aggData->taskIds[ACCID_MIN_CPU] = client->taskid;
    } else if (aggData->minCputime > tmp) {
	aggData->minCputime = tmp;
	aggData->taskIds[ACCID_MIN_CPU] = client->taskid;
    }

    /* total cputime */
    aggData->totCputime +=
		client->data.rusage.ru_utime.tv_sec +
		client->data.rusage.ru_stime.tv_sec;

    /* major page faults */
    aggData->totMajflt += client->data.totMajflt;
    if (client->data.totMajflt > aggData->maxMajflt) {
	aggData->maxMajflt = client->data.totMajflt;
	aggData->taskIds[ACCID_MAX_PAGES] = client->taskid;
    }

    /* disc read */
    dtmp = (double) client->data.totDiskRead / (double)1048576;
    aggData->totDiskRead += dtmp;
    if (dtmp > aggData->maxDiskRead) {
	aggData->maxDiskRead = dtmp;
	aggData->taskIds[ACCID_MAX_DISKREAD] = client->taskid;
    }

    /* disc write */
    dtmp = (double) client->data.totDiskWrite / (double)1048576;
    aggData->totDiskWrite += dtmp;
    if (dtmp > aggData->maxDiskWrite) {
	aggData->maxDiskWrite = dtmp;
	aggData->taskIds[ACCID_MAX_DISKWRITE] = client->taskid;
    }

    aggData->numTasks++;

    /* cpu freq */
    aggData->cpuFreq += client->data.cpuFreq;

    mdbg(PSACC_LOG_AGGREGATE, "%s: client '%s' maxThreads '%lu' maxVsize '%lu' "
	    "maxRss '%lu' cutime '%lu' cstime '%lu' cputime '%lu' avg cpuFreq "
	    "'%.2fG'\n", __func__, PSC_printTID(client->taskid),
	    client->data.maxThreads, client->data.maxVsize,
	    client->data.maxRss, client->data.cutime, client->data.cstime,
	    client->data.cputime,
	    ((double) aggData->cpuFreq / (double) aggData->numTasks) /
	    (double) 1048576);
}

/**
 * @brief Add two data aggregations
 *
 * Create the sum of each data item of the two data aggregations @a
 * srcData and @a destData and store the results into @a destData.
 *
 * @param srcData Data aggregation to be added
 *
 * @param destData Data aggregation acting as the accumulator
 *
 * @return No return value
 */
static void addAggData(AccountDataExt_t *srcData, AccountDataExt_t *destData)
{
    /* sum up for maxima totals */
    destData->maxThreadsTotal += srcData->maxThreadsTotal;
    destData->maxRssTotal += srcData->maxRssTotal;
    destData->maxVsizeTotal += srcData->maxVsizeTotal;

    /* calculate averages per client if data available */
    if (srcData->avgThreadsCount > 0) {
	destData->avgThreadsTotal += srcData->avgThreadsTotal;
	destData->avgThreadsCount += srcData->numTasks;
    }
    if (srcData->avgVsizeCount > 0) {
	destData->avgVsizeTotal += srcData->avgVsizeTotal;
	destData->avgVsizeCount += srcData->numTasks;
    }
    if (srcData->avgRssCount > 0) {
	destData->avgRssTotal += srcData->avgRssTotal;
	destData->avgRssCount += srcData->numTasks;
    }

    /* max threads */
    if (destData->maxThreads < srcData->maxThreads) {
	destData->maxThreads = srcData->maxThreads;
    }

    /* max rss */
    if (destData->maxRss < srcData->maxRss) {
	destData->maxRss = srcData->maxRss;
	destData->taskIds[ACCID_MAX_RSS] = srcData->taskIds[ACCID_MAX_RSS];
    }

    /* max vsize */
    if (destData->maxVsize < srcData->maxVsize) {
	destData->maxVsize = srcData->maxVsize;
	destData->taskIds[ACCID_MAX_VSIZE] = srcData->taskIds[ACCID_MAX_VSIZE];
    }

    /* major page faults */
    destData->totMajflt += srcData->totMajflt;
    if (srcData->totMajflt > destData->maxMajflt) {
	destData->maxMajflt = srcData->totMajflt;
	destData->taskIds[ACCID_MAX_PAGES] = srcData->taskIds[ACCID_MAX_PAGES];
    }

    /* disc read */
    destData->totDiskRead += srcData->totDiskRead;
    if (srcData->totDiskRead > destData->maxDiskRead) {
	destData->maxDiskRead = srcData->totDiskRead;
	destData->taskIds[ACCID_MAX_DISKREAD] =
			srcData->taskIds[ACCID_MAX_DISKREAD];
    }

    /* disc write */
    destData->totDiskWrite += srcData->totDiskWrite;
    if (srcData->totDiskWrite > destData->maxDiskWrite) {
	destData->maxDiskWrite = srcData->totDiskWrite;
	destData->taskIds[ACCID_MAX_DISKWRITE] =
			srcData->taskIds[ACCID_MAX_DISKWRITE];
    }

    destData->cutime += srcData->cutime;
    destData->cstime += srcData->cstime;
    destData->cputime += srcData->cputime;

    destData->rusage.ru_utime.tv_sec += srcData->rusage.ru_utime.tv_sec;
    destData->rusage.ru_utime.tv_usec += srcData->rusage.ru_utime.tv_usec;
    destData->rusage.ru_stime.tv_sec += srcData->rusage.ru_stime.tv_sec;
    destData->rusage.ru_stime.tv_usec += srcData->rusage.ru_stime.tv_usec;

    /* min cputime */
    if (!destData->numTasks) {
	destData->minCputime = srcData->minCputime;
	destData->taskIds[ACCID_MIN_CPU] = srcData->taskIds[ACCID_MIN_CPU];
    } else if (destData->minCputime > srcData->minCputime) {
	destData->minCputime = srcData->minCputime;
	destData->taskIds[ACCID_MIN_CPU] = srcData->taskIds[ACCID_MIN_CPU];
    }

    /* total cputime */
    destData->totCputime += srcData->totCputime;

    /* cpu freq */
    destData->cpuFreq += srcData->cpuFreq;

    destData->numTasks += srcData->numTasks;
    destData->pageSize = srcData->pageSize;

    mdbg(PSACC_LOG_AGGREGATE, "%s: maxThreads '%lu' maxVsize '%lu' "
	 "maxRss '%lu' cutime '%lu' cstime '%lu' cputime '%lu' avg cpuFreq "
	 "'%.2fG'\n", __func__, srcData->maxThreads, srcData->maxVsize,
	 srcData->maxRss, srcData->cutime, srcData->cstime, srcData->cputime,
	 ((double)destData->cpuFreq / destData->numTasks) / (1024*1024));
}

void setAggData(PStask_ID_t tid, PStask_ID_t logger, AccountDataExt_t *data)
{
    Client_t *client;
    bool found = false;
    list_t *pos;

    list_for_each(pos, &clientList) {
	client = list_entry(pos, Client_t, next);
	if (client->taskid == tid &&  client->logger == logger) {
	    found = true;
	    break;
	}
    }

    if (!found) {
	client = addClient(tid, ACC_CHILD_REMOTE);
	client->logger = logger;
	client->doAccounting = false;
    }

    memcpy(&client->data, data, sizeof(client->data));
}

void finishAggData(PStask_ID_t tid, PStask_ID_t logger)
{
    list_t *pos;
    list_for_each(pos, &clientList) {
	Client_t *client = list_entry(pos, Client_t, next);
	if (client->taskid == tid && client->logger == logger) {
	    client->endTime = time(NULL);
	    break;
	}
    }
}

void getPidsByLogger(PStask_ID_t logger, pid_t **pids, uint32_t *count)
{
    list_t *pos;
    uint32_t index = 0;

    *count = 0;
    *pids = NULL;

    list_for_each(pos, &clientList) {
	Client_t *client = list_entry(pos, Client_t, next);
	if (client->logger == logger && client->type == ACC_CHILD_PSIDCHILD) {
	    (*count)++;
	}
    }

    if (! *count) return;

    *pids = umalloc(sizeof(pid_t) * *count);

    list_for_each(pos, &clientList) {
	Client_t *client = list_entry(pos, Client_t, next);
	if (client->logger == logger && client->type == ACC_CHILD_PSIDCHILD) {
	    if (index == *count) break;
	    (*pids)[index++] = client->pid;
	}
    }
}

PStask_ID_t getLoggerByClientPID(pid_t pid)
{
    ProcStat_t pS;
    bool psOK = false;
    list_t *pos;

    if (list_empty(&clientList)) return -1;

    if (readProcStatInfo(pid, &pS)) {
	psOK = true;
    }

    /* try to find the pid in the acc children */
    list_for_each(pos, &clientList) {
	Client_t *client = list_entry(pos, Client_t, next);

	/* try pid */
	if (client->pid == pid) return client->logger;

	if (!psOK) continue;

	/* try sid */
	if (client->data.session && client->data.session == pS.session) {
	    return client->logger;
	}

	/* try pgroup */
	if (client->data.pgroup && client->data.pgroup == pS.pgrp) {
	    return client->logger;
	}
    }

    /* try all grand-children now */
    list_for_each(pos, &clientList) {
	Client_t *client = list_entry(pos, Client_t, next);
	if (isDescendant(client->pid, pid)) return client->logger;
    }

    return -1;
}

bool aggregateDataByLogger(PStask_ID_t logger, AccountDataExt_t *accData)
{
    int res = false;
    list_t *pos;

    list_for_each(pos, &clientList) {
	Client_t *client = list_entry(pos, Client_t, next);
	if (client->logger == logger && client->type != ACC_CHILD_JOBSCRIPT) {
	    if (client->type == ACC_CHILD_PSIDCHILD) {
		addClientToAggData(client, accData);
	    } else if (client->type == ACC_CHILD_REMOTE) {
		addAggData(&client->data, accData);
	    }
	    res = true;
	}
    }
    return res;
}

Client_t *addClient(PStask_ID_t taskID, PS_Acct_job_types_t type)
{
    Client_t *client = umalloc(sizeof(*client));

    client->taskid = taskID;
    client->pid = PSC_getPID(taskID);
    client->status = -1;
    client->logger = -1;
    client->doAccounting = 1;
    client->type = type;
    client->job = NULL;
    client->jobid = NULL;
    client->rank = -1;
    client->uid = 0;
    client->gid = 0;
    client->startTime = time(NULL);
    client->endTime = 0;

    memset(&client->data, 0, sizeof(client->data));
    client->data.numTasks = 1;

    list_add_tail(&client->next, &clientList);

    return client;
}

static void doDeleteClient(Client_t *client)
{
    if (!client) return;

    list_del(&client->next);

    if (client->jobid) ufree(client->jobid);
    ufree(client);
}

bool deleteClient(PStask_ID_t tid)
{
    Client_t *client = findClientByTID(tid);
    if (!client) return false;

    doDeleteClient(client);
    return true;
}

void deleteClientsByLogger(PStask_ID_t loggerTID)
{
    list_t *pos, *tmp;
    list_for_each_safe(pos, tmp, &clientList) {
	Client_t *client = list_entry(pos, Client_t, next);
	if (client->logger == loggerTID) doDeleteClient(client);
    }
}

bool haveActiveClients(void)
{
    list_t *pos;
    list_for_each(pos, &clientList) {
	Client_t *client = list_entry(pos, Client_t, next);
	if (client->doAccounting) return true;
    }
    return false;
}

void clearAllClients(void)
{
    list_t *pos, *tmp;
    list_for_each_safe(pos, tmp, &clientList) {
	Client_t *client = list_entry(pos, Client_t, next);
	doDeleteClient(client);
    }
}

void cleanupClients(void)
{
    list_t *pos, *tmp;
    time_t now = time(NULL);
    int grace = getConfValueI(&config, "TIME_CLIENT_GRACE");

    list_for_each_safe(pos, tmp, &clientList) {
	Client_t *client = list_entry(pos, Client_t, next);

	if (client->doAccounting || !client->endTime) continue;
	if (findJobByLogger(client->logger)) continue;

	/* check timeout */
	if (client->endTime + grace * 60 <= now) {
	    mdbg(PSACC_LOG_VERBOSE, "%s: cleanup client '%i'\n", __func__,
		    client->pid);
	    doDeleteClient(client);
	}
    }
}

void forwardAggData(void)
{
    AccountDataExt_t aggData;
    PStask_ID_t loggerTIDs[MAX_JOBS_PER_NODE];
    list_t *pos;
    int i, numJobs = 0;

    /* extract uniq logger TIDs */
    list_for_each(pos, &clientList) {
	Client_t *client = list_entry(pos, Client_t, next);
	if (client->doAccounting) {
	    for (i=0; i<numJobs; i++) {
		if (loggerTIDs[i] == client->logger) break;
	    }
	    if (i == numJobs) loggerTIDs[numJobs++] = client->logger;
	    if (numJobs == MAX_JOBS_PER_NODE) {
		mlog("%s: MAX_JOBS_PER_NODE exceeded. Truncating aggregation\n",
		     __func__);
		break;
	    }
	}
    }

    for (i=0; i<numJobs; i++) {
	if (PSC_getID(loggerTIDs[i]) == PSC_getMyID()) continue;

	/* aggreagate accounting data on a per logger basis */
	memset(&aggData, 0, sizeof(AccountDataExt_t));
	list_for_each(pos, &clientList) {
	    Client_t *client = list_entry(pos, Client_t, next);
	    if (client->logger == loggerTIDs[i] && client->doAccounting) {
		addClientToAggData(client, &aggData);
	    }
	}

	/* send the update */
	if (aggData.numTasks) sendAggData(loggerTIDs[i], &aggData);
    }
}

void updateClients(Job_t *job)
{
    static int updateCount = 0;
    list_t *pos;

    list_for_each(pos, &clientList) {
	Client_t *client = list_entry(pos, Client_t, next);
	if (!client->doAccounting) continue;
	if (!job || client->job == job) updateAccountData(client);
    }

    if (globalCollectMode) {
	int forwInterval = getConfValueI(&config, "FORWARD_INTERVAL");

	if (++updateCount >= forwInterval) {
	    forwardAggData();
	    updateCount = 0;
	}
    }
}

void switchClientUpdate(PStask_ID_t clientTID, bool enable)
{
    Client_t *client = findClientByTID(clientTID);
    if (client) {
	mdbg(PSACC_LOG_ACC_SWITCH, "%s: %s accounting for '%s'\n", __func__,
	     (enable) ? "enable" : "disable", PSC_printTID(clientTID));
	client->doAccounting = enable;
    }
}

char *listClients(char *buf, size_t *bufSize, bool detailed)
{
    char line[160];
    list_t *pos;

    if (list_empty(&clientList)) {
	return str2Buf("\nNo current clients.\n", &buf, bufSize);
    }

    str2Buf("\nclients:\n", &buf, bufSize);

    list_for_each(pos, &clientList) {
	Client_t *c = list_entry(pos, Client_t, next);

	snprintf(line, sizeof(line), "taskID '%s'\n", PSC_printTID(c->taskid));
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "rank '%i'\n", c->rank);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "logger '%s'\n", PSC_printTID(c->logger));
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "account '%i'\n", c->doAccounting);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "type '%s'\n", clientType2Str(c->type));
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "uid '%i'\n", c->uid);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "gid '%i'\n", c->gid);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "page size '%zu'\n", c->data.pageSize);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "start time %s", ctime(&c->startTime));
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "end time %s",
		 c->endTime ? ctime(&c->endTime) : "-\n");
	str2Buf(line, &buf, bufSize);

	if (detailed) {
	    snprintf(line, sizeof(line), "max mem '%zu'\n",
		     c->data.maxRss * pageSize);
	    str2Buf(line, &buf, bufSize);

	    snprintf(line, sizeof(line), "max vmem '%zu'\n",
		     c->data.maxVsize);
	    str2Buf(line, &buf, bufSize);

	    snprintf(line, sizeof(line), "cutime '%zu'\n", c->data.cutime);
	    str2Buf(line, &buf, bufSize);

	    snprintf(line, sizeof(line), "cstime '%zu'\n", c->data.cstime);
	    str2Buf(line, &buf, bufSize);

	    snprintf(line, sizeof(line), "cputime '%zu'\n", c->data.cputime);
	    str2Buf(line, &buf, bufSize);

	    snprintf(line, sizeof(line), "max threads '%zu'\n",
		     c->data.maxThreads);
	    str2Buf(line, &buf, bufSize);
	}

	str2Buf("-\n", &buf, bufSize);
    }

    return buf;
}

void setClockTicks(int clkTics)
{
    clockTicks = clkTics;
}
