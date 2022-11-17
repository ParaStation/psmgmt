/*
 * ParaStation
 *
 * Copyright (C) 2022 ParTec AG, Munich
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
PSresinfo_t *getResinfo(void)
{
    PSresinfo_t *resinfo = PSitems_getItem(resinfoPool);
    resinfo->nEntries = 0;
    resinfo->minRank = 0;
    resinfo->maxRank = 0;
    resinfo->entries = NULL;
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
PSjob_t *getJob(void)
{
    PSjob_t *job = PSitems_getItem(jobPool);
    INIT_LIST_HEAD(&job->resInfos);

    return job;
}

/**
 * Detach all resinfos from job, release them and put the job back
 * into the pool
 *
 * @param job Job to release
 */
static void putJob(PSjob_t *job)
{
    list_t *r, *tmp;
    list_for_each_safe(r, tmp, &job->resInfos) {
	PSresinfo_t *resinfo = list_entry(r, PSresinfo_t, next);
	list_del(&resinfo->next);
	putResinfo(resinfo);
    }
    PSitems_putItem(jobPool, job);
}

/**
 * Fetch a new session from the corresponding pool and initialize it
 */
PSsession_t *getSession(void)
{
    PSsession_t *session = PSitems_getItem(sessionPool);
    INIT_LIST_HEAD(&session->jobs);

    return session;
}

/**
 * Detach all jobs from a session, release the jobs and their attached
 * resinfos, and put the session back into the pool
 *
 * @param session Session to release
 */
