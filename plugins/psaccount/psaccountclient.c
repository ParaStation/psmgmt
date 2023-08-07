/*
 * ParaStation
 *
 * Copyright (C) 2010-2019 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psaccountclient.h"

#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "pluginconfig.h"
#include "pluginmalloc.h"

#include "psaccountcomm.h"
#include "psaccountconfig.h"
#include "psaccountenergy.h"
#include "psaccountlog.h"
#include "psaccountproc.h"
#include "psaccountinterconnect.h"

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
    list_t *c;
    list_for_each(c, &clientList) {
	Client_t *client = list_entry(c, Client_t, next);
	if (client->taskid == clientTID) return client;
    }
    return NULL;
}

Client_t *findClientByPID(pid_t clientPID)
{
    list_t *c;
    list_for_each(c, &clientList) {
	Client_t *client = list_entry(c, Client_t, next);
	if (client->pid == clientPID) return client;
    }
    return NULL;
}

/**
 * @brief Try to find the jobscript for a root task
 *
 * The root process (which is typically a logger (mpiexec) process)
 * must be a child of a jobscript which was started by the psmom. This
 * functions tries to find the correct jobscript for a root task using
 * the /proc filesystem parent-child relations.
 *
 * @param rootTID The root task's ID to find the jobscript for
 *
 * @return On success the found jobscript is returned: on error
 * NULL is returned
 */
static Client_t *findJobscriptByRoot(PStask_ID_t rootTID)
{
    list_t *c;
    list_for_each(c, &clientList) {
	Client_t *client = list_entry(c, Client_t, next);

	if (client->type == ACC_CHILD_JOBSCRIPT) {
	    /* check if the jobscript is a parent of the root task */
	    if (isDescendant(client->pid, PSC_getPID(rootTID))) {
		client->root = rootTID;
		return client;
	    } else {
		/*
		flog("js %i not parent of root %i\n",
			jobscript->pid, PSC_getPID(rootTID));
		*/
	    }
	}
    }
    return NULL;
}

Client_t *findJobscriptInClients(Job_t *job)
{
    list_t *c;
    list_for_each(c, &clientList) {
	Client_t *client = list_entry(c, Client_t, next);

	if (client->job == job && client->type == ACC_CHILD_PSIDCHILD) {
	    Client_t *js = findJobscriptByRoot(client->root);
	    if (js) return js;
	}
    }
    return NULL;
}

/************************** Data collection **************************/

/** System's clock granularity. Set upon first call of @ref updateClntData() */
static int clockTicks = 0;

