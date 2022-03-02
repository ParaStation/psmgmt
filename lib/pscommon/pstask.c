/*
 * ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pstask.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include "pscommon.h"
#include "list.h"
#include "pssignal.h"
#include "psreservation.h"
#include "psserial.h"

void PStask_printStat(void)
{
    PSsignal_printStat();
    PSrsrvtn_printStat();
}

const char* PStask_printGrp(PStask_group_t tg)
{
    switch (tg) {
    case TG_ANY:
	return "TG_ANY";
    case TG_ADMIN:
	return "TG_ADMIN";
    case TG_RESET:
	return "TG_RESET";
    case TG_LOGGER:
	return "TG_LOGGER";
    case TG_FORWARDER:
	return "TG_FORWARDER";
    case TG_SPAWNER:
	return "TG_SPAWNER";
    case TG_GMSPAWNER:
	return "TG_GMSPAWNER";
    case TG_ACCOUNT:
	return "TG_ACCOUNT";
    case TG_MONITOR:
	return "TG_MONITOR";
    case TG_PSCSPAWNER:
	return "TG_PSCSPAWNER";
    case TG_ADMINTASK:
	return "TG_ADMINTASK";
    case TG_SERVICE:
	return "TG_SERVICE";
    case TG_SERVICE_SIG:
	return "TG_SERVICE_SIG";
    case TG_KVS:
	return "TG_KVS";
    case TG_DELEGATE:
	return "TG_DELEGATE";
    case TG_PLUGINFW:
	return "TG_PLUGINFW";
    }
    return "UNKNOWN";
}

PStask_t* PStask_new(void)
{
    PStask_t* task;

    PSC_log(PSC_LOG_TASK, "%s()\n", __func__);
    task = malloc(sizeof(PStask_t));

    if (task) PStask_init(task);

    return task;
}

bool PStask_init(PStask_t* task)
{
    PSC_log(PSC_LOG_TASK, "%s(%p)\n", __func__, task);

    if (!task) return false;

    INIT_LIST_HEAD(&task->next);
    task->tid = 0;
    task->ptid = 0;
    task->uid = -1;
    task->gid = -1;
    task->aretty = 0;
    task->interactive = false;
    task->stdin_fd = -1;
    task->stdout_fd = -1;
    task->stderr_fd = -1;
    task->group = TG_ANY;
    task->childGroup = TG_ANY;
    task->resID = -1;
    task->loggertid = 0;
    task->forwarder = NULL;
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
    task->activeStops = 0;
    task->releaseAnswer = true;
    task->released = false;
    task->parentReleased = false;
    task->duplicate = false;
    task->suspended = false;
    task->removeIt = false;
    task->deleted = false;
    task->obsolete = false;
    task->noParricide = false;
    task->killat = 0;
    gettimeofday(&task->started, NULL);
    task->protocolVersion = -1;

    INIT_LIST_HEAD(&task->childList);
    INIT_LIST_HEAD(&task->releasedBefore);
    INIT_LIST_HEAD(&task->deadBefore);

    task->request = NULL;
    task->options = 0;
    task->partitionSize = 0;
    task->partition = NULL;
    task->totalThreads = 0;
    task->partThrds = NULL;
    task->usedThreads = -1;
    INIT_LIST_HEAD(&task->reservations);
    INIT_LIST_HEAD(&task->resRequests);
    task->activeChild = 0;
    task->numChild = 0;
    task->spawnNodes = NULL;
    task->spawnNodesSize = 0;
    task->spawnNum = 0;
    task->delegate = NULL;
    task->injectedEnv = 0;
    task->sigChldCB = NULL;
    task->info = NULL;
    task->resPorts = NULL;

    INIT_LIST_HEAD(&task->signalSender);
    INIT_LIST_HEAD(&task->signalReceiver);
    INIT_LIST_HEAD(&task->assignedSigs);
    INIT_LIST_HEAD(&task->keptChildren);

    return true;
}

static void delSigList(list_t *list)
{
    list_t *s, *tmp;
    list_for_each_safe(s, tmp, list) {
	PSsignal_t *signal = list_entry(s, PSsignal_t, next);
	list_del(&signal->next);
	PSsignal_put(signal);
    }
}

static void delReservationList(list_t *list)
{
    list_t *r, *tmp;
    list_for_each_safe(r, tmp, list) {
	PSrsrvtn_t *reservation = list_entry(r, PSrsrvtn_t, next);
	free(reservation->slots);
	reservation->slots = NULL;
	list_del(&reservation->next);
	PSrsrvtn_put(reservation);
    }
}

bool PStask_reinit(PStask_t* task)
{
    PSC_log(PSC_LOG_TASK, "%s(%p)\n", __func__, task);

    if (!task) return false;

    if (!list_empty(&task->next)) list_del_init(&task->next);

    free(task->workingdir);
    if (task->argv) {
	for (uint32_t i = 0; i < task->argc; i++) free(task->argv[i]);
	free(task->argv);
	task->argv = NULL;
    }


    if (task->environ) {
	for (uint32_t i = 0; task->environ[i]; i++) free(task->environ[i]);
	free(task->environ);
	task->environ = NULL;
    }

    delSigList(&task->childList);
    delSigList(&task->releasedBefore);
    delSigList(&task->deadBefore);

    if (task->request) PSpart_delReq(task->request);
    free(task->partition);
    free(task->partThrds);

    delReservationList(&task->reservations);
    delReservationList(&task->resRequests);

    free(task->spawnNodes);
    free(task->info);
    free(task->resPorts);

    delSigList(&task->signalSender);
    delSigList(&task->signalReceiver);
    delSigList(&task->assignedSigs);
    delSigList(&task->keptChildren);

    return PStask_init(task);
}

bool PStask_delete(PStask_t* task)
{
    PSC_log(PSC_LOG_TASK, "%s(%p)\n", __func__, task);

    if (!task) return false;

    PStask_reinit(task);
    free(task);

    return true;
}

/**
 * @brief Clone a signal list.
 *
 * Create an exact clone of the signal list @a origList and store it
 * to the new list @a cloneList.
 *
 * @warning This function may fail silently when running out of
 * memory.
 *
 * @param cloneList The list-head of the cloned list to be created.
 *
 * @param origList The original list of signals to be cloned.
 *
 * @return No return value.
 */