static void putSession(PSsession_t *session)
{
    list_t *j, *tmp;
    list_for_each_safe(j, tmp, &session->jobs) {
	PSjob_t *job = list_entry(j, PSjob_t, next);
	list_del(&job->next);
	putJob(job);
    }
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
    repl->spawnertid = orig->spawnertid;
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
    repl->loggertid = orig->loggertid;
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

PSsession_t* PSID_findSessionByLoggerTID(PStask_ID_t loggerTID)
{
    list_t *s;
    list_for_each(s, &localSessions) {
	PSsession_t *session = list_entry(s, PSsession_t, next);
	if (session->loggertid == loggerTID) return session;
    }
    return NULL;
}

/**
 * @brief Try to add reservation to job
 *
 * If the reservation already exists and is identical, it is simply ignored,
 * if it is not the same, it is ignored and a warning is logged.
 *
 * @param job   job to add the reservation to
 * @param res   reservation to add
 *
 * @return Returns true if the reservation is added to the job, false if not
 */
static bool addReservationToJob(PSjob_t *job, PSresinfo_t *res)
{
    list_t *r;
    list_for_each(r, &job->resInfos) {
	PSresinfo_t *cur = list_entry(r, PSresinfo_t, next);
	if (cur->resID != res->resID) continue;
	if (cur->nEntries != res->nEntries || cur->minRank != res->minRank
	    || cur->maxRank != res->maxRank) {
	    PSID_log(-1, "%s: Reservation %d already known but differs,"
		     " this should never happen\n", __func__, res->resID);
	}
	/* Note: Could also check all entries, but that may be overkill? */
	return false;
    }

    list_add_tail(&res->next, &job->resInfos);

    return true;
}

/**
 * @brief Find job in session by spawner
 *
 * @param session      Session to search in
 * @param spawnerTID   Task ID of spawner identifying the job
 *
 * @return Returns the job or NULL if none found
 */
PSjob_t* PSID_findJobInSession(PSsession_t *session, PStask_ID_t spawnerTID)
{
    list_t *j;
    list_for_each(j, &session->jobs) {
	PSjob_t *job = list_entry(j, PSjob_t, next);
	if (job->spawnertid == spawnerTID) return job;
    }
    return NULL;
}

/**
 * @brief Find reservation info by session and job identifier
 *
 * @param loggerTID    Task ID of logger identifying the session
 * @param spawnerTID   Task ID of spawner identifying the job
 * @param resID        ID of the reservation to get the info object for
 *
 * @return Returns the reservation or NULL if none found
 */
PSresinfo_t* PSID_findResInfo(PStask_ID_t loggerTID, PStask_ID_t spawnerTID,
			  PSrsrvtn_ID_t resID)
{
    PSsession_t *session = PSID_findSessionByLoggerTID(loggerTID);
    if (!session) return NULL;

    PSjob_t *job = PSID_findJobInSession(session, spawnerTID);
    if (!job) return NULL;

    list_t *r;
    list_for_each(r, &job->resInfos) {
	PSresinfo_t *resInfo = list_entry(r, PSresinfo_t, next);
	if (resInfo->resID == resID) return resInfo;
    }
    return NULL;
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
    char *ptr = rData->buf;

    /* get reservation, logger task id and spawner task id */
    int32_t resID;
    getInt32(&ptr, &resID);
    PStask_ID_t loggerTID, spawnerTID;
    getTaskId(&ptr, &loggerTID);
    getTaskId(&ptr, &spawnerTID);

    /* create reservation info */
    PSresinfo_t *res = getResinfo();
    if (!res) {
	PSID_log(-1, "%s: No memory for reservation info\n", __func__);
	return;
    }

    /* set this early to enable putResinfo(res) to cleanup delayed tasks */
    res->resID = resID;

    /* calculate size of one entry in the messsage */
    size_t entrysize = sizeof(res->entries->node)
	+ sizeof(res->entries->firstrank) + sizeof(res->entries->lastrank);

    /* calculate number of entries */
    res->nEntries = (rData->buf + rData->used - ptr) / entrysize;

    res->entries = calloc(res->nEntries, sizeof(*res->entries));
    if (!res->entries) {
	putResinfo(res);
	PSID_log(-1, "%s: No memory for reservation info entries\n", __func__);
	return;
    }

    /* get entries */
    for (size_t i = 0; i < res->nEntries; i++) {
	getNodeId(&ptr, &res->entries[i].node);
	getInt32(&ptr, &res->entries[i].firstrank);
	getInt32(&ptr, &res->entries[i].lastrank);
	/* adapt minRank / maxRank */
	if (!i) {
	    res->minRank = res->entries[i].firstrank;
	    res->maxRank = res->entries[i].lastrank;
	} else {
	    if (res->entries[i].firstrank < res->minRank)
		res->minRank = res->entries[i].firstrank;
	    if (res->entries[i].lastrank > res->maxRank)
		res->maxRank = res->entries[i].lastrank;
	}

	/* add to reservation */
	PSID_log(PSID_LOG_SPAWN, "%s: Reservation %d: Adding node %hd:"
		 " ranks %d-%d\n", __func__, resID, res->entries[i].node,
		 res->entries[i].firstrank, res->entries[i].lastrank);
    }

    /* try to find existing session */
    bool sessionCreated = false;
    PSsession_t *session = PSID_findSessionByLoggerTID(loggerTID);
    if (!session) {
	/* create new session */
	session = getSession();
	if (!session) {
	    putResinfo(res);
	    PSID_log(-1, "%s: No memory for session\n", __func__);
	    return;
	}
	session->loggertid = loggerTID;
	list_add_tail(&session->next, &localSessions);
	sessionCreated = true;
	PSID_log(PSID_LOG_SPAWN, "%s: Session created for logger %s\n",
		 __func__, PSC_printTID(loggerTID));
    }

    /* try to find existing job */
    bool jobCreated = false;
    PSjob_t *job = PSID_findJobInSession(session, spawnerTID);
    if (!job) {
	/* create new job */
	job = getJob();
	if (!job) {
	    if (sessionCreated) {
		list_del(&session->next);
		putSession(session);
	    }
	    putResinfo(res);
	    PSID_log(-1, "%s: No memory for job\n", __func__);
	    return;
	}
	job->spawnertid = spawnerTID;
	list_add_tail(&job->next, &session->jobs);
	jobCreated = true;
	PSID_log(PSID_LOG_SPAWN, "%s: Job created for spawner %s",
		 __func__, PSC_printTID(spawnerTID));
	PSID_log(PSID_LOG_SPAWN, "in session with loggertid %s\n",
		 PSC_printTID(loggerTID));
    }

    /* try to add reservation to job */
    if (!addReservationToJob(job, res)) {
	PSID_log(PSID_LOG_SPAWN, "%s: Reservation %d exists (spawner %s",
		__func__, resID, PSC_printTID(spawnerTID));
	PSID_log(PSID_LOG_SPAWN, " logger %s)\n", PSC_printTID(loggerTID));
	if (sessionCreated) {
	    list_del(&session->next);
	    putSession(session);
	} else if (jobCreated) {
	    list_del(&job->next);
	    putJob(job);
	}
	putResinfo(res);
	return;
    } else {
	PSID_log(PSID_LOG_SPAWN, "%s: Reservation %d added (spawner %s",
		__func__, resID, PSC_printTID(spawnerTID));
	PSID_log(PSID_LOG_SPAWN, " logger %s)\n", PSC_printTID(loggerTID));
    }

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
	PSID_log(-1, "%s: unable to forward fragment to %s (node down)\n",
		 __func__, PSC_printTID(msg->header.dest));
	return true;
    }

    PSID_log(PSID_LOG_SPAWN, "%s: forward to node %d\n", __func__,
	     PSC_getID(msg->header.dest));
    if (sendMsg(msg) < 0) {
	PSID_log(-1, "%s: unable to forward fragment to %s (sendMsg failed)\n",
		 __func__, PSC_printTID(msg->header.dest));
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
    PStask_ID_t logTID, spawnTID;
    size_t used = 0;

    PSP_getMsgBuf(msg, &used, "resID", &resID, sizeof(resID));
    PSP_getMsgBuf(msg, &used, "loggerTID", &logTID, sizeof(logTID));
    PSP_getMsgBuf(msg, &used, "spawnerTID", &spawnTID, sizeof(spawnTID));

    /* try to find corresponding session */
    PSsession_t *session = PSID_findSessionByLoggerTID(logTID);
    if (!session) {
	PSID_log(-1, "%s: No session (%s) expected to hold resID %d\n",
		 __func__, PSC_printTID(logTID), resID);
	return true;
    }

    /* try to find corresponding job within the session */
    PSjob_t* job = PSID_findJobInSession(session, spawnTID);
    if (!job) {
	PSID_log(-1, "%s: No job (%s) expected to hold resID %d",
		 __func__, PSC_printTID(spawnTID), resID);
	PSID_log(-1, " in session (%s)\n", PSC_printTID(logTID));
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
	PSID_log(-1, "%s: No reservation (%d) in session (%s)\n", __func__,
		 resID, PSC_printTID(logTID));
    }

    /* if job has no reservations left, delete it */
    if (list_empty(&job->resInfos)) {
	/* Give plugins the option to react on job removal */
	PSIDhook_call(PSIDHOOK_LOCALJOBREMOVED, job);

	list_del(&job->next);
	putJob(job);
    }

    /* if session has no jobs left, delete it */
    if (list_empty(&session->jobs)) {
	list_del(&session->next);
	putSession(session);
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
	|| !(task->delayReasons | DELAY_RESINFO)) return false;

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
    char *ptr = rData->buf;

    /* get logger task id, spawner task id, and reservation */
    PStask_ID_t loggerTID, spawnerTID;
    getTaskId(&ptr, &loggerTID);
    getTaskId(&ptr, &spawnerTID);
    int32_t resID;
    getInt32(&ptr, &resID);

    /* identify reservation info */
    PSresinfo_t *res = PSID_findResInfo(loggerTID, spawnerTID, resID);
    if (!res) {
	PSID_log(-1, "%s: No reservation info for logger %s", __func__,
		 PSC_printTID(loggerTID));
	PSID_log(-1, "spawner %s and resID %d\n", PSC_printTID(spawnerTID),
		 resID);
	/* we might have to cleanup delayed tasks */
	res = getResinfo();
	res->resID = resID;
	putResinfo(res);

	return;
    }
    if (res->localSlots) {
	PSID_log(-1, "%s: reservation %d has localSlots?!\n", __func__, resID);
	return;
    }

    uint16_t nBytes;
    getUint16(&ptr, &nBytes);
    if (nBytes != PSCPU_bytesForCPUs(PSIDnodes_getNumThrds(PSC_getMyID()))) {
	PSID_log(-1, "%s: CPU-set size mismatch %ud", __func__, nBytes);
	return;
    }

    uint16_t num;
    getUint16(&ptr, &num);
    res->localSlots = malloc(num * sizeof(*res->localSlots));
    res->nLocalSlots = num;

    int32_t rank;
    for (uint32_t s = 0; s < num; s++) {
	getInt32(&ptr, &rank);
	if (rank < 0 || rank < res->minRank || rank > res->maxRank) {
	    PSID_log(-1, "%s: invalid rank %d @ %ud in resID %d (%d,%d)\n",
		     __func__, rank, s, resID, res->minRank, res->maxRank);
	    free(res->localSlots);
	    res->localSlots = NULL;
	    return;
	}
	res->localSlots[s].rank = rank;

	PSCPU_clrAll(res->localSlots[s].CPUset);
	PSCPU_inject(res->localSlots[s].CPUset, ptr, nBytes);
	ptr += nBytes;

	PSID_log(PSID_LOG_PART, "%s: Add cpuset %s for rank %d in res %d\n",
		 __func__, PSCPU_print_part(res->localSlots[s].CPUset, nBytes),
		 rank, resID);
    }

    /* check for end of message */
    getInt32(&ptr, &rank);
    if (rank != -1)
	PSID_log(-1, "%s: trailing slot for rank %d", __func__, rank);

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
	PSID_log(-1, "%s: unable to forward fragment to %s (node down)\n",
		 __func__, PSC_printTID(msg->header.dest));
	return true;
    }

    PSID_log(PSID_LOG_SPAWN, "%s: forward to node %d\n", __func__,
	     PSC_getID(msg->header.dest));
    if (sendMsg(msg) < 0) {
	PSID_log(-1, "%s: unable to forward fragment to %s (sendMsg failed)\n",
		 __func__, PSC_printTID(msg->header.dest));
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
	putSession(session);
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
    PSID_log(PSID_LOG_VERB, "%s()\n", __func__);

    /* initialize various item pools */
    resinfoPool = PSitems_new(sizeof(PSresinfo_t), "resinfoPool");
    if (!resinfoPool) {
	PSID_log(-1, "%s: cannot get resinfo items\n", __func__);
	return false;
    }

    jobPool = PSitems_new(sizeof(PSjob_t), "jobPool");
    if (!jobPool) {
	PSID_log(-1, "%s: cannot get job items\n", __func__);
	return false;
    }

    sessionPool = PSitems_new(sizeof(PSsession_t), "sessionPool");
    if (!sessionPool) {
	PSID_log(-1, "%s: cannot get session items\n", __func__);
	return false;
    }
    PSID_registerLoopAct(PSIDsession_gc);

    if (!PSIDhook_add(PSIDHOOK_CLEARMEM, clearMem)) {
	PSID_log(-1, "%s: cannot register to PSIDHOOK_CLEARMEM\n", __func__);
	return false;
    }

    /* init fragmentation layer used for PSP_DD_RESCREATED messages */
    if (!initSerial(0, sendMsg)) {
	PSID_log(-1, "%s: initSerial() failed\n", __func__);
	return false;
    }

    PSID_registerMsg(PSP_DD_RESCREATED, (handlerFunc_t) msg_RESCREATED);
    PSID_registerMsg(PSP_DD_RESRELEASED, msg_RESRELEASED);
    PSID_registerMsg(PSP_DD_RESSLOTS, (handlerFunc_t) msg_RESSLOTS);

    return true;
}

void PSIDsession_printStat(void)
{
    PSID_log(-1, "%s: Sessions %d/%d (used/avail)", __func__,
	     PSitems_getUsed(sessionPool), PSitems_getAvail(sessionPool));
    PSID_log(-1, "\t%d/%d (gets/grows)\n", PSitems_getUtilization(sessionPool),
	     PSitems_getDynamics(sessionPool));
    PSID_log(-1, "%s: Jobs %d/%d (used/avail)", __func__,
	     PSitems_getUsed(jobPool), PSitems_getAvail(jobPool));
    PSID_log(-1, "\t%d/%d (gets/grows)\n", PSitems_getUtilization(jobPool),
	     PSitems_getDynamics(jobPool));
    PSID_log(-1, "%s: ResInfos %d/%d (used/avail)", __func__,
	     PSitems_getUsed(resinfoPool), PSitems_getAvail(resinfoPool));
    PSID_log(-1, "\t%d/%d (gets/grows)\n", PSitems_getUtilization(resinfoPool),
	     PSitems_getDynamics(resinfoPool));
}
