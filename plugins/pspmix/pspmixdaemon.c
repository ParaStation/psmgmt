/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
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
#include "psenv.h"
#include "pspluginprotocol.h"
#include "psprotocol.h"
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
 * @brief Find job with given spawnertid
 *
 * Ignores only invalid servers that have been already finalized
 * by @a stopServer() and are waiting for SIGCHILD handling
 * in @a serverTerminated_cb().
 *
 * @param spawnertid  TID of the spawner creating the job (unique ID)
 *
 * @return Returns the job or NULL if not in list
 */
static PspmixJob_t * findJob(PStask_ID_t spawnertid)
{
    list_t *s, *t, *j;
    list_for_each(s, &pmixServers) {
	PspmixServer_t *server = list_entry(s, PspmixServer_t, next);
	if (server->timerId >= 0 /* server already in shutdown */) continue;
	list_for_each(t, &server->sessions) {
	    PspmixSession_t *session = list_entry(t, PspmixSession_t, next);
	    list_for_each(j, &session->jobs) {
		PspmixJob_t *job = list_entry(j, PspmixJob_t, next);
		if (job->spawnertid == spawnertid) return job;
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
	mlog("%s: no PMIx server found (uid %d)\n", __func__, uid);
	return -1;
    }
    return server->fwdata->tid;
}

/**
 * @brief Find a PMIx job in a server object
 *
 * Finds the server by @a servertid and the PMIx job by spawner @a spawnertid
 * in its jobs list.
 *
 * @param servertid  TID of the PMIx server of the job
 * @param spawnertid  TID of the spawner requested the PMIx job
 *
 * @returns PMIx job or NULL of not found or on error
 */
static PspmixJob_t * findJobInServer(PStask_ID_t servertid,
				     PStask_ID_t spawnertid)
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
	mlog("%s: no PMIx server %s\n", __func__, PSC_printTID(servertid));
	return NULL;
    }

    list_for_each(s, &server->sessions) {
	PspmixSession_t *pmixsession = list_entry(s, PspmixSession_t, next);
	list_t *j;
	list_for_each(j, &pmixsession->jobs) {
	    PspmixJob_t *job = list_entry(j, PspmixJob_t, next);
	    if (job->spawnertid == spawnertid) return job;
	}
    }

    mlog("%s: no matching PMIx job (spawner %s", __func__,
	 PSC_printTID(spawnertid));
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
	    || eS != sizeof(*extra)) {
	mlog("%s: UNEXPECTED: Fetching extra from header failed\n", __func__);
	return NULL;
    }
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
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    PspmixServer_t *server = findServer(extra->uid);
    if (!server) {
	mlog("%s: UNEXPECTED: No PMIx server for uid %d found\n", __func__,
	     extra->uid);
	return false;
    }

    if (!server->fwdata) {
	mlog("%s: fwdata is NULL, PMIx server seems dead (uid %d)\n",
		__func__, extra->uid);
	return false;
    }
    msg->header.dest = server->fwdata->tid;

    mdbg(PSPMIX_LOG_COMM, "%s: setting destination %s\n", __func__,
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
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    DDTypedBufferMsg_t *msg = (DDTypedBufferMsg_t *)vmsg;

    mdbg(PSPMIX_LOG_COMM, "%s: msg: type %s length %hu [%s", __func__,
	 pspmix_getMsgTypeString(msg->type), msg->header.len,
	 PSC_printTID(msg->header.sender));
    mdbg(PSPMIX_LOG_COMM, "->%s]\n", PSC_printTID(msg->header.dest));

    PspmixMsgExtra_t *extra = getExtra(msg);

    /* local sender */
    if (PSC_getID(msg->header.sender) == PSC_getMyID()) {
	switch(msg->type) {
	case PSPMIX_ADD_JOB:
	case PSPMIX_REMOVE_JOB:
	    /* message types are only sent by this psid to local pmix servers */
	    if (msg->header.sender != PSC_getMyTID()) {
		mlog("%s: invalid sender (sender %s type %s)\n", __func__,
		     PSC_printTID(msg->header.sender),
		     pspmix_getMsgTypeString(msg->type));
		return false;
	    }
	    goto forward_msg;
	case PSPMIX_REGISTER_CLIENT:
	case PSPMIX_CLIENT_INIT_RES:
	case PSPMIX_CLIENT_FINALIZE_RES:
	    /* message from forwarder to local pmix server without extra
	       verify match of user IDs */

	    if (PSC_getID(msg->header.dest) != PSC_getMyID()) {
		mlog("%s: destination task is not local (dest %s type %s)\n",
		     __func__, PSC_printTID(msg->header.dest),
		     pspmix_getMsgTypeString(msg->type));
		return false;
	    }

	    PStask_t *sender = PStasklist_find(&managedTasks,
					       msg->header.sender);
	    if (!sender) {
		mlog("%s: sender task not found (sender %s type %s)\n",
		     __func__, PSC_printTID(msg->header.sender),
		     pspmix_getMsgTypeString(msg->type));
		return false;
	    }

	    PStask_t *dest = PStasklist_find(&managedTasks, msg->header.dest);
	    if (!dest) {
		mlog("%s: dest task not found (dest %s type %s)\n",
		     __func__, PSC_printTID(msg->header.dest),
		     pspmix_getMsgTypeString(msg->type));
		return false;
	    }

	    if (sender->uid != dest->uid) {
		mlog("%s: sender (%s) and", __func__,
		     PSC_printTID(msg->header.sender));
		mlog(" dest (%s) uids mismatch (%d != %d)\n",
		     PSC_printTID(msg->header.dest), sender->uid, dest->uid);
		return false;
	    }
	    goto forward_msg;
	}

	/* all other PMIX messages should have extra information set */
	if (!extra) return false;

	PStask_t *sender = PStasklist_find(&managedTasks, msg->header.sender);
	if (!sender) {
	    mlog("%s: sender task not found (sender %s type %s)\n",
		 __func__, PSC_printTID(msg->header.sender),
		 pspmix_getMsgTypeString(msg->type));
	    return false;
	}

	if (sender->uid != extra->uid) {
	    mlog("%s: sender %s claims wrong uid (%u != %u)\n", __func__,
		 PSC_printTID(msg->header.sender), sender->uid, extra->uid);
	    return false;
	}
    }

    /* destination is remote, just forward */
    if (PSC_getID(msg->header.dest) != PSC_getMyID()) {
	mdbg(PSPMIX_LOG_COMM, "%s: sending to remote %s\n", __func__,
	     PSC_printTID(msg->header.dest));
	sendMsg(vmsg);
	return true;
    }

    /* destination is local, we might have to tweak dest */
    if (!extra) return false;

    switch(msg->type) {
    case PSPMIX_FENCE_DATA:
	if (PSC_getPID(msg->header.dest)) break; // destination already fixed
	__attribute__((fallthrough));
    case PSPMIX_FENCE_IN:
    case PSPMIX_MODEX_DATA_REQ:
	if (!setTargetToPmixServer(extra, msg)) {
	    mlog("%s: setting target PMIx server failed (type %s), dropping\n",
		 __func__, pspmix_getMsgTypeString(msg->type));
	    return false;
	}
    }
    if (!PSC_getPID(msg->header.dest)) {
	mlog("%s: no dest (sender %s type %s)\n", __func__,
	     PSC_printTID(msg->header.sender),
	     pspmix_getMsgTypeString(msg->type));
	return false;
    }

    /* check uid of destination task */
    PStask_t *dest = PStasklist_find(&managedTasks, msg->header.dest);
    if (!dest) {
	mlog("%s: dest task not found (dest %s type %s)\n", __func__,
	     PSC_printTID(msg->header.dest), pspmix_getMsgTypeString(msg->type));
	return false;
    }

    if (dest->uid != extra->uid) {
	mlog("%s: dest (%s) and", __func__, PSC_printTID(msg->header.dest));
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
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    if (msg->header.type != PSP_PLUG_PSPMIX) return false;
    if (msg->type == PSPMIX_CLIENT_INIT) {
	PspmixMsgExtra_t *extra = getExtra(msg);
	if (!extra) {
	    mlog("%s: Cannot mark job as using PMIx\n", __func__);
	} else {
	    PspmixJob_t *job = findJobInServer(msg->header.sender,
					       extra->spawnertid);
	    if (job) {
		if (!job->used) {
		    mlog("%s: job starts using PMIx (uid %d %s)\n", __func__,
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
 * @param spawnertid PMIx job identifier
 * @param resInfos   reservation information list
 * @param env        job's environment
 *
 * @returns True on success or if job already known to server, false on error
 */
static bool sendAddJob(PspmixServer_t *server, PStask_ID_t loggertid,
		       PStask_ID_t spawnertid, list_t *resInfos, env_t *env)
{
    mdbg(PSPMIX_LOG_CALL, "%s(uid %d spawner %s)\n", __func__,
	 server->uid, PSC_printTID(spawnertid));

    if (!server->fwdata) {
	mlog("%s: server seems to be dead (uid %d)\n", __func__, server->uid);
	return false;
    }

    PStask_ID_t targetTID = server->fwdata->tid;

    PS_SendDB_t msg;
    initFragBuffer(&msg, PSP_PLUG_PSPMIX, PSPMIX_ADD_JOB);
    setFragDest(&msg, targetTID);

    addTaskIdToMsg(loggertid, &msg);
    addTaskIdToMsg(spawnertid, &msg);

    list_t *r;
    uint32_t count = 0;
    list_for_each(r, resInfos) count++;

    addUint32ToMsg(count, &msg);

    list_for_each(r, resInfos) {
	PSresinfo_t *resInfo = list_entry(r, PSresinfo_t, next);
	addResIdToMsg(resInfo->resID, &msg);
	addDataToMsg(resInfo->entries,
		     resInfo->nEntries * sizeof(*resInfo->entries), &msg);
	addDataToMsg(resInfo->localSlots,
		     resInfo->nLocalSlots * sizeof(*resInfo->localSlots), &msg);
    }

    addStringArrayToMsg(env->vars, &msg);

    mdbg(PSPMIX_LOG_COMM, "%s: sending PSPMIX_ADD_JOB to %s", __func__,
	    PSC_printTID(targetTID));
    mdbg(PSPMIX_LOG_COMM, " (spawner %s)\n", PSC_printTID(spawnertid));

    if (sendFragMsg(&msg) < 0) {
	mlog("%s: sendFragMsg(uid %d spawner %s", __func__,
	     server->uid, PSC_printTID(spawnertid));
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
 * @param server     target server
 * @param spawnertid PMIx job identifier
 *
 * @returns True on success or if job already known to server, false on error
 */
static bool sendRemoveJob(PspmixServer_t *server, PStask_ID_t spawnertid)
{
    mdbg(PSPMIX_LOG_CALL, "%s(uid %d spawnertid %s)\n", __func__,
	 server->uid, PSC_printTID(spawnertid));

    if (!server->fwdata) {
	mlog("%s: server seems to be dead (uid %d)\n", __func__, server->uid);
	return false;
    }

    PS_SendDB_t msg;
    initFragBuffer(&msg, PSP_PLUG_PSPMIX, PSPMIX_REMOVE_JOB);
    setFragDest(&msg, server->fwdata->tid);

    addInt32ToMsg(spawnertid, &msg);

    mdbg(PSPMIX_LOG_COMM, "%s: sending PSPMIX_REMOVE_JOB to %s", __func__,
	    PSC_printTID(server->fwdata->tid));
    mdbg(PSPMIX_LOG_COMM, " (spawner %s)\n", PSC_printTID(spawnertid));

    if (sendFragMsg(&msg) < 0) {
	mlog("%s: sending job remove message failed (uid %d spawner %s",
	       __func__, server->uid, PSC_printTID(spawnertid));
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
 * @param loggertid PMIx session identifier
 * @param psjob     job to add
 * @param environ   environment for the job
 * @param envSize   size of the environment
 *
 * @returns True on success or if job already known to server, false on error
 */
static bool addJobToServer(PspmixServer_t *server, PStask_ID_t loggertid,
			   PSjob_t *psjob, env_t *env)
{
    mdbg(PSPMIX_LOG_CALL, "%s(uid %d loggertid %s)\n", __func__,
	 server->uid, PSC_printTID(loggertid));

    PspmixSession_t *session = findSessionInList(loggertid, &server->sessions);
    if (session) {
	mdbg(PSPMIX_LOG_VERBOSE, "%s: session already exists (uid %d loggertid"
	     " %s)\n", __func__, server->uid, PSC_printTID(loggertid));
    } else {
	session = ucalloc(sizeof(*session));
	if (!session) return false;

	session->loggertid = loggertid;
	session->server = server;
	INIT_LIST_HEAD(&session->jobs);

	list_add_tail(&session->next, &server->sessions);

	mdbg(PSPMIX_LOG_VERBOSE, "%s: new session created (uid %d loggertid"
	     " %s)\n", __func__, server->uid, PSC_printTID(loggertid));
    }

    /* check if the user's server already knows this job */
    if (findJobInList(psjob->spawnertid, &session->jobs)) {
	mdbg(PSPMIX_LOG_VERBOSE, "%s: job already known (uid %d spawner %s)\n",
	     __func__, server->uid, PSC_printTID(psjob->spawnertid));
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

    job->spawnertid = psjob->spawnertid;
    job->session = session;
    INIT_LIST_HEAD(&job->resInfos); /* init even if never used */

    list_add_tail(&job->next, &session->jobs);

    return sendAddJob(server, loggertid, job->spawnertid, &psjob->resInfos, env);
}

/**
 * Kill the PMIx server by signal when timer is up
 */
static void killServer(int timerId, void *data)
{
    PspmixServer_t *server = data;

    mlog("%s: sending SIGKILL to PMIx server (uid %d server %s)\n", __func__,
	 server->uid, PSC_printTID(server->fwdata->tid));

    pid_t pid = PSC_getPID(server->fwdata->tid);
    if (pid) {
	kill(pid, SIGKILL);
    } else {
	mlog("%s: Not killing myself!\n", __func__);
    }
}

/**
 * @brief Terminate the session associated with @a loggertid
 */
static void terminateSession(PStask_ID_t loggertid)
{
    mdbg(PSPMIX_LOG_CALL, "%s(logger %s)\n", __func__, PSC_printTID(loggertid));

    PSID_sendSignal(loggertid, getuid(), PSC_getMyTID(), -1 /* signal */,
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
    mdbg(PSPMIX_LOG_CALL, "%s(server %s status %d)\n", __func__,
	    PSC_printTID(fw->tid), exit_status);

    PspmixServer_t *server = fw->userData;
    if (!server) {
	mlog("%s: UNEXPECTED: invalid server reference (server %s status %d",
	     __func__, PSC_printTID(fw->tid), WEXITSTATUS(exit_status));
	if (WIFSIGNALED(exit_status)) {
	    mlog(" signal %d", WTERMSIG(exit_status));
	}
	mlog(")\n");
	return;
    }

    if (WEXITSTATUS(exit_status) || WIFSIGNALED(exit_status)) {
	mlog("%s: server terminated (uid %d status %d",
		__func__, server->uid, WEXITSTATUS(exit_status));
	if (WIFSIGNALED(exit_status)) {
	    mlog(" signal %d", WTERMSIG(exit_status));
	}
	mlog(")\n");

	if (getConfValueI(&config, "KILL_JOB_ON_SERVERFAIL")) {
	    list_t *s;
	    list_for_each(s, &server->sessions) {
		PspmixSession_t *session = list_entry(s, PspmixSession_t, next);
		mlog("%s: terminating session (logger %s)"
			" (KILL_JOB_ON_SERVERFAIL set)\n", __func__,
			PSC_printTID(session->loggertid));
		if (session->used) {
		    terminateSession(session->loggertid);
		} else {
		    mdbg(PSPMIX_LOG_VERBOSE, "%s: session not using server"
			 " (uid %d logger %s), not terminating\n", __func__,
			 server->uid, PSC_printTID(session->loggertid));
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
    mdbg(PSPMIX_LOG_CALL, "%s(pid %d signal %d)\n", __func__, pid, signal);

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
int initialCleanup(Forwarder_Data_t *fwdata)
{
    PspmixServer_t *myserver = fwdata->userData;

    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    /* there has to be a server object */
    if (!myserver) {
	mlog("%s: FATAL: no server object\n", __func__);
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

    if (!startForwarder(fwdata)) {
	mlog("%s: starting PMIx server for user ID %d failed\n", __func__,
		server->uid);
	return false;
    }

    server->fwdata = fwdata;

    return true;
}

/*
 * @brief Stop the PMIx server process
 *
 * Tries to shutdown the pluginforwarder working as PMIx server regularly.
 * Afterwards, start a timer to kill it in exceptional case.
 */
static void stopServer(PspmixServer_t *server)
{
    mdbg(PSPMIX_LOG_CALL, "%s(uid %d)\n", __func__, server->uid);

    if (!server->fwdata) {
	mlog("%s: no valid forwarder data, removing from list (uid %d)\n",
	     __func__, server->uid);
	pspmix_deleteServer(server, true);
	return;
    }

    /* setup timer to kill the server in case it will not go smoothly
     * this is also used to ensure the server will not be used again */
    if (server->timerId < 0) {
	int grace = getConfValueI(&config, "SERVER_KILL_WAIT");
	struct timeval timeout = {grace, 0};
	server->timerId = Timer_registerEnhanced(&timeout, killServer, server);
    }
    if (server->timerId < 0) {
	mlog("%s: setting up kill timer failed (uid %d server %s)\n",
	     __func__, server->uid, PSC_printTID(server->fwdata->tid));
    }

    shutdownForwarder(server->fwdata);
    mdbg(PSPMIX_LOG_VERBOSE, "%s: PMIx server stopped (uid %d)\n", __func__,
	 server->uid);

    /* server object is freed in callback after SIGCHILD from PMIx server */
}

/**
 * @brief Hook function for PSIDHOOK_RECV_SPAWNREQ
 *
 * This hook is called after receiving a spawn request message
 *
 * Marks in the prototask's environment whether or not PMIx usage is
 * requested for this spawn request by mpiexec.
 *
 * Attention: The task passed is a prototype containing only the values
 * decoded in @a PStask_decodeTask and in addition spawnerid, workingdir,
 * argv, argc, environ, and envSize.
 *
 * @param data Pointer to task structure to be spawned
 *
 * @return Returns 0 on success and -1 on error.
 */
static int hookRecvSpawnReq(void *data)
{
    PStask_t *prototask = data;

    /* leave all special task groups alone */
    if (prototask->group != TG_ANY) return 0;

    mdbg(PSPMIX_LOG_CALL, "%s(task group TG_ANY)\n", __func__);

    env_t env = { prototask->environ, prototask->envSize, prototask->envSize };

    /* mark environment if mpiexec demands PMIx for this job */
    if (envGet(&env, "__PMIX_NODELIST")) {
	envPut(&env, strdup("__USE_PMIX=1")); /* for pspmix_common_usePMIx() */
    }

    prototask->environ = env.vars;
    prototask->envSize = env.cnt;

    return 0;
}

/**
 * @brief Hook function for PSIDHOOK_SPAWN_TASK
 *
 * This hook is called right before the forwarder for a task is started
 *
 * Starts the user's PMIx server and/or notifies this PMIx server of the job.
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

    /* leave all special task groups alone */
    if (task->group != TG_ANY) return 0;

    mdbg(PSPMIX_LOG_CALL, "%s(task group TG_ANY)\n", __func__);

    env_t env = { task->environ, task->envSize, task->envSize };

    /* continue only if PMIx support is requested
     * or singleton support is configured and np == 1 */
    bool usePMIx = pspmix_common_usePMIx(&env);
    if (!usePMIx && !getConfValueI(&config, "SUPPORT_MPI_SINGLETON")) return 0;
    char *jobsize = envGet(&env, "PMI_SIZE");
    if (!usePMIx && (jobsize ? atoi(jobsize) : 1) != 1) return 0;

    /* find ParaStation session */
    PStask_ID_t loggertid = task->loggertid;
    PSsession_t *pssession = PSID_findSessionByLoggerTID(loggertid);
    if (!pssession) {
	mlog("%s: no session (logger %s)\n", __func__, PSC_printTID(loggertid));
	return -1;
    }

    /* find ParaStation job */
    PStask_ID_t spawnertid = task->spawnertid;
    PSjob_t *psjob = PSID_findJobInSession(pssession, spawnertid);
    if (!psjob) {
	mlog("%s: no job (spawner %s", __func__, PSC_printTID(spawnertid));
	mlog(" logger %s)\n", PSC_printTID(loggertid));
	return -1;
    }

    /* get the reservation information for this task */
    PSrsrvtn_ID_t resID = task->resID;
    PSresinfo_t *resInfo = findReservationInList(resID, &psjob->resInfos);
    if (!resInfo) {
	mlog("%s: no reservation %d (spawner %s", __func__, resID,
	     PSC_printTID(spawnertid));
	mlog(" logger %s)\n", PSC_printTID(loggertid));
	return -1;
    }

    if (mset(PSPMIX_LOG_VERBOSE)) printServers();

    /* is there already a PMIx server running for this user? */
    PspmixServer_t *server = findServer(task->uid);
    if (server) {
	/* nothing to do if job is already known to the user's server */
	PspmixSession_t *session = findSessionInList(loggertid,
						     &server->sessions);
	if (session && findJobInList(spawnertid, &session->jobs)) {
	    mdbg(PSPMIX_LOG_VERBOSE, "%s: rank %d: job already known (uid %d"
		 " spawner %s)\n", __func__, task->rank, server->uid,
		 PSC_printTID(spawnertid));
	    return 0;
	}
    } else {

	/* No suitable server found, start one */
	server = ucalloc(sizeof(*server));
	server->uid = task->uid;
	server->gid = task->gid;
	server->timerId = -1;
	INIT_LIST_HEAD(&server->sessions);

	if (!startServer(server)) {
	    mlog("%s: starting PMIx server failed (uid %d)\n", __func__,
		 server->uid);
	    return -1;
	}

	list_add_tail(&server->next, &pmixServers);

	mdbg(PSPMIX_LOG_VERBOSE, "%s: new PMIx server started (uid %d server "
	     "%s)\n", __func__, server->uid, PSC_printTID(server->fwdata->tid));
    }

    /* clone environment so we can modify it in singleton case */
    env_t jobenv;
    if (!envClone(&env, &jobenv, NULL)) {
	mlog("%s: cloning env failed\n", __func__);
	return -1;
    }

    /* fake environment for one process if MPI singleton support is enabled */
    if (!usePMIx) {
	envSet(&jobenv, "PMI_UNIVERSE_SIZE", "1");
	envSet(&jobenv, "PMI_SIZE", "1");
	envSet(&jobenv, "PMIX_APPCOUNT", "1");
	envSet(&jobenv, "PMIX_APPSIZE_0", "1");
	envSet(&jobenv, "PMIX_APPWDIR_0", task->workingdir);
	char **argv = task->argv;
	int argc = task->argc;
	size_t sum = 1;
	for (int j = 0; j < argc; j++) sum += strlen(argv[j]) + 1;
	char *str = umalloc(sum);
	char *ptr = str;
	for (int j = 0; j < argc; j++) ptr += sprintf(ptr, "%s ", argv[j]);
	*(ptr-1)='\0';
	envSet(&jobenv, "PMIX_APPARGV_0", str);
	char var[HOST_NAME_MAX + 1];
	gethostname(var, sizeof(var));
	envSet(&jobenv, "__PMIX_NODELIST", var);
	snprintf(var, sizeof(var), "%d", resID);
	envSet(&jobenv, "__PMIX_RESID_0", var);
    }

    /* save job in server (if not yet known) and notify running server */
    if (!addJobToServer(server, loggertid, psjob, &jobenv)) {
	mlog("%s: sending job failed (uid %d server %s", __func__, server->uid,
	     PSC_printTID(server->fwdata->tid));
	mlog(" logger %s)\n", PSC_printTID(loggertid));

	mlog("%s: stopping PMIx server (uid %d)\n", __func__, server->uid);
	stopServer(server);

	envDestroy(&jobenv);
	return -1;
    }

    envDestroy(&jobenv);
    return 0;
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

    mdbg(PSPMIX_LOG_CALL, "%s(spawner %s)\n", __func__,
	 PSC_printTID(psjob->spawnertid));

    if (mset(PSPMIX_LOG_VERBOSE)) printServers();

    PspmixJob_t *job = findJob(psjob->spawnertid);
    if (!job) {
	mlog("%s: job not found in any PMIx server (spawner %s)"
	     " (This is fine for jobs not configured to use PMIx.)\n",
	     __func__, PSC_printTID(psjob->spawnertid));
	return -1;
    }

    PspmixServer_t *server = job->session->server;

    /* send info about removed job to the PMIx server */
    if (!sendRemoveJob(server, job->spawnertid)) {
	mlog("%s: sending job remove failed (uid %d %s)\n", __func__,
	     server->uid, pspmix_jobStr(job));
	//TODO @todo stop server if still alive ???
    }

    list_del(&job->next);

    /* remove session if this was the last job in it */
    if (list_empty(&job->session->jobs)) {
	mdbg(PSPMIX_LOG_VERBOSE, "%s: removing session (uid %d logger %s)\n",
	     __func__, server->uid, PSC_printTID(job->session->loggertid));
	list_del(&job->session->next);

	/* stop server if this was the last session on it */
	if (list_empty(&server->sessions)) {
	    mdbg(PSPMIX_LOG_VERBOSE, "%s: stopping server (uid %d server %s)\n",
		 __func__, server->uid,
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

    mdbg(PSPMIX_LOG_CALL, "%s(nodeid %hd)\n", __func__, *nodeid);

    /** @todo
     * Is there any action needed here?
     * Isn't a job effected by the node down canceled anyhow and we get
     * the thing via hookLocalJobRemoved?
     */

    return 0;
}

void pspmix_initDaemonModule(void)
{
    PSIDhook_add(PSIDHOOK_RECV_SPAWNREQ, hookRecvSpawnReq);
    PSIDhook_add(PSIDHOOK_SPAWN_TASK, hookSpawnTask);
    PSIDhook_add(PSIDHOOK_LOCALJOBREMOVED, hookLocalJobRemoved);
    PSIDhook_add(PSIDHOOK_NODE_DOWN, hookNodeDown);
    PSID_registerMsg(PSP_PLUG_PSPMIX, forwardPspmixMsg);
}

void pspmix_finalizeDaemonModule(void)
{
    PSIDhook_del(PSIDHOOK_RECV_SPAWNREQ, hookRecvSpawnReq);
    PSIDhook_del(PSIDHOOK_SPAWN_TASK, hookSpawnTask);
    PSIDhook_del(PSIDHOOK_LOCALJOBREMOVED, hookLocalJobRemoved);
    PSIDhook_del(PSIDHOOK_NODE_DOWN, hookNodeDown);
    PSID_clearMsg(PSP_PLUG_PSPMIX, forwardPspmixMsg);
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
