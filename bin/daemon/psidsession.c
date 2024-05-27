/*
 * ParaStation
 *
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psidsession.h"

#include <stdlib.h>

#include "pscommon.h"
#include "psitems.h"
#include "psserial.h"
#include "psdaemonprotocol.h"

#include "psidcomm.h"
#include "psidhook.h"
#include "psidnodes.h"
#include "psidspawn.h"
#include "psidtask.h"
#include "psidutil.h"

/** Pool of compact reservation information structures (of type PSresinfo_t) */
static PSitems_t resinfoPool = NULL;

/** Pool of job structures (of type PSjob_t) */
static PSitems_t jobPool = NULL;

/** Pool of session structures (of type PSsession_t) */
static PSitems_t sessionPool = NULL;

/** List of reservations this node is part of organized in sessions and jobs */
static LIST_HEAD(localSessions);

/********************* A bunch of item helper functions **********************/

/**
 * Fetch a new resinfo from the corresponding pool and initialize it
 */
static PSresinfo_t *getResinfo(void)
{
    PSresinfo_t *resinfo = PSitems_getItem(resinfoPool);
    resinfo->creation = time(NULL);
    resinfo->partHolder = 0;
    resinfo->rankOffset = 0;
    resinfo->minRank = 0;
    resinfo->maxRank = 0;
    resinfo->nEntries = 0;
    resinfo->entries = NULL;
    resinfo->nLocalSlots = 0;
    resinfo->localSlots = NULL;

    return resinfo;
}

/**
 * @brief Filter passed to @ref PSIDspawn_cleanupDelayedTasks()
 *
 * @param task Task to investigate
 *
 * @param info Pointer to the reservation to match
 *
 * @return Return true if @a task is associated to reservation passed
 * in @a info; or false otherwise
 */
static bool cleanupFilter(PStask_t *task, void *info)
{
    PSresinfo_t *resinfo = info;

    if (task->resID != resinfo->resID) return false;

    task->delayReasons &= ~DELAY_RESINFO;
    return true;
}

/**
 * Release all memory of a resinfo and put it back into the pool. As a
 * side effect associated delayed tasks will be cleaned up, too.
 *
 * @param resinfo Resinfo to release
 */
static void putResinfo(PSresinfo_t *resinfo)
{
    PSIDspawn_cleanupDelayedTasks(cleanupFilter, resinfo);

    free(resinfo->entries);
    resinfo->entries = NULL;
    free(resinfo->localSlots);
    resinfo->localSlots = NULL;

    PSitems_putItem(resinfoPool, resinfo);
}

/**
 * Fetch a new job from the corresponding pool and initialize it
 */
static PSjob_t *getJob(void)
{
    PSjob_t *job = PSitems_getItem(jobPool);
    job->creation = time(NULL);
    job->registered = false;
    INIT_LIST_HEAD(&job->resInfos);

    return job;
}

/**
 * Detach all resinfos from job, release them and put the job back
 * into the pool
 *
 * @param job Job to release
 *
 * @param sessionID Embedding session's ID (for logging only)
 *
 * @param caller Name of the calling function (for logging only); if
 * NULL, no logging
 */
static void putJob(PSjob_t *job, PStask_ID_t sessionID, const char *caller)
{
    list_t *r, *tmp;
    list_for_each_safe(r, tmp, &job->resInfos) {
	PSresinfo_t *resinfo = list_entry(r, PSresinfo_t, next);
	list_del(&resinfo->next);
	putResinfo(resinfo);
    }

    if (caller) {  // do not log or call from PSIDsession_clearMem()
	/* Give plugins the option to react on job removal */
	PSIDhook_call(PSIDHOOK_LOCALJOBREMOVED, job);

	PSID_dbg(PSID_LOG_SPAWN, "%s: remove job %s", caller,
		 PSC_printTID(job->ID));
	PSID_dbg(PSID_LOG_SPAWN, " from session %s\n", PSC_printTID(sessionID));
    }

    PSitems_putItem(jobPool, job);
}

/**
 * Fetch a new session from the corresponding pool and initialize it
 */
static PSsession_t *getSession(void)
{
    PSsession_t *session = PSitems_getItem(sessionPool);
    session->creation = time(NULL);
    INIT_LIST_HEAD(&session->jobs);

    return session;
}

