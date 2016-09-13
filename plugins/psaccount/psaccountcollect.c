/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdint.h>

#include "pluginmalloc.h"

#include "psaccount.h"
#include "psaccountproc.h"
#include "psaccountlog.h"

#include "psaccountcollect.h"

int clockTicks = -1;

void updateAccountData(Client_t *client)
{
    unsigned long rssnew = 0, vsizenew = 0;
    uint64_t cutime = 0, cstime = 0;
    AccountDataExt_t *accData;
    Proc_Snapshot_t *proc, *pChildren;
    ProcIO_t procIO;
    uint64_t cputime, maxRssMB = 0;
    int64_t diffCputime;

    if (!client->doAccounting) return;

    if (!(proc = findProcSnapshot(client->pid))) {
	client->doAccounting = false;
	client->endTime = time(NULL);
	return;
    }

    accData = &client->data;
    if (!accData->session) {
	accData->session = proc->session;
    }
    if (!accData->pgroup) {
	accData->pgroup = proc->pgroup;
    }

    /* get infos for all children  */
    pChildren = getAllChildrenData(client->pid);
    if (pChildren) {
	rssnew = proc->mem + pChildren->mem;
	vsizenew = proc->vmem + pChildren->vmem;

	/* save cutime and cstime in seconds */
	cutime = (proc->cutime + pChildren->cutime) / clockTicks;
	cstime = (proc->cstime + pChildren->cstime) / clockTicks;

	ufree(pChildren);
    }

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
	    cpuFreq[proc->cpu] * diffCputime;
	if (cputime) {
	    accData->cpuFreq = accData->cpuWeight / cputime;
	}
    }
    if (!cputime) accData->cpuFreq = cpuFreq[proc->cpu];

    maxRssMB = (accData->maxRss * (pageSize / (1024)) / 1024);

    mdbg(PSACC_LOG_COLLECT, "%s: tid '%s' rank '%i' cutime: '%lu' cstime: '%lu'"
	 " session '%i' mem '%lu MB' vmem '%lu MB' threads '%lu' "
	 "majflt '%lu' cpu '%u' cpuFreq '%lu'\n", __func__,
	 PSC_printTID(client->taskid),
	 client->rank, accData->cutime, accData->cstime, accData->session,
	 maxRssMB, accData->maxVsize / (1024 * 1024), accData->maxThreads,
	 accData->totMajflt, proc->cpu, accData->cpuFreq);
}
