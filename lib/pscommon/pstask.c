/*
 *               ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2007 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <signal.h>
#include <sys/ioctl.h>

#include "pscommon.h"

#include "pstask.h"

char* PStask_printGrp(PStask_group_t tg)
{
    return (tg==TG_ANY) ? "TG_ANY" :
	(tg==TG_ADMIN) ? "TG_ADMIN" :
	(tg==TG_RESET) ? "TG_RESET" :
	(tg==TG_LOGGER) ? "TG_LOGGER" :
	(tg==TG_FORWARDER) ? "TG_FORWARDER" :
	(tg==TG_SPAWNER) ? "TG_SPAWNER" :
	(tg==TG_GMSPAWNER) ? "TG_GMSPAWNER" :
	(tg==TG_MONITOR) ? "TG_MONITOR" :
	(tg==TG_PSCSPAWNER) ? "TG_PSCSPAWNER" :
	(tg==TG_ADMINTASK) ? "TG_ADMINTASK" :
	(tg==TG_SERVICE) ? "TG_SERVICE" :
	"UNKNOWN";
}

PStask_t* PStask_new(void)
{
    PStask_t* task;

    task = (PStask_t*)malloc(sizeof(PStask_t));

    if (task) PStask_init(task);

    return task;
}

int PStask_init(PStask_t* task)
{
    task->next = NULL;
    task->prev = NULL;

    task->tid = 0;
    task->ptid = 0;
    task->uid = -1;
    task->gid = -1;
    task->aretty = 0;
    task->interactive = 0;
    task->stdin_fd = -1;
    task->stdout_fd = -1;
    task->stderr_fd = -1;
    task->group = TG_ANY;
    task->loggertid = 0;
    task->forwardertid = 0;
    task->rank = -1;
    task->fd = -1;
    task->workingdir = NULL;
    task->argc = 0;
    task->argv = NULL;
    task->environ = NULL;
    task->envSize = 0;
    task->relativesignal = SIGTERM;
    task->pendingReleaseRes = 0;
    task->released = 0;
    task->duplicate = 0;
    task->suspended = 0;
    task->removeIt = 0;
    task->killat = 0;
    task->protocolVersion = -1;

    task->childs = NULL;

    task->request = NULL;
    task->partitionSize = 0;
    task->options = 0;
    task->partition = NULL;
    task->nextRank = -1;

    task->signalSender = NULL;
    task->signalReceiver = NULL;
    task->assignedSigs = NULL;

    return 1;
}

int PStask_reinit(PStask_t* task)
{
    int i;

    if (!task)
	return 0;

    if (task->workingdir)
	free(task->workingdir);

    for (i=0;i<task->argc;i++)
	if (task->argv && task->argv[i])
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

    while (task->childs) {
	PStask_sig_t* thissignal = task->childs;
	task->childs = thissignal->next;
	free(thissignal);
    }

    if (task->request) PSpart_delReq(task->request);
    if (task->partition) free(task->partition);

    while (task->signalSender) {
	PStask_sig_t* thissignal = task->signalSender;
	task->signalSender = thissignal->next;
	free(thissignal);
    }

    while (task->signalReceiver) {
	PStask_sig_t* thissignal = task->signalReceiver;
	task->signalReceiver = thissignal->next;
	free(thissignal);
    }

    while (task->assignedSigs) {
	PStask_sig_t* thissignal = task->assignedSigs;
	task->assignedSigs = thissignal->next;
	free(thissignal);
    }
    
    PStask_init(task);

    return 1;
}

int PStask_delete(PStask_t* task)
{
    if (!task)
	return 0;

    PStask_reinit(task);
    free(task);

    return 1;
}

PStask_sig_t* PStask_cloneSigList(PStask_sig_t* list)
{
    PStask_sig_t* clone = NULL,* signal = NULL;

    while (list) {
	if (!signal) {
	    /* First item */
	    signal = (PStask_sig_t*) malloc(sizeof(PStask_sig_t));
	    clone = signal;
	} else {
	    signal->next = (PStask_sig_t*) malloc(sizeof(PStask_sig_t));
	    signal = signal->next;
	}

	if (!signal) break;

	signal->tid = list->tid;
	signal->signal = list->signal;
	signal->next = NULL;

	list = list->next;
    }

    if (list) {
	/* loop interrupted due to memory-shortage */
	while (clone) {
	    PStask_sig_t* thissignal = clone;
	    clone = thissignal->next;
	    free(thissignal);
	}
	PSC_log(-1, "%s(): no memory\n", __func__);
    }

    return clone;
}

