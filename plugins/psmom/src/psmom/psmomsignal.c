/*
 * ParaStation
 *
 * Copyright (C) 2010 - 2014 ParTec Cluster Competence Center GmbH, Munich
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
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <execinfo.h>
#include <signal.h>

#include "psmom.h"
#include "psmomlog.h"
#include "psmomjob.h"
#include "psmomcomm.h"
#include "psmomproto.h"
#include "psmomcollect.h"
#include "psmomconfig.h"
#include "psmomchild.h"
#include "psmomscript.h"
#include "psmomconv.h"
#include "psmomlocalcomm.h"

#include "psaccounthandles.h"
#include "timer.h"

#include "psmomsignal.h"

static struct sigTable sig_Table[] =
{
    { "SIGHUP",	    "1",    SIGHUP },
    { "SIGINT",	    "2",    SIGINT },
    { "SIGQUIT",    "3",    SIGQUIT },
    { "SIGILL",	    "4",    SIGILL },
    { "SIGTRAP",    "5",    SIGTRAP },
    { "SIGABRT",    "6",    SIGABRT },
    { "SIGIOT",	    "6",    SIGIOT },
    { "SIGBUS",	    "7",    SIGBUS },
    { "SIGFPE",	    "8",    SIGFPE },
    { "SIGKILL",    "9",    SIGKILL },
    { "SIGUSR1",    "10",   SIGUSR1 },
    { "SIGSEGV",    "11",   SIGSEGV },
    { "SIGUSR2",    "12",   SIGUSR2 },
    { "SIGPIPE",    "13",   SIGPIPE },
    { "SIGALRM",    "14",   SIGALRM },
    { "SIGTERM",    "15",   SIGTERM },
    { "SIGSTKFLT",  "16",   SIGSTKFLT },
    { "SIGCHLD",    "17",   SIGCHLD },
    { "SIGCLD",	    "17",   SIGCHLD },
    { "SIGCONT",    "18",   SIGCONT },
    { "SIGSTOP",    "19",   SIGSTOP },
    { "SIGTSTP",    "20",   SIGTSTP },
    { "SIGTTIN",    "21",   SIGTTIN },
    { "SIGTTOU",    "22",   SIGTTOU },
    { "SIGXCPU",    "24",   SIGXCPU },
    { "SIGXFSZ",    "25",   SIGXFSZ },
    { "SIGVTALRM",  "26",   SIGVTALRM },
    { "SIGPROF",    "27",   SIGPROF },
    { "SIGWINCH",   "28",   SIGWINCH },
    { "SIGPOLL",    "29",   SIGPOLL },
    { "SIGIO",      "29",   SIGIO },
    { "SIGPWR",     "30",   SIGPWR },
    { "SIGSYS",     "31",   SIGSYS },
    { "SIGUNUSED",  "31",   SIGUNUSED },
    { "",	    "",	    0}
};

int string2Signal(char *signal)
{
    struct sigTable *ptr;
    ptr = sig_Table;

    while (ptr->sigNum !=  0) {
	if (!(strcmp(ptr->sigName, signal)) ||
		!(strcmp(ptr->sigStrNum, signal))) {
	    return ptr->sigNum;
	}
	ptr++;
    }
    return 0;
}

char *signal2String(int signal)
{
    struct sigTable *ptr;
    ptr = sig_Table;

    while (ptr->sigNum !=  0) {
	if (ptr->sigNum == signal) {
	    return ptr->sigName;
	}
	ptr++;
    }
    return NULL;
}

/**
 * @brief Send a signal to a job using various ways.
 *
 * @param job A pointer to the job structure to send the signal to.
 *
 * @param signal The signal to send.
 *
 * @return Returns 0 on success and 1 on error.
 */
static int doSendSignal(Job_t *job, int signal, char *reason)
{
    ComHandle_t *comForward;
    Child_t *child;

    if (job->sid != -1) {
	mlog("signal '%s (%i)' to job '%s' - reason '%s' - sid '%i'\n",
		signal2String(signal), signal, job->id, reason, job->sid);
	psAccountsendSignal2Session(job->sid, signal);
    } else if (job->pid != -1) {
	mlog("signal '%s (%i)' to job '%s' - reason '%s' - pid '%i'\n",
		signal2String(signal), signal, job->id, reason, job->pid);
	kill(job->pid, signal);
    } else if ((comForward = getJobCom(job, JOB_CON_FORWARD))) {
	mlog("signal '%s (%i)' to job '%s' - reason '%s' - forwarder\n",
		signal2String(signal), signal, job->id, reason);
	WriteDigit(comForward, CMD_LOCAL_SIGNAL);
	WriteDigit(comForward, signal);
	wDoSend(comForward);
    } else if ((child = findChildByJobid(job->id, -1))) {
	mlog("signal '%s (%i)' to job '%s' - reason '%s' - child\n",
		signal2String(signal), signal, job->id, reason);
	if (signal == SIGKILL) {
	    /* let the forwarder a little time for cleanup before killing it
	     * hard via SIGKILL */
	    if (!child->killFlag) {
		kill(child->pid, SIGTERM);
		child->killFlag = 1;
	    } else {
		kill(child->pid, signal);
	    }
	} else {
	    kill(child->pid, signal);
	}
    } else {
	return 1;
    }

    return 0;
}

int sendSignaltoJob(Job_t *job, int signal, char *reason)
{
    struct list_head *pos;
    int ret = 0, tmp;

    /* send to all jobs */
    if (!job) {
	if (list_empty(&JobList.list)) return 1;

	list_for_each(pos, &JobList.list) {
	    if ((job = list_entry(pos, Job_t, list)) == NULL) return 0;
	    tmp = doSendSignal(job, signal, reason);
	    if (tmp > ret) ret = tmp;
	}
    } else {
	return doSendSignal(job, signal, reason);
    }

    return ret;
}
