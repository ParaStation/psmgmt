/*
 * ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
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
#include "pluginforwarder.h"
#include "pssignal.h"
#include "psreservation.h"
#include "psserial.h"

/** Information structure */
typedef struct {
    list_t next;         /**< used to put into info-lists */
    PStask_info_t type;  /**< type of information */
    void *info;          /**< actual extra informatin to store */
} PStask_infoItem_t;

/** data structure to handle a pool of info items */
static PSitems_t infoPool = NULL;

bool PStask_infoAdd(PStask_t *task, PStask_info_t type, void *info)
{
    if (!task || type < 0 || type == TASKINFO_ALL
	|| !PSitems_isInitialized(infoPool)) return false;

    PStask_infoItem_t *ip = PSitems_getItem(infoPool);
    if (!ip) return false;

    ip->type = type;
    ip->info = info;
    list_add_tail(&ip->next, &task->info);

    return true;
}

void * PStask_infoGet(PStask_t *task, PStask_info_t type)
{
    if (!task || type < 0 || type == TASKINFO_ALL) return NULL;

    list_t *i;
    list_for_each(i, &task->info) {
	PStask_infoItem_t *infoItem = list_entry(i, PStask_infoItem_t, next);
	if (infoItem->type == type) return infoItem->info;
    }

    return NULL;
}

bool PStask_infoRemove(PStask_t *task, PStask_info_t type, void *info)
{
    if (!task || type < 0 || type == TASKINFO_ALL) return false;

    list_t *i;
    list_for_each(i, &task->info) {
	PStask_infoItem_t *infoItem = list_entry(i, PStask_infoItem_t, next);
	if (infoItem->type != type) continue;
	if (!info || infoItem->info == info) {
	    list_del(&infoItem->next);
	    PSitems_putItem(infoPool, infoItem);
	    return true;
	}
    }

    return false;
}

static void clearInfoList(list_t *list)
{
    list_t *i, *tmp;
    list_for_each_safe(i, tmp, list) {
	PStask_infoItem_t *infoItem = list_entry(i, PStask_infoItem_t, next);
	list_del(&infoItem->next);
	if (infoItem->type == TASKINFO_FORWARDER) {
	    // TASKINFO_FORWARDER info (Forwarder_Data_t) is owned by the task
	    // Do not use ForwarderData_delete() to avoid dependency
	    Forwarder_Data_t *fw = infoItem->info;
	    if (!fw) continue;
	    free(fw->pTitle);
	    free(fw->jobID);
	    free(fw->userName);
	    free(fw);
	}
	PSitems_putItem(infoPool, infoItem);
    }
}

bool PStask_infoTraverse(PStask_t *task, PStask_info_t type,
			 PStask_infoVisitor_t visitor)
{
    if (!task || !visitor) return false;

    list_t *i;
    list_for_each(i, &task->info) {
	PStask_infoItem_t *infoItem = list_entry(i, PStask_infoItem_t, next);
	if (type != TASKINFO_ALL && type != infoItem->type) continue;
	if (!visitor(task, infoItem->type, infoItem->info)) return false;
    }

    return true;
}

static bool relocPStask_infoItem(void *item)
{
    PStask_infoItem_t *orig = item, *repl = PSitems_getItem(infoPool);
    if (!repl) return false;

    /* copy infoItem struct's content */
    repl->type = orig->type;
    repl->info = orig->info;

    /* tweak the list */
    __list_add(&repl->next, orig->next.prev, orig->next.next);

    return true;
}

void PStask_gc(void)
{
    PSitems_gc(infoPool, relocPStask_infoItem);
}

void PStask_clearMem(void)
{
    PSitems_clearMem(infoPool);
    infoPool = NULL;
}