/** System's page size. Set upon first call of @ref updateClntData() */
static int pageSize = 0;

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
static void updateClntData(Client_t *client)
{
    unsigned long rssnew, vsizenew;
    uint64_t cutime, cstime;
    AccountDataExt_t *accData = &client->data;
    ProcSnapshot_t *proc = findProcSnapshot(client->pid), pChildren;
    ProcIO_t procIO;
    uint64_t cputime;
    int64_t diffCputime;

    if (!client->doAccounting) return;

    if (!proc) {
	client->doAccounting = false;
	client->endTime = time(NULL);
	return;
    }

    if (!clockTicks) {
	/* determine system's clock ticks */
	clockTicks = sysconf(_SC_CLK_TCK);
	if (clockTicks < 1) {
	    mlog("%s: reading clock ticks failed\n", __func__);
	    clockTicks = 0;
	    return;
	}
    }

    if (!pageSize) {
	/* determine system's page size */
	pageSize = sysconf(_SC_PAGESIZE);
	if (pageSize < 1) {
	    mlog("%s: reading page size failed\n", __func__);
	    pageSize = 0;
	    return;
	}
    }

    if (!accData->session) accData->session = proc->session;
    if (!accData->pgroup) accData->pgroup = proc->pgrp;
    if (!accData->pageSize) accData->pageSize = pageSize;

    /* collect data for all descendants */
    getDescendantData(client->pid, &pChildren);

    /* save cutime and cstime in seconds */
    cutime = (proc->cutime + pChildren.cutime) / clockTicks;
    cstime = (proc->cstime + pChildren.cstime) / clockTicks;

    /* set rss (resident set size) */
    rssnew = (proc->mem + pChildren.mem) * accData->pageSize / 1024;
    if (rssnew > accData->maxRss) accData->maxRss = rssnew;
    accData->avgRssTotal += rssnew;
    accData->avgRssCount++;

    /* set virtual memory */
    vsizenew = (proc->vmem + pChildren.vmem) / 1024;
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

    /* set total local disk read/write */
    if (procIO.diskRead > accData->totDiskRead) {
	accData->totDiskRead = procIO.diskRead;
    }
    if (procIO.diskWrite > accData->totDiskWrite) {
	accData->totDiskWrite = procIO.diskWrite;
    }

    /* set local disk readBytes/writeBytes */
    if (procIO.readBytes > accData->diskReadBytes) {
	accData->diskReadBytes = procIO.readBytes;
    }
    if (procIO.writeBytes > accData->diskWriteBytes) {
	accData->diskWriteBytes = procIO.writeBytes;
    }

    /* calculate CPU frequency */
    if (diffCputime > 0) {
	accData->cpuWeight = accData->cpuWeight +
	    getCpuFreq(proc->cpu) * diffCputime;
	if (cputime) {
	    accData->cpuFreq = accData->cpuWeight / cputime;
	}
    }
    if (!cputime) accData->cpuFreq = getCpuFreq(proc->cpu);

    mdbg(PSACC_LOG_COLLECT, "%s: tid %s rank %i cutime: %lu cstime: %lu"
	 " session %i mem %lukB vmem %lukB threads %lu majflt %lu"
	 " cpu %u cpuFreq %lu\n", __func__, PSC_printTID(client->taskid),
	 client->rank, accData->cutime, accData->cstime, accData->session,
	 accData->maxRss, accData->maxVsize, accData->maxThreads,
	 accData->totMajflt, proc->cpu, accData->cpuFreq);
}

/************************* Data aggregations *************************/

