/*
 *               ParaStation3
 * psitask.c
 *
 * ParaStation tasks and helper functions.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psitask.c,v 1.5 2002/02/19 09:31:06 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psitask.c,v 1.5 2002/02/19 09:31:06 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "psilog.h"

#include "psitask.h"

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
    task->nodeno = -1;
    task->group = TG_ANY;
    task->rank = -1;
    task->options = TaskOption_SENDSTDHEADER;
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

void PStask_sprintf(char*txt, PStask_t * task)
{
    int i;

    if (task==NULL)
	return ;

    sprintf(txt," links(%08lx,%08lx) "
	    "tid %08lx,ptid %08lx, uid %d loggernode %x loggerport %d node %d"
	    " group0x%lx rank %x options = %lx error %ld fd %d argc %d ",
	    (long)task->link, (long)task->rlink, task->tid, task->ptid,
	    task->uid, task->loggernode, task->loggerport, task->nodeno,
	    task->group, task->rank, task->options, task->error, task->fd,
	    task->argc);
    sprintf(txt+strlen(txt),"dir=\"%s\",command=\"",
	    (task->workingdir)?task->workingdir:"");
    for (i=0;i<task->argc;i++) {
	sprintf(txt+strlen(txt),"%s ",task->argv[i]);
    }
    sprintf(txt+strlen(txt),"\" env=");

    if (task->environ) {
	int i;
	for (i=0; task->environ[i]; i++) {
	    sprintf(txt+strlen(txt), "%s ", task->environ[i]);
	}
    }
}

int PStask_encode(char* buffer, PStask_t * task)
{
    int msglen=0;
    int i;

#if defined(DEBUG)||defined(PSID)
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

#if defined(DEBUG)||defined(PSID)
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

#if defined(DEBUG)||defined(PSID)
    if (PSP_DEBUGTASK & PSI_debugmask) {
	sprintf(PSI_txt, "PStask_decode(%lx,task(", (long)buffer);
	PStask_sprintf(PSI_txt+strlen(PSI_txt), task);
	sprintf(PSI_txt+strlen(PSI_txt), ")Returns %d\n", msglen);
	PSI_logerror(PSI_txt);
    }
#endif

    return msglen;
}

void PStask_setsignalreceiver(PStask_t* task, long tid, int signal)
{
    struct PSsignal_t* thissignal;
    struct PSsignal_t* prevsignal;

    thissignal = (struct PSsignal_t*) malloc(sizeof(struct PSsignal_t));
    thissignal->signal = signal;
    thissignal->tid = tid;
    thissignal->next = NULL;

    if (task->signalreceiver==NULL) {
	task->signalreceiver = thissignal;
    } else {
	prevsignal = task->signalreceiver;
	while (prevsignal->next) prevsignal = prevsignal->next;
	prevsignal->next = thissignal;
    }
}

long PStask_getsignalreceiver(PStask_t *task, int *signal)
{
    long tid;
    struct PSsignal_t *thissignal;
    struct PSsignal_t *prevsignal;

    if (task->signalreceiver==NULL)
	return 0;

    if ((*signal== -1) || (task->signalreceiver->signal == *signal)) {
	/*
	 * get the receiver of any signal
	 * or the first signal sent is the one requested
	 */
	thissignal = task->signalreceiver;
	*signal = thissignal->signal;
	tid = thissignal->tid;

	task->signalreceiver = thissignal->next;
	free(thissignal);
	return tid;
    }

    for (prevsignal = task->signalreceiver,
	     thissignal = task->signalreceiver; thissignal;) {
	if (thissignal->signal==*signal) {
	    *signal = thissignal->signal;
	    tid = thissignal->tid;
	    prevsignal->next = thissignal->next;
	    free(thissignal);
	    return tid;
	} else {
	    prevsignal= thissignal;
	    thissignal = thissignal->next;
	}
    }

    return 0;
}

