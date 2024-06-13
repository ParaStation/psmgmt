/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Implementation of pspmix functions running in the daemon
 *
 * Four tasks are fulfilled inside the daemon:
 * 1. Start the PMIx server for the user as plugin forwarder
 * 2. Inform an already running PMIx server about new jobs
 * 3. Forward plugin messages
 * 4. Handle (unexpected) death of PMIx servers
 */
#include "pspmixdaemon.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "list.h"
#include "pscommon.h"
#include "psdaemonprotocol.h"
#include "psenv.h"
#include "pspluginprotocol.h"
#include "psreservation.h"
#include "psserial.h"
#include "timer.h"

#include "pluginconfig.h"
#include "pluginforwarder.h"
#include "pluginmalloc.h"

#include "psidclient.h"
#include "psidcomm.h"
#include "psidhook.h"
#include "psidsession.h"
#include "psidsignal.h"
#include "psidtask.h"

#include "pspmixtypes.h"
#include "pspmixlog.h"
#include "pspmixcommon.h"
#include "pspmixuserserver.h"
#include "pspmixcomm.h"
#include "pspmixconfig.h"
#include "pspmixutil.h"

/** The list of running PMIx servers on this node */
static LIST_HEAD(pmixServers);

/**
 * @brief Detailed print all PMIx servers
 */
static void __printServers(const char *caller, int line)
{
    list_t *s;
    list_for_each(s, &pmixServers) {
	PspmixServer_t *server = list_entry(s, PspmixServer_t, next);
	__pspmix_printServer(server, true, caller, line);
    }
}
#define printServers() __printServers(__func__, __LINE__)

/**
 * @brief Find local PMIx server for user
 *
 * Find PMIx server by user id @a uid
 * Ignores both kind of invalid servers, those died and waiting for
 * PSIDHOOK_LOCALJOBREMOVED called and those finalized by @a stopServer()
 * and waiting for SIGCHILD handling in @a serverTerminated_cb().
 *
 * @param uid user id
 *
 * @return Returns the job or NULL if not in list
 */
static PspmixServer_t* findServer(uid_t uid)
{
    list_t *s;
    list_for_each(s, &pmixServers) {
	PspmixServer_t *server = list_entry(s, PspmixServer_t, next);
	if (server->uid == uid
	    && server->timerId < 0 /* server not already in shutdown */
	    && server->fwdata != NULL /* server not already died */) {
	    return server;
	}
    }
    return NULL;
}

/**
 * @brief Find job by ID
 *
 * Ignores only invalid servers that have been already finalized by @a
 * stopServer() and are waiting for SIGCHILD handling in @a
 * serverTerminated_cb().
 *
 * @param jobID Job's unique ID (TID of the spawner creating the job)
 *
 * @return Returns the job or NULL if not in list
 */
static PspmixJob_t * findJob(PStask_ID_t jobID)
{
    list_t *s, *t, *j;
    list_for_each(s, &pmixServers) {
	PspmixServer_t *server = list_entry(s, PspmixServer_t, next);
	if (server->timerId >= 0 /* server already in shutdown */) continue;
	list_for_each(t, &server->sessions) {
	    PspmixSession_t *session = list_entry(t, PspmixSession_t, next);
	    list_for_each(j, &session->jobs) {
		PspmixJob_t *job = list_entry(j, PspmixJob_t, next);
		if (job->ID == jobID) return job;
	    }
	}
    }
    return NULL;
}

/**
 * @brief Find the task ID of the PMIx server for user
 *
 * Find PMIx server by user id @a uid
 *
 * @param uid user id
 *
 * @return Returns the TID or -1 if not in list
 */
PStask_ID_t pspmix_daemon_getServerTID(uid_t uid)
{
    PspmixServer_t* server = findServer(uid);
    if (!server) {
	flog("no PMIx server found (uid %d)\n", uid);
	return -1;
    }
    return server->fwdata->tid;
}

/**
 * @brief Find a PMIx job in a server object
 *
 * Finds the server by @a servertid and the PMIx job by its ID @a
 * jobID in its jobs list.
 *
 * @param servertid  TID of the PMIx server of the job
 * @param jobID Unique ID (TID of the spawner requested the PMIx job)
 *
 * @returns PMIx job or NULL of not found or on error
 */
static PspmixJob_t * findJobInServer(PStask_ID_t servertid, PStask_ID_t jobID)
{
    PspmixServer_t *server;
    bool found = false;
    list_t *s;
    list_for_each(s, &pmixServers) {
	server = list_entry(s, PspmixServer_t, next);
	if (server->fwdata && server->fwdata->tid == servertid
	    && server->timerId < 0) {
	    found = true;
	    break;
	}
    }

    if (!found) {
	flog("no PMIx server %s\n", PSC_printTID(servertid));
	return NULL;
    }

    list_for_each(s, &server->sessions) {
	PspmixSession_t *pmixsession = list_entry(s, PspmixSession_t, next);
	list_t *j;
	list_for_each(j, &pmixsession->jobs) {
	    PspmixJob_t *job = list_entry(j, PspmixJob_t, next);
	    if (job->ID == jobID) return job;
	}
    }

    flog("no matching PMIx job (ID %s", PSC_printTID(jobID));
    mlog(" server %s)\n", PSC_printTID(servertid));
    return NULL;
}


/* ****************************************************** *
 *                 Send/Receive functions                 *
 * ****************************************************** */

/**
 * @brief Extract the extra info field from the message fragment header
 *
 * @param msg   message fragment
 *
 * @return pointer to the extra field inside the header or NULL on error
 */
static PspmixMsgExtra_t* getExtra(DDTypedBufferMsg_t *msg)
{
    size_t used = 0, eS;
    PspmixMsgExtra_t *extra;
    if (!fetchFragHeader(msg, &used, NULL, NULL, (void **)&extra, &eS)
	|| eS != sizeof(*extra)) return NULL;

    return extra;
}

