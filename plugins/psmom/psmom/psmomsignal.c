/*
 * ParaStation
 *
 * Copyright (C) 2010-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <signal.h>
#include <string.h>

#include "psaccounthandles.h"

#include "psmomchild.h"
#include "psmomcomm.h"
#include "psmomconv.h"
#include "psmomlocalcomm.h"
#include "psmomlog.h"

#include "psmomsignal.h"

static struct sigTable {
    char *name;
    char *strNum;
    int num;
} sigTable[] = {
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
    { "",	    "",	    0}
};

int string2Signal(char *sig)
{
    struct sigTable *s = sigTable;
    while (s->num) {
	if (!strcmp(s->name, sig) || !strcmp(s->strNum, sig)) return s->num;
	s++;
    }

    return 0;
}

char *signal2String(int sig)
{
    struct sigTable *s = sigTable;
    while (s->num) {
	if (s->num == sig) return s->name;
	s++;
    }

    return NULL;
}

bool signalJob(Job_t *job, int signal, char *reason)
{
    ComHandle_t *comForward;
    Child_t *child;

    if (job->sid != -1) {
	mlog("signal '%s'(%i) to job '%s' - reason '%s' - sid %i\n",
	     signal2String(signal), signal, job->id, reason, job->sid);
	psAccountSignalSession(job->sid, signal);
    } else if (job->pid != -1) {
	mlog("signal '%s'(%i) to job '%s' - reason '%s' - pid %i\n",
	     signal2String(signal), signal, job->id, reason, job->pid);
	kill(job->pid, signal);
    } else if ((comForward = getJobCom(job, JOB_CON_FORWARD))) {
	mlog("signal '%s'(%i) to job '%s' - reason '%s' - forwarder\n",
	     signal2String(signal), signal, job->id, reason);
	WriteDigit(comForward, CMD_LOCAL_SIGNAL);
	WriteDigit(comForward, signal);
	wDoSend(comForward);
    } else if ((child = findChildByJobid(job->id, -1))) {
	mlog("signal '%s'(%i) to job '%s' - reason '%s' - child\n",
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
	return false;
    }

    return true;
}