/**
 * Detach all jobs from a session, release the jobs and their attached
 * resinfos, and put the session back into the pool
 *
 * @param session Session to release
 *
 * @param caller Name of the calling function (for logging only); if
 * NULL, no logging
 */
static void putSession(PSsession_t *session, const char *caller)
{
    list_t *j, *tmp;
    list_for_each_safe(j, tmp, &session->jobs) {
	PSjob_t *job = list_entry(j, PSjob_t, next);

	list_del(&job->next);
	putJob(job, session->ID, caller);
    }

    if (caller) PSID_dbg(PSID_LOG_SPAWN, "%s: put session %s\n", caller,
			 PSC_printTID(session->ID));

    PSitems_putItem(sessionPool, session);
}

/**
 * Relocate the resinfo @a item suitable to be called from PSitems_gc()
 */
static bool relocResinfo(void *item)
{
    PSresinfo_t *orig = item, *repl = PSitems_getItem(resinfoPool);
    if (!repl) return false;

    /* copy content */
    repl->resID = orig->resID;
    repl->partHolder = orig->partHolder;
    repl->rankOffset = orig->rankOffset;
    repl->minRank = orig->minRank;
    repl->maxRank = orig->maxRank;
    repl->nEntries = orig->nEntries;
    repl->entries = orig->entries;

    /* tweak the list */
    __list_add(&repl->next, orig->next.prev, orig->next.next);

    return true;
}

/**
 * Relocate the job @a item suitable to be called from PSitems_gc()
 */
static bool relocJob(void *item)
{
    PSjob_t *orig = item, *repl = PSitems_getItem(jobPool);
    if (!repl) return false;

    /* copy content */
    repl->ID = orig->ID;
    /* rebase reservations if necessary */
    if (list_empty(&orig->resInfos)) {
	INIT_LIST_HEAD(&repl->resInfos);
    } else {
	__list_add(&repl->resInfos, orig->resInfos.prev, orig->resInfos.next);
    }

    /* tweak the list */
    __list_add(&repl->next, orig->next.prev, orig->next.next);

    return true;
}

/**
 * Relocate the session @a item suitable to be called from PSitems_gc()
 */
static bool relocSession(void *item)
{
    PSsession_t *orig = item, *repl = PSitems_getItem(sessionPool);
    if (!repl) return false;

    /* copy content */
    repl->ID = orig->ID;
    /* rebase jobs if necessary */
    if (list_empty(&orig->jobs)) {
	INIT_LIST_HEAD(&repl->jobs);
    } else {
	__list_add(&repl->jobs, orig->jobs.prev, orig->jobs.next);
    }

    /* tweak the list */
    __list_add(&repl->next, orig->next.prev, orig->next.next);

    return true;
}

static void PSIDsession_gc(void)
{
    if (!PSitems_gcRequired(resinfoPool)) PSitems_gc(resinfoPool, relocResinfo);
    if (!PSitems_gcRequired(jobPool)) PSitems_gc(jobPool, relocJob);
    if (!PSitems_gcRequired(sessionPool)) PSitems_gc(sessionPool, relocSession);
}

/************************* Actual functionality ****************************/

PSsession_t* PSID_findSessionByID(PStask_ID_t sessionID)
{
    list_t *s;
    list_for_each(s, &localSessions) {
	PSsession_t *session = list_entry(s, PSsession_t, next);
	if (session->ID == sessionID) return session;
    }
    return NULL;
}

/**
 * @brief Try to add reservation to job
 *
 * If a reservation with the same ID already exists, it will be
 * dropped. Adding @a res to the job @a job will always be
 * successful.
 **
 * @param job   job to add the reservation to
 * @param res   reservation to add
 *
 * @return No return value
 */