/**
 * @brief Set the target of the message to the TID of the right PMIx server.
 *
 * Identify the PMIx server serving the user referenced in @a extra
 * and set its TID as target for @a msg.
 *
 * @param extra  extra information containing uid
 * @param msg    message fragment to manipulate
 *
 * @return Returns true on success, i.e. when the destination could be
 * determined or false otherwise
 */
static bool setTargetToPmixServer(PspmixMsgExtra_t *extra,
				  DDTypedBufferMsg_t *msg)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

    PspmixServer_t *server = findServer(extra->uid);
    if (!server) {
	flog("UNEXPECTED: No PMIx server for uid %d found\n", extra->uid);
	return false;
    }

    if (!server->fwdata) {
	flog("fwdata is NULL, PMIx server seems dead (uid %d)\n", extra->uid);
	return false;
    }
    msg->header.dest = server->fwdata->tid;

    fdbg(PSPMIX_LOG_COMM, "setting destination %s\n",
	 PSC_printTID(msg->header.dest));

    return true;
}

/**
 * @brief Forward messages of type PSP_PLUG_PSPMIX in the main daemon
 *
 * This function is registered in the daemon and used for messages coming
 * from the client forwarder and from other deamons and thus from PMIx
 * servers running there.
 *
 * @param vmsg Pointer to message to handle
 *
 * @return Return true if message was forwarded or false otherwise
 */
static bool forwardPspmixMsg(DDBufferMsg_t *vmsg)
{
    fdbg(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM, "(type %s)\n",
	 PSDaemonP_printMsg(vmsg->header.type));

    DDTypedBufferMsg_t *msg = (DDTypedBufferMsg_t *)vmsg;

    if (mset(PSPMIX_LOG_COMM)) {
       flog("msg: type %s length %hu [%s", pspmix_getMsgTypeString(msg->type),
	    msg->header.len, PSC_printTID(msg->header.sender));
       mlog("->%s]\n", PSC_printTID(msg->header.dest));
    }

    /* local sender (types without extra) */
    if (PSC_getID(msg->header.sender) == PSC_getMyID()) {
	switch(msg->type) {
	case PSPMIX_ADD_JOB:
	case PSPMIX_REMOVE_JOB:
	    /* message types are only sent by this psid to local pmix servers */
	    if (msg->header.sender != PSC_getMyTID()) {
		flog("invalid sender (sender %s type %s)\n",
		     PSC_printTID(msg->header.sender),
		     pspmix_getMsgTypeString(msg->type));
		return false;
	    }
	    goto forward_msg;
	case PSPMIX_REGISTER_CLIENT:
	case PSPMIX_CLIENT_INIT_RES:
	case PSPMIX_CLIENT_FINALIZE_RES:
	case PSPMIX_CLIENT_SPAWN_RES:
	case PSPMIX_CLIENT_STATUS:
	    /* message from forwarder to local pmix server without extra
	       verify match of user IDs */
	case PSPMIX_SPAWNER_FAILED:
	    /* message from the spawner to inform about failed respawnes */

	    if (PSC_getID(msg->header.dest) != PSC_getMyID()) {
		flog("destination task is not local (dest %s type %s)\n",
		     PSC_printTID(msg->header.dest),
		     pspmix_getMsgTypeString(msg->type));
		return false;
	    }

	    PStask_t *sender = PStasklist_find(&managedTasks,
					       msg->header.sender);
	    if (!sender) {
		flog("sender task not found (sender %s type %s)\n",
		     PSC_printTID(msg->header.sender),
		     pspmix_getMsgTypeString(msg->type));
		return false;
	    }

	    PStask_t *dest = PStasklist_find(&managedTasks, msg->header.dest);
	    if (!dest) {
		flog("dest task not found (dest %s type %s)\n",
		     PSC_printTID(msg->header.dest),
		     pspmix_getMsgTypeString(msg->type));
		return false;
	    }

	    if (sender->uid != dest->uid) {
		flog("sender (%s) and", PSC_printTID(msg->header.sender));
		mlog(" dest (%s) uids mismatch (%d != %d)\n",
		     PSC_printTID(msg->header.dest), sender->uid, dest->uid);
		return false;
	    }
	    goto forward_msg;
	}
    }

    /* all other PSPMIX messages should have extra information set */
    PspmixMsgExtra_t *extra = getExtra(msg);
    if (!extra) {
	flog("sender (%s) does not provide extra in %s\n",
	     PSC_printTID(msg->header.sender),
	     pspmix_getMsgTypeString(msg->type));
	return false;
    }

    /* local sender (types with extra) */
    if (PSC_getID(msg->header.sender) == PSC_getMyID()) {
	PStask_t *sender = PStasklist_find(&managedTasks, msg->header.sender);
	if (!sender) {
	    flog("sender task not found (sender %s type %s)\n",
		 PSC_printTID(msg->header.sender),
		 pspmix_getMsgTypeString(msg->type));
	    return false;
	}

	if (sender->uid != extra->uid) {
	    flog("sender %s claims wrong uid (%u != %u)\n",
		 PSC_printTID(msg->header.sender), sender->uid, extra->uid);
	    return false;
	}
    }

    /* destination is remote, just forward */
    if (PSC_getID(msg->header.dest) != PSC_getMyID()) {
	fdbg(PSPMIX_LOG_COMM, "sending to remote %s\n",
	     PSC_printTID(msg->header.dest));
	sendMsg(vmsg);
	return true;
    }

    /* destination is local, we might have to tweak dest */
    switch(msg->type) {
    case PSPMIX_FENCE_DATA:
	if (PSC_getPID(msg->header.dest)) break; // destination already fixed
	__attribute__((fallthrough));
    case PSPMIX_FENCE_IN:
    case PSPMIX_MODEX_DATA_REQ:
    case PSPMIX_SPAWN_INFO:
    case PSPMIX_TERM_CLIENTS:
	if (!setTargetToPmixServer(extra, msg)) {
	    flog("setting target PMIx server failed (type %s), dropping\n",
		 pspmix_getMsgTypeString(msg->type));
	    return false;
	}
    }
    if (!PSC_getPID(msg->header.dest)) {
	flog("no dest (sender %s type %s)\n", PSC_printTID(msg->header.sender),
	     pspmix_getMsgTypeString(msg->type));
	return false;
    }

    /* check uid of destination task */
    PStask_t *dest = PStasklist_find(&managedTasks, msg->header.dest);
    if (!dest) {
	flog("dest task not found (dest %s type %s)\n",
	     PSC_printTID(msg->header.dest),
	     pspmix_getMsgTypeString(msg->type));
	return false;
    }

    if (dest->uid != extra->uid) {
	flog("dest (%s) and", PSC_printTID(msg->header.dest));
	mlog(" sender (%s) uid mismatch (%d != %d)\n",
	     PSC_printTID(msg->header.sender), dest->uid, extra->uid);
	return false;
    }

forward_msg:
    ;
    int ret = PSIDclient_send((DDMsg_t *)vmsg);
    if (ret < 0 && errno == EWOULDBLOCK) return true; /* message postponed */
    return (ret >= 0);
}

