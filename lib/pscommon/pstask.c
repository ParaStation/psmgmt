/*
 *               ParaStation3
 * psitask.c
 *
 * ParaStation tasks and helper functions.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: pstask.c,v 1.2 2002/07/08 16:42:52 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: pstask.c,v 1.2 2002/07/08 16:42:52 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pstask.h"

PStask_t *PStask_new()
{
    PStask_t *task;

    task = (PStask_t*)malloc(sizeof(PStask_t));

    PStask_init(task);

    return task;
}

int PStask_init(PStask_t *task)
{
    task->link = 0;
    task->rlink = 0;
    task->tid = 0;
    task->ptid = 0;
    task->uid = -1;
    task->gid = -1;
    task->group = TG_ANY;
    task->rank = -1;
    task->loggernode = 0;
    task->loggerport = 0;
    task->fd = -1;
    task->error = 0;
    task->confirmed = 1;
    task->workingdir = NULL;
    task->argc = 0;
    task->argv = NULL;
    task->environ = NULL;
    task->childsignal = -1;
    task->signalsender = NULL;
    task->signalreceiver = NULL;
    /* CAUTION: each pointer must be set to NULL in PStask_encode
       after copying an task to the buffer */

    return 1;
}

int PStask_reinit(PStask_t *task)
{
    int i;

    if (task==0)
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

    while (task->signalsender) {
	struct PSsignal_t* thissignal = task->signalsender;
	task->signalsender = thissignal->next;
	free(thissignal);
    }

    while (task->signalreceiver) {
	struct PSsignal_t* thissignal = task->signalreceiver;
	task->signalreceiver = thissignal->next;
	free(thissignal);
    }
    PStask_init(task);

    return 1;
}

int PStask_delete(PStask_t * task)
{
    if (task==0)
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

    snprintf(txt, size, " links(%08lx,%08lx) tid %08lx, ptid %08lx, uid %d"
	     " loggernode %x loggerport %d"
	     " group0x%lx rank %x error %ld fd %d argc %d ",
	     (long)task->link, (long)task->rlink, task->tid, task->ptid,
	     task->uid, task->loggernode, task->loggerport,
	     task->group, task->rank, task->error, task->fd, task->argc);
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

int PStask_encode(char* buffer, PStask_t * task)
{
    int msglen=0;
    int i;

#if 0
    if (PSP_DEBUGTASK & PSI_debugmask) {
	sprintf(PSI_txt,"PStask_encode(%lx,task(",(long)buffer);
	PStask_sprintf(PSI_txt+strlen(PSI_txt),task);
	sprintf(PSI_txt+strlen(PSI_txt),")\n");
	PSI_logerror(PSI_txt);
    }
#endif

    msglen = sizeof(PStask_t);
    memcpy(buffer, task, sizeof(PStask_t));
    /* reinit the pointers */
    ((PStask_t*)buffer)->workingdir = NULL;
    ((PStask_t*)buffer)->argv = NULL;
    ((PStask_t*)buffer)->environ = NULL;
    ((PStask_t*)buffer)->signalsender = NULL;
    ((PStask_t*)buffer)->signalreceiver = NULL;

    if (task->workingdir) {
	strcpy(&buffer[msglen],task->workingdir);
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

int PStask_decode(char* buffer, PStask_t * task)
{
    int msglen, len, count, i;

#if 0
    if (PSP_DEBUGTASK & PSI_debugmask) {
	sprintf(PSI_txt,"PStask_decode(%lx,%lx)\n",
		(long)buffer,(long)task);
	PSI_logerror(PSI_txt);
    }
#endif

    if (!task)
	return 0;
    /* unpack buffer */

    msglen = sizeof(PStask_t);
    memcpy(task, buffer, sizeof(PStask_t));
    task->link = NULL;
    task->rlink = NULL;

    len = strlen(&buffer[msglen]);

    if (len) {
	task->workingdir = strdup(&buffer[msglen]);
    } else {
	task->workingdir = NULL;
    }
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

    if (count) {
	task->environ = (char**)malloc((count+1)*sizeof(char*));
    } else {
	task->environ = NULL;
    }
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

#if 0
    if (PSP_DEBUGTASK & PSI_debugmask) {
	sprintf(PSI_txt, "PStask_decode(%lx,task(", (long)buffer);
	PStask_sprintf(PSI_txt+strlen(PSI_txt), task);
	sprintf(PSI_txt+strlen(PSI_txt), ")Returns %d\n", msglen);
	PSI_logerror(PSI_txt);
    }
#endif

    return msglen;
}