static void cloneSigList(list_t *cloneList, list_t *origList)
{
    list_t *s;

    PSC_log(PSC_LOG_TASK, "%s(%p)\n", __func__, origList);

    delSigList(cloneList);

    list_for_each(s, origList) {
	PSsignal_t *origSig = list_entry(s, PSsignal_t, next);
	PSsignal_t *cloneSig;

	if (origSig->deleted) continue;

	cloneSig = PSsignal_get();
	if (!cloneSig) {
	    delSigList(cloneList);
	    PSC_warn(-1, ENOMEM, "%s()", __func__);
	    break;
	}

	cloneSig->tid = origSig->tid;
	cloneSig->signal = origSig->signal;
	cloneSig->deleted = origSig->deleted;
	list_add_tail(&cloneSig->next, cloneList);
    }
}

PStask_t* PStask_clone(PStask_t* task)
{
    PStask_t *clone;
    int eno = 0;
    uint32_t i;

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
    clone->resID = task->resID;
    clone->loggertid = task->loggertid;
    clone->forwarder = task->forwarder;
    clone->rank = task->rank;
    memcpy(clone->CPUset, task->CPUset, sizeof(clone->CPUset));
    /* clone->fd = -1; */

    clone->workingdir = (task->workingdir) ? strdup(task->workingdir) : NULL;
    if (task->workingdir && !clone->workingdir) {
	eno = errno;
	PSC_warn(-1, eno, "%s: strdup(workingdir)", __func__);
	goto error;
    }

    clone->argc = task->argc;
    if (!task->argv) {
	PSC_log(-1, "%s: argv is NULL\n", __func__);
	eno = EINVAL;
	goto error;
    }
    clone->argv = malloc((task->argc + 1) * sizeof(*clone->argv));
    if (!clone->argv) {
	eno = errno;
	PSC_warn(-1, eno, "%s: malloc(argv)", __func__);
	goto error;
    }
    for (i=0; i<task->argc; i++) {
	if (!task->argv[i]) {
	    PSC_log(-1, "%s: argv[%d] is NULL\n", __func__, i);
	    eno = EINVAL;
	    goto error;
	}
	clone->argv[i] = strdup(task->argv[i]);
	if (!clone->argv[i]) {
	    eno = errno;
	    PSC_warn(-1, eno, "%s: strdup(argv[%d])", __func__, i);
	    goto error;
	}
    }
    clone->argv[clone->argc] = NULL;

    if (task->envSize) {
	if (!task->environ) {
	    PSC_log(-1, "%s: environ is NULL\n", __func__);
	    eno = EINVAL;
	    goto error;
	}
	clone->environ = malloc((task->envSize + 1) * sizeof(*clone->environ));
	if (!clone->environ) {
	    eno = errno;
	    PSC_warn(-1, eno, "%s: malloc(environ)", __func__);
	    goto error;
	}
	for (i=0; task->environ[i]; i++) {
	    clone->environ[i] = strdup(task->environ[i]);
	    if (!clone->environ[i]) {
		eno = errno;
		PSC_warn(-1, eno, "%s: strdup(environ[%d])", __func__, i);
		goto error;
	    }
	}
	clone->environ[i] = NULL;
    }
    clone->envSize = task->envSize;
    clone->relativesignal = task->relativesignal;
    clone->pendingReleaseRes = task->pendingReleaseRes;
    clone->pendingReleaseErr = task->pendingReleaseErr;
    clone->activeStops = task->activeStops;
    clone->releaseAnswer = task->releaseAnswer;
    clone->released = task->released;
    clone->parentReleased = task->parentReleased;
    clone->duplicate = task->duplicate;
    clone->suspended = task->suspended;
    clone->removeIt = task->removeIt;
    clone->deleted = task->deleted;
    clone->obsolete = task->obsolete;
    clone->noParricide = task->noParricide;
    clone->killat = task->killat;
    gettimeofday(&clone->started, NULL);
    clone->protocolVersion = task->protocolVersion;

    cloneSigList(&clone->childList, &task->childList);
    cloneSigList(&clone->releasedBefore, &task->releasedBefore);
    cloneSigList(&clone->deadBefore, &task->deadBefore);

    clone->request = NULL; /* Do not clone requests */
    clone->options = task->options;
    clone->partitionSize = task->partitionSize;
    if (clone->partitionSize) {
	clone->partition = malloc(clone->partitionSize
				  * sizeof(*clone->partition));
	if (!clone->partition) {
	    eno = errno;
	    PSC_warn(-1, eno, "%s: malloc(partition)", __func__);
	    goto error;
	}
	memcpy(clone->partition, task->partition,
	       task->partitionSize * sizeof(*task->partition));
    }
    clone->totalThreads = task->totalThreads;
    if (clone->totalThreads) {
	clone->partThrds = malloc(task->totalThreads
				  * sizeof(*task->partThrds));
	if (!clone->partThrds) {
	    eno = errno;
	    PSC_warn(-1, eno, "%s: malloc(partThrds)", __func__);
	    goto error;
	}
	memcpy(clone->partThrds, task->partThrds,
	       task->totalThreads * sizeof(*task->partThrds));
    }
    clone->usedThreads = task->usedThreads;

    /* Do not clone reservations */

    clone->activeChild = task->activeChild;
    clone->numChild = task->numChild;
    clone->spawnNodesSize = task->spawnNodesSize;
    if (clone->spawnNodesSize) {
	clone->spawnNodes = malloc(task->spawnNodesSize
				   * sizeof(*task->spawnNodes));
	if (!clone->spawnNodes) {
	    eno = errno;
	    PSC_warn(-1, eno, "%s: malloc(spawnNodes)", __func__);
	    goto error;
	}
	memcpy(clone->spawnNodes, task->spawnNodes,
	       clone->spawnNodesSize * sizeof(*task->spawnNodes));
    }
    clone->spawnNum = task->spawnNum;
    clone->delegate = task->delegate;
    clone->injectedEnv = task->injectedEnv;
    /* Ignore sigChldCB and info */

    cloneSigList(&clone->signalSender, &task->signalSender);
    cloneSigList(&clone->signalReceiver, &task->signalReceiver);
    cloneSigList(&clone->assignedSigs, &task->assignedSigs);
    cloneSigList(&clone->keptChildren, &task->keptChildren);

    return clone;

error:
    PStask_delete(clone);
    errno = eno;
    return NULL;
}

