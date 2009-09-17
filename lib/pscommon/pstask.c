/*
 *               ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2009 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
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

    PSC_log(PSC_LOG_TASK, "%s()\n", __func__);
    task = (PStask_t*)malloc(sizeof(PStask_t));

    if (task) PStask_init(task);

    return task;
}

int PStask_init(PStask_t* task)
{
    PSC_log(PSC_LOG_TASK, "%s(%p)\n", __func__, task);

    INIT_LIST_HEAD(&task->next);
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
    task->childGroup = TG_ANY;
    task->loggertid = 0;
    task->forwardertid = 0;
    task->rank = -1;
    PSCPU_clrAll(task->CPUset);
    task->fd = -1;
    task->workingdir = NULL;
    task->argc = 0;
    task->argv = NULL;
    task->environ = NULL;
    task->envSize = 0;
    task->relativesignal = SIGTERM;
    task->pendingReleaseRes = 0;
    task->pendingReleaseErr = 0;
    task->releaseAnswer = 1;
    task->released = 0;
    task->duplicate = 0;
    task->suspended = 0;
    task->removeIt = 0;
    task->deleted = 0;
    task->killat = 0;
    gettimeofday(&task->started, NULL);
    task->protocolVersion = -1;

    INIT_LIST_HEAD(&task->childs);
    INIT_LIST_HEAD(&task->preReleased);

    task->request = NULL;
    task->partitionSize = 0;
    task->options = 0;
    task->partition = NULL;
    task->nextRank = -1;
    task->spawnNodes = NULL;
    task->spawnNodesSize = 0;
    task->spawnNum = 0;

    INIT_LIST_HEAD(&task->signalSender);
    INIT_LIST_HEAD(&task->signalReceiver);
    INIT_LIST_HEAD(&task->assignedSigs);

    return 1;
}

static void delSigList(list_t *list)
{
    list_t *s, *tmp;

    list_for_each_safe(s, tmp, list) {
	PStask_sig_t *signal = list_entry(s, PStask_sig_t, next);
	list_del(&signal->next);
	free(signal);
    }
}

int PStask_reinit(PStask_t* task)
{
    int i;

    PSC_log(PSC_LOG_TASK, "%s(%p)\n", __func__, task);

    if (!task)
	return 0;

    if (!list_empty(&task->next)) list_del_init(&task->next);

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

    delSigList(&task->childs);
    delSigList(&task->preReleased);

    if (task->request) PSpart_delReq(task->request);
    if (task->partition) free(task->partition);
    if (task->spawnNodes) free(task->spawnNodes);

    delSigList(&task->signalSender);
    delSigList(&task->signalReceiver);
    delSigList(&task->assignedSigs);

    PStask_init(task);

    return 1;
}

int PStask_delete(PStask_t* task)
{
    PSC_log(PSC_LOG_TASK, "%s(%p)\n", __func__, task);

    if (!task)
	return 0;

    PStask_reinit(task);
    free(task);

    return 1;
}

static void cloneSigList(list_t *cloneList, list_t *origList)
{
    list_t *s;

    PSC_log(PSC_LOG_TASK, "%s(%p)\n", __func__, origList);

    delSigList(cloneList);

    list_for_each(s, origList) {
	PStask_sig_t *origSig = list_entry(s, PStask_sig_t, next);
	PStask_sig_t *cloneSig = malloc(sizeof(PStask_sig_t));

	if (!cloneSig) {
	    delSigList(cloneList);
	    PSC_warn(-1, ENOMEM, "%s()", __func__);
	    break;
	}

	cloneSig->tid = origSig->tid;
	cloneSig->signal = origSig->signal;
	list_add_tail(&cloneSig->next, cloneList);
    }
}

PStask_t* PStask_clone(PStask_t* task)
{
    PStask_t *clone;
    int i;

    PSC_log(PSC_LOG_TASK, "%s(%p)\n", __func__, task);

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
    clone->childGroup = task->childGroup;
    clone->loggertid = task->loggertid;
    clone->forwardertid = task->forwardertid;
    clone->rank = task->rank;
    memcpy(clone->CPUset, task->CPUset, sizeof(clone->CPUset));
    /* clone->fd = -1; */

    clone->workingdir = (task->workingdir) ? strdup(task->workingdir) : NULL;
    if (task->workingdir && !clone->workingdir) {
	PSC_warn(-1, errno, "%s: strdup(workingdir)", __func__);
	goto error;
    }

    clone->argc = task->argc;
    if (!task->argv) {
	PSC_log(-1, "%s: argv is NULL\n", __func__);
	errno = EINVAL;
	goto error;
    }
    clone->argv = (char**)malloc(sizeof(char*)*(task->argc+1));
    if (!clone->argv) {
	PSC_warn(-1, errno, "%s: malloc(argv)", __func__);
	goto error;
    }
    for (i=0; i<task->argc; i++) {
	if (!task->argv[i]) {
	    PSC_log(-1, "%s: argv[%d] is NULL\n", __func__, i);
	    errno = EINVAL;
	    goto error;
	}
	clone->argv[i] = strdup(task->argv[i]);
	if (!clone->argv[i]) {
	    PSC_warn(-1, errno, "%s: strdup(argv[%d])", __func__, i);
	    goto error;
	}
    }
    clone->argv[clone->argc] = NULL;

    if (task->envSize) {
	if (!task->environ) {
	    PSC_log(-1, "%s: environ is NULL\n", __func__);
	    errno = EINVAL;
	    goto error;
	}
	clone->environ = (char**)malloc(task->envSize*sizeof(char*));
	if (!clone->environ) {
	    PSC_warn(-1, errno, "%s: malloc(environ)", __func__);
	    goto error;
	}
	for (i=0; task->environ[i]; i++) {
	    if (!task->environ[i]) {
		PSC_log(-1, "%s: environ[%d] is NULL\n", __func__, i);
		errno = EINVAL;
		goto error;
	    }
	    clone->environ[i] = strdup(task->environ[i]);
	    if (!clone->environ[i]) {
		PSC_warn(-1, errno, "%s: strdup(environ[%d])", __func__, i);
		goto error;
	    }
	}
	clone->environ[i] = NULL;
    }
    clone->envSize = task->envSize;
    clone->relativesignal = task->relativesignal;
    clone->pendingReleaseRes = task->pendingReleaseRes;
    clone->pendingReleaseErr = task->pendingReleaseErr;
    clone->releaseAnswer = task->releaseAnswer;
    clone->released = task->released;
    clone->duplicate = task->duplicate;
    clone->suspended = task->suspended;
    clone->removeIt = task->removeIt;
    clone->deleted = task->deleted;
    clone->killat = task->killat;
    gettimeofday(&clone->started, NULL);
    clone->protocolVersion = task->protocolVersion;

    cloneSigList(&clone->childs, &task->childs);
    cloneSigList(&clone->preReleased, &task->preReleased);

    clone->request = NULL; /* Do not clone requests */
    clone->partitionSize = task->partitionSize;
    clone->options = task->options;
    clone->partition = malloc(task->partitionSize * sizeof(*task->partition));
    if (!clone->partition) {
	PSC_warn(-1, errno, "%s: malloc(partition)", __func__);
	goto error;
    }
    memcpy(clone->partition, task->partition,
	   task->partitionSize * sizeof(*task->partition));
    clone->nextRank = task->nextRank;
    clone->spawnNodesSize = task->spawnNodesSize;
    clone->spawnNodes = malloc(task->spawnNodesSize*sizeof(*task->spawnNodes));
    if (!clone->spawnNodes) {
	PSC_warn(-1, errno, "%s: malloc(spawnNodes)", __func__);
	goto error;
    }
    memcpy(clone->spawnNodes, task->spawnNodes,
	   clone->spawnNodesSize * sizeof(*task->spawnNodes));
    clone->spawnNum = task->spawnNum;

    cloneSigList(&clone->signalSender, &task->signalSender);
    cloneSigList(&clone->signalReceiver, &task->signalReceiver);
    cloneSigList(&clone->assignedSigs, &task->assignedSigs);

    return clone;