void addClientToAggData(Client_t *client, AccountDataExt_t *aggData,
			bool addEnergy)
{
    AccountDataExt_t *cData = &client->data;

    /* sum up for maxima totals */
    aggData->maxThreadsTotal += cData->maxThreads;
    aggData->maxRssTotal += cData->maxRss;
    aggData->maxVsizeTotal += cData->maxVsize;

    /* maxima per client */
    if (aggData->maxThreads < cData->maxThreads) {
	aggData->maxThreads = cData->maxThreads;
    }

    if (cData->maxRss > aggData->maxRss) {
	aggData->maxRss = cData->maxRss;
	aggData->taskIds[ACCID_MAX_RSS] = client->taskid;
    }

    if (cData->maxVsize > aggData->maxVsize) {
	aggData->maxVsize = cData->maxVsize;
	aggData->taskIds[ACCID_MAX_VSIZE] = client->taskid;
    }

    /* calculate averages per client if data available */
    if (cData->avgThreadsCount > 0) {
	aggData->avgThreadsTotal +=
	    cData->avgThreadsTotal / cData->avgThreadsCount;
	aggData->avgThreadsCount++;
    }
    if (cData->avgVsizeCount > 0) {
	aggData->avgVsizeTotal += cData->avgVsizeTotal / cData->avgVsizeCount;
	aggData->avgVsizeCount++;
    }
    if (cData->avgRssCount > 0) {
	aggData->avgRssTotal += cData->avgRssTotal / cData->avgRssCount;
	aggData->avgRssCount++;
    }

    aggData->cutime += cData->cutime;
    aggData->cstime += cData->cstime;

    aggData->pageSize = cData->pageSize;

    timeradd(&aggData->rusage.ru_utime, &cData->rusage.ru_utime,
	     &aggData->rusage.ru_utime);
    timeradd(&aggData->rusage.ru_stime, &cData->rusage.ru_stime,
	     &aggData->rusage.ru_stime);

    /* min CPU time */
    uint64_t CPUtime = cData->rusage.ru_utime.tv_sec +
		       cData->rusage.ru_stime.tv_sec;
    if (!aggData->numTasks || CPUtime < aggData->minCputime) {
	aggData->minCputime = CPUtime;
	aggData->taskIds[ACCID_MIN_CPU] = client->taskid;
    }

    /* total CPU time */
    aggData->totCputime += CPUtime;

    /* major page faults */
    aggData->totMajflt += cData->totMajflt;
    if (cData->totMajflt > aggData->maxMajflt) {
	aggData->maxMajflt = cData->totMajflt;
	aggData->taskIds[ACCID_MAX_PAGES] = client->taskid;
    }

    /* local disk read */
    double dtmp = (double)cData->totDiskRead / (1024*1024);
    aggData->totDiskRead += dtmp;
    if (dtmp > aggData->maxDiskRead) {
	aggData->maxDiskRead = dtmp;
	aggData->taskIds[ACCID_MAX_DISKREAD] = client->taskid;
    }

    /* local disk write */
    dtmp = (double)cData->totDiskWrite / (1024*1024);
    aggData->totDiskWrite += dtmp;
    if (dtmp > aggData->maxDiskWrite) {
	aggData->maxDiskWrite = dtmp;
	aggData->taskIds[ACCID_MAX_DISKWRITE] = client->taskid;
    }

    /* CPU frequency */
    aggData->cpuFreq += cData->cpuFreq;

    /* energy is calculated on a per node basis, add only once for all local
     * clients */
    if (addEnergy && client->job) {
	psAccountEnergy_t *eData = Energy_getData();

	/* energy */
	uint64_t energyTot = eData->energyCur - client->job->energyBase;
	if (energyTot > aggData->energyMax) {
	    aggData->energyMax = energyTot;
	    aggData->taskIds[ACCID_MAX_ENERGY] = client->taskid;
	}
	if (!aggData->numTasks || energyTot < aggData->energyMin) {
	    aggData->energyMin = energyTot;
	    aggData->taskIds[ACCID_MIN_ENERGY] = client->taskid;
	}
	aggData->energyTot += energyTot;

	/* power */
	aggData->powerAvg += eData->powerAvg;
	if (eData->powerMax > aggData->powerMax) {
	    aggData->powerMax = eData->powerMax;
	    aggData->taskIds[ACCID_MAX_POWER] = client->taskid;
	}
	if (!aggData->numTasks || eData->powerMin < aggData->powerMin) {
	    aggData->powerMin = eData->powerMin;
	    aggData->taskIds[ACCID_MIN_POWER] = client->taskid;
	}

	fdbg(PSACC_LOG_AGGREGATE, "node %i energy tot %zu min %zu max %zu "
	     " power avg %zu power min %zu power max %zu\n",
	     PSC_getID(client->taskid), aggData->energyTot, aggData->energyMin,
	     aggData->energyMax, aggData->powerAvg, aggData->powerMin,
	     aggData->powerMax);
    }

    psAccountIC_t *icData = IC_getData();

    /* received bytes from interconnect */
    uint64_t IC_recvBytes = 0;
    if (client->job) {
	IC_recvBytes = icData->recvBytes - client->job->IC_recvBase;
	aggData->IC_recvBytesTot += IC_recvBytes;
	if (IC_recvBytes > aggData->IC_recvBytesMax) {
	    aggData->IC_recvBytesMax = IC_recvBytes;
	    aggData->taskIds[ACCID_MAX_IC_RECV] = client->taskid;
	}
	if (!aggData->numTasks || IC_recvBytes < aggData->IC_recvBytesMin) {
	    aggData->IC_recvBytesMin = IC_recvBytes;
	    aggData->taskIds[ACCID_MIN_IC_RECV] = client->taskid;
	}
    }

    /* sent bytes from interconnect */
    uint64_t IC_sendBytes = 0;
    if (client->job) {
	IC_sendBytes = icData->sendBytes - client->job->IC_sendBase;
	aggData->IC_sendBytesTot += IC_sendBytes;
	if (IC_sendBytes > aggData->IC_sendBytesMax) {
	    aggData->IC_sendBytesMax = IC_sendBytes;
	    aggData->taskIds[ACCID_MAX_IC_SEND] = client->taskid;
	}
	if (!aggData->numTasks || IC_sendBytes < aggData->IC_sendBytesMin) {
	    aggData->IC_sendBytesMin = IC_sendBytes;
	    aggData->taskIds[ACCID_MIN_IC_SEND] = client->taskid;
	}
    }

    aggData->numTasks++;

    fdbg(PSACC_LOG_AGGREGATE, "client %s maxThreads %lu maxVsize %lu"
	 " maxRss %lu cutime %lu cstime %lu avg cpuFreq %.2fG"
	 " IC_recvBytesTot %zu IC_sendBytesTot %zu\n",
	 PSC_printTID(client->taskid), cData->maxThreads,
	 cData->maxVsize, cData->maxRss, cData->cutime, cData->cstime,
	 (double) aggData->cpuFreq / aggData->numTasks / (1024*1024),
	 IC_recvBytes, IC_sendBytes);
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
	destData->avgThreadsCount += srcData->avgThreadsCount;
    }
    if (srcData->avgVsizeCount > 0) {
	destData->avgVsizeTotal += srcData->avgVsizeTotal;
	destData->avgVsizeCount += srcData->avgVsizeCount;
    }
    if (srcData->avgRssCount > 0) {
	destData->avgRssTotal += srcData->avgRssTotal;
	destData->avgRssCount += srcData->avgRssCount;
    }

    /* max threads */
    if (srcData->maxThreads > destData->maxThreads) {
	destData->maxThreads = srcData->maxThreads;
    }

    /* max rss */
    if (srcData->maxRss > destData->maxRss) {
	destData->maxRss = srcData->maxRss;
	destData->taskIds[ACCID_MAX_RSS] = srcData->taskIds[ACCID_MAX_RSS];
    }

    /* max vsize */
    if (srcData->maxVsize > destData->maxVsize) {
	destData->maxVsize = srcData->maxVsize;
	destData->taskIds[ACCID_MAX_VSIZE] = srcData->taskIds[ACCID_MAX_VSIZE];
    }

    /* major page faults */
    destData->totMajflt += srcData->totMajflt;
    if (srcData->totMajflt > destData->maxMajflt) {
	destData->maxMajflt = srcData->totMajflt;
	destData->taskIds[ACCID_MAX_PAGES] = srcData->taskIds[ACCID_MAX_PAGES];
    }

    /* local disk read */
    destData->totDiskRead += srcData->totDiskRead;
    if (srcData->totDiskRead > destData->maxDiskRead) {
	destData->maxDiskRead = srcData->totDiskRead;
	destData->taskIds[ACCID_MAX_DISKREAD] =
	    srcData->taskIds[ACCID_MAX_DISKREAD];
    }

    /* local disk write */
    destData->totDiskWrite += srcData->totDiskWrite;
    if (srcData->totDiskWrite > destData->maxDiskWrite) {
	destData->maxDiskWrite = srcData->totDiskWrite;
	destData->taskIds[ACCID_MAX_DISKWRITE] =
	    srcData->taskIds[ACCID_MAX_DISKWRITE];
    }

    destData->cutime += srcData->cutime;
    destData->cstime += srcData->cstime;

    timeradd(&destData->rusage.ru_utime, &srcData->rusage.ru_utime,
	     &destData->rusage.ru_utime);
    timeradd(&destData->rusage.ru_stime, &srcData->rusage.ru_stime,
	     &destData->rusage.ru_stime);

    /* minimum CPU time */
    if (!destData->numTasks ||
	srcData->minCputime < destData->minCputime) {
	destData->minCputime = srcData->minCputime;
	destData->taskIds[ACCID_MIN_CPU] = srcData->taskIds[ACCID_MIN_CPU];
    }

    /* total CPU time */
    destData->totCputime += srcData->totCputime;

    /* CPU frequency */
    destData->cpuFreq += srcData->cpuFreq;

    destData->pageSize = srcData->pageSize;

    /* energy */
    destData->energyTot += srcData->energyTot;
    if (srcData->energyTot > destData->energyMax) {
	destData->energyMax = srcData->energyTot;
	destData->taskIds[ACCID_MAX_ENERGY] =
	    srcData->taskIds[ACCID_MAX_ENERGY];
    }
    if (!destData->numTasks || srcData->energyTot < destData->energyMin) {
	destData->energyMin = srcData->energyTot;
	destData->taskIds[ACCID_MIN_ENERGY] =
	    srcData->taskIds[ACCID_MIN_ENERGY];
    }

    /* power */
    destData->powerAvg += srcData->powerAvg;
    if (srcData->powerMax > destData->powerMax) {
	destData->powerMax = srcData->powerMax;
	destData->taskIds[ACCID_MAX_POWER] = srcData->taskIds[ACCID_MAX_POWER];
    }
    if (!destData->numTasks || srcData->powerMin < destData->powerMin) {
	destData->powerMin = srcData->powerMin;
	destData->taskIds[ACCID_MIN_POWER] = srcData->taskIds[ACCID_MIN_POWER];
    }

    /* received bytes from interconnect */
    destData->IC_recvBytesTot += srcData->IC_recvBytesTot;

    if (srcData->IC_recvBytesMax > destData->IC_recvBytesMax) {
	destData->IC_recvBytesMax = srcData->IC_recvBytesMax;
	destData->taskIds[ACCID_MAX_IC_RECV] =
	    srcData->taskIds[ACCID_MAX_IC_RECV];
    }
    if (!destData->numTasks
	|| srcData->IC_recvBytesMin < destData->IC_recvBytesMin) {
	destData->IC_recvBytesMin = srcData->IC_recvBytesMin;
	destData->taskIds[ACCID_MIN_IC_RECV] =
	    srcData->taskIds[ACCID_MIN_IC_RECV];
    }

    /* sent bytes from interconnect */
    destData->IC_sendBytesTot += srcData->IC_sendBytesTot;
    if (srcData->IC_sendBytesMax > destData->IC_sendBytesMax) {
	destData->IC_sendBytesMax = srcData->IC_sendBytesMax;
	destData->taskIds[ACCID_MAX_IC_SEND] =
	    srcData->taskIds[ACCID_MAX_IC_SEND];
    }
    if (!destData->numTasks
	|| srcData->IC_sendBytesMin < destData->IC_sendBytesMin) {
	destData->IC_sendBytesMin = srcData->IC_sendBytesMin;
	destData->taskIds[ACCID_MIN_IC_SEND] =
	    srcData->taskIds[ACCID_MIN_IC_SEND];
    }

    /* counters from file-system */
    destData->FS_writeBytes += srcData->FS_writeBytes;
    destData->FS_readBytes += srcData->FS_readBytes;

    /* number of tasks */
    destData->numTasks += srcData->numTasks;

    fdbg(PSACC_LOG_AGGREGATE, "node %u numTasks %u maxThreads %lu maxVsize %lu"
	 " maxRss %lu cutime %lu cstime %lu avg cpuFreq %.2fG total energy %zu"
	 " power avg %zu power min %zu power max %zu IC_recvTot %zu"
	 " IC_recvMin %zu IC_recvMax %zu IC_sendTot %zu IC_sendMin %zu"
	 " IC_sendMax %zu FS_write %zu FS_read %zu\n",
	 PSC_getID(srcData->taskIds[ACCID_MAX_RSS]), srcData->numTasks,
	 srcData->maxThreads, srcData->maxVsize, srcData->maxRss,
	 srcData->cutime, srcData->cstime,
	 ((double)destData->cpuFreq / destData->numTasks) / (1024*1024),
	 srcData->energyTot, srcData->powerAvg, srcData->powerMin,
	 srcData->powerMax, srcData->IC_recvBytesTot, srcData->IC_recvBytesMin,
	 srcData->IC_recvBytesMax,srcData->IC_sendBytesTot,
	 srcData->IC_sendBytesMin, srcData->IC_sendBytesMax,
	 srcData->FS_writeBytes, srcData->FS_readBytes);
}

