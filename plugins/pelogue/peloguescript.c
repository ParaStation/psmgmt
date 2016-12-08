/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>

#include "peloguejob.h"
#include "peloguecomm.h"
#include "peloguelog.h"
#include "pelogueconfig.h"

#include "pluginmalloc.h"
#include "pluginhelper.h"
#include "timer.h"

#include "peloguescript.h"

int checkPELogueFileStats(char *filename, int root)
{
    struct stat statbuf;

    if (stat(filename, &statbuf) == -1) {
	return -1;
    }

    if (root) {
	/* readable and executable by root and NOT writable by anyone
	 * besides root */
	if (statbuf.st_uid != 0) {
	    return -2;
	}
	if (!S_ISREG(statbuf.st_mode) ||
	    ((statbuf.st_mode & (S_IRUSR | S_IXUSR)) != (S_IRUSR | S_IXUSR)) ||
	    (statbuf.st_mode & (S_IWGRP | S_IWOTH))) {
	    return -2;
	}
    } else {
	/* readable and executable by root and other  */
	if ((statbuf.st_mode & (S_IROTH | S_IXOTH)) != (S_IROTH | S_IXOTH)) {
	    return -2;
	}
    }
    return 1;
}

void removePELogueTimeout(Job_t *job)
{
    if (!checkJobPtr(job)) return;

    if (job->pelogueMonitorId != -1) {
	Timer_remove(job->pelogueMonitorId);
	job->pelogueMonitorId = -1;
    }
}

/**
 * @brief Callback handler for the global PElogue timeout.
 *
 * @param timerId The id of my timer that expired.
 *
 * @param data Holds the jobid of the pelogue script.
 *
 * @return No return value.
 */
static void handlePELogueTimeout(int timerId, void *data)
{
    Job_t *job = data;
    char *buf = NULL, tmp[100];
    size_t buflen = 0;
    int i, count = 0;

    /* don't call myself again */
    Timer_remove(timerId);

    /* job could be already deleted */
    if (!checkJobPtr(job)) return;

    /* don't break job if it got re-queued */
    if (timerId != job->pelogueMonitorId) {
	mlog("%s: timer of old job, skipping it\n", __func__);
	return;
    }
    job->pelogueMonitorId = -1;

    mlog("%s: global %s timeout for job %s, send SIGKILL\n", __func__,
	 job->state == JOB_PROLOGUE ? "prologue" : "epilogue", job->id);

    str2Buf("missing nodeID(s): ", &buf, &buflen);

    for (i=0; i<job->nrOfNodes; i++) {
	if (job->state == JOB_PROLOGUE) {
	    if (job->nodes[i].prologue == -1) {
		if (count>0) {
		    str2Buf(",", &buf, &buflen);
		}
		snprintf(tmp, sizeof(tmp), "%i", job->nodes[i].id);
		str2Buf(tmp, &buf, &buflen);
		count++;
		job->nodes[i].prologue = 2;
	    }
	} else {
	    if (job->nodes[i].epilogue == -1) {
		if (count>0) {
		    str2Buf(",", &buf, &buflen);
		}
		snprintf(tmp, sizeof(tmp), "%i", job->nodes[i].id);
		str2Buf(tmp, &buf, &buflen);
		count++;
		job->nodes[i].epilogue = 2;
	    }
	}
    }
    mlog("%s: %s\n", __func__, buf);
    ufree(buf);

    sendPElogueSignal(job, SIGKILL, "global pelogue timeout");
    stopJobExecution(job);
}

void monitorPELogueTimeout(Job_t *job)
{
    struct timeval pelogueTimer = {1,0};
    int timeout, grace, id;

    if (job->state == JOB_PROLOGUE) {
	timeout = getConfParamI(job->plugin, "TIMEOUT_PROLOGUE");
    } else {
	timeout = getConfParamI(job->plugin, "TIMEOUT_EPILOGUE");
    }
    grace = getConfParamI(job->plugin, "TIMEOUT_PE_GRACE");

    if (timeout < 0 || grace < 0) {
	mlog("%s: invalid pe timeout %i or grace time %i\n", __func__,
	     timeout, grace);

    }

    /* timeout monitoring disabled */
    if (!timeout) return;

    pelogueTimer.tv_sec = timeout + (2 * grace);

    if ((id = Timer_registerEnhanced(&pelogueTimer, handlePELogueTimeout,
		    job)) == -1) {
	mlog("%s: register PElogue monitor timer failed\n", __func__);
    } else {
	job->pelogueMonitorId = id;
    }
}