void PStask_printStat(void)
{
    PSC_flog("Infos %d/%d (used/avail)", PSitems_getUsed(infoPool),
	     PSitems_getAvail(infoPool));
    PSC_log("\t%d/%d (gets/grows)\n", PSitems_getUtilization(infoPool),
	    PSitems_getDynamics(infoPool));
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
    case TG_ACCOUNT:
	return "TG_ACCOUNT";
    case TG_MONITOR:
	return "TG_MONITOR";
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

/**
 * @brief Initialize a task structure
 *
 * Initialize the task structure @a task, i.e. set all member to
 * default values.
 *
 * @param task Pointer to the task structure to be initialized
 *
 * @return On success true is returned; or false in case of error
 */
static bool initTask(PStask_t* task)
{
    PSC_fdbg(PSC_LOG_TASK, "task %p\n", task);

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
    task->spawnertid = 0;
    task->forwarder = NULL;
    task->rank = -1;
    PSCPU_clrAll(task->CPUset);
    task->partHolder = -1;
    task->jobRank = -1;
    task->fd = -1;
    task->workingdir = NULL;
    task->argV = NULL;
    task->env = NULL;
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
    task->delayReasons = 0;
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
    INIT_LIST_HEAD(&task->sisterParts);
    task->usedThreads = -1;
    task->firstSpawner = -1;
    INIT_LIST_HEAD(&task->resRequests);
    INIT_LIST_HEAD(&task->reservations);
    task->extraJobData = NULL;
    task->activeChild = 0;
    task->numChild = 0;
    task->spawnNodes = NULL;
    task->spawnNodesSize = 0;
    task->spawnNum = 0;
    task->delegate = NULL;
    task->sigChldCB = NULL;

    INIT_LIST_HEAD(&task->info);
    INIT_LIST_HEAD(&task->signalSender);
    INIT_LIST_HEAD(&task->signalReceiver);
    INIT_LIST_HEAD(&task->assignedSigs);
    INIT_LIST_HEAD(&task->keptChildren);

    return true;
}

PStask_t* PStask_new(void)
{
    PSC_fdbg(PSC_LOG_TASK, "\n");
    PStask_t* task = malloc(sizeof(PStask_t));
    if (task) initTask(task);

    return task;
}

static void delReservationList(list_t *list)
{
    list_t *r, *tmp;
    list_for_each_safe(r, tmp, list) {
	PSrsrvtn_t *reservation = list_entry(r, PSrsrvtn_t, next);
	PSC_fdbg(PSC_LOG_TASK, "put(rid %d, slots %p, state %d)\n",
		 reservation->rid, reservation->slots, reservation->state);
	free(reservation->slots);
	reservation->slots = NULL;
	list_del(&reservation->next);
	PSrsrvtn_put(reservation);
    }
}

/**
 * @brief Cleanup task
 *
 * Cleanup the task @a task, i.e. free() all memory associated to
 * it. This will *not* include any signal list or reservation list.
 *
 * This function is intended for aggressive cleanup of memory. At the
 * same time it is assumed that memory associated to the mentioned
 * lists will be cleaned up separately via the according item pools.
 *
 * @param task Pointer to task structure to be cleaned up
 *
 * @return No return value
 */
static void cleanupTask(PStask_t* task)
{
    free(task->workingdir);
    strvDestroy(task->argV);
    envDestroy(task->env);

    if (task->request) PSpart_delReq(task->request);
    free(task->partition);
    free(task->partThrds);

    free(task->spawnNodes);
}

/**
 * @brief Reinitialize a task structure
 *
 * Reinitialize the task structure @a task that was previously
 * used. All allocated strings, signal-lists, etc. shall be removed,
 * all links are reset to NULL.
 *
 * @param task Pointer to the task structure to be reinitialized
 *
 * @return On success true is returned; or false in case of error
 */
static bool reinitTask(PStask_t* task)
{
    PSC_fdbg(PSC_LOG_TASK, "task %p\n", task);

    if (!task) return false;

    if (!list_empty(&task->next)) list_del_init(&task->next);

    cleanupTask(task);

    PSsignal_clearList(&task->childList);
    PSsignal_clearList(&task->releasedBefore);
    PSsignal_clearList(&task->deadBefore);

    if (!list_empty(&task->sisterParts)) {
	PSC_fdbg(PSC_LOG_TASK, "cleanup %s sisters\n", PSC_printTID(task->tid));
    }
    PSpart_clrQueue(&task->sisterParts);

    delReservationList(&task->resRequests);
    delReservationList(&task->reservations);
    envDestroy(task->extraJobData);

    clearInfoList(&task->info);

    PSsignal_clearList(&task->signalSender);
    PSsignal_clearList(&task->signalReceiver);
    PSsignal_clearList(&task->assignedSigs);
    PSsignal_clearList(&task->keptChildren);

    return initTask(task);
}

bool PStask_delete(PStask_t* task)
{
    PSC_fdbg(PSC_LOG_TASK, "task %p\n", task);

    if (!task) return false;

    reinitTask(task);
    free(task);

    return true;
}

bool PStask_destroy(PStask_t* task)
{
    PSC_fdbg(PSC_LOG_TASK, "task %p\n", task);

    if (!task) return false;

    if (!list_empty(&task->next)) list_del(&task->next);

    cleanupTask(task);
    free(task);

    return true;
}

PStask_t* PStask_clone(PStask_t* task)
{
    PSC_fdbg(PSC_LOG_TASK, "task %p\n", task);
    int eno = 0;

    PStask_t *clone = PStask_new();
    if (!clone) return NULL;

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
    clone->spawnertid = task->spawnertid;
    clone->forwarder = task->forwarder;
    clone->rank = task->rank;
    memcpy(clone->CPUset, task->CPUset, sizeof(clone->CPUset));
    clone->partHolder = task->partHolder;
    clone->jobRank = task->jobRank;
    /* clone->fd = -1; */

    clone->workingdir = (task->workingdir) ? strdup(task->workingdir) : NULL;
    if (task->workingdir && !clone->workingdir) {
	eno = errno;
	PSC_fwarn(eno, "strdup(workingdir)");
	goto error;
    }

    if (!strvSize(task->argV)) {
	PSC_flog("empty argV (%s initialized)\n",
		 strvInitialized(task->argV) ? "but" : "not");
	eno = EINVAL;
	goto error;
    }
    clone->argV = strvClone(task->argV);
    if (!strvSize(clone->argV)) {
	eno = ENOMEM;
	PSC_fwarn(eno, "strvClone(argV)");
	goto error;
    }

    clone->env = envClone(task->env, NULL);
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
    clone->delayReasons = task->delayReasons;
    clone->killat = task->killat;
    gettimeofday(&clone->started, NULL);
    clone->protocolVersion = task->protocolVersion;

    PSsignal_cloneList(&clone->childList, &task->childList);
    PSsignal_cloneList(&clone->releasedBefore, &task->releasedBefore);
    PSsignal_cloneList(&clone->deadBefore, &task->deadBefore);

    clone->request = NULL; /* Do not clone requests */
    clone->options = task->options;
    clone->partitionSize = task->partitionSize;
    if (clone->partitionSize) {
	clone->partition = malloc(clone->partitionSize
				  * sizeof(*clone->partition));
	if (!clone->partition) {
	    eno = errno;
	    PSC_fwarn(eno, "malloc(partition)");
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
	    PSC_fwarn(eno, "malloc(partThrds)");
	    goto error;
	}
	memcpy(clone->partThrds, task->partThrds,
	       task->totalThreads * sizeof(*task->partThrds));
    }
    clone->usedThreads = task->usedThreads;
    /* do not inherit firstSpawner */

    /* do not clone sister partitions */

    /* do not clone reservations */

    clone->activeChild = task->activeChild;
    clone->numChild = task->numChild;
    clone->spawnNodesSize = task->spawnNodesSize;
    if (clone->spawnNodesSize) {
	clone->spawnNodes = malloc(task->spawnNodesSize
				   * sizeof(*task->spawnNodes));
	if (!clone->spawnNodes) {
	    eno = errno;
	    PSC_fwarn(eno, "malloc(spawnNodes)");
	    goto error;
	}
	memcpy(clone->spawnNodes, task->spawnNodes,
	       clone->spawnNodesSize * sizeof(*task->spawnNodes));
    }
    clone->spawnNum = task->spawnNum;
    clone->delegate = task->delegate;
    /* Ignore sigChldCB and info */

    PSsignal_cloneList(&clone->signalSender, &task->signalSender);
    PSsignal_cloneList(&clone->signalReceiver, &task->signalReceiver);
    PSsignal_cloneList(&clone->assignedSigs, &task->assignedSigs);
    PSsignal_cloneList(&clone->keptChildren, &task->keptChildren);

    return clone;

error:
    PStask_delete(clone);
    errno = eno;
    return NULL;
}

static void snprintfStruct(char *txt, size_t size, PStask_t *task)
{
    if (!task) return;

    snprintf(txt, size, "tid %#.12lx ptid %#.12lx uid %d gid %d group %s"
	     " childGroup %s rank %d cpus ...%s loggertid %#.12lx fd %d",
	     task->tid, task->ptid, task->uid, task->gid,
	     PStask_printGrp(task->group), PStask_printGrp(task->childGroup),
	     task->rank, PSCPU_print_part(task->CPUset, 8), task->loggertid,
	     task->fd);
}

static void snprintfStrV(char *txt, size_t size, char **strV)
{
    if (!strV) return;
    for (uint32_t i = 0; strV[i]; i++) {
	snprintf(txt+strlen(txt), size-strlen(txt), "%s%s", i?" ":"", strV[i]);
	if (strlen(txt)+1 == size) return;
    }
}

void PStask_snprintf(char *txt, size_t size, PStask_t *task)
{
    if (!task) return;

    snprintfStruct(txt, size, task);
    if (strlen(txt)+1 == size) return;
    snprintf(txt+strlen(txt), size-strlen(txt), " ");
    if (strlen(txt)+1 == size) return;
    snprintf(txt+strlen(txt), size-strlen(txt), "dir=\"%s\" argV=%p argV=\"",
	     (task->workingdir) ? task->workingdir : "", task->argV);
    if (strlen(txt)+1 == size) return;
    snprintfStrV(txt+strlen(txt), size-strlen(txt), strvGetArray(task->argV));
    if (strlen(txt)+1 == size) return;
    snprintf(txt+strlen(txt), size-strlen(txt), "\" env=\"");
    if (strlen(txt)+1 == size) return;
    snprintfStrV(txt+strlen(txt), size-strlen(txt), envGetArray(task->env));
    if (strlen(txt)+1 == size) return;
    snprintf(txt+strlen(txt), size-strlen(txt), "\"");
}

static char someStr[256];

/* helper struct to compress the relvant parts of PStask_t to be sent */
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
    int32_t noParricide;
} tmpTask;