void setAggData(PStask_ID_t tid, PStask_ID_t rootTID, AccountDataExt_t *data)
{
    Client_t *client;
    bool found = false;
    list_t *c;

    list_for_each(c, &clientList) {
	client = list_entry(c, Client_t, next);
	if (client->taskid == tid &&  client->root == rootTID) {
	    found = true;
	    break;
	}
    }

    if (!found) {
	client = addClient(tid, ACC_CHILD_REMOTE);
	client->root = rootTID;
	client->doAccounting = false;
    }

    memcpy(&client->data, data, sizeof(client->data));
}

void finishAggData(PStask_ID_t tid, PStask_ID_t rootTID)
{
    list_t *c;
    list_for_each(c, &clientList) {
	Client_t *client = list_entry(c, Client_t, next);
	if (client->taskid == tid && client->root == rootTID) {
	    client->endTime = time(NULL);
	    break;
	}
    }
}

void getPidsByRoot(PStask_ID_t rootTID, pid_t **pids, uint32_t *count)
{
    *count = 0;
    *pids = NULL;

    list_t *c;
    list_for_each(c, &clientList) {
	Client_t *client = list_entry(c, Client_t, next);
	if (client->root == rootTID && client->type == ACC_CHILD_PSIDCHILD) {
	    (*count)++;
	}
    }

    if (! *count) return;

    *pids = umalloc(sizeof(pid_t) * *count);

    uint32_t index = 0;
    list_for_each(c, &clientList) {
	Client_t *client = list_entry(c, Client_t, next);
	if (client->root == rootTID && client->type == ACC_CHILD_PSIDCHILD) {
	    if (index == *count) break;
	    (*pids)[index++] = client->pid;
	}
    }
}