PStask_t* PStask_clone(PStask_t* task)
{
    PStask_t *clone;
    int i;

    clone = PStask_new();

    /* clone->tid = 0; */
    clone->ptid = task->ptid;
    clone->uid = task->uid;
    clone->gid = task->gid;
    clone->aretty = task->aretty;
    clone->interactive = task->interactive;
    clone->stdin_fd = task->stdin_fd;
    clone->stdout_fd = task->stdout_fd;
    clone->stderr_fd = task->stderr_fd;
    clone->termios = task->termios;
    clone->winsize = task->winsize;
    clone->group = task->group;
    clone->loggertid = task->loggertid;
    clone->forwardertid = task->forwardertid;
    clone->rank = task->rank;
    /* clone->fd = -1; */
    clone->workingdir = (task->workingdir) ? strdup(task->workingdir) : NULL;
    clone->argc = task->argc;
    clone->argv = (char**)malloc(sizeof(char*)*(task->argc+1));
    for (i=0; i<task->argc; i++) clone->argv[i] = strdup(task->argv[i]);
    clone->argv[clone->argc] = NULL;
    if (task->envSize)
	clone->environ = (char**)malloc(task->envSize*sizeof(char*));

    if (clone->environ) {
	for (i=0; task->environ[i]; i++) {
	    clone->environ[i] = strdup(task->environ[i]);
	}
	clone->environ[i] = NULL;
    }
    clone->envSize = task->envSize;
    clone->relativesignal = task->relativesignal;
    clone->pendingReleaseRes = task->pendingReleaseRes;
    clone->released = task->released;
    clone->duplicate = task->duplicate;
    clone->suspended = task->suspended;
    clone->removeIt = task->removeIt;
    clone->killat = task->killat;
    clone->protocolVersion = task->protocolVersion;

    clone->childs = PStask_cloneSigList(task->childs);

    clone->request = NULL; /* Do not clone requests */
    clone->partitionSize = task->partitionSize;
    clone->options = task->options;
    clone->partition = malloc(clone->partitionSize * sizeof(short));
    memcpy(clone->partition, task->partition,
	   clone->partitionSize * sizeof(short));
    clone->nextRank = task->nextRank;
 
    clone->signalSender = PStask_cloneSigList(task->signalSender);
    clone->signalReceiver = PStask_cloneSigList(task->signalReceiver);
    clone->assignedSigs = PStask_cloneSigList(task->assignedSigs);

    return clone;
}

static void snprintfStruct(char *txt, size_t size, PStask_t * task)
{
    if (!task) return;

    snprintf(txt, size, "tid 0x%08x ptid 0x%08x uid %d gid %d group %s"
	     " rank %d links(%p,%p) loggertid %08x fd %d argc %d",
	     task->tid, task->ptid, task->uid, task->gid,
 	     PStask_printGrp(task->group), task->rank,
 	     task->next, task->prev, task->loggertid, task->fd, task->argc);
}

static void snprintfArgv(char *txt, size_t size, PStask_t * task)
{
    if (!task) return;

    snprintf(txt, size, "dir=\"%s\",command=\"",
	     (task->workingdir)?task->workingdir:"");
    if (strlen(txt)+1 == size) return;

    if (task->argv) {
	int i;
	for (i=0; i<task->argc; i++) {
	    snprintf(txt+strlen(txt), size-strlen(txt), "%s ", task->argv[i]);
	    if (strlen(txt)+1 == size) return;
	}
    }
    snprintf(txt+strlen(txt), size-strlen(txt), "\"");
}

