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

#include <stdbool.h>
#include <stdlib.h>

#include "pscommon.h"
#include "psserial.h"
#include "psdaemonprotocol.h"

#include "psidutil.h"
#include "psidhook.h"
#include "psidnodes.h"
#include "psidcomm.h"

/** List of reservations this node is part of organized in sessions and jobs */
static LIST_HEAD(localSessions);

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
	if (cur->resID == res->resID) {
	    if (cur->nEntries != res->nEntries) {
		PSID_log(-1, "%s: Reservation %d already known but differs,"
			 " this should never happen\n", __func__, res->resID);
	    }
	    /* Note: Could also check all entries, but that may be overkill? */
	    return false;
	}
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
PSjob_t* findJobInSession(PSsession_t *session, PStask_ID_t spawnerTID)
{
    list_t *j;
    list_for_each(j, &session->jobs) {
	PSjob_t *job = list_entry(j, PSjob_t, next);
	if (job->spawnertid == spawnerTID) return job;
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

    int32_t resID;
    PStask_ID_t loggerTID, spawnerTID;

    /* get reservation, logger task id and spawner task id */
    getInt32(&ptr, &resID);
    getTaskId(&ptr, &loggerTID);
    getTaskId(&ptr, &spawnerTID);

    /* create reservation info */
    PSresinfo_t *res = malloc(sizeof(*res));
    if (!res) {
	PSID_log(-1, "%s: No memory for reservation info\n", __func__);
	return;
    }

    /* calculate size of one entry */
    size_t entrysize = sizeof(res->entries->node)
	+ sizeof(res->entries->firstrank) + sizeof(res->entries->lastrank);

    /* calculate number of entries */
    res->nEntries = (rData->buf + rData->used - ptr) / entrysize;

    res->entries = calloc(res->nEntries, sizeof(*res->entries));
    if (!res->entries) {
	free(res);
	PSID_log(-1, "%s: No memory for reservation info entries\n", __func__);
	return;
    }

    res->resID = resID;

    /* get entries */
    for (size_t i = 0; i < res->nEntries; i++) {
	getNodeId(&ptr, &res->entries[i].node);
	getInt32(&ptr, &res->entries[i].firstrank);
	getInt32(&ptr, &res->entries[i].lastrank);

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
	session = malloc(sizeof(*session));
	if (!session) {
	    free(res->entries);
	    free(res);
	    PSID_log(-1, "%s: No memory for session\n", __func__);
	    return;
	}
	session->loggertid = loggerTID;
	INIT_LIST_HEAD(&session->jobs);
	list_add_tail(&session->next, &localSessions);
	sessionCreated = true;
	PSID_log(PSID_LOG_SPAWN, "%s: Session created for logger %s\n",
		 __func__, PSC_printTID(loggerTID));
    }

    /* try to find existing job */
    bool jobCreated = false;
    PSjob_t *job = findJobInSession(session, spawnerTID);
    if (!job) {
	/* create new job */
	job = malloc(sizeof(*job));
	if (!job) {
	    if (sessionCreated) {
		list_del(&session->next);
		free(session);
	    }
	    free(res->entries);
	    free(res);
	    PSID_log(-1, "%s: No memory for job\n", __func__);
	    return;
	}
	job->spawnertid = spawnerTID;
	INIT_LIST_HEAD(&job->resInfos);
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
	if (jobCreated) {
	    list_del(&job->next);
	    free(job);
	    if (sessionCreated) {
		list_del(&session->next);
		free(session);
	    }
	}
	free(res->entries);
	free(res);
	return;
    } else {
	PSID_log(PSID_LOG_SPAWN, "%s: Reservation %d added (spawner %s",
		__func__, resID, PSC_printTID(spawnerTID));
	PSID_log(PSID_LOG_SPAWN, " logger %s)\n", PSC_printTID(loggerTID));
    }

    if (jobCreated) {
	/* Give plugins the option to react on job creation */
	PSIDhook_call(PSIDHOOK_LOCALJOBCREATED, job);
    }
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
    PSjob_t* job = findJobInSession(session, spawnTID);
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
	free(res->entries);
	free(res);
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
	free(job);
    }

    /* if session has no jobs left, delete it */
    if (list_empty(&session->jobs)) {
	list_del(&session->next);
	free(session);
    }

    return true;
}

void PSIDsession_init(void)
{
    PSID_log(PSID_LOG_VERB, "%s()\n", __func__);

    /* init fragmentation layer used for PSP_DD_RESCREATED messages */
    if (!initSerial(0, sendMsg)) {
	PSID_log(-1, "%s: initSerial() failed\n", __func__);
    }

    PSID_registerMsg(PSP_DD_RESCREATED, (handlerFunc_t) msg_RESCREATED);
    PSID_registerMsg(PSP_DD_RESRELEASED, msg_RESRELEASED);
}

/**
 * Aggressively delete a job and free all memory of it and its reservations
 *
 * @param job  job to delete
 */
static void PSjob_delete(PSjob_t *job)
{
    list_t *r, *tmp;
    list_for_each_safe(r, tmp, &job->resInfos) {
	PSresinfo_t *res = list_entry(r, PSresinfo_t, next);
	free(res->entries);
	list_del(&res->next);
    }
    list_del(&job->next);
    free(job);
}

/**
 * Aggressively delete a session and free all memory of it and its jobs
 *
 * @param session  session to delete
 */
static void PSsession_delete(PSsession_t *session)
{
    list_t *j, *tmp;
    list_for_each_safe(j, tmp, &session->jobs) {
	PSjob_t *job = list_entry(j, PSjob_t, next);
	PSjob_delete(job);
    }
    list_del(&session->next);
    free(session);
}

void PSIDsession_clearMem(void)
{
    list_t *s, *tmp;

    list_for_each_safe(s, tmp, &localSessions) {
	PSsession_t *ss = list_entry(s, PSsession_t, next);

	PSsession_delete(ss);
    }
}