PStask_ID_t getLoggerByClientPID(pid_t pid)
{
    ProcStat_t pS;
    bool psOK = false;
    list_t *c;

    if (list_empty(&clientList)) return -1;

    if (readProcStat(pid, &pS)) psOK = true;

    /* try to find the pid in the acc children */
    list_for_each(c, &clientList) {
	Client_t *client = list_entry(c, Client_t, next);

	/* try pid */
	if (client->pid == pid) return client->root;

	if (!psOK) continue;

	/* try sid */
	if (client->data.session && client->data.session == pS.session) {
	    return client->root;
	}

	/* try pgroup */
	if (client->data.pgroup && client->data.pgroup == pS.pgrp) {
	    return client->root;
	}
    }

    /* try all grand-children now */
    list_for_each(c, &clientList) {
	Client_t *client = list_entry(c, Client_t, next);
	if (isDescendant(client->pid, pid)) return client->root;
    }

    return -1;
}

bool aggregateDataByRoot(PStask_ID_t rootTID, AccountDataExt_t *accData)
{
    bool res = false;

    bool addEnergy = true;
    list_t *c;
    list_for_each(c, &clientList) {
	Client_t *client = list_entry(c, Client_t, next);
	if (client->root == rootTID && client->type != ACC_CHILD_JOBSCRIPT) {
	    if (client->type == ACC_CHILD_PSIDCHILD) {
		addClientToAggData(client, accData, addEnergy);
		if (client->job) addEnergy = false;
	    } else if (client->type == ACC_CHILD_REMOTE) {
		addAggData(&client->data, accData);
	    }
	    res = true;
	}
    }

    fdbg(PSACC_LOG_AGGREGATE, "aggregated: numTasks %u maxThreads %lu"
	 " maxVsize %lu maxRss %lu cutime %lu cstime %lu avg cpuFreq %.2fG"
	 " total energy %zu" " power avg %zu power min %zu power max %zu"
	 " IC_recvTot %zu IC_recvMin %zu IC_recvMax %zu IC_sendTot %zu"
	 " IC_sendMin %zu IC_sendMax %zu FS_write %zu FS_read %zu\n",
	 accData->numTasks, accData->maxThreads, accData->maxVsize,
	 accData->maxRss, accData->cutime, accData->cstime,
	 !accData->numTasks ? 0 :
	 ((double)accData->cpuFreq / accData->numTasks) / (1024*1024),
	 accData->energyTot, accData->powerAvg, accData->powerMin,
	 accData->powerMax, accData->IC_recvBytesTot, accData->IC_recvBytesMin,
	 accData->IC_recvBytesMax, accData->IC_sendBytesTot,
	 accData->IC_sendBytesMin, accData->IC_sendBytesMax,
	 accData->FS_writeBytes, accData->FS_readBytes);

    return res;
}