static void snprintfEnv(char *txt, size_t size, PStask_t * task)
{
    if (!task) return;

    snprintf(txt, size, "env=");
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


void PStask_snprintf(char *txt, size_t size, PStask_t * task)
{
    if (!task) return;

    snprintfStruct(txt, size, task);
    if (strlen(txt)+1 == size) return;
    snprintf(txt+strlen(txt), size-strlen(txt), " ");
    if (strlen(txt)+1 == size) return;
    snprintfArgv(txt+strlen(txt), size-strlen(txt), task);
    if (strlen(txt)+1 == size) return;
    snprintf(txt+strlen(txt), size-strlen(txt), " ");
    if (strlen(txt)+1 == size) return;
    snprintfEnv(txt+strlen(txt), size-strlen(txt), task);
}

static struct {
    PStask_ID_t tid;
    PStask_ID_t ptid;
    uid_t uid;
    gid_t gid;
    uint32_t aretty;
    struct termios termios;
    struct winsize winsize;
    PStask_group_t group;
    int32_t rank;
    PStask_ID_t loggertid;
    int32_t argc;
} tmpTask;

static char taskString[256];

size_t PStask_encodeFull(char *buffer, size_t size, PStask_t *task)
{
    size_t msglen;
    int i;

    PStask_snprintf(taskString, sizeof(taskString), task);
    PSC_log(PSC_LOG_TASK, "%s(%p, %ld, task(%s))\n",
	    __func__, buffer, (long)size, taskString);

    msglen = sizeof(tmpTask);
    if (msglen > size) return msglen; /* buffer to small */

    tmpTask.tid = task->tid;
    tmpTask.ptid = task->ptid;
    tmpTask.uid = task->uid;
    tmpTask.gid = task->gid;
    tmpTask.aretty = task->aretty;
    tmpTask.termios = task->termios;
    tmpTask.winsize = task->winsize;
    tmpTask.group = task->group;
    tmpTask.rank = task->rank;
    tmpTask.loggertid = task->loggertid;
    tmpTask.argc = task->argc;

    memcpy(buffer, &tmpTask, sizeof(tmpTask));

    if (task->workingdir) {
	if (msglen + strlen(task->workingdir) < size) {
	    strcpy(&buffer[msglen], task->workingdir);
	    msglen += strlen(task->workingdir);
	} else {
	    /* buffer to small */
	    return msglen + strlen(task->workingdir);
	}
    } else {
	buffer[msglen]='\0';
    }
    msglen++; /* zero byte */

    for (i=0; i<task->argc; i++) {
	if (msglen + strlen(task->argv[i]) < size) {
	    strcpy(&buffer[msglen], task->argv[i]);
	    msglen += strlen(task->argv[i])+1;
	} else {
	    /* buffer to small */
	    return msglen + strlen(task->argv[i]) + 1;
	}
    }

    if (task->environ) {
	int i;
	for (i=0; task->environ[i]; i++) {
	    if (msglen + strlen(task->environ[i]) < size) {
		strcpy(&buffer[msglen], task->environ[i]);
		msglen += strlen(task->environ[i])+1;
	    } else {
		/* buffer to small */
		return msglen + strlen(task->environ[i]) + 1;
	    }
	}
    }
    /* append zero byte */
    if (msglen < size) {
	buffer[msglen] = '\0';
	msglen++;
    } else {
	/* buffer to small */
	return msglen + 1;
    }

    return msglen;
}

int PStask_decodeFull(char *buffer, PStask_t *task)
{
    int msglen, len, count, i;

    PStask_snprintf(taskString, sizeof(taskString), task);
    PSC_log(PSC_LOG_TASK, "%s(%p, task(%s))\n",
	    __func__, buffer, taskString);

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
    task->aretty = tmpTask.aretty;
    task->termios = tmpTask.termios;
    task->winsize = tmpTask.winsize;
    task->group = tmpTask.group;
    task->rank = tmpTask.rank;
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
    task->argv[task->argc] = NULL;

    /* Get number of environment variables */
    count = 0;
    len = msglen;
    while (strlen(&buffer[len])) {
	count ++;
	len += strlen(&buffer[len])+1;
    }

    if (count) {
	task->environ = (char**)malloc((count+1)*sizeof(char*));
	task->envSize = count+1;
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

    PStask_snprintf(taskString, sizeof(taskString), task);
    PSC_log(PSC_LOG_TASK, " received task = (%s)\n", taskString);
    PSC_log(PSC_LOG_TASK, "%s returns %d\n", __func__, msglen);

    return msglen;
}

size_t PStask_encodeTask(char *buffer, size_t size, PStask_t *task)
{
    size_t msglen = sizeof(tmpTask);

    snprintfStruct(taskString, sizeof(taskString), task);
    PSC_log(PSC_LOG_TASK, "%s(%p, %ld, task(%s))\n",
	    __func__, buffer, (long)size, taskString);

    if (msglen > size) return msglen; /* buffer to small */

    tmpTask.tid = task->tid;
    tmpTask.ptid = task->ptid;
    tmpTask.uid = task->uid;
    tmpTask.gid = task->gid;
    tmpTask.aretty = task->aretty;
    tmpTask.termios = task->termios;
    tmpTask.winsize = task->winsize;
    tmpTask.group = task->group;
    tmpTask.rank = task->rank;
    tmpTask.loggertid = task->loggertid;
    tmpTask.argc = task->argc;

    memcpy(buffer, &tmpTask, sizeof(tmpTask));

    if (task->workingdir) {
	if (msglen + strlen(task->workingdir) < size) {
	    strcpy(&buffer[msglen], task->workingdir);
	    msglen += strlen(task->workingdir);
	} else {
	    /* buffer to small */
	    return msglen + strlen(task->workingdir);
	}
    } else {
	buffer[msglen]='\0';
    }
    msglen++; /* zero byte */

    return msglen;
}

int PStask_decodeTask(char *buffer, PStask_t *task)
{
    int msglen, len;

    if (!task) {
	PSC_log(-1, "%s: task is NULL\n", __func__);
	return 0;
    }

    snprintfStruct(taskString, sizeof(taskString), task);
    PSC_log(PSC_LOG_TASK, "%s(%p, task(%s))\n",
	    __func__, buffer, taskString);

    PStask_reinit(task);

    /* unpack buffer */
    msglen = sizeof(tmpTask);
    memcpy(&tmpTask, buffer, sizeof(tmpTask));

    task->tid = tmpTask.tid;
    task->ptid = tmpTask.ptid;
    task->uid = tmpTask.uid;
    task->gid = tmpTask.gid;
    task->aretty = tmpTask.aretty;
    task->termios = tmpTask.termios;
    task->winsize = tmpTask.winsize;
    task->group = tmpTask.group;
    task->rank = tmpTask.rank;
    task->loggertid = tmpTask.loggertid;
    task->argc = tmpTask.argc;

    len = strlen(&buffer[msglen]);

    if (len) task->workingdir = strdup(&buffer[msglen]);
    msglen += len+1;

    snprintfStruct(taskString, sizeof(taskString), task);
    PSC_log(PSC_LOG_TASK, " received task = (%s)\n", taskString);
    PSC_log(PSC_LOG_TASK, "%s returns %d\n", __func__, msglen);

    return msglen;
}

size_t PStask_encodeArgs(char *buffer, size_t size, PStask_t *task)
{
    size_t msglen = 0;
    int i;

    snprintfArgv(taskString, sizeof(taskString), task);
    PSC_log(PSC_LOG_TASK, "%s(%p, %ld, task %s / argv(%s))\n",
	    __func__, buffer, (long)size, PSC_printTID(task->tid), taskString);

    for (i=0; i<task->argc; i++) {
	if (msglen + strlen(task->argv[i]) < size) {
	    strcpy(&buffer[msglen], task->argv[i]);
	    msglen += strlen(task->argv[i])+1;
	} else {
	    /* buffer to small */
	    return msglen + strlen(task->argv[i]) + 1;
	}
    }

    return msglen;
}

int PStask_decodeArgs(char *buffer, PStask_t *task)
{
    int msglen=0, i;

    if (!task) {
	PSC_log(-1, "%s: task is NULL\n", __func__);
	return 0;
    }

    snprintfStruct(taskString, sizeof(taskString), task);
    PSC_log(PSC_LOG_TASK, "%s(%p, task(%s))\n",
	    __func__, buffer, taskString);


    /* Get the arguments */
    task->argv = (char**)malloc(sizeof(char*)*(task->argc+1));
    for (i=0; i<task->argc; i++) {
	task->argv[i] = strdup(&buffer[msglen]);
	msglen += strlen(&buffer[msglen])+1;
    }
    task->argv[task->argc] = NULL;

    snprintfArgv(taskString, sizeof(taskString), task);
    PSC_log(PSC_LOG_TASK, " received argv = (%s)\n", taskString);
    PSC_log(PSC_LOG_TASK, "%s returns %d\n", __func__, msglen);

    return msglen;
}

size_t PStask_encodeEnv(char *buffer, size_t size, PStask_t *task, int *cur)
{
    size_t msglen = 0;
    int first=*cur;

    snprintfEnv(taskString, sizeof(taskString), task);
    PSC_log(PSC_LOG_TASK, "%s(%p, %ld, task %s / env(%s))\n",
	    __func__, buffer, (long)size, PSC_printTID(task->tid), taskString);

    if (task->environ) {
	for (; task->environ[*cur]; (*cur)++) {
	    if (msglen + strlen(task->environ[*cur]) + 1 < size) {
		strcpy(&buffer[msglen], task->environ[*cur]);
		msglen += strlen(task->environ[*cur])+1;
	    } else if (*cur > first) {
		break;
	    } else {
		/* buffer to small */
		return msglen + strlen(task->environ[*cur]) + 1;
	    }
	}
    }
    /* append zero byte */
    if (msglen < size) {
	buffer[msglen] = '\0';
	msglen++;
    } else {
	/* buffer to small */
	return msglen + 1;
    }

    return msglen;
}

int PStask_decodeEnv(char *buffer, PStask_t *task)
{
    int msglen=0, i;
    int envSize, envNew;

    if (!task) {
	PSC_log(-1, "%s: task is NULL\n", __func__);
	return 0;
    }

    snprintfStruct(taskString, sizeof(taskString), task);
    PSC_log(PSC_LOG_TASK, "%s(%p, task(%s))\n",
	    __func__, buffer, taskString);

    if (!task) return 0;

    /* Get number of existing environment variables */
    envSize = 0;
    if (task->environ) {
	while (task->environ[envSize]) envSize++;
    }

    /* Get number of new environment variables */
    envNew = task->envSize;
    while (strlen(&buffer[msglen])) {
	envNew ++;
	msglen += strlen(&buffer[msglen])+1;
    }

    /* Extend environ (if necessary) */
    if (envSize+envNew+1 > task->envSize) {
	task->environ = realloc(task->environ,
				(envSize+envNew+1)*sizeof(char*));
	task->envSize = envSize+envNew+1;
    }

    /* Unpack new environment */
    if (task->environ) {
	msglen = 0;
	i = envSize;
	while (strlen(&buffer[msglen])) {
	    task->environ[i] = strdup(&buffer[msglen]);
	    msglen += strlen(&buffer[msglen])+1;
	    i++;
	}
	task->environ[i] = NULL;
	msglen++;
    }

    snprintfEnv(taskString, sizeof(taskString), task);
    PSC_log(PSC_LOG_TASK, " received env = (%s)\n", taskString);
    PSC_log(PSC_LOG_TASK, "%s returns %d\n", __func__, msglen);

    return msglen;
}