static void addReservationToJob(PSjob_t *job, PSresinfo_t *res)
{
    list_t *r, *tmp;
    list_for_each_safe(r, tmp, &job->resInfos) {
	PSresinfo_t *cur = list_entry(r, PSresinfo_t, next);
	if (cur->resID != res->resID) continue;

	PSID_flog("drop remnant reservation %#x created at %s, this should"
		 " never happen:\n", cur->resID, ctime(&cur->creation));
	PSID_log("\tminRank %d maxRank %d with %u entries:", cur->minRank,
		 cur->maxRank, cur->nEntries);
	for (uint32_t i = 0; i < cur->nEntries; i++) {
	    PSID_log(" (%d, %d, %d)", cur->entries[i].node,
		     cur->entries[i].firstRank, cur->entries[i].lastRank);
	}
	PSID_log("\n\tbelonging to job %s created at %s\n",
		 PSC_printTID(job->ID), ctime(&job->creation));

	list_del(&cur->next);
	putResinfo(cur);
    }

    list_add_tail(&res->next, &job->resInfos);
}

PSjob_t* PSID_findJobInSession(PSsession_t *session, PStask_ID_t jobID)
{
    list_t *j;
    list_for_each(j, &session->jobs) {
	PSjob_t *job = list_entry(j, PSjob_t, next);
	if (job->ID == jobID) return job;
    }
    return NULL;
}

PSresinfo_t* PSID_findResInfo(PStask_ID_t sessionID, PStask_ID_t jobID,
			      PSrsrvtn_ID_t resID)
{
    PSsession_t *session = PSID_findSessionByID(sessionID);
    if (!session) return NULL;

    PSjob_t *job = PSID_findJobInSession(session, jobID);
    if (!job) return NULL;

    list_t *r;
    list_for_each(r, &job->resInfos) {
	PSresinfo_t *resInfo = list_entry(r, PSresinfo_t, next);
	if (resInfo->resID == resID) return resInfo;
    }
    return NULL;
}

static inline void checkJob(PSjob_t *job, PStask_ID_t sessionID,
			    const char *caller)
{
    /* if job has no reservations left, delete it */
    if (list_empty(&job->resInfos)) {
	list_del(&job->next);
	putJob(job, sessionID, caller);
    }
}

static inline void checkSession(PSsession_t *session, const char *caller)
{
    /* if session has no jobs left, delete it */
    if (list_empty(&session->jobs)) {
	list_del(&session->next);
	putSession(session, caller);
    }
}

/**
 * @brief Store reservation information
 *
 * Actually stores the reservation information contained in the data
 * buffer @a rData. It contains a whole message of type PSP_DD_RESCREATED
 * holding all information about which rank will run on which node in
 * a specific reservation created.
 *
 * Additional information can be obtained from @a msg containing
 * meta-information of the last fragment received.
 *
 * @param msg Message header (including the type) of the last fragment
 *
 * @param rData Data buffer presenting the actual PSP_DD_RESCREATED
 *
 * @return No return value
 */