Client_t *addClient(PStask_ID_t taskID, PS_Acct_job_types_t type)
{
    Client_t *client = umalloc(sizeof(*client));

    client->taskid = taskID;
    client->pid = PSC_getPID(taskID);
    client->status = -1;
    client->root = -1;
    client->doAccounting = true;
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

    ufree(client->jobid);
    list_del(&client->next);
    ufree(client);
}

bool deleteClient(PStask_ID_t tid)
{
    Client_t *client = findClientByTID(tid);
    if (!client) return false;

    doDeleteClient(client);
    return true;
}

void deleteClientsByRoot(PStask_ID_t rootTID)
{
    list_t *c, *tmp;
    list_for_each_safe(c, tmp, &clientList) {
	Client_t *client = list_entry(c, Client_t, next);
	if (client->root == rootTID) doDeleteClient(client);
    }
}

bool haveActiveClients(void)
{
    list_t *c;
    list_for_each(c, &clientList) {
	Client_t *client = list_entry(c, Client_t, next);
	if (client->doAccounting) return true;
    }
    return false;
}

void clearAllClients(void)
{
    list_t *c, *tmp;
    list_for_each_safe(c, tmp, &clientList) {
	Client_t *client = list_entry(c, Client_t, next);
	doDeleteClient(client);
    }
}