error:
    PStask_delete(clone);
    return NULL;
}

static void snprintfStruct(char *txt, size_t size, PStask_t *task)
{
    if (!task) return;

    snprintf(txt, size, "tid 0x%08x ptid 0x%08x uid %d gid %d group %s"
	     " childGroup %s rank %d cpus %s loggertid %08x fd %d argc %d",
	     task->tid, task->ptid, task->uid, task->gid,
	     PStask_printGrp(task->group), PStask_printGrp(task->childGroup),
	     task->rank, PSCPU_print(task->CPUset), task->loggertid, task->fd,
	     task->argc);
}

static void snprintfArgv(char *txt, size_t size, PStask_t *task)
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

static void snprintfEnv(char *txt, size_t size, char **env)
{
    snprintf(txt, size, "env=");
    if (strlen(txt)+1 == size) return;

    if (env) {
	int i;
	for (i=0; env[i]; i++) {
	    snprintf(txt+strlen(txt), size-strlen(txt), "%s ", env[i]);
	    if (strlen(txt)+1 == size) return;
	}
    }
}


void PStask_snprintf(char *txt, size_t size, PStask_t *task)
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
    snprintfEnv(txt+strlen(txt), size-strlen(txt), task->environ);
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
    if (! task->argv) {
	PSC_warn(-1, errno, "%s: malloc()", __func__);
	return 0;
    }
    for (i=0; i<task->argc; i++) {
	task->argv[i] = strdup(&buffer[msglen]);
	if (! task->argv[i]) {
	    PSC_warn(-1, errno, "%s: strdup(%d)", __func__, i);
	    return 0;
	}
	msglen += strlen(&buffer[msglen])+1;
    }
    task->argv[task->argc] = NULL;

    snprintfArgv(taskString, sizeof(taskString), task);
    PSC_log(PSC_LOG_TASK, " received argv = (%s)\n", taskString);
    PSC_log(PSC_LOG_TASK, "%s returns %d\n", __func__, msglen);

    return msglen;
}