/**
 * @brief Forward messages of type PSP_PLUG_PSPMIX in the main daemon.
 *
 * This function is used to forward messages received from the local
 * PMIx server in the daemon.
 *
 * As a side effect, this function is setting both, the server's, the
 * session's and the job's @ref used flag as soon as the first
 * PSPMIX_CLIENT_INIT message is send by a PMIx server concerning a
 * job.
 *
 * @param msg    message received
 * @param fw     the plugin forwarder hosting the PMIx user server
 *
 * @return Return true if message was handled or false otherwise
 */
static bool forwardPspmixFwMsg(DDTypedBufferMsg_t *msg, ForwarderData_t *fw)
{
    fdbg(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM, "(type %s fw %s)\n",
	 PSDaemonP_printMsg(msg->header.type), PSC_printTID(fw->tid));

    switch(msg->header.type) {
	case PSP_PLUG_PSPMIX:
	    break;
	case PSP_CD_SIGNAL:
	    return PSID_handleMsg((DDBufferMsg_t *)msg);
	default:
	    flog("Received message of unhandled type %s\n",
		 PSDaemonP_printMsg(msg->header.type));
	    return false;
    }

    /* handle PSP_PLUG_PSPMIX */
    if (msg->type == PSPMIX_CLIENT_INIT) {
	PspmixMsgExtra_t *extra = getExtra(msg);
	if (!extra) {
	    flog("Cannot mark job as using PMIx\n");
	} else {
	    PspmixJob_t *job = findJobInServer(msg->header.sender,
					       extra->spawnertid);
	    if (job) {
		if (!job->used) {
		    flog("job starts using PMIx (uid %d %s)\n",
			 job->session->server->uid, pspmix_jobStr(job));
		}
		job->used = true;
		if (job->session) {
		    job->session->used = true;
		    if (job->session->server) job->session->server->used = true;
		}
	    }
	}
    }

    forwardPspmixMsg((DDBufferMsg_t *)msg);

    return true;
}

/**
 * Send message of type PSPMIX_ADD_JOB
 *
 * @param server     target server
 * @param sessID     PMIx session identifier (logger's tid)
 * @param jobID      PMIx job identifier (spawner's tid)
 * @param resInfos   reservation information list
 * @param env        job's environment
 *
 * @returns True on success or if job already known to server, false on error
 */
static bool sendAddJob(PspmixServer_t *server, PStask_ID_t sessID,
		       PStask_ID_t jobID, list_t *resInfos, env_t env)
{
    fdbg(PSPMIX_LOG_CALL, "(uid %d job %s)\n", server->uid,
	 PSC_printTID(jobID));

    if (!server->fwdata) {
	flog("server seems to be dead (uid %d)\n", server->uid);
	return false;
    }

    PStask_ID_t targetTID = server->fwdata->tid;

    PS_SendDB_t msg;
    initFragBuffer(&msg, PSP_PLUG_PSPMIX, PSPMIX_ADD_JOB);
    setFragDest(&msg, targetTID);

    addTaskIdToMsg(sessID, &msg);
    addTaskIdToMsg(jobID, &msg);

    list_t *r;
    uint32_t count = 0;
    list_for_each(r, resInfos) count++;

    addUint32ToMsg(count, &msg);

    list_for_each(r, resInfos) {
	PSresinfo_t *resInfo = list_entry(r, PSresinfo_t, next);
	addResIdToMsg(resInfo->resID, &msg);
	addTaskIdToMsg(resInfo->partHolder, &msg);
	addUint32ToMsg(resInfo->rankOffset, &msg);
	addInt32ToMsg(resInfo->minRank, &msg);
	addInt32ToMsg(resInfo->maxRank, &msg);
	addDataToMsg(resInfo->entries,
		     resInfo->nEntries * sizeof(*resInfo->entries), &msg);
	addDataToMsg(resInfo->localSlots,
		     resInfo->nLocalSlots * sizeof(*resInfo->localSlots), &msg);
    }

    addStringArrayToMsg(envGetArray(env), &msg);

    if (mset(PSPMIX_LOG_COMM)) {
	flog("sending PSPMIX_ADD_JOB to %s", PSC_printTID(targetTID));
	mlog(" (job %s)\n", PSC_printTID(jobID));
    }

    if (sendFragMsg(&msg) < 0) {
	flog("sendFragMsg(uid %d job %s", server->uid, PSC_printTID(jobID));
	mlog(" server %s) failed\n", PSC_printTID(targetTID));
	return false;
    }

    return true;
}