void cleanupClients(void)
{
    list_t *c, *tmp;
    time_t now = time(NULL);
    int grace = getConfValueI(config, "TIME_CLIENT_GRACE");

    list_for_each_safe(c, tmp, &clientList) {
	Client_t *client = list_entry(c, Client_t, next);

	if (client->doAccounting || !client->endTime) continue;
	if (findJobByRoot(client->root)) continue;

	/* check timeout */
	if (client->endTime + grace * 60 <= now) {
	    mdbg(PSACC_LOG_VERBOSE, "%s: %i\n", __func__, client->pid);
	    doDeleteClient(client);
	}
    }
}

void forwardJobData(Job_t *job, bool force)
{
    PStask_ID_t rootTID = job->root;
    if (PSC_getID(rootTID) == PSC_getMyID()) return;

    /* aggregate accounting data on a per root task basis */
    AccountDataExt_t aggData;
    memset(&aggData, 0, sizeof(AccountDataExt_t));
    bool addEnergy = true;
    list_t *c;
    list_for_each(c, &clientList) {
	Client_t *client = list_entry(c, Client_t, next);
	if (client->root == rootTID && (client->doAccounting || force)) {
	    addClientToAggData(client, &aggData, addEnergy);
	    if (client->job) addEnergy = false;
	}
    }

    mdbg(PSACC_LOG_ENERGY, "energy aggregation for root task: %s "
	 "consumption: %zu  base: %zu\n", PSC_printTID(job->root),
	 aggData.energyTot, job->energyBase);

    /* send the update */
    if (aggData.numTasks) sendAggData(rootTID, &aggData);
}

void updateClients(Job_t *job)
{
    static int updateCount = 0;
    list_t *c;

    list_for_each(c, &clientList) {
	Client_t *client = list_entry(c, Client_t, next);
	if (!client->doAccounting) continue;
	if (!job || client->job == job) updateClntData(client);
    }

    if (globalCollectMode) {
	int forwInterval = getConfValueI(config, "FORWARD_INTERVAL");
	if (job) {
	    forwardJobData(job, false);
	} else if (++updateCount >= forwInterval) {
	    forwardAllData();
	    updateCount = 0;
	}
    }
}