size_t PStask_encodeEnv(char *buffer, size_t size, char **env,
			int *cur, char **offset)
{
    size_t msglen = 0;
    int first=*cur;

    if (!env) {
	PSC_log(-1, "%s: env is NULL\n", __func__);
	return 0;
    }

    if (PSC_getDebugMask() & PSC_LOG_TASK) {
	snprintfEnv(taskString, sizeof(taskString), env);
	PSC_log(PSC_LOG_TASK, "%s(%p, %ld, %s, %d, %p)\n", __func__,
		buffer, (long)size, taskString, *cur, *offset);
    }

    if (*offset) {
	msglen = strlen(*offset) < size ? strlen(*offset)+1 : size - 1;
	strncpy(buffer, *offset, msglen);
	if (strlen(*offset) < size) {
	    *offset = NULL;
	    (*cur)++;
	} else {
	    *offset += msglen;
	    buffer[msglen] = '\0';
	    msglen++;
	}
    } else {
	for (; env[*cur]; (*cur)++) {
	    if (msglen + strlen(env[*cur]) < size-1) {
		strcpy(&buffer[msglen], env[*cur]);
		msglen += strlen(env[*cur])+1;
	    } else if (*cur > first) {
		break;
	    } else {
		/* buffer to small */
		*offset = env[*cur];
		msglen = size-2;
		strncpy(buffer, *offset, msglen);
		*offset += msglen;
		buffer[msglen] = '\0';
		msglen++;
		break;
	    }
	}
	/* append extra zero byte (marks end of message) */
	buffer[msglen] = '\0';
	msglen++;
    }

    return msglen;
}

size_t PStask_encodeTaskEnv(char *buffer, size_t size, PStask_t *task,
			    int *cur, char **offset)
{
    if (!task) {
	PSC_log(-1, "%s: task is NULL\n", __func__);
	return 0;
    }

    PSC_log(PSC_LOG_TASK, "%s(%p, %ld, task %s)\n", __func__,
	    buffer, (long)size, PSC_printTID(task->tid));

    return PStask_encodeEnv(buffer, size, task->environ, cur, offset);
}

int PStask_decodeEnv(char *buffer, PStask_t *task)
{
    int msglen=0, i;
    int envSize = 0, envNew;

    if (!task) {
	PSC_log(-1, "%s: task is NULL\n", __func__);
	return 0;
    }

    snprintfStruct(taskString, sizeof(taskString), task);
    PSC_log(PSC_LOG_TASK, "%s(%p, task(%s))\n", __func__, buffer, taskString);

    /* Get number of existing environment variables */
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
	if (!task->environ) {
	    PSC_warn(-1, errno, "%s: malloc()", __func__);
	    return 0;
	}
    }

    /* Unpack new environment */
    if (task->environ) {
	msglen = 0;
	i = envSize;
	while (strlen(&buffer[msglen])) {
	    task->environ[i] = strdup(&buffer[msglen]);
	    if (! task->environ[i]) {
		PSC_warn(-1, errno, "%s: strdup(%d)", __func__, i);
		return 0;
	    }
	    msglen += strlen(&buffer[msglen])+1;
	    i++;
	}
	task->environ[i] = NULL;
	msglen++;
    }

    snprintfStruct(taskString, sizeof(taskString), task);
    PSC_log(PSC_LOG_TASK, " received env = (%s)\n", taskString);
    PSC_log(PSC_LOG_TASK, "%s returns %d\n", __func__, msglen);

    return msglen;
}

int PStask_decodeEnvAppend(char *buffer, PStask_t *task)
{
    int msglen, envSize = 0;
    size_t newLen;

    if (!task) {
	PSC_log(-1, "%s: task is NULL\n", __func__);
	return 0;
    }

    snprintfStruct(taskString, sizeof(taskString), task);
    PSC_log(PSC_LOG_TASK, "%s(%p, task(%s))\n", __func__, buffer, taskString);

    if (!task->environ) {
	PSC_log(-1, "%s: No environment in task %s\n", __func__, taskString);
	return 0;
    }

    /* Find trailing environment variable */
    while (task->environ[envSize]) envSize++;

    /* Append to environment */
    newLen = strlen(task->environ[envSize-1]) + strlen(buffer) + 1;
    task->environ[envSize-1] = realloc(task->environ[envSize-1], newLen);
    if (! task->environ[envSize-1]) {
	PSC_warn(-1, errno, "%s: strdup(%d)", __func__, envSize-1);
	return 0;
    }
    strcpy(task->environ[envSize-1]+strlen(task->environ[envSize-1]), buffer);
    msglen=strlen(buffer)+1;

    snprintfStruct(taskString, sizeof(taskString), task);
    PSC_log(PSC_LOG_TASK, " received env = (%s)\n", taskString);
    PSC_log(PSC_LOG_TASK, "%s returns %d\n", __func__, msglen);

    return msglen;
}
