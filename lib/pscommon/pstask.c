/*
 *               ParaStation3
 * psitask.c
 *
 * ParaStation tasks and helper functions.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: pstask.c,v 1.5 2002/07/18 12:51:34 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: pstask.c,v 1.5 2002/07/18 12:51:34 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pscommon.h"

#include "pstask.h"

static char errtxt[256];

char *PStask_printGrp(PStask_group_t tg)
{
    return (tg==TG_ANY) ? "TG_ANY" :
	(tg==TG_ADMIN) ? "TG_ADMIN" :
	(tg==TG_RESET) ? "TG_RESET" :
	(tg==TG_LOGGER) ? "TG_ADMIN" : "UNKNOWN";
}

PStask_t *PStask_new()
{
    PStask_t *task;

    task = (PStask_t*)malloc(sizeof(PStask_t));

    PStask_init(task);

    return task;
}

int PStask_init(PStask_t *task)
{
    task->next = NULL;
    task->prev = NULL;

    task->tid = 0;
    task->ptid = 0;
    task->uid = -1;
    task->gid = -1;
    task->group = TG_ANY;
    task->rank = -1;
    task->loggernode = 0; /* obsolete */
    task->loggerport = 0; /* obsolete */
    task->loggertid = 0;
    task->fd = -1;
    task->workingdir = NULL;
    task->argc = 0;
    task->argv = NULL;
    task->environ = NULL;
    task->childsignal = -1;
    task->pendingReleaseRes = 0;
    task->released = 0;

    task->signalSender = NULL;
    task->signalReceiver = NULL;
    task->assignedSigs = NULL;

    return 1;
}

int PStask_reinit(PStask_t *task)
{
    int i;

    if (!task)
	return 0;

    if (task->workingdir)
	free(task->workingdir);

    for (i=0;i<task->argc;i++)
	if (task->argv[i])
	    free(task->argv[i]);
    if (task->argv) free(task->argv);

    if (task->environ) {
	int i;
	for (i=0; task->environ[i]; i++) {
	    free(task->environ[i]);
	}
	free(task->environ);
	task->environ = NULL;
    }

    while (task->signalSender) {
	PStask_sig_t *thissignal = task->signalSender;
	task->signalSender = thissignal->next;
	free(thissignal);
    }

    while (task->signalReceiver) {
	PStask_sig_t *thissignal = task->signalReceiver;
	task->signalReceiver = thissignal->next;
	free(thissignal);
    }

    while (task->assignedSigs) {
	PStask_sig_t *thissignal = task->assignedSigs;
	task->assignedSigs = thissignal->next;
	free(thissignal);
    }
    
    PStask_init(task);

    return 1;
}

int PStask_delete(PStask_t * task)
{
    if (!task)
	return 0;

    PStask_reinit(task);
    free(task);

    return 1;
}

void PStask_snprintf(char *txt, size_t size, PStask_t * task)
{
    int i;

    if (task==NULL)
	return ;

/*      snprintf(txt, size, "tid 0x%08lx ptid 0x%08lx uid %d gid %d group %s" */
/*  	     " rank %d links(0x%08lx,0x%08lx) loggertid %08lx fd %d argc %d ", */
/*  	     task->tid, task->ptid, task->uid, task->gid, */
/*  	     PStask_printGrp(task->group), task->rank, */
/*  	     (long)task->next, (long)task->prev, */
/*  	     task->loggertid, task->fd, task->argc); */
    snprintf(txt, size, " links(0x%08lx,0x%08lx) tid 0x%08lx, ptid 0x%08lx, uid %d"
	     " loggernode 0x%08x loggerport %d group %s rank %d fd %d argc %d ",
	     (long)task->next, (long)task->prev, task->tid, task->ptid,
	     task->uid, task->loggernode, task->loggerport,
	     PStask_printGrp(task->group), task->rank, task->fd, task->argc);
    if (strlen(txt)+1 == size) return;

    snprintf(txt+strlen(txt), size-strlen(txt), "dir=\"%s\",command=\"",
	     (task->workingdir)?task->workingdir:"");
    if (strlen(txt)+1 == size) return;

    for (i=0; i<task->argc; i++) {
	snprintf(txt+strlen(txt), size-strlen(txt), "%s ", task->argv[i]);
	if (strlen(txt)+1 == size) return;
    }

    snprintf(txt+strlen(txt), size-strlen(txt), "\" env=");
    if (strlen(txt)+1 == size) return;

    if (task->environ) {
	int i;
	for (i=0; task->environ[i]; i++) {
	    snprintf(txt+strlen(txt), size-strlen(txt), "%s ",
		     task->environ[i]);
	    if (strlen(txt)+1 == size) return;
	}
    }
}

