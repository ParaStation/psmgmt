/*
 * ParaStation
 *
 * Copyright (C) 2010-2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <dirent.h>
#include <ctype.h>

#include "psaccountinter.h"
#include "psaccountlog.h"
#include "psaccountproc.h"
#include "psaccount.h"

#include "pscommon.h"

#include "psaccountcollect.h"

void updateAccountData(Client_t *client)
{
    unsigned long rssnew;
    unsigned long vsizenew = 0;
    uint64_t cutime, cstime;
    AccountData_t *accData;
    Proc_Snapshot_t *proc, *procChilds;

    if (!(proc = findProcSnapshot(client->pid))) {
	/*
	mlog("%s: snapshot for child '%i' not found, stopping accounting\n",
	    __func__, client->pid);
	*/
	client->doAccounting = 0;
	return;
    }

    accData = &client->data;
    if (!(accData->session)) {
	accData->session = proc->session;
    }
    if (!(accData->pgroup)) {
	accData->pgroup = proc->pgroup;
    }

    /* get mem and vmem for all children  */
    procChilds = getAllClientChildsMem(client->pid);
    rssnew = proc->mem + procChilds->mem;
    vsizenew = proc->vmem + procChilds->vmem;
    free(procChilds);

    /* set rss (resident set size) */
    if (rssnew > accData->maxRss) accData->maxRss = rssnew;
    accData->avgRss += rssnew;
    accData->avgRssCount++;

    /* set virtual mem */
    if (vsizenew > accData->maxVsize) accData->maxVsize = vsizenew;
    accData->avgVsize += vsizenew;
    accData->avgVsizeCount++;

    /* set max threads */
    if (proc->threads > accData->maxThreads) accData->maxThreads = proc->threads;
    accData->avgThreads += proc->threads;
    accData->avgThreadsCount++;

    /* set cutime and cstime in seconds */
    cutime = proc->cutime / clockTicks;
    cstime = proc->cstime / clockTicks;

    if (cutime > accData->cutime) accData->cutime = cutime;
    if (cstime > accData->cstime) accData->cstime = cstime;

    /*
    if (globalCollectMode && PSC_getID(client->logger) != PSC_getMyID()) {
	sendAccountUpdate(client);
    }
    */

    /*
    mlog("%s: pid:%i cutime: '%lu' cstime: '%lu' session '%i' mem '%lu' vmem '%lu'"
	"threads '%lu'\n", __func__, client->pid, accData->cutime, accData->cstime,
	accData->session, accData->maxRss, accData->maxVsize, accData->maxThreads);
    */
}