/**
 * Send message of type PSPMIX_REMOVE_JOB
 *
 * @todo To actually do this right, we need a new hook in
 * psidspawn.c:msg_RESRELEASED() to call this function.
 * This is not critical, since collecting orphaned reservations in the PMIx
 * server will do no harm beside the memory they use and the slightly increased
 * search time when walking though the list. So, perhaps it is enough
 * to cleanup them when the job they belong to ends.
 *
 * @param server   target server
 * @param jobID    PMIx job identifier (spawner's tid)
 *
 * @returns True on success or if job already known to server, false on error
 */
static bool sendRemoveJob(PspmixServer_t *server, PStask_ID_t jobID)
{
    fdbg(PSPMIX_LOG_CALL, "(uid %d job %s)\n", server->uid,
	 PSC_printTID(jobID));

    if (!server->fwdata) {
	flog("server seems to be dead (uid %d)\n", server->uid);
	return false;
    }

    PS_SendDB_t msg;
    initFragBuffer(&msg, PSP_PLUG_PSPMIX, PSPMIX_REMOVE_JOB);
    setFragDest(&msg, server->fwdata->tid);

    addInt32ToMsg(jobID, &msg);

    if (mset(PSPMIX_LOG_COMM)) {
	flog("sending PSPMIX_REMOVE_JOB to %s",
	     PSC_printTID(server->fwdata->tid));
	mlog(" (job %s)\n", PSC_printTID(jobID));
    }

    if (sendFragMsg(&msg) < 0) {
	flog("sending job remove message failed (uid %d job %s", server->uid,
	     PSC_printTID(jobID));
	mlog(" server %s)\n", PSC_printTID(server->fwdata->tid));
	return false;
    }

    return true;
}


/* ****************************************************** *
 *              hook and helper functions                 *
 * ****************************************************** */

/**
 * Add a PMIx job to a PMIx server
 *
 * - Creates a new PMIx session if no existing matches.
 * - Creates a new PMIx job, fails if one exists
 * - Notify the running server via a PSPMIX_ADD_JOB message.
 *
 * @param server    PMIx server to add the job to
 * @param psjob     job to add
 * @param env       environment for the job
 *
 * @returns True on success or if job already known to server, false on error
 */
static bool addJobToServer(PspmixServer_t *server, PSjob_t *psjob, env_t env)
{
    PStask_ID_t sessID = psjob->sessID;

    fdbg(PSPMIX_LOG_CALL, "(uid %d session ID %s)\n", server->uid,
	 PSC_printTID(sessID));

    PspmixSession_t *session = findSessionInList(sessID, &server->sessions);
    if (session) {
	fdbg(PSPMIX_LOG_VERBOSE, "session already exists (uid %d ID %s)\n",
	     server->uid, PSC_printTID(sessID));
    } else {
	session = ucalloc(sizeof(*session));
	if (!session) return false;

	session->ID = sessID;
	session->server = server;
	INIT_LIST_HEAD(&session->jobs);

	list_add_tail(&session->next, &server->sessions);

	fdbg(PSPMIX_LOG_VERBOSE, "new session created (uid %d ID %s)\n",
	     server->uid, PSC_printTID(sessID));
    }

    /* check if the user's server already knows this job */
    if (findJobInList(psjob->ID, &session->jobs)) {
	fdbg(PSPMIX_LOG_VERBOSE, "job already known (uid %d job %s)\n",
	     server->uid, PSC_printTID(psjob->ID));
	return true;
    }

    PspmixJob_t *job = ucalloc(sizeof(*job));
    if (!job) {
	if (list_empty(&session->jobs)) {
	    /* remove newly created session again */
	    list_del(&session->next);
	    ufree(session);
	}
	return false;
    }

    job->ID = psjob->ID;
    job->session = session;
    INIT_LIST_HEAD(&job->resInfos); /* init even if never used */

    list_add_tail(&job->next, &session->jobs);

    return sendAddJob(server, sessID, job->ID, &psjob->resInfos, env);
}

/**
 * Kill the PMIx server by signal when timer is up
 */
static void killServer(int timerId, void *data)
{
    PspmixServer_t *server = data;

    flog("sending SIGKILL to PMIx server (uid %d server %s)\n", server->uid,
	 PSC_printTID(server->fwdata->tid));

    pid_t pid = PSC_getPID(server->fwdata->tid);
    if (pid) {
	kill(pid, SIGKILL);
    } else {
	flog("Not killing myself!\n");
    }
}

/**
 * @brief Terminate the session associated with ID @a sessID
 */
static void terminateSession(PStask_ID_t sessID)
{
    fdbg(PSPMIX_LOG_CALL, "(ID %s)\n", PSC_printTID(sessID));

    PSID_sendSignal(sessID, getuid(), PSC_getMyTID(), -1 /* signal */,
		    false /* pervasive */, false /* answer */);
}

/**
 * @brief Function called when the PMIx server terminated
 *
 * This function distinguishes two situations:
 * - @a stopServer() had been called for the same server and has exited
 *   regularly as expected.
 * - the PMIx server terminated unexpected
 *
 * In any case, the server is removed from the list of servers and freed.
 */