static struct {
    long tid;
    long ptid;
    uid_t uid;
    gid_t gid;
    PStask_group_t group;
    int rank;
    unsigned int loggernode; /* obsolete */
    int loggerport;          /* obsolete */
    long loggertid;
    int argc;
} tmpTask;
    
int PStask_encode(char *buffer, size_t size, PStask_t *task)
{
    /* @todo size ignored !! */
    int msglen, i;

    snprintf(errtxt, sizeof(errtxt),
	     "PStask_encode(%p, %ld, task(", buffer, (long)size);
    PStask_snprintf(errtxt+strlen(errtxt),
		    sizeof(errtxt)-strlen(errtxt), task);
    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt), ")");
    PSC_errlog(errtxt, 10);

    tmpTask.tid = task->tid;
    tmpTask.ptid = task->ptid;
    tmpTask.uid = task->uid;
    tmpTask.gid = task->gid;
    tmpTask.group = task->group;
    tmpTask.rank = task->rank;
    tmpTask.loggernode = task->loggernode; /* obsolete */
    tmpTask.loggerport = task->loggerport; /* obsolete */
    tmpTask.loggertid = task->loggertid;
    tmpTask.argc = task->argc;

    msglen = sizeof(tmpTask);
    memcpy(buffer, &tmpTask, sizeof(tmpTask));

    if (task->workingdir) {
	strcpy(&buffer[msglen], task->workingdir);
	msglen += strlen(task->workingdir);
    } else {
	buffer[msglen]=0;
    }
    msglen++; /* zero byte */

    for (i=0; i<task->argc; i++) {
	strcpy(&buffer[msglen],task->argv[i]);
	msglen +=strlen(task->argv[i])+1;
    }

    if (task->environ) {
	int i;
	for (i=0; task->environ[i]; i++) {
	    strcpy(&buffer[msglen], task->environ[i]);
	    msglen += strlen(task->environ[i])+1;
	}
    }
    /* append zero byte */
    buffer[msglen] = 0;
    msglen++;

    return msglen;
}

int PStask_decode(char *buffer, PStask_t *task)
{
    int msglen, len, count, i;

    snprintf(errtxt, sizeof(errtxt), "PStask_decode(%p, task(", buffer);
    PStask_snprintf(errtxt+strlen(errtxt),
		    sizeof(errtxt)-strlen(errtxt), task);
    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt), ")");
    PSC_errlog(errtxt, 10);

    if (!task)
	return 0;

    PStask_reinit(task);

    /* unpack buffer */
    msglen = sizeof(tmpTask);
    memcpy(&tmpTask, buffer, sizeof(tmpTask));

    task->tid = tmpTask.tid;
    task->ptid = tmpTask.ptid;
    task->uid = tmpTask.uid;
    task->gid = tmpTask.gid;
    task->group = tmpTask.group;
    task->rank = tmpTask.rank;
    task->loggernode = tmpTask.loggernode; /* obsolete */
    task->loggerport = tmpTask.loggerport; /* obsolete */
    task->loggertid = tmpTask.loggertid;
    task->argc = tmpTask.argc;

    len = strlen(&buffer[msglen]);

    if (len) task->workingdir = strdup(&buffer[msglen]);
    msglen += len+1;

    /* Get the arguments */
    task->argv = (char**)malloc(sizeof(char*)*(task->argc+1));
    for (i=0; i<task->argc; i++) {
	task->argv[i] = strdup(&buffer[msglen]);
	msglen += strlen(&buffer[msglen])+1;
    }
    task->argv[task->argc] = 0;

    /* Get number of environment variables */
    count = 0;
    len = msglen;
    while (strlen(&buffer[len])) {
	count ++;
	len += strlen(&buffer[len])+1;
    }

    if (count) task->environ = (char**)malloc((count+1)*sizeof(char*));

    if (task->environ) {
	i = 0;
	while (strlen(&buffer[msglen])) {
	    task->environ[i] = strdup(&buffer[msglen]);
	    msglen += strlen(&buffer[msglen])+1;
	    i++;
	}
	task->environ[i] = NULL;
	msglen++;
    }

    snprintf(errtxt, sizeof(errtxt), "PStask_decode() returns %d", msglen);
    PSC_errlog(errtxt, 10);

    return msglen;
}