static void handleResCreated(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *rData)
{
    /* get reservation, session and job id */
    int32_t resID;
    getInt32(rData, &resID);
    PStask_ID_t sessionID, jobID;
    getTaskId(rData, &sessionID);
    getTaskId(rData, &jobID);

    uint32_t rankOffset = 0;
    PStask_ID_t partHolderTID = 0;
    if (PSIDnodes_getDmnProtoV(PSC_getID(msg->header.sender)) >= 416) {
	getUint32(rData, &rankOffset);
	getTaskId(rData, &partHolderTID);
    }

    /* create reservation info */
    PSresinfo_t *res = getResinfo();
    if (!res) {
	PSID_flog("no memory for reservation info\n");
	return;
    }

    /* set this early to enable putResinfo(res) to cleanup delayed tasks */
    res->resID = resID;
    res->rankOffset = rankOffset;
    res->partHolder = partHolderTID;

    /* calculate size of one entry in the message */
    size_t entrysize = sizeof(res->entries->node)
	+ sizeof(res->entries->firstRank) + sizeof(res->entries->lastRank);

    /* calculate number of entries */
    res->nEntries = (rData->buf + rData->used - rData->unpackPtr) / entrysize;

    res->entries = calloc(res->nEntries, sizeof(*res->entries));
    if (!res->entries) {
	putResinfo(res);
	PSID_flog("no memory for reservation info entries\n");
	return;
    }

    /* get entries */
    for (size_t i = 0; i < res->nEntries; i++) {
	getNodeId(rData, &res->entries[i].node);
	getInt32(rData, &res->entries[i].firstRank);
	getInt32(rData, &res->entries[i].lastRank);
	/* adapt minRank / maxRank */
	if (!i) {
	    res->minRank = res->entries[i].firstRank;
	    res->maxRank = res->entries[i].lastRank;
	} else {
	    if (res->entries[i].firstRank < res->minRank)
		res->minRank = res->entries[i].firstRank;
	    if (res->entries[i].lastRank > res->maxRank)
		res->maxRank = res->entries[i].lastRank;
	}

	/* add to reservation */
	PSID_fdbg(PSID_LOG_SPAWN, "reservation %#x: add node %hd: ranks %d-%d\n",
		  resID, res->entries[i].node,
		  res->entries[i].firstRank, res->entries[i].lastRank);
    }

    /* try to find existing session */
    bool sessionCreated = false;
    PSsession_t *session = PSID_findSessionByID(sessionID);
    if (!session) {
	/* create new session */
	session = getSession();
	if (!session) {
	    putResinfo(res);
	    PSID_flog("no memory for session\n");
	    return;
	}
	session->ID = sessionID;
	list_add_tail(&session->next, &localSessions);
	sessionCreated = true;
	PSID_fdbg(PSID_LOG_SPAWN, "add session %s\n", PSC_printTID(sessionID));
    }

    /* try to find existing job */
    bool jobCreated = false;
    PSjob_t *job = PSID_findJobInSession(session, jobID);
    if (!job) {
	/* create new job */
	job = getJob();
	if (!job) {
	    if (sessionCreated) {
		list_del(&session->next);
		putSession(session, __func__);
	    }
	    putResinfo(res);
	    PSID_flog("no memory for job\n");
	    return;
	}
	job->ID = jobID;
	job->sessID = session->ID;
	list_add_tail(&job->next, &session->jobs);
	jobCreated = true;
	PSID_fdbg(PSID_LOG_SPAWN, "add job %s to", PSC_printTID(jobID));
	PSID_dbg(PSID_LOG_SPAWN, " session %s\n", PSC_printTID(sessionID));
    }

    /* try to add reservation to job */
    addReservationToJob(job, res);
    PSID_fdbg(PSID_LOG_SPAWN, "add reservation %#x to job %s",
	      resID, PSC_printTID(jobID));
    PSID_dbg(PSID_LOG_SPAWN, " (session %s)\n", PSC_printTID(sessionID));

    /* Give plugins the option to react on job creation */
    if (jobCreated) PSIDhook_call(PSIDHOOK_LOCALJOBCREATED, job);
}

/**
 * @brief Handle a PSP_DD_RESCREATED message
 *
 * Handle the message @a msg of type PSP_DD_RESCREATED.
 *
 * This will store the reservation information described within this
 * message. Since the serialization layer is utilized depending on
 * the size and structure of the reservation the messages might be
 * split into multiple fragments.
 *
 * This function will collect these fragments into a single message
 * using the serialization layer or forward single fragments to their
 * final destination.
 *
 * The actual handling of the payload once all fragments are received
 * is done within @ref handleResCreated().
 *
 * @param msg Pointer to message holding the fragment to handle
 *
 * @return Always return true
 */
static bool msg_RESCREATED(DDTypedBufferMsg_t *msg)
{
    if (PSC_getID(msg->header.dest) == PSC_getMyID()) {
	/* destination is here */
	recvFragMsg(msg, handleResCreated);
	return true;
    }

    /* destination is remote */
    if (!PSIDnodes_isUp(PSC_getID(msg->header.dest))) {
	PSID_flog("unable to forward fragment to %s (node down)\n",
		  PSC_printTID(msg->header.dest));
	return true;
    }

    PSID_fdbg(PSID_LOG_SPAWN, "forward to node %d\n",
	      PSC_getID(msg->header.dest));
    if (sendMsg(msg) < 0) {
	PSID_flog("unable to forward fragment to %s (sendMsg failed)\n",
		  PSC_printTID(msg->header.dest));
    }
    return true;
}