void switchClientUpdate(PStask_ID_t clientTID, bool enable)
{
    Client_t *client = findClientByTID(clientTID);
    if (client) {
	mdbg(PSACC_LOG_ACC_SWITCH, "%s: %s accounting for %s\n", __func__,
	     (enable) ? "enable" : "disable", PSC_printTID(clientTID));
	client->doAccounting = enable;
    }
}

char *listClients(char *buf, size_t *bufSize, bool detailed)
{
    char l[160];
    list_t *c;

    if (list_empty(&clientList)) {
	return str2Buf("\nNo current clients.\n", &buf, bufSize);
    }

    str2Buf("\nclients:\n", &buf, bufSize);

    list_for_each(c, &clientList) {
	Client_t *cl = list_entry(c, Client_t, next);

	snprintf(l, sizeof(l), "taskID %s\n", PSC_printTID(cl->taskid));
	str2Buf(l, &buf, bufSize);
	snprintf(l, sizeof(l), "rank %i\n", cl->rank);
	str2Buf(l, &buf, bufSize);
	snprintf(l, sizeof(l), "root %s\n", PSC_printTID(cl->root));
	str2Buf(l, &buf, bufSize);
	snprintf(l, sizeof(l), "account %i\n", cl->doAccounting);
	str2Buf(l, &buf, bufSize);
	snprintf(l, sizeof(l), "type '%s'\n", clientType2Str(cl->type));
	str2Buf(l, &buf, bufSize);
	snprintf(l, sizeof(l), "uid %i\n", cl->uid);
	str2Buf(l, &buf, bufSize);
	snprintf(l, sizeof(l), "gid %i\n", cl->gid);
	str2Buf(l, &buf, bufSize);
	snprintf(l, sizeof(l), "page size %lu\n", cl->data.pageSize);
	str2Buf(l, &buf, bufSize);
	snprintf(l, sizeof(l), "start time %s", ctime(&cl->startTime));
	str2Buf(l, &buf, bufSize);
	snprintf(l, sizeof(l), "end time %s",
		 cl->endTime ? ctime(&cl->endTime) : "-\n");
	str2Buf(l, &buf, bufSize);

	if (detailed) {
	    snprintf(l, sizeof(l), "max mem %lukB\n", cl->data.maxRss);
	    str2Buf(l, &buf, bufSize);
	    snprintf(l, sizeof(l), "max vmem %lukB\n", cl->data.maxVsize);
	    str2Buf(l, &buf, bufSize);
	    snprintf(l, sizeof(l), "cutime %lu\n", cl->data.cutime);
	    str2Buf(l, &buf, bufSize);
	    snprintf(l, sizeof(l), "cstime %lu\n", cl->data.cstime);
	    str2Buf(l, &buf, bufSize);
	    snprintf(l, sizeof(l), "CPU time %lu\n", cl->data.totCputime);
	    str2Buf(l, &buf, bufSize);
	    snprintf(l, sizeof(l), "max threads %lu\n", cl->data.maxThreads);
	    str2Buf(l, &buf, bufSize);
	    snprintf(l, sizeof(l), "numTask %u\n", cl->data.numTasks);
	    str2Buf(l, &buf, bufSize);
	    snprintf(l, sizeof(l), "Vsize %lu\n", cl->data.avgVsizeTotal);
	    str2Buf(l, &buf, bufSize);
	    snprintf(l, sizeof(l), "#Vsize %lu\n",cl->data.avgVsizeCount);
	    str2Buf(l, &buf, bufSize);
	    snprintf(l, sizeof(l), "Rss %lu\n", cl->data.avgRssTotal);
	    str2Buf(l, &buf, bufSize);
	    snprintf(l, sizeof(l), "#Rss %lu\n", cl->data.avgRssCount);
	    str2Buf(l, &buf, bufSize);
	    snprintf(l, sizeof(l), "Threads %lu\n", cl->data.avgThreadsTotal);
	    str2Buf(l, &buf, bufSize);
	    snprintf(l, sizeof(l), "#Threads %lu\n", cl->data.avgThreadsCount);
	    str2Buf(l, &buf, bufSize);
	}
	str2Buf("-\n", &buf, bufSize);
    }

    return buf;
}