static void serverTerminated_cb(int32_t exit_status, Forwarder_Data_t *fw)
{
    fdbg(PSPMIX_LOG_CALL, "(server %s status %d)\n", PSC_printTID(fw->tid),
	 exit_status);

    PspmixServer_t *server = fw->userData;
    if (!server) {
	flog("UNEXPECTED: invalid server reference (server %s status %d",
	     PSC_printTID(fw->tid), WEXITSTATUS(exit_status));
	if (WIFSIGNALED(exit_status)) {
	    mlog(" signal %d", WTERMSIG(exit_status));
	}
	mlog(")\n");
	return;
    }

    if (WEXITSTATUS(exit_status) || WIFSIGNALED(exit_status)) {
	flog("server terminated (uid %d status %d", server->uid,
	     WEXITSTATUS(exit_status));
	if (WIFSIGNALED(exit_status)) {
	    mlog(" signal %d", WTERMSIG(exit_status));
	}
	mlog(")\n");

	if (getConfValueI(config, "KILL_JOB_ON_SERVERFAIL")) {
	    list_t *s;
	    list_for_each(s, &server->sessions) {
		PspmixSession_t *session = list_entry(s, PspmixSession_t, next);
		flog("terminating session (ID %s) (KILL_JOB_ON_SERVERFAIL"
		     " set)\n", PSC_printTID(session->ID));
		if (session->used) {
		    terminateSession(session->ID);
		} else {
		    fdbg(PSPMIX_LOG_VERBOSE, "session not using server"
			 " (uid %d ID %s), not terminating\n",
			 server->uid, PSC_printTID(session->ID));
		}
	    }
	}
    }

    if (mset(PSPMIX_LOG_VERBOSE)) printServers();

    /* only unlists and frees the server if stopServer() has set timerId */
    if (server->timerId >= 0) {
	Timer_remove(server->timerId);
	pspmix_deleteServer(server, true);
	return;
    }

    /* if no timer is set, stopServer() has not been called yet and
     * deleting is done later there but eliminate reference to the fwdata */
    server->fwdata = NULL;
}

/**
 * Fake killSessions since this needs to be set in ForwarderData_t
 */
static int fakeKillSession(pid_t pid, int signal)
{
    fdbg(PSPMIX_LOG_CALL, "(pid %d signal %d)\n", pid, signal);

    /* since we do not have a child, we need nothing to do here */
    return 0;
}

/**
 * Cleanup memory in the server process as root before switching user
 *
 * This function does NOT run in the daemon but already in the plugin forwarder
 * that becomes the PMIx server. It is used to cleanup stuff available in the
 * daemon that should not be available in the memory of the PMIx server running
 * as user, mainly for security reasons. For example this are all information
 * about other user's jobs and processes.
 *
 * @param fwdata  the forwarders user data containing the server struct
 */
static int initialCleanup(Forwarder_Data_t *fwdata)
{
    PspmixServer_t *myserver = fwdata->userData;

    fdbg(PSPMIX_LOG_CALL, "\n");

    /* there has to be a server object */
    if (!myserver) {
	flog("FATAL: no server object\n");
	return -1;
    }

    /* delete all server information but our's */
    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &pmixServers) {
	PspmixServer_t *server = list_entry(s, PspmixServer_t, next);
	if (server->uid == myserver->uid) continue;
	pspmix_deleteServer(server, false);
    }

    /* in execForwarder() PSID_clearMem() is not called aggressively,
     * so we need to cleanup the tasks here */
    PSIDtask_clearMem();

    /* let pluginfowarder jail the server */
    char *user = PSC_userFromUID(myserver->uid);
    setenv("__PSJAIL_ADD_USER_TO_CGROUP", user, 1);
    ufree(user);

    return 0;
}

/*
 * @brief Start the PMIx server process for a user
 *
 * Start a pluginforwarder as PMIx server handling all processes of the user
 * on this node.
 */
static bool startServer(PspmixServer_t *server)
{
    Forwarder_Data_t *fwdata = ForwarderData_new();
    char fname[300];
    snprintf(fname, sizeof(fname), "pspmix-server:%d", server->uid);
    char id[16];
    snprintf(id, sizeof(id), "uid:%d", server->uid);


    fwdata->pTitle = ustrdup(fname);
    fwdata->jobID = ustrdup(id);
    fwdata->userData = server;
    fwdata->uID = server->uid;
    fwdata->gID = server->gid;
    fwdata->graceTime = 3;
//    fwdata->accounted = true;
    fwdata->killSession = fakeKillSession;
    fwdata->callback = serverTerminated_cb;
    fwdata->hookFWInit = initialCleanup;
    fwdata->hookFWInitUser = pspmix_userserver_initialize;
    fwdata->hookLoop = pspmix_userserver_prepareLoop;
    fwdata->hookFinalize = pspmix_userserver_finalize;
    fwdata->handleMthrMsg = pspmix_comm_handleMthrMsg;
    fwdata->handleFwMsg = forwardPspmixFwMsg;
    fwdata->jailChild = true;

    if (!startForwarder(fwdata)) {
	flog("starting PMIx server for user ID %d failed\n", server->uid);
	ForwarderData_delete(fwdata);
	return false;
    }

    server->fwdata = fwdata;

    return true;
}

static PspmixServer_t* findOrStartServer(uid_t uid, gid_t gid)
{
    /* is there already a PMIx server running for this user? */
    PspmixServer_t *server = findServer(uid);
    if (server) return server;

    /* No suitable server found, start one */
    server = ucalloc(sizeof(*server));
    server->uid = uid;
    server->gid = gid;
    server->timerId = -1;
    INIT_LIST_HEAD(&server->sessions);

    if (!startServer(server)) {
	flog("starting PMIx server failed (uid %d)\n", server->uid);
	return NULL;
    }

    list_add_tail(&server->next, &pmixServers);

    fdbg(PSPMIX_LOG_VERBOSE, "new PMIx server started (uid %d server %s)\n",
	 server->uid, PSC_printTID(server->fwdata->tid));

    return server;
}

/*
 * @brief Stop the PMIx server process
 *
 * Tries to shutdown the pluginforwarder working as PMIx server regularly.
 * Afterwards, start a timer to kill it in exceptional case.
 */