static void snprintfStruct(char *txt, size_t size, PStask_t *task)
{
    if (!task) return;

    snprintf(txt, size, "tid 0x%08x ptid 0x%08x uid %d gid %d group %s"
	     " childGroup %s rank %d cpus ...%s loggertid %08x fd %d argc %u",
	     task->tid, task->ptid, task->uid, task->gid,
	     PStask_printGrp(task->group), PStask_printGrp(task->childGroup),
	     task->rank, PSCPU_print_part(task->CPUset, 8), task->loggertid,
	     task->fd, task->argc);
}

static void snprintfStrV(char *txt, size_t size, char **strV)
{
    if (strV) {
	int i;
	for (i=0; strV[i]; i++) {
	    snprintf(txt+strlen(txt), size-strlen(txt), "%s ", strV[i]);
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
    snprintf(txt+strlen(txt), size-strlen(txt), "dir=\"%s\" argv=\"",
	     (task->workingdir) ? task->workingdir : "");
    if (strlen(txt)+1 == size) return;
    snprintfStrV(txt+strlen(txt), size-strlen(txt), task->argv);
    if (strlen(txt)+1 == size) return;
    snprintf(txt+strlen(txt), size-strlen(txt), "\" env=\"");
    if (strlen(txt)+1 == size) return;
    snprintfStrV(txt+strlen(txt), size-strlen(txt), task->environ);
    if (strlen(txt)+1 == size) return;
    snprintf(txt+strlen(txt), size-strlen(txt), "\"");
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
    PSrsrvtn_ID_t resID;
    int32_t rank;
    PStask_ID_t loggertid;
    uint32_t argc;
    int32_t noParricide;
} tmpTask;

static char someStr[256];

size_t PStask_encodeTask(char *buffer, size_t size, PStask_t *task, char **off)
{
    size_t msglen = sizeof(tmpTask);

    snprintfStruct(someStr, sizeof(someStr), task);
    PSC_log(PSC_LOG_TASK, "%s(%p, %ld, task(%s))\n", __func__, buffer,
	    (long)size, someStr);

    if (msglen > size) return msglen; /* buffer too small */

    tmpTask.tid = task->tid;
    tmpTask.ptid = task->ptid;
    tmpTask.uid = task->uid;
    tmpTask.gid = task->gid;
    tmpTask.aretty = task->aretty;
    tmpTask.termios = task->termios;
    tmpTask.winsize = task->winsize;
    tmpTask.group = task->group;
    tmpTask.resID = task->resID;
    tmpTask.rank = task->rank;
    tmpTask.loggertid = task->loggertid;
    tmpTask.argc = task->argc;
    tmpTask.noParricide = task->noParricide;

    memcpy(buffer, &tmpTask, sizeof(tmpTask));

    *off = NULL;
    if (task->workingdir) {
	if (msglen + strlen(task->workingdir) < size) {
	    strcpy(&buffer[msglen], task->workingdir);
	    msglen += strlen(task->workingdir);
	} else {
	    /* buffer too small */
	    strncpy(&buffer[msglen], task->workingdir, size - msglen - 1);
	    *off = &task->workingdir[size - msglen - 1];
	    buffer[size-1] = '\0';
	    msglen += strlen(&buffer[msglen]);
	}
    } else {
	buffer[msglen]='\0';
    }
    msglen++; /* zero byte */

    return msglen;
}

bool PStask_sendTask(PS_SendDB_t *msg, PStask_t *task)
{
    char *wDir;

    snprintfStruct(someStr, sizeof(someStr), task);
    PSC_log(PSC_LOG_TASK, "%s(%p, task(%s))\n", __func__, msg, someStr);

    tmpTask.tid = task->tid;
    tmpTask.ptid = task->ptid;
    tmpTask.uid = task->uid;
    tmpTask.gid = task->gid;
    tmpTask.aretty = task->aretty;
    tmpTask.termios = task->termios;
    tmpTask.winsize = task->winsize;
    tmpTask.group = task->group;
    tmpTask.resID = task->resID;
    tmpTask.rank = task->rank;
    tmpTask.loggertid = task->loggertid;
    tmpTask.argc = task->argc;
    tmpTask.noParricide = task->noParricide;

    if (!addMemToMsg(&tmpTask, sizeof(tmpTask), msg)) return false;

    if (task->workingdir) {
	wDir = task->workingdir;
    } else {
	wDir = "";
    }
    if (!addStringToMsg(wDir, msg)) return false;

    return true;
}

int PStask_decodeTask(char *buffer, PStask_t *task, bool withWDir)
{
    int msglen;

    if (!task) {
	PSC_log(-1, "%s: task is NULL\n", __func__);
	return 0;
    }

    if (PSC_getDebugMask() & PSC_LOG_TASK) {
	snprintfStruct(someStr, sizeof(someStr), task);
	PSC_log(PSC_LOG_TASK, "%s(%p, task(%s))\n", __func__, buffer, someStr);
    }

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
    task->resID = tmpTask.resID;
    task->rank = tmpTask.rank;
    task->loggertid = tmpTask.loggertid;
    task->argc = tmpTask.argc;
    task->noParricide = tmpTask.noParricide;

    if (withWDir) {
	int len = strlen(&buffer[msglen]);

	if (len) task->workingdir = strdup(&buffer[msglen]);
	msglen += len+1;
    }

    if (PSC_getDebugMask() & PSC_LOG_TASK) {
	snprintfStruct(someStr, sizeof(someStr), task);
	PSC_log(PSC_LOG_TASK, " received task = (%s)\n", someStr);
	PSC_log(PSC_LOG_TASK, "%s returns %d\n", __func__, msglen);
    }

    return msglen;
}

bool PStask_sendStrV(PS_SendDB_t *msg, char **strV)
{
    uint32_t num = 0, i;

    while (strV[num]) num++;

    addUint32ToMsg(num, msg);
    for (i = 0; i < num; i++) if (!addStringToMsg(strV[i], msg)) return false;

    return true;
}

/**
 * @brief Encode string-vector
 *
 * Encode the string-vector @a strV into the buffer @a buffer of size
 * @a size. @a cur will hold the index of the string currently handled
 * while @a offset might point to a trailing part of a string not
 * fitting into @a buffer as a whole.
 *
 * In order to encode @a strV completely, several calls to this
 * function might be necessary depending on the sizes of @a strV and
 * @a buffer. Depending on the settings of @a cur and @a offset is
 * will behave differently.
 *
 * Before calling this function the first time @a cur has to be set to
 * 0 and @a offset to NULL. Both shall not be modified during the
 * course of repeated calls. Once @a cur holds the value -1, all
 * strings of @a strV are encoded.
 *
 * If a single string does not fit into @a buffer, i.e. if the length
 * of this string is larger than @a size, upon return @a offset will
 * point to the trailing part of the string. Further calls to this
 * function will encode the trailing part(s) into @a buffer. Once all
 * trailing parts are encoded, @a offset is reset to NULL by the
 * function.
 *
 * @param buffer The buffer used to encode the string-vector
 *
 * @param size The size of the buffer
 *
 * @param strV String-vector to encode
 *
 * @param cur Index of the string to encode next
 *
 * @param offset Offset to a trailing part of the string currently
 * encoded, if any.
 *
 * @return The number of characters put into the buffer. Or 0, if an
 * error occurred.
 */
static size_t encodeStrV(char *buffer, size_t size, char **strV,
			 int *cur, char **offset)
{
    size_t msglen = 0;
    int first=*cur;

    if (!strV) {
	PSC_log(-1, "%s: strV is NULL\n", __func__);
	return 0;
    }

    if (*cur == -1) {
	PSC_log(-1, "%s: encoding complete\n", __func__);
	return 0;
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
	for (; strV[*cur]; (*cur)++) {
	    if (msglen + strlen(strV[*cur]) < size-1) {
		strcpy(&buffer[msglen], strV[*cur]);
		msglen += strlen(strV[*cur])+1;
	    } else if (*cur > first) {
		break;
	    } else {
		/* buffer too small */
		*offset = strV[*cur];
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
	if (!strV[*cur]) *cur = -1; /* last entry done */
    }

    return msglen;
}

/**
 * @brief Decode string-vector
 *
 * Decode the string-vector @a strV from the buffer @a buffer. @a size
 * holds the current size of the array @a strV containing the
 * pointers to the character string.
 *
 * All memory provided by this function is allocated using
 * malloc(). Especially, both @a strV and @a size might get modified
 * during execution of this function.
 *
 * @param buffer Buffer holding the encoded string-vector.
 *
 * @param strV String-vector to decode.
 *
 * @param size Current size of the string-vector.
 *
 * @return The number of characters within @a buffer used in order to
 * decode the string-vector.
 */
static int decodeStrV(char *buffer, char ***strV, uint32_t *size)
{
    int msglen = 0;
    uint32_t oldSize = *size;

    if (!strV) {
	PSC_log(-1, "%s: strV is NULL\n", __func__);
	return 0;
    }

    /* Get number of new environment variables */
    while (strlen(&buffer[msglen])) {
	(*size)++;
	msglen += strlen(&buffer[msglen])+1;
    }

    if (!oldSize) (*size)++; /* first decode, thus, add trailing NULL entry */

    /* Extend environ (if necessary) */
    if (*size > oldSize) {
	*strV = realloc(*strV, *size * sizeof(char*));
	if (!*strV) {
	    PSC_warn(-1, errno, "%s: realloc()", __func__);
	    return 0;
	}
    }

    msglen = 0;

    /* Unpack new environment */
    if (*strV) {
	int i = (oldSize) ? oldSize - 1 : 0;
	while (strlen(&buffer[msglen])) {
	    (*strV)[i] = strdup(&buffer[msglen]);
	    if (! (*strV)[i]) {
		PSC_warn(-1, errno, "%s: strdup(%d)", __func__, i);
		return 0;
	    }
	    msglen += strlen(&buffer[msglen])+1;
	    i++;
	}
	(*strV)[i] = NULL; /* trailing entry */
	msglen++;
    }

    return msglen;
}

/**
 * @brief Append to string-vector
 *
 * Append some trailing part of a string to string-vector @a strV from
 * the buffer @a buffer. @a size holds the current size of the array
 * @a strV containing the pointers to the character strings.
 *
 * All memory provided by this function is allocated using
 * malloc().
 *
 * @param buffer Buffer holding the encoded string-vector.
 *
 * @param strV String-vector to decode.
 *
 * @param size Current size of the string-vector.
 *
 * @return The number of characters within @a buffer used in order to
 * decode the string-vector.
 */
static int decodeStrVApp(char *buffer, char **strV, int size)
{
    size_t newLen;

    if (!strV) {
	PSC_log(-1, "%s: No string in strV yet\n", __func__);
	return 0;
    }

    /* Append to environment */
    /* size-1 is closing NULL, thus use size-2 */
    newLen = strlen(strV[size-2]) + strlen(buffer) + 1;
    strV[size-2] = realloc(strV[size-2], newLen);
    if (! strV[size-2]) {
	PSC_warn(-1, errno, "%s: realloc()", __func__);
	return 0;
    }

    strcpy(strV[size-2] + strlen(strV[size-2]), buffer);

    return strlen(buffer)+1;
}

size_t PStask_encodeArgv(char *buffer, size_t size, char **argv, int *cur,
			 char **offset)
{
    if (!argv) {
	PSC_log(-1, "%s: argv is NULL\n", __func__);
	return 0;
    }

    if (PSC_getDebugMask() & PSC_LOG_TASK) {
	snprintfStrV(someStr, sizeof(someStr), argv);
	PSC_log(PSC_LOG_TASK, "%s(%p, %ld, argv(%s))\n", __func__, buffer,
		(long)size, someStr);
    }

    return encodeStrV(buffer, size, argv, cur, offset);
}

int PStask_decodeArgv(char *buffer, PStask_t *task)
{
    int ret;

    if (!task) {
	PSC_log(-1, "%s: task is NULL\n", __func__);
	return 0;
    }

    if (PSC_getDebugMask() & PSC_LOG_TASK) {
	snprintfStruct(someStr, sizeof(someStr), task);
	PSC_log(PSC_LOG_TASK, "%s(%p, task(%s))\n", __func__, buffer, someStr);
    }

    /* argc ignores closing NULL in argv */
    if (task->argc) task->argc++;
    ret = decodeStrV(buffer, &task->argv, &task->argc);
    task->argc--;

    if (PSC_getDebugMask() & PSC_LOG_TASK) {
	snprintfStrV(someStr, sizeof(someStr), task->argv);
	PSC_log(PSC_LOG_TASK, " received argv = (%s)\n", someStr);
	PSC_log(PSC_LOG_TASK, "%s returns %d\n", __func__, ret);
    }

    return ret;
}

int PStask_decodeArgvAppend(char *buffer, PStask_t *task)
{
    int ret;

    if (!task) {
	PSC_log(-1, "%s: task is NULL\n", __func__);
	return 0;
    }

    if (PSC_getDebugMask() & PSC_LOG_TASK) {
	snprintfStruct(someStr, sizeof(someStr), task);
	PSC_log(PSC_LOG_TASK, "%s(%p, task(%s))\n", __func__, buffer, someStr);
    }

    if (!task->argv) {
	PSC_log(-1, "%s: No arguments in task %s\n", __func__, someStr);
	return 0;
    }

    /* argc ignores closing NULL in argv */
    if (task->argc) task->argc++;
    ret = decodeStrVApp(buffer, task->argv, task->argc);
    task->argc--;

    if (PSC_getDebugMask() & PSC_LOG_TASK) {
	snprintfStrV(someStr, sizeof(someStr), task->argv);
	PSC_log(PSC_LOG_TASK, " received argv = (%s)\n", someStr);
	PSC_log(PSC_LOG_TASK, "%s returns %d\n", __func__, ret);
    }

    return ret;
}

size_t PStask_encodeEnv(char *buffer, size_t size, char **env,
			int *cur, char **offset)
{
    if (!env) {
	PSC_log(-1, "%s: env is NULL\n", __func__);
	return 0;
    }

    if (PSC_getDebugMask() & PSC_LOG_TASK) {
	snprintfStrV(someStr, sizeof(someStr), env);
	PSC_log(PSC_LOG_TASK, "%s(%p, %ld, %s, %d, %p)\n", __func__,
		buffer, (long)size, someStr, *cur, *offset);
    }

    return encodeStrV(buffer, size, env, cur, offset);
}

int PStask_decodeEnv(char *buffer, PStask_t *task)
{
    int ret;

    if (!task) {
	PSC_log(-1, "%s: task is NULL\n", __func__);
	return 0;
    }

    if (PSC_getDebugMask() & PSC_LOG_TASK) {
	snprintfStruct(someStr, sizeof(someStr), task);
	PSC_log(PSC_LOG_TASK, "%s(%p, task(%s))\n", __func__, buffer, someStr);
    }

    ret = decodeStrV(buffer, &task->environ, &task->envSize);

    if (PSC_getDebugMask() & PSC_LOG_TASK) {
	snprintfStrV(someStr, sizeof(someStr), task->environ);
	PSC_log(PSC_LOG_TASK, " received env = (%s)\n", someStr);
	PSC_log(PSC_LOG_TASK, "%s returns %d\n", __func__, ret);
    }

    return ret;
}

int PStask_decodeEnvAppend(char *buffer, PStask_t *task)
{
    int ret;

    if (!task) {
	PSC_log(-1, "%s: task is NULL\n", __func__);
	return 0;
    }

    if (PSC_getDebugMask() & PSC_LOG_TASK) {
	snprintfStruct(someStr, sizeof(someStr), task);
	PSC_log(PSC_LOG_TASK, "%s(%p, task(%s))\n", __func__, buffer, someStr);
    }

    if (!task->environ) {
	PSC_log(-1, "%s: No environment in task %s\n", __func__, someStr);
	return 0;
    }

    ret = decodeStrVApp(buffer, task->environ, task->envSize);

    if (PSC_getDebugMask() & PSC_LOG_TASK) {
	snprintfStrV(someStr, sizeof(someStr), task->environ);
	PSC_log(PSC_LOG_TASK, " received env = (%s)\n", someStr);
	PSC_log(PSC_LOG_TASK, "%s returns %d\n", __func__, ret);
    }

    return ret;
}

PSrsrvtn_ID_t PStask_getNextResID(PStask_t *task)
{
    task->nextResID++;

    if (!task->nextResID) task->nextResID++; // prevent resID == 0

    return task->nextResID;
}