/**
 * @brief Handle a PSP_DD_RESRELEASED message
 *
 * Handle the message @a msg of type PSP_DD_RESRELEASED.
 *
 * This will stop all services connected to the reservation being
 * released and remove all information about it.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_RESRELEASED(DDBufferMsg_t *msg)
{
    PSrsrvtn_ID_t resID;
    PStask_ID_t sessionID, jobID;
    size_t used = 0;

    PSP_getMsgBuf(msg, &used, "resID", &resID, sizeof(resID));
    PSP_getMsgBuf(msg, &used, "sessionID", &sessionID, sizeof(sessionID));
    PSP_getMsgBuf(msg, &used, "jobID", &jobID, sizeof(jobID));

    /* try to find corresponding session */
    PSsession_t *session = PSID_findSessionByID(sessionID);
    if (!session) {
	PSID_fdbg(PSID_LOG_PART, "no session %s expected to hold %#x",
		  PSC_printTID(sessionID), resID);
	PSID_dbg(PSID_LOG_PART, " from %s\n", PSC_printTID(msg->header.sender));
	return true;
    }

    /* try to find corresponding job within the session */
    PSjob_t* job = PSID_findJobInSession(session, jobID);
    if (!job) {
	PSID_flog("no job %s expected to hold resID %#x",
		  PSC_printTID(jobID), resID);
	PSID_log(" in session %s\n", PSC_printTID(sessionID));
	return true;
    }

    /* try to find reservation in job and delete it */
    bool found = false;
    list_t *r;
    list_for_each(r, &job->resInfos) {
	PSresinfo_t *res = list_entry(r, PSresinfo_t, next);
	if (res->resID != resID) continue;

	list_del(&res->next);
	putResinfo(res);
	found = true;
	break;
    }

    if (!found) {
	PSID_flog("no reservation %#x in session %s\n", resID,
		  PSC_printTID(sessionID));
    } else {
	PSID_fdbg(PSID_LOG_SPAWN, "remove reservation %#x from job %s", resID,
		  PSC_printTID(jobID));
	PSID_dbg(PSID_LOG_SPAWN, " in session %s)\n", PSC_printTID(sessionID));
    }

    checkJob(job, sessionID, __func__);
    checkSession(session, __func__);

    return true;
}

/**
 * @brief Handle a PSP_DD_RESCLEANUP message
 *
 * Handle the message @a msg of type PSP_DD_RESCLEANUP.
 *
 * This will cleanup all information on a session stored on the local
 * node. This type of message is sent by the session leader (i.e. the
 * logger process) to all nodes of a leaving sister partitions if they
 * are not utilized by the partition itself or any other sister
 * partition. This will guarantee that no remnant reservation
 * information is left on those nodes.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_RESCLEANUP(DDBufferMsg_t *msg)
{
    PSID_fdbg(PSID_LOG_SPAWN, "session %s", PSC_printTID(msg->header.sender));
    PSsession_t *session = PSID_findSessionByID(msg->header.sender);
    if (session) {
	list_del(&session->next);
	putSession(session, __func__);
    } else {
	PSID_dbg(PSID_LOG_SPAWN, " already gone\n");
    }

    return true;
}

/**
 * @brief Filter passed to @ref PSIDspawn_startDelayedTasks().
 *
 * If the reservation info passed in @a info matches @a task, this
 * function will try to fill the missing CPUset into the task to spawn
 * utilizing @ref PSIDspawn_fillTaskFromResInfo(). If successful, the
 * filter will return true. Otherwise a PSP_CD_SPAWNFAILED message is
 * emitted to the spawner and @a task is evicted from the list of
 * delayed tasks and deleted.
 *
 * @param task Task to investigate
 *
 * @param info Pointer to the reservation to match
 *
 * @return Return true if @a task is associated to reservation passed
 * in @a info and filled with the missing CPUset; or false otherwise
 */
static bool startFilter(PStask_t *task, void *info)
{
    PSresinfo_t *res = info;

    if (task->resID != res->resID || !res->nLocalSlots
	|| !(task->delayReasons & DELAY_RESINFO)) return false;

    DDErrorMsg_t answer = {
	.header = {
	    .type = PSP_CD_SPAWNFAILED,
	    .sender = PSC_getMyTID(),
	    .dest = task->spawnertid,
	    .len = sizeof(answer) },
	.error = 0,
	.request = task->rank,};

    answer.error = PSIDspawn_fillTaskFromResInfo(task, res);

    if (answer.error) {
	sendMsg(&answer);
	PStasklist_dequeue(task);
	PStask_delete(task);
	return false;
    }

    task->delayReasons &= ~DELAY_RESINFO;
    return true;
}