static void stopServer(PspmixServer_t *server)
{
    fdbg(PSPMIX_LOG_CALL, "(uid %d)\n", server->uid);

    if (!server->fwdata) {
	flog("no valid forwarder data, removing from list (uid %d)\n",
	     server->uid);
	pspmix_deleteServer(server, true);
	return;
    }

    /* setup timer to kill the server in case it will not go smoothly
     * this is also used to ensure the server will not be used again */
    if (server->timerId < 0) {
	int grace = getConfValueI(config, "SERVER_KILL_WAIT");
	struct timeval timeout = {grace, 0};
	server->timerId = Timer_registerEnhanced(&timeout, killServer, server);
    }
    if (server->timerId < 0) {
	flog("setting up kill timer failed (uid %d server %s)\n", server->uid,
	     PSC_printTID(server->fwdata->tid));
    }

    shutdownForwarder(server->fwdata);
    fdbg(PSPMIX_LOG_VERBOSE, "PMIx server stopped (uid %d)\n", server->uid);

    /* server object is freed in callback after SIGCHILD from PMIx server */
}

/**
 * @brief Hook function for PSIDHOOK_FILL_RESFINALIZED
 *
 * This hook is called on the spawner's node after fully receiving a
 * message of type PSP_DD_RESFINALIZED sent by the spawner once the
 * final reservation of a job was done.
 *
 * It can modify the environment that will be distributed to all
 * session nodes which will not run tasks of this job via
 * PSP_DD_JOBCOMPLETE message. On these nodes it is passed to
 * PSIDHOOK_JOBCOMPLETE. This hook is utilized to register the
 * corresponding PMIx job and create namespaces for jobs in the same
 * session as a local job but having tasks running on remote nodes
 * only.
 *
 * @param data Pointer to env to fill
 *
 * @return Returns 0 on success and -1 on error
 */
static int hookFillResFinalized(void *data)
{
    env_t env = data;

    if (!envInitialized(env)) {
	flog("UNEXPECTED: no initialized environment passed to hook\n");
	return -1;
    }

    if (!pspmix_common_usePMIx(env)) return 0; /* no PMIx support requested */

    char *stid = envGet(env, "SPAWNER_TID");
    if (!stid) return -1;

    char *endptr;
    PStask_ID_t spawnertid = strtol(stid, &endptr, 0);
    if (endptr == stid || *endptr) return -1;

    PStask_t *task = PStasklist_find(&managedTasks, spawnertid);
    if (!task) {
	flog("UNEXPECTED: spawner task %s not found\n",
	     PSC_printTID(spawnertid));
	return -1;
    }

    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%d", task->uid);
    envSet(env, "UID", tmp);

    snprintf(tmp, sizeof(tmp), "%d", task->gid);
    envSet(env, "GID", tmp);

    /* print all used variables for debugging */
    if (mset(PSPMIX_LOG_ENV)) {
	char *val = envGet(env, "PMIX_JOB_SIZE");
	flog("PMIX_JOB_SIZE = %s\n", val ? val : "(N/A)");

	val = envGet(env, "PMIX_JOB_NUM_APPS");
	flog("PMIX_JOB_NUM_APPS = %s\n", val ? val : "(N/A)");

	int apps = atol(val);
	for (int i = 0; i < apps; i++) {
	    char tmp[32];
	    snprintf(tmp, sizeof(tmp), "PMIX_APP_WDIR_%d", i);
	    val = envGet(env, tmp);
	    flog("%s = %s\n", tmp, val ? val : "(N/A)");

	    snprintf(tmp, sizeof(tmp), "PMIX_APP_ARGV_%d", i);
	    val = envGet(env, tmp);
	    flog("%s = %s\n", tmp, val ? val : "(N/A)");

	    snprintf(tmp, sizeof(tmp), "PMIX_APP_NAME_%d", i);
	    val = envGet(env, tmp);
	    flog("%s = %s\n", tmp, val ? val : "(N/A)");
	}

	val = envGet(env, "PMIX_SPAWNID");
	flog("PMIX_SPAWNID = %s\n", val ? val : "(N/A)");

	val = envGet(env, "__PMIX_SPAWN_PARENT_NSPACE");
	flog("PMIX_SPAWN_PARENT_NSPACE = %s\n", val ? val : "(N/A)");

	val = envGet(env, "__PMIX_SPAWN_PARENT_RANK");
	flog("PMIX_SPAWN_PARENT_RANK = %s\n", val ? val : "(N/A)");

	val = envGet(env, "__PMIX_SPAWN_PARENT_FWTID");
	flog("PMIX_SPAWN_PARENT_FWTID = %s\n", val ? val : "(N/A)");

	val = envGet(env, "__PMIX_SPAWN_OPTS");
	flog("PMIX_SPAWN_OPTS = %s\n", val ? val : "(N/A)");
    }

    return 0;
}

/**
 * @brief Hook function for PSIDHOOK_JOBCOMPLETE
 *
 * This hook is called when all reservation info of a job has been
 * received, i.e. once the job is completely known on the local node.
 *
 * It will only be called for remote jobs, i.e. jobs **not** having
 * tasks on the local node.
 *
 * @param data Pointer to job structure
 *
 * @return Returns 0 on success and -1 on error
 */
