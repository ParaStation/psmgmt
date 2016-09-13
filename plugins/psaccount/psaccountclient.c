/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
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

Client_t AccClientList;

void initAccClientList(void)
{
    INIT_LIST_HEAD(&AccClientList.list);
}

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

/**
 * @brief Search an account client.
 *
 * Find an account client by the client TID or by the logger TID.
 *
 * @return Return the found client, or NULL on error and if no client
 * was found.
 */
static Client_t *findAccClient(PStask_ID_t clientTID, PStask_ID_t loggerTID,
				pid_t clientPID)
{
    list_t *pos, *tmp;
    Client_t *client;

    list_for_each_safe(pos, tmp, &AccClientList.list) {
	if (!(client = list_entry(pos, Client_t, list))) break;

	if (clientTID != -1) {
	    if (client->taskid == clientTID) return client;
	}
	if (loggerTID != -1) {
	    if (client->logger == loggerTID) return client;
	}
	if (clientPID != -1) {
	    if (client->pid == clientPID) return client;
	}
    }
    return NULL;
}

Client_t *findAccClientByClientTID(PStask_ID_t clientTID)
{
    return findAccClient(clientTID, -1, -1);
}

Client_t *findAccClientByLogger(PStask_ID_t loggerTID)
{
    return findAccClient(-1, loggerTID, -1);
}

