/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
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

#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>

#include "peloguejob.h"
#include "peloguecomm.h"
#include "peloguelog.h"
#include "pelogueconfig.h"

#include "psidcomm.h"
#include "pscommon.h"
#include "plugincomm.h"
#include "pluginmalloc.h"
#include "pluginhelper.h"
#include "pspluginprotocol.h"
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
    if (!isValidJobPointer(job)) return;

    if (job->pelogueMonitorId != -1) {
	Timer_remove(job->pelogueMonitorId);
	job->pelogueMonitorId = -1;
    }
}

void signalPElogue(Job_t *job, int signal, char *reason)
{
    PStask_ID_t myTID = PSC_getMyTID();
    DDTypedBufferMsg_t msg;
    char *ptr, *finishPtr;
    int i, id;

    /* signal PElogue on all nodes */
    msg = (DDTypedBufferMsg_t) {
       .header = (DDMsg_t) {
       .type = PSP_CC_PLUG_PELOGUE,
       .sender = PSC_getMyTID(),
       .dest = PSC_getMyTID(),
       .len = sizeof(msg.header) },
       .buf = {'\0'} };

    msg.type = PSP_PELOGUE_SIGNAL;
    msg.header.len += sizeof(msg.type);
    msg.header.sender = myTID;

    ptr = msg.buf;
    addStringToMsgBuf(&msg, &ptr, job->plugin);
    addStringToMsgBuf(&msg, &ptr, job->id);
    addInt32ToMsgBuf(&msg, &ptr, signal);

    /* add space for finish flag */
    addInt32ToMsgBuf(&msg, &ptr, 1);
    finishPtr = ptr - sizeof(int32_t);

    addStringToMsgBuf(&msg, &ptr, reason);

    for (i=0; i<job->nrOfNodes; i++) {
	id = job->nodes[i].id;

	/* add the individual pelogue finish flag */
	if (job->state == JOB_PROLOGUE) {
	    *(int32_t *) finishPtr = job->nodes[i].prologue;
	} else {
	    *(int32_t *) finishPtr = job->nodes[i].epilogue;
	}
	msg.header.dest = PSC_getTID(id, 0);

	mdbg(PELOGUE_LOG_PSIDCOM, "%s: send to %i [%i->%i]\n", __func__, id,
		msg.header.sender, msg.header.dest);
	if ((sendMsg(&msg)) == -1 && errno != EWOULDBLOCK) {
	    mwarn(errno, "%s: sendMsg() to '%s' failed ", __func__,
		    PSC_printTID(msg.header.sender));
	}
    }
}

void PElogueExit(Job_t *job, int status, bool prologue)
{
    char *peType;
    int *track;
    int *epExit;

    peType = prologue ? "prologue" : "epilogue";
    track = (prologue) ? &job->prologueTrack : &job->epilogueTrack;
    epExit = (prologue) ? &job->prologueExit : &job->epilogueExit;

    if (*track < 0) {
	mlog("%s: %s tracking error for job '%s'\n", __func__, peType,
		job->id);
	return;
    }

    *track = *track -1;

    /* check if PElogue was running on all hosts */
    if (!(*track)) {
	if (*epExit == 0) {
	    *epExit = status;
	}

	/* stop monitoring the PELouge script for timeout */
	removePELogueTimeout(job);
	job->pluginCallback(job->id, *epExit, 0, job->nodes);

    } else if (status != 0 && *epExit == 0) {
	char *reason;

	/* update job state */
	job->state = (prologue) ? JOB_CANCEL_PROLOGUE : JOB_CANCEL_EPILOGUE;
	reason = (prologue) ? "prologue failed" : "epilogue failed";

	/* Cancel the PElogue scripts on all hosts. The signal
	 * SIGTERM will force the forwarder for PElogue scripts
	 * to kill the script. */
	if (job->signalFlag != SIGTERM && job->signalFlag != SIGKILL) {
	    signalPElogue(job, SIGTERM, reason);
	}
    }

    if (status != 0 && (status > *epExit || status < 0)) {
	*epExit = status;
    }
}

void stopPElogueExecution(Job_t *job)
{
    if (isValidJobPointer(job)) {
	removePELogueTimeout(job);
	job->pluginCallback(job->id, -4, 1, job->nodes);
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
    if (!isValidJobPointer(job)) return;

    /* don't break job if it got re-queued */
    if (timerId != job->pelogueMonitorId) {
	mlog("%s: timer of old job, skipping it\n", __func__);
	return;
    }
    job->pelogueMonitorId = -1;

    mlog("%s: global %s timeout for job '%s', stopping job using SIGKILL\n",
	    __func__, job->state == JOB_PROLOGUE ? "prologue" : "epilogue",
	    job->id);

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

    signalPElogue(job, SIGKILL, "global pelogue timeout");
    stopPElogueExecution(job);
}

void monitorPELogueTimeout(Job_t *job)
{
    struct timeval pelogueTimer = {1,0};
    int timeout, grace, id;

    if (job->state == JOB_PROLOGUE) {
	getConfParamI(job->plugin, "TIMEOUT_PROLOGUE", &timeout);
    } else {
	getConfParamI(job->plugin, "TIMEOUT_EPILOGUE", &timeout);
    }
    getConfParamI(job->plugin, "TIMEOUT_PE_GRACE", &grace);

    if (timeout < 0 || grace < 0) {
	mlog("%s: invalid pe timeout '%i' or grace time '%i'\n", __func__,
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