static int hookJobComplete(void *data)
{
    PSjob_t *psjob = data;

    if (!psjob) {
	flog("UNEXPECTED: no job passed to hook\n");
	return -1;
    }

    fdbg(PSPMIX_LOG_CALL, "job %s\n", PSC_printTID(psjob->ID));

    PspmixJob_t *job = findJob(psjob->ID);
    if (job) {
	flog("UNEXPECTED: job %s already known\n", PSC_printTID(psjob->ID));
	return -1;
    }

    /* Check whether this job has extraData set. This is only the case for
     * jobs with no local processes on this node, otherwise the information
     * comes with the SPAWN_TASK_REQUEST and job registration needs to be done
     * in PSIDHOOK_SPAWN_TASK a using the task environment. */
    if (!psjob->extraData) return 0;

    if (mset(PSPMIX_LOG_ENV)) {
	int cnt = 0;
	for (char **e = envGetArray(psjob->extraData); e && *e; e++, cnt++) {
	    flog("%d: %s\n", cnt, *e);
	}
    }

    /* continue only if PMIx support is requested
     * no singleton check needed here, singletons never have extraData set */
    if (!pspmix_common_usePMIx(psjob->extraData)) return 0;

    /* get information to start PMIx server */
    char *tmp;
    tmp = envGet(psjob->extraData, "UID");
    if (!tmp) {
	flog("UID not in extraData of job %s\n", PSC_printTID(psjob->ID));
	return -1;
    }

    char *endptr;
    uid_t uid = strtol(tmp, &endptr, 0);
    if (endptr == tmp || *endptr) {
	flog("invalid UID in extraData of job %s\n", PSC_printTID(psjob->ID));
	return -1;
    }

    tmp = envGet(psjob->extraData, "GID");
    if (!tmp) {
	flog("GID not in extraData of job %s\n", PSC_printTID(psjob->ID));
	return -1;
    }

    gid_t gid = strtol(tmp, &endptr, 0);
    if (endptr == tmp || *endptr) {
	flog("invalid UID in extraData of job %s\n", PSC_printTID(psjob->ID));
	return -1;
    }

    if (mset(PSPMIX_LOG_VERBOSE)) printServers();

    PspmixServer_t *server = findOrStartServer(uid, gid);
    if (!server) {
	flog("unable to get PMIx server for uid %d", uid);
	return -1;
    }

    /* nothing to do if job is already known to the user's server */
    PspmixSession_t *session = findSessionInList(psjob->sessID,
						 &server->sessions);
    if (session && findJobInList(psjob->ID, &session->jobs)) {
	flog("UNEXPECTED: job already known (uid %d ID %s)\n", server->uid,
	     PSC_printTID(psjob->ID));
	return 0;
    }

    /* save job in server (if not yet known) and notify running server */
    bool success = addJobToServer(server, psjob, psjob->extraData);
    if (success) return 0;

    flog("sending job failed (uid %d server %s", server->uid,
	 PSC_printTID(server->fwdata->tid));
    mlog(" session %s)\n", PSC_printTID(psjob->sessID));

    flog("stopping PMIx server (uid %d)\n", server->uid);
    stopServer(server);

    return -1;
}

/**
 * @brief Hook function for PSIDHOOK_SPAWN_TASK
 *
 * This hook is called right before the forwarder for a task is started
 *
 * Starts the user's PMIx server and/or notifies this PMIx server of the job.
 * This hook handles jobs having local tasks including the Singleton case.
 * Jobs without local tasks are handled by @a hookJobComplete().
 *
 * This function depends on all reservation information of the job being
 * received. This is guaranteed at the moment this hook is called.
 *
 * @param data Pointer to task structure to be spawned
 *
 * @return Returns 0 on success and -1 on error.
 */
static int hookSpawnTask(void *data)
{
    PStask_t *task = data;

    if (!task) {
	flog("UNEXPECTED: no task passed to hook\n");
	return -1;
    }

    /* leave all special task groups alone */
    if (task->group != TG_ANY) return 0;

    fdbg(PSPMIX_LOG_CALL, "(task group TG_ANY)\n");

    /* continue only for singleton case (see hookJobComplete for other cases)
     * which is if
     * - PMIx support is NOT requested
     * - AND singleton support is configured
     * - AND np == 1 (little below)*/
    bool usePMIx = pspmix_common_usePMIx(task->env);
    if (!usePMIx && !getConfValueI(config, "SUPPORT_MPI_SINGLETON")) return 0;

    /* find ParaStation session */
    PStask_ID_t sessID = task->loggertid;
    PSsession_t *pssession = PSID_findSessionByID(sessID);
    if (!pssession) {
	flog("no session (ID %s)\n", PSC_printTID(sessID));
	return -1;
    }

    /* find ParaStation job */
    PStask_ID_t jobID = task->spawnertid;
    PSjob_t *psjob = PSID_findJobInSession(pssession, jobID);
    if (!psjob) {
	flog("no job (ID %s", PSC_printTID(jobID));
	mlog(" session %s)\n", PSC_printTID(sessID));
	return -1;
    }
    if (psjob->sessID != sessID) {
	flog("UNEXPECTED: session ID from job and task do not match (%s",
	     PSC_printTID(psjob->sessID));
	mlog(" != %s)\n", PSC_printTID(sessID));
	return -1;
    }

    /* get the reservation information for this task */
    PSrsrvtn_ID_t resID = task->resID;
    PSresinfo_t *resInfo = findReservationInList(resID, &psjob->resInfos);
    if (!resInfo) {
	flog("no reservation %d (ID %s", resID, PSC_printTID(jobID));
	mlog(" session %s)\n", PSC_printTID(sessID));
	return -1;
    }

    /* count processes */
    uint32_t np = getResSize(resInfo);

    /* no singleton (see above) */
    if (!usePMIx && np != 1) return 0;

    if (mset(PSPMIX_LOG_VERBOSE)) printServers();

    PspmixServer_t *server = findOrStartServer(task->uid, task->gid);
    if (!server) {
	flog("unable to get PMIx server for uid %d", task->uid);
	return -1;
    }

    /* nothing to do if job is already known to the user's server */
    PspmixSession_t *session = findSessionInList(sessID, &server->sessions);
    if (session && findJobInList(jobID, &session->jobs)) {
	fdbg(PSPMIX_LOG_VERBOSE, "rank %d: job already known (uid %d ID %s)\n",
	     task->rank, server->uid, PSC_printTID(jobID));
	return 0;
    }

    /* fake environment for one process if MPI singleton support is enabled */
    env_t env = task->env;
    if (!usePMIx) {
	/* clone environment so we can modify it */
	env = envClone(task->env, NULL);
	if (!envInitialized(env)) {
	    flog("cloning env failed\n");
	    return -1;
	}
	envSet(env, "PMIX_JOB_SIZE", "1");
	envSet(env, "PMIX_JOB_NUM_APPS", "1");
	envSet(env, "PMIX_APP_SIZE_0", "1");
	envSet(env, "PMIX_APP_WDIR_0", task->workingdir);
	char **argv = task->argv;
	int argc = task->argc;
	size_t sum = 1;
	for (int j = 0; j < argc; j++) sum += strlen(argv[j]) + 1;
	char *str = umalloc(sum);
	char *ptr = str;
	for (int j = 0; j < argc; j++) ptr += sprintf(ptr, "%s ", argv[j]);
	*(ptr-1)='\0';
	envSet(env, "PMIX_APP_ARGV_0", str);
    }

    /* save job in server (if not yet known) and notify running server */
    bool success = addJobToServer(server, psjob, env);
    if (!usePMIx) envDestroy(env);
    if (success) return 0;

    flog("sending job failed (uid %d server %s", server->uid,
	 PSC_printTID(server->fwdata->tid));
    mlog(" session %s)\n", PSC_printTID(sessID));

    flog("stopping PMIx server (uid %d)\n", server->uid);
    stopServer(server);

    return -1;
}

