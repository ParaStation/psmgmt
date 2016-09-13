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
#include "psaccountcollect.h"
#include "psaccountlog.h"
#include "psaccountproc.h"
#include "psaccountconfig.h"
#include "psaccountinter.h"

#include "psaccountclient.h"

#define MAX_JOBS_PER_NODE 1024

LIST_HEAD(clientList);

const char* clientType2Str(int type)
{
    switch(type) {
	case ACC_CHILD_JOBSCRIPT:
	    return "JOBSCRIPT";
	case ACC_CHILD_PSIDCHILD:
	    return "PSIDCHILD";
	case ACC_CHILD_REMOTE:
	    return "REMOTE";
    }
    return "UNKOWN";
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
	    if ((isChildofParent(client->pid, PSC_getPID(logger)))) {
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

void addClientToAggData(Client_t *client, AccountDataExt_t *accData)
{
    uint64_t maxRss, maxVsize, tmp;
    double dtmp;

    if (!client->data.pageSize) client->data.pageSize = pageSize;

    maxRss =  client->data.maxRss * (client->data.pageSize / 1024);
    maxVsize = client->data.maxVsize / 1024;

    /* sum up for maxima totals */
    accData->maxThreads += client->data.maxThreads;
    accData->maxRssTotal += maxRss;
    accData->maxVsizeTotal += maxVsize;

    /* maxima per client */
    if (accData->maxThreads < client->data.maxThreads) {
	accData->maxThreads = client->data.maxThreads;
    }

    if (accData->maxRss < maxRss) {
	accData->maxRss = maxRss;
	accData->taskIds[ACCID_MAX_RSS] = client->taskid;
    }

    if (accData->maxVsize < maxVsize) {
	accData->maxVsize = maxVsize;
	accData->taskIds[ACCID_MAX_VSIZE] = client->taskid;
    }

    /* calculate averages per client if data available */
    if (client->data.avgThreadsCount > 0) {
	accData->avgThreadsTotal +=
	    client->data.avgThreadsTotal / client->data.avgThreadsCount;
	accData->avgThreadsCount++;
    }
    if (client->data.avgVsizeCount > 0) {
	accData->avgVsizeTotal +=
	    client->data.avgVsizeTotal / (client->data.avgVsizeCount * 1024);
	accData->avgVsizeCount++;
    }
    if (client->data.avgRssCount > 0) {
	accData->avgRssTotal +=
			(client->data.avgRssTotal * client->data.pageSize)
			    / (client->data.avgRssCount * 1024);
	accData->avgRssCount++;
    }

    accData->cutime += client->data.cutime;
    accData->cstime += client->data.cstime;

    accData->cputime += client->data.cputime;
    accData->pageSize = client->data.pageSize;

    accData->rusage.ru_utime.tv_sec += client->data.rusage.ru_utime.tv_sec;
    accData->rusage.ru_utime.tv_usec += client->data.rusage.ru_utime.tv_usec;
    accData->rusage.ru_stime.tv_sec += client->data.rusage.ru_stime.tv_sec;
    accData->rusage.ru_stime.tv_usec += client->data.rusage.ru_stime.tv_usec;

    /* min cputime */
    tmp = client->data.rusage.ru_utime.tv_sec +
		client->data.rusage.ru_stime.tv_sec;
    if (!accData->numTasks) {
	accData->minCputime = tmp;
	accData->taskIds[ACCID_MIN_CPU] = client->taskid;
    } else if (accData->minCputime > tmp) {
	accData->minCputime = tmp;
	accData->taskIds[ACCID_MIN_CPU] = client->taskid;
    }

    /* total cputime */
    accData->totCputime +=
		client->data.rusage.ru_utime.tv_sec +
		client->data.rusage.ru_stime.tv_sec;

    /* major page faults */
    accData->totMajflt += client->data.totMajflt;
    if (client->data.totMajflt > accData->maxMajflt) {
	accData->maxMajflt = client->data.totMajflt;
	accData->taskIds[ACCID_MAX_PAGES] = client->taskid;
    }

    /* disc read */
    dtmp = (double) client->data.totDiskRead / (double)1048576;
    accData->totDiskRead += dtmp;
    if (dtmp > accData->maxDiskRead) {
	accData->maxDiskRead = dtmp;
	accData->taskIds[ACCID_MAX_DISKREAD] = client->taskid;
    }

    /* disc write */
    dtmp = (double) client->data.totDiskWrite / (double)1048576;
    accData->totDiskWrite += dtmp;
    if (dtmp > accData->maxDiskWrite) {
	accData->maxDiskWrite = dtmp;
	accData->taskIds[ACCID_MAX_DISKWRITE] = client->taskid;
    }

    accData->numTasks++;

    /* cpu freq */
    accData->cpuFreq += client->data.cpuFreq;

    mdbg(PSACC_LOG_AGGREGATE, "%s: client '%s' maxThreads '%lu' maxVsize '%lu' "
	    "maxRss '%lu' cutime '%lu' cstime '%lu' cputime '%lu' avg cpuFreq "
	    "'%.2fG'\n", __func__, PSC_printTID(client->taskid),
	    client->data.maxThreads, client->data.maxVsize,
	    client->data.maxRss, client->data.cutime, client->data.cstime,
	    client->data.cputime,
	    ((double) accData->cpuFreq / (double) accData->numTasks) /
	    (double) 1048576);
}

void addAggData(AccountDataExt_t *srcData, AccountDataExt_t *destData)
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
	    "'%.2fG'\n", __func__,
	    srcData->maxThreads, srcData->maxVsize,
	    srcData->maxRss, srcData->cutime, srcData->cstime,
	    srcData->cputime,
	    ((double) destData->cpuFreq / (double) destData->numTasks) /
	    (double) 1048576);
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

Client_t *addClient(PStask_ID_t taskid, PS_Acct_job_types_t type)
{
    Client_t *client;

    client = (Client_t *) umalloc(sizeof(Client_t));
    client->taskid = taskid;
    client->pid = PSC_getPID(taskid);
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
    long grace = 0;

    getConfParamL("TIME_CLIENT_GRACE", &grace);

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
	if (aggData.numTasks) sendAggregatedData(&aggData, loggerTIDs[i]);
    }
}

void updateClients(Job_t *job)
{
    static int updateCount = 0;
    list_t *pos;

    list_for_each(pos, &clientList) {
	Client_t *client = list_entry(pos, Client_t, next);
	if (!client->doAccounting) continue;
	if (job) {
	    if (client->job == job) updateAccountData(client);
	} else {
	    updateAccountData(client);
	}
    }

    if (globalCollectMode) {
	int forwardInterval;
	getConfParamI("FORWARD_INTERVAL", &forwardInterval);

	if (++updateCount >= forwardInterval) {
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