void PStask_setsignalsender(PStask_t *task, long tid, int signal)
{
    struct PSsignal_t *thissignal;
    struct PSsignal_t *prevsignal;

    thissignal = (struct PSsignal_t*) malloc(sizeof(struct PSsignal_t));
    thissignal->signal = signal;
    thissignal->tid = tid;
    thissignal->next = NULL;

    if (task->signalsender==NULL) {
	task->signalsender = thissignal;
    } else{
	prevsignal = task->signalsender;
	while (prevsignal->next) prevsignal = prevsignal->next;
	prevsignal->next = thissignal;
    }
}

long PStask_getsignalsender(PStask_t *task, int *signal)
{
    long tid;
    struct PSsignal_t *thissignal;
    struct PSsignal_t *prevsignal;

    if (task->signalsender==NULL)
	return 0;

    if ((*signal== -1) || (task->signalsender->signal == *signal)) {
	/*
	 * get the sender of any signal
	 * or the first signal sent is the one requested
	 */
	 thissignal = task->signalsender;
	 *signal = thissignal->signal;
	 tid = thissignal->tid;
	 task->signalsender = thissignal->next;
	 free(thissignal);
	 return tid;
    }

    for (prevsignal = task->signalsender,
	     thissignal = task->signalsender; thissignal;) {
	if (thissignal->signal==*signal) {
	    *signal = thissignal->signal;
	    tid = thissignal->tid;
	    prevsignal->next = thissignal->next;
	    free(thissignal);
	    return tid;
	} else {
	    prevsignal = thissignal;
	    thissignal = thissignal->next;
	}
    }

    return 0;
}

/****************** TAKSLIST MANIPULATING ROUTINES **********************/

void PStasklist_delete(PStask_t **list)
{
    PStask_t *task;
#ifdef DEBUG
    if (PSP_DEBUGTASK & PSI_debugmask) {
	sprintf(PSI_txt, "PStasklist_delete(%p[%lx])\n",
		list, *list ? (long)*list : -1);
	PSI_logerror(PSI_txt);
    }
#endif
    while (*list) {
	task = (*list);
	(*list) = (*list)->link;
	PStask_delete(task);
    }
}

int PStasklist_enqueue(PStask_t **list, PStask_t *newtask)
{
#ifdef DEBUG
    if (PSP_DEBUGTASK & PSI_debugmask) {
	sprintf(PSI_txt, "PStasklist_enqueue(%p[%lx],%p)\n",
		list, *list ? (long)*list : -1, newtask);
	PSI_logerror(PSI_txt);
    }
#endif
    if (*list) {
	newtask->link = (*list)->link;
	newtask->rlink = (*list);
	(*list)->link = newtask;
	if (newtask->link) {
	    newtask->link->rlink = newtask;
	}
    } else {
	(*list) = newtask;
    }

    return 0;
}

PStask_t* PStasklist_dequeue(PStask_t **list, long tid)
{
    PStask_t *task = NULL;
    PStask_t *prevtask = NULL;

#ifdef DEBUG
    if (PSP_DEBUGTASK & PSI_debugmask) {
	sprintf(PSI_txt, "PStasklist_dequeue(%p[%lx],%lx)\n",
		list, *list ? (long)*list : -1, tid);
	PSI_logerror(PSI_txt);
    }
#endif
    task = (*list);

    if (tid!=-1) {
	while ((task)&&(task->tid != tid)) {
	    prevtask = task;
	    task = task->link;
	}
    }
    if (task) {
	if (prevtask) {
	    /* found in the middle of the list */
	    prevtask->link = task->link;
	} else {
	    /* the task was the head of the list */
	    *list = task->link;
	}
	if (task->link) {
	    task->link->rlink = task->rlink;
	}
    }

    return task;
}

PStask_t* PStasklist_find(PStask_t *list, long tid)
{
    PStask_t *task;

#ifdef DEBUG
    if (PSP_DEBUGTASK & PSI_debugmask) {
	sprintf(PSI_txt, "PStasklist_find(%p,%lx)\n", list, tid);
	PSI_logerror(PSI_txt);
    }
#endif

    task = list;

    while ((task)&&(task->tid != tid)) {
	task = task->link;
    }

    return task;
}