/**
 * @brief Hook function for PSIDHOOK_LOCALJOBREMOVED
 *
 * This hook is called before a local job gets removed
 *
 * In this function we send a message to the PMIx server handling the job.
 * If this is the last job handled by that server, it will terminate. This is
 * marked in the local representation of the server.
 *
 * @param data Pointer to job structure to be removed.
 *
 * @return Returns 0 on success and -1 on error.
 */
static int hookLocalJobRemoved(void *data)
{
    PSjob_t *psjob = data;

    if (!psjob) {
	flog("UNEXPECTED: no job passed to hook\n");
	return -1;
    }

    fdbg(PSPMIX_LOG_CALL, "(job %s)\n", PSC_printTID(psjob->ID));

    if (mset(PSPMIX_LOG_VERBOSE)) printServers();

    PspmixJob_t *job = findJob(psjob->ID);
    if (!job) {
	flog("job %s not found in any PMIx server (This is fine for jobs not"
	     " configured to use PMIx.)\n", PSC_printTID(psjob->ID));
	return -1;
    }

    PspmixServer_t *server = job->session->server;

    /* send info about removed job to the PMIx server */
    if (!sendRemoveJob(server, job->ID)) {
	flog("sending job remove failed (uid %d %s)\n", server->uid,
	     pspmix_jobStr(job));
	//TODO @todo stop server if still alive ???
    }

    list_del(&job->next);

    /* remove session if this was the last job in it */
    if (list_empty(&job->session->jobs)) {
	fdbg(PSPMIX_LOG_VERBOSE, "removing session (uid %d ID %s)\n",
	     server->uid, PSC_printTID(job->session->ID));
	list_del(&job->session->next);

	/* stop server if this was the last session on it */
	if (list_empty(&server->sessions)) {
	    fdbg(PSPMIX_LOG_VERBOSE, "stopping server (uid %d server %s)\n",
		 server->uid,
		 server->fwdata ? PSC_printTID(server->fwdata->tid) : "(nil)");
	    stopServer(job->session->server);
	}

	ufree(job->session);
    }

    ufree(job);
    return 0;
}

/**
 * @brief Hook function for PSIDHOOK_NODE_DOWN
 *
 * This hook is called if a remote node disappeared
 *
 * @param nodeid ID of the disappeared node
 *
 * @return Returns 0 on success and -1 on error.
 */
static int hookNodeDown(void *data)
{
    PSnodes_ID_t *nodeid = data;

    fdbg(PSPMIX_LOG_CALL, "(nodeid %hd)\n", *nodeid);

    /** @todo
     * Is there any action needed here?
     * Isn't a job effected by the node down canceled anyhow and we get
     * the thing via hookLocalJobRemoved?
     */

    return 0;
}

void pspmix_initDaemonModule(void)
{
    PSIDhook_add(PSIDHOOK_FILL_RESFINALIZED, hookFillResFinalized);
    PSIDhook_add(PSIDHOOK_JOBCOMPLETE, hookJobComplete);
    PSIDhook_add(PSIDHOOK_SPAWN_TASK, hookSpawnTask);
    PSIDhook_add(PSIDHOOK_LOCALJOBREMOVED, hookLocalJobRemoved);
    PSIDhook_add(PSIDHOOK_NODE_DOWN, hookNodeDown);
    PSID_registerMsg(PSP_PLUG_PSPMIX, forwardPspmixMsg);
}

void pspmix_finalizeDaemonModule(void)
{
    PSIDhook_del(PSIDHOOK_FILL_RESFINALIZED, hookFillResFinalized);
    PSIDhook_del(PSIDHOOK_JOBCOMPLETE, hookJobComplete);
    PSIDhook_del(PSIDHOOK_SPAWN_TASK, hookSpawnTask);
    PSIDhook_del(PSIDHOOK_LOCALJOBREMOVED, hookLocalJobRemoved);
    PSIDhook_del(PSIDHOOK_NODE_DOWN, hookNodeDown);
    PSID_clearMsg(PSP_PLUG_PSPMIX, forwardPspmixMsg);
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