bool PStask_addToMsg(PStask_t *task, PS_SendDB_t *msg)
{
    snprintfStruct(someStr, sizeof(someStr), task);
    PSC_fdbg(PSC_LOG_TASK, "msg %p task %s\n", msg, someStr);

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
    tmpTask.noParricide = task->noParricide;

    if (!addMemToMsg(&tmpTask, sizeof(tmpTask), msg)) return false;

    char *wDir = task->workingdir ? task->workingdir : "";
    if (!addStringToMsg(wDir, msg)) return false;

    return true;
}

int PStask_decodeTask(char *buffer, PStask_t *task, bool withWDir)
{
    if (!task) {
	PSC_flog("no task\n");
	return 0;
    }

    if (PSC_getDebugMask() & PSC_LOG_TASK) {
	snprintfStruct(someStr, sizeof(someStr), task);
	PSC_fdbg(PSC_LOG_TASK, "buffer %p task %s\n", buffer, someStr);
    }

    reinitTask(task);

    /* unpack buffer */
    int msglen = sizeof(tmpTask);
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
    task->noParricide = tmpTask.noParricide;

    if (withWDir) {
	int len = strlen(&buffer[msglen]);

	if (len) task->workingdir = strdup(&buffer[msglen]);
	msglen += len+1;
    }

    if (PSC_getDebugMask() & PSC_LOG_TASK) {
	snprintfStruct(someStr, sizeof(someStr), task);
	PSC_dbg(PSC_LOG_TASK, " received task = (%s)\n", someStr);
	PSC_fdbg(PSC_LOG_TASK, "return %d\n", msglen);
    }

    return msglen;
}

PSrsrvtn_ID_t PStask_getNextResID(PStask_t *task)
{
    task->nextResID++;
    if (task->nextResID == -2) task->nextResID += 3; // prevent resID == [-2..0]

    return task->nextResID;
}

void PStask_init(void)
{
    if (PSitems_isInitialized(infoPool)) return;
    infoPool = PSitems_new(sizeof(PStask_infoItem_t), "PStask_info");
}