/**
 * @brief Store local reservation information
 *
 * Actually stores the local reservation information contained in the
 * data buffer @a rData. It contains a whole message of type
 * PSP_DD_RESSLOTS holding additional information about the local part
 * of the reservation like pinning information of the processes.
 *
 * Additional information can be obtained from @a msg containing
 * meta-information of the last fragment received.
 *
 * @param msg Message header (including the type) of the last fragment
 *
 * @param rData Data buffer presenting the actual PSP_DD_RESSLOTS
 *
 * @return No return value
 */
static void handleResSlots(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *rData)
{
    /* get logger task id, spawner task id, and reservation */
    PStask_ID_t sessionID, jobID;
    getTaskId(rData, &sessionID);
    getTaskId(rData, &jobID);
    int32_t resID;
    getInt32(rData, &resID);

    /* identify reservation info */
    PSresinfo_t *res = PSID_findResInfo(sessionID, jobID, resID);
    if (!res) {
	PSID_flog("no reservation info on session %s", PSC_printTID(sessionID));
	PSID_log(" job %s and resID %#x\n", PSC_printTID(jobID), resID);
	/* we might have to cleanup delayed tasks */
	res = getResinfo();
	res->resID = resID;
	putResinfo(res);

	return;
    }
    if (res->localSlots) {
	PSID_flog("reservation %#x already has localSlots?!\n", resID);
	return;
    }

    uint16_t nBytes;
    getUint16(rData, &nBytes);
    if (nBytes != PSCPU_bytesForCPUs(PSIDnodes_getNumThrds(PSC_getMyID()))) {
	PSID_flog("CPU-set size mismatch %ud\n", nBytes);
	return;
    }

    uint16_t num;
    getUint16(rData, &num);
    res->localSlots = malloc(num * sizeof(*res->localSlots));
    res->nLocalSlots = num;

    int32_t rank;
    for (uint32_t s = 0; s < num; s++) {
	getInt32(rData, &rank);
	if (rank < 0 || rank < res->minRank || rank > res->maxRank) {
	    PSID_flog("invalid rank %d @ %ud in resID %#x (%d,%d)\n",
		      rank, s, resID, res->minRank, res->maxRank);
	    free(res->localSlots);
	    res->localSlots = NULL;
	    return;
	}
	res->localSlots[s].rank = rank;

	PSCPU_clrAll(res->localSlots[s].CPUset);
	PSCPU_inject(res->localSlots[s].CPUset, rData->unpackPtr, nBytes);
	rData->unpackPtr += nBytes;

	PSID_fdbg(PSID_LOG_SPAWN, "add cpuset %s for job rank %d to res %#x\n",
		  PSCPU_print_part(res->localSlots[s].CPUset, nBytes),
		  rank, resID);
    }

    /* check for end of message */
    getInt32(rData, &rank);
    if (rank != -1) PSID_flog("trailing slot for rank %d\n", rank);

    /* there might be delayed tasks that are capable to start now */
    PSIDspawn_startDelayedTasks(startFilter, res);
}

/**
 * @brief Handle a PSP_DD_RESSLOTS message
 *
 * Handle the message @a msg of type PSP_DD_RESSLOTS.
 *
 * This will store the reservation information described within this
 * message. Since the serialization layer is utilized depending on
 * the size and structure of the reservation the messages might be
 * split into multiple fragments.
 *
 * This function will collect these fragments into a single message
 * using the serialization layer or forward single fragments to their
 * final destination.
 *
 * The actual handling of the payload once all fragments are received
 * is done within @ref handleResSlots().
 *
 * @param msg Pointer to message holding the fragment to handle
 *
 * @return Always return true
 */