Client_t *findAccClientByClientPID(pid_t clientPID)
{
    return findAccClient(-1, -1, clientPID);
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
    struct list_head *pos;
    Client_t *jobscript;

    list_for_each(pos, &AccClientList.list) {
	if (!(jobscript = list_entry(pos, Client_t, list))) return NULL;

	if (jobscript->type == ACC_CHILD_JOBSCRIPT) {
	    /* check if the jobscript is a parent of logger */
	    if ((isChildofParent(jobscript->pid, PSC_getPID(logger)))) {
		jobscript->logger = logger;
		return jobscript;
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
    struct list_head *pos;
    Client_t *client, *js;

    list_for_each(pos, &AccClientList.list) {
	if (!(client = list_entry(pos, Client_t, list))) return NULL;

	if (client->job == job && client->type == ACC_CHILD_PSIDCHILD) {
	    if ((js = findJobscriptByLogger(client->logger))) return js;
	}
    }
    return NULL;
}

void addAccDataForClient(Client_t *client, AccountDataExt_t *accData)
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

int getPidsByLogger(PStask_ID_t logger, pid_t **pids, uint32_t *count)
{
    struct list_head *pos;
    Client_t *client;
    uint32_t index = 0;

    *count = 0;
    *pids = NULL;
    if (list_empty(&AccClientList.list)) return 0;

    list_for_each(pos, &AccClientList.list) {
	if (!(client = list_entry(pos, Client_t, list))) break;

	if (client->logger == logger && client->type == ACC_CHILD_PSIDCHILD) {
	    (*count)++;
	}
    }

    *pids = (pid_t *) umalloc(sizeof(pid_t) * *count);

    list_for_each(pos, &AccClientList.list) {
	if (!(client = list_entry(pos, Client_t, list))) break;

	if (client->logger == logger && client->type == ACC_CHILD_PSIDCHILD) {
	    if (index == *count) break;
	    (*pids)[index++] = client->pid;
	}
    }

    return 1;
}

int getAccountDataByLogger(PStask_ID_t logger, AccountDataExt_t *accData)
{
    struct list_head *pos;
    Client_t *client;
    int res = 0;

    list_for_each(pos, &AccClientList.list) {
	if (!(client = list_entry(pos, Client_t, list))) break;

	if (client->logger == logger && client->type != ACC_CHILD_JOBSCRIPT) {

	    if (client->type == ACC_CHILD_PSIDCHILD) {
		addAccDataForClient(client, accData);
	    } else if (client->type == ACC_CHILD_REMOTE) {
		addAggData(&client->data, accData);
	    }

	    res = 1;
	}
    }
    return res;
}

void addAccInfoForClient(Client_t *client, psaccAccountInfo_t *accData)
{
    uint64_t cputime = 0;

    cputime = client->data.rusage.ru_utime.tv_sec +
	1.0e-6 * client->data.rusage.ru_utime.tv_usec +
	client->data.rusage.ru_stime.tv_sec +
	1.0e-6 * client->data.rusage.ru_stime.tv_usec;

    client->data.cputime = cputime;
    accData->cputime += cputime;
    if (client->data.pageSize) {
	accData->mem += client->data.maxRss * client->data.pageSize;
    } else {
	accData->mem += client->data.maxRss * pageSize;
    }
    accData->vmem += client->data.maxVsize;
    accData->utime += client->data.cutime;
    accData->stime += client->data.cstime;
    accData->count++;
}

int getAccountInfoByLogger(PStask_ID_t logger, psaccAccountInfo_t *accData)
{
    struct list_head *pos;
    Client_t *client;

    if (list_empty(&AccClientList.list)) return false;

    list_for_each(pos, &AccClientList.list) {
	if (!(client = list_entry(pos, Client_t, list))) break;

	if (client->logger == logger && client->type != ACC_CHILD_JOBSCRIPT) {
	    addAccInfoForClient(client, accData);
	}
    }
    return true;
}

Client_t *addAccClient(PStask_ID_t taskid, PS_Acct_job_types_t type)
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

    memset(&client->data, 0, sizeof(AccountDataExt_t));
    client->data.numTasks = 1;

    list_add_tail(&(client->list), &AccClientList.list);

    return client;
}

static void doDeleteClient(Client_t *client)
{

    if (!client) return;

    list_del(&client->list);

    ufree(client->jobid);
    ufree(client);
}

int deleteAccClient(PStask_ID_t tid)
{
    Client_t *client;

    if (!(client = findAccClientByClientTID(tid))) return 0;

    doDeleteClient(client);

    return 1;
}

void deleteAllAccClientsByLogger(PStask_ID_t loggerTID)
{
    Client_t *client;

    while ((client = findAccClientByLogger(loggerTID))) {
	doDeleteClient(client);
    }
}

int haveActiveAccClients(void)
{
    struct list_head *pos;
    Client_t *client;

    list_for_each(pos, &AccClientList.list) {
	if (!(client = list_entry(pos, Client_t, list))) break;

	if (client->doAccounting) return 1;
    }
    return 0;
}

void clearAllAccClients(void)
{
    list_t *pos, *tmp;
    Client_t *client;

    list_for_each_safe(pos, tmp, &AccClientList.list) {
	if (!(client = list_entry(pos, Client_t, list))) return;

	if (!(deleteAccClient(client->taskid))) {
	    mlog("%s: deleting acc client '%i' failed\n", __func__,
		client->pid);
	}
    }
    return;
}

void cleanupClients(void)
{
    list_t *pos, *tmp;
    Client_t *client;
    time_t now = time(NULL);
    long grace = 0;

    getConfParamL("TIME_CLIENT_GRACE", &grace);

    list_for_each_safe(pos, tmp, &AccClientList.list) {
	if (!(client = list_entry(pos, Client_t, list))) break;

	if (client->doAccounting || !client->endTime) continue;
	if (findJobByLogger(client->logger)) continue;

	/* check timeout */
	if (client->endTime + grace * 60 <= now) {
	    mdbg(PSACC_LOG_VERBOSE, "%s: cleanup client '%i'\n", __func__,
		    client->pid);
	    deleteAccClient(client->taskid);
	}
    }
}

void forwardAggData(void)
{
    struct list_head *pos;
    Client_t *client;
    AccountDataExt_t aggData;
    PStask_ID_t loggerTIDs[MAX_JOBS_PER_NODE];
    int i;

    for (i=0; i<MAX_JOBS_PER_NODE; i++) loggerTIDs[i] = -1;

    /* extract uniq logger TIDs */
    list_for_each(pos, &AccClientList.list) {
	if (!(client = list_entry(pos, Client_t, list))) break;
	if (client->doAccounting) {
	    for (i=0; i<MAX_JOBS_PER_NODE; i++) {
		if (loggerTIDs[i] == client->logger) break;
		if (loggerTIDs[i] == -1) {
		    loggerTIDs[i] = client->logger;
		    break;
		}
	    }
	}
    }

    for (i=0; i<MAX_JOBS_PER_NODE; i++) {
	if (loggerTIDs[i] == -1) break;
	if (PSC_getID(loggerTIDs[i]) == PSC_getMyID()) continue;

	/* aggreagate accounting data on a per logger basis */
	memset(&aggData, 0, sizeof(AccountDataExt_t));

	list_for_each(pos, &AccClientList.list) {
	    if (!(client = list_entry(pos, Client_t, list))) break;
	    if (client->logger == loggerTIDs[i] && client->doAccounting) {
		addAccDataForClient(client, &aggData);
	    }
	}

	/* send the update */
	if (aggData.numTasks) sendAggregatedData(&aggData, loggerTIDs[i]);
    }
}

void updateAllAccClients(Job_t *job)
{
    struct list_head *pos;
    Client_t *client;
    static int updateCount = 0;
    int forwardInterval;

    list_for_each(pos, &AccClientList.list) {
	if (!(client = list_entry(pos, Client_t, list))) break;
	if (client->doAccounting) {
	    if (job) {
		if (client->job == job) {
		    updateAccountData(client);
		}
	    } else {
		updateAccountData(client);
	    }
	}
    }

    getConfParamI("FORWARD_INTERVAL", &forwardInterval);

    if (++updateCount >= forwardInterval) {
	if (globalCollectMode) forwardAggData();
	updateCount = 0;
    }
}

void switchClientUpdate(PStask_ID_t clientTID, int enable)
{
    struct list_head *pos;
    Client_t *client;

    list_for_each(pos, &AccClientList.list) {
	if (!(client = list_entry(pos, Client_t, list))) return;

	if (client->taskid == clientTID) {
	    mdbg(PSACC_LOG_ACC_SWITCH, "%s: %s accounting for '%s'\n", __func__,
		    (enable) ? "enable" : "disable", PSC_printTID(clientTID));
	    client->doAccounting = (enable) ? 1 : 0;
	}
    }
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet:*/