static bool msg_RESSLOTS(DDTypedBufferMsg_t *msg)
{
    if (PSC_getID(msg->header.dest) == PSC_getMyID()) {
	/* destination is here */
	recvFragMsg(msg, handleResSlots);
	return true;
    }

    /* destination is remote */
    if (!PSIDnodes_isUp(PSC_getID(msg->header.dest))) {
	PSID_flog("unable to forward fragment to %s (node down)\n",
		  PSC_printTID(msg->header.dest));
	return true;
    }

    PSID_fdbg(PSID_LOG_SPAWN, "forward to node %d\n",
	      PSC_getID(msg->header.dest));
    if (sendMsg(msg) < 0) {
	PSID_flog("unable to forward fragment to %s (sendMsg failed)\n",
		  PSC_printTID(msg->header.dest));
    }
    return true;
}

/**
 * @brief Memory cleanup
 *
 * Cleanup all dynamic memory currently retained in session structures
 * and all descendants. This will very aggressively free() all
 * allocated memory destroying all information on sessions, jobs, and
 * reservations.
 *
 * The purpose of this function is to cleanup before a fork()ed
 * process is handling other businesses, e.g. becoming a forwarder. It
 * will be registered to the PSIDHOOK_CLEARMEM hook in order to be
 * called accordingly.
 *
 * @param dummy Ignored pointer to aggressive flag
 *
 * @return Always return 0
 */
static int clearMem(void *dummy)
{
    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &localSessions) {
	PSsession_t *session = list_entry(s, PSsession_t, next);
	list_del(&session->next);
	putSession(session, NULL);
    }

    PSitems_clearMem(sessionPool);
    sessionPool = NULL;

    PSitems_clearMem(jobPool);
    jobPool = NULL;

    PSitems_clearMem(resinfoPool);
    resinfoPool = NULL;

    return 0;
}


bool PSIDsession_init(void)
{
    PSID_fdbg(PSID_LOG_VERB, "\n");

    /* initialize various item pools */
    resinfoPool = PSitems_new(sizeof(PSresinfo_t), "resinfoPool");
    if (!resinfoPool) {
	PSID_flog("cannot get resinfo items\n");
	return false;
    }

    jobPool = PSitems_new(sizeof(PSjob_t), "jobPool");
    if (!jobPool) {
	PSID_flog("cannot get job items\n");
	return false;
    }

    sessionPool = PSitems_new(sizeof(PSsession_t), "sessionPool");
    if (!sessionPool) {
	PSID_flog("cannot get session items\n");
	return false;
    }
    PSID_registerLoopAct(PSIDsession_gc);

    if (!PSIDhook_add(PSIDHOOK_CLEARMEM, clearMem)) {
	PSID_flog("cannot register to PSIDHOOK_CLEARMEM\n");
	return false;
    }

    /* init fragmentation layer used for PSP_DD_RESCREATED messages */
    if (!initSerial(0, sendMsg)) {
	PSID_flog("initSerial() failed\n");
	return false;
    }

    PSID_registerMsg(PSP_DD_RESCREATED, (handlerFunc_t) msg_RESCREATED);
    PSID_registerMsg(PSP_DD_RESRELEASED, msg_RESRELEASED);
    PSID_registerMsg(PSP_DD_RESSLOTS, (handlerFunc_t) msg_RESSLOTS);
    PSID_registerMsg(PSP_DD_RESCLEANUP, msg_RESCLEANUP);

    return true;
}

void PSIDsession_printStat(void)
{
    PSID_flog("Sessions %d/%d (used/avail)\t%d/%d (gets/grows)\n",
	      PSitems_getUsed(sessionPool), PSitems_getAvail(sessionPool),
	      PSitems_getUtilization(sessionPool),
	      PSitems_getDynamics(sessionPool));
    PSID_flog("Jobs %d/%d (used/avail)\t%d/%d (gets/grows)\n",
	      PSitems_getUsed(jobPool), PSitems_getAvail(jobPool),
	      PSitems_getUtilization(jobPool), PSitems_getDynamics(jobPool));
    PSID_flog("ResInfos %d/%d (used/avail)\t%d/%d (gets/grows)\n",
	      PSitems_getUsed(resinfoPool), PSitems_getAvail(resinfoPool),
	      PSitems_getUtilization(resinfoPool),
	      PSitems_getDynamics(resinfoPool));
}
