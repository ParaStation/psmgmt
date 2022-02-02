/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Implementation of pspmix functions running in the daemon
 *
 * Two jobs are done inside the daemon:
 * 1. Start the PMIx jobserver as plugin forwarder
 * 2. Forward plugin messages
 */
#include "pspmixdaemon.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "list.h"
#include "pscommon.h"
#include "pslog.h"
#include "pspluginprotocol.h"
#include "psprotocol.h"
#include "psserial.h"

#include "pluginforwarder.h"
#include "pluginmalloc.h"

#include "psidclient.h"
#include "psidcomm.h"
#include "psidhook.h"
#include "psidspawn.h"

#include "pspmixtypes.h"
#include "pspmixlog.h"
#include "pspmixcommon.h"
#include "pspmixjobserver.h"
#include "pspmixcomm.h"

/** The list of running PMIx jobservers on this node */
static LIST_HEAD(pmixJobservers);

/**
 * @brief Find local PMIx jobserver for job with logger tid
 *
 * @param loggertid tid of the logger
 *
 * @return Returns the job or NULL if not in list
 */
static PspmixJobserver_t* findJobserver(PStask_ID_t loggertid)
{
    list_t *j;
    list_for_each(j, &pmixJobservers) {
	PspmixJobserver_t *jobserver = list_entry(j, PspmixJobserver_t, next);
	if (jobserver->loggertid == loggertid) return jobserver;
    }
    return NULL;
}

/* ****************************************************** *
 *                 Send/Receive functions                 *
 * ****************************************************** */

/*
 * @brief Set the target of the message to the TID of the right jobserver.
 *
 * This function looks into the message fragment header, reads the logger TID
 * from there and find the right jobserver for the job. It's TID is then set
 * as target for @a msg.
 *
 * @param msg    message fragment to manipulate
 *
 * @return Returns true on success, i.e. when the destination could be
 * determined or false otherwise
 */
static bool setTargetToPmixJobserver(DDTypedBufferMsg_t *msg)
{
    size_t used = 0, eS;
    PStask_ID_t *loggerTID;
    if (!fetchFragHeader(msg, &used, NULL, NULL, (void **)&loggerTID, &eS)
	|| eS != sizeof(*loggerTID)) {
	mlog("%s: UNEXPECTED: Fetching header information failed\n", __func__);
	return false;
    }

    PspmixJobserver_t *server = findJobserver(*loggerTID);
    if (!server) {
	mlog("%s: UNEXPECTED: No PMIx jobserver found.\n", __func__);
	return false;
    }

    if (!server->fwdata) {
	mlog("%s: fwdata is NULL, PMIx jobserver seems to be dead\n", __func__);
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
 * jobservers running there.
 *
 * @param vmsg Pointer to message to handle
 *
 * @return Return true if message was forwarded or false otherwise
 */
static bool forwardPspmixMsg(DDBufferMsg_t *vmsg)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    DDTypedBufferMsg_t *msg = (DDTypedBufferMsg_t *)vmsg;

    mdbg(PSPMIX_LOG_COMM, "%s: msg: type %s (%i) length %hu [%s",
	    __func__, pspmix_getMsgTypeString(msg->type), msg->type,
	    msg->header.len, PSC_printTID(msg->header.sender));
    mdbg(PSPMIX_LOG_COMM, "->%s]\n", PSC_printTID(msg->header.dest));

    /* destination is remote, just forward */
    if (PSC_getID(msg->header.dest) != PSC_getMyID()) {
	sendMsg(vmsg);
	return true;
    }

    /* destination is local, we might have to tweak dest */
    switch(msg->type) {
    case PSPMIX_FENCE_IN:
    case PSPMIX_MODEX_DATA_REQ:
	if (!setTargetToPmixJobserver(msg)) {
	    mlog("%s: Could not set PMIx server as target for"
		    " PSPMIX_MODEX_DATA_REQ message, dropping\n",
		    __func__);
	    return false;
	}
    }
    if (!PSC_getPID(msg->header.dest)) {
	mlog("%s: no dest (sender %s type  %s)\n", __func__,
	     PSC_printTID(msg->header.sender),
	     pspmix_getMsgTypeString(msg->type));
	return false;
    }

    return PSIDclient_send((DDMsg_t *)vmsg) >= 0;
}

/**
 * @brief Forward messages of type PSP_PLUG_PSPMIX in the main daemon.
 *
 * This function is used to forward messages coming from the local PMIx
 * jobserver in the daemon.
 *
 * @param vmsg   message received
 * @param fw     the plugin forwarder hosting the PMIx jobserver
 *
 * @return Returns 1 if the type is known, 0 if not
 */
static int forwardPspmixFwMsg(PSLog_Msg_t *tmpmsg, ForwarderData_t *fw)
{

    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    DDBufferMsg_t *vmsg = (DDBufferMsg_t *)tmpmsg; /* HACK */

    if (vmsg->header.type != PSP_PLUG_PSPMIX) return 0;

    forwardPspmixMsg(vmsg);

    return 1;
}

/* ****************************************************** *
 *              hook and helper functions                 *
 * ****************************************************** */

/**
 * Kill the PMIx jobserver
 */
static int killJobserver(pid_t session, int sig)
{
    mlog("%s() called\n", __func__);

    //TODO  send signal to the correct jobserver

    return 0;
}

/**
 * Function called when the PMIx jobserver terminated
 */
static void jobserverTerminated_cb(int32_t exit_status, Forwarder_Data_t *fw)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called with forwarder %s status %d\n", __func__,
	    PSC_printTID(fw->tid), exit_status);

    /* regularly this function has nothing to do */
    if (exit_status == 0 && fw->userData == NULL) return;

    PspmixJobserver_t *server = fw->userData;

    if (server != NULL) {
	/* the jobserver forwarder died unintentionally, eliminate reference to
	 * the fwdata in the corresponding server object */
	server->fwdata = NULL;

	mlog("%s: PMIx jobserver for job with loggertid %s terminated with"
		" status %d\n", __func__, PSC_printTID(server->loggertid),
		exit_status);
    }
    else {
	mlog("%s: UNEXPECTED: PMIx jobserver with tid %s and invalid server"
		" reference terminated with status %d\n", __func__,
		PSC_printTID(fw->tid), exit_status);
    }

    fw->userData = NULL;

    /* The server is removed from PMIx jobservers list later by stopJobserver()
     * which will be called in hookLocalJobRemoved() after the psid detected
     * that the job is canceled */
}

/*
 * @brief Start the PMIx server process for a job
 *
 * Start a pluginforwarder as PMIx jobserver handling all processes of the job
 * on this node.
 */
static bool startJobserver(PspmixJobserver_t *server)
{
    Forwarder_Data_t *fwdata;
    char *jobid;
    char fname[300];

    fwdata = ForwarderData_new();

    jobid = PSC_printTID(server->loggertid);
    snprintf(fname, sizeof(fname), "pspmix-server:%s", jobid);

    fwdata->pTitle = ustrdup(fname);
    fwdata->jobID = ustrdup(jobid);
    fwdata->userData = server;
    fwdata->uID = server->prototask->uid;
    fwdata->gID = server->prototask->gid;
    fwdata->graceTime = 3;
//    fwdata->accounted = true;
    fwdata->killSession = killJobserver;
    fwdata->callback = jobserverTerminated_cb;
    fwdata->hookFWInit = pspmix_jobserver_initialize;
    fwdata->hookLoop = pspmix_jobserver_prepareLoop;
    fwdata->hookFinalize = pspmix_jobserver_finalize;
    fwdata->handleMthrMsg = pspmix_comm_handleMthrMsg;
    fwdata->handleFwMsg = forwardPspmixFwMsg;

    if (!startForwarder(fwdata)) {
	mlog("%s: starting PMIx jobserver for job '%s' failed\n", __func__,
		jobid);
	return false;
    }

    server->fwdata = fwdata;

    return true;
}

/*
 * @brief Stop the PMIx server process for a job
 *
 * Stop the pluginforwarder working as PMIx jobserver and removes it from
 * the list of jobservers.
 * If the server already died, just remove it from the list.
 */
static void stopJobserver(PspmixJobserver_t *server)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called for job with loggertid %s\n", __func__,
	    PSC_printTID(server->loggertid));

    if (server->fwdata) {
	shutdownForwarder(server->fwdata);
	server->fwdata->userData = NULL;
	mdbg(PSPMIX_LOG_VERBOSE, "%s: PMIx jobserver stopped for job with"
		" loggertid %s\n", __func__, PSC_printTID(server->loggertid));
    }
    else {
	mlog("%s: PMIx jobserver for job with loggertid %s has no valid"
		" forwarder data, just removing from list.\n", __func__,
		PSC_printTID(server->loggertid));
    }

    list_del(&server->next);
    ufree(server);

}

/**
 * @brief Hook function for PSIDHOOK_RECV_SPAWNREQ
 *
 * This hook is called after receiving a spawn request message
 *
 * In this function we do start the PMIx jobserver.
 *
 * @param data Pointer to task structure to be spawned.
 *
 * @return Returns 0 on success and -1 on error.
 */
static int hookRecvSpawnReq(void *data)
{
    PStask_t *prototask = data;

    /* leave all special task groups alone */
    if (prototask->group != TG_ANY) return 0;

    mdbg(PSPMIX_LOG_CALL, "%s() called with task group TG_ANY\n", __func__);

    /* decide if this job wants to use PMIx */
    if (!pspmix_common_usePMIx(prototask)) return 0;

    /* find job */
    PSjob_t *job = PSID_findJobByLoggerTID(prototask->loggertid);
    if (!job) {
	mlog("%s: No job with logger %s\n", __func__,
	     PSC_printTID(prototask->loggertid));
	return -1;
    }

    if (list_empty(&job->resInfos)) {
	mlog("%s: No reservation in job with logger %s\n", __func__,
	     PSC_printTID(prototask->loggertid));
    }

    /* is there already a PMIx jobserver running for this job? */
    PspmixJobserver_t *server;
    server = findJobserver(job->loggertid);

    if (!server) {
	/* No jobserver found, start one */
	server = ucalloc(sizeof(*server));
	server->loggertid = prototask->loggertid;

	// @todo what needs to be copied when cleanup daemon stuff
	// in jobserver_init?

	/* set prototask to access it in the forked jobserver process */
	server->prototask = prototask;

	/* copy stuff from job */
	server->resInfos = job->resInfos;

	if (!startJobserver(server)) {
	    mlog("%s: Failed to start PMIx jobserver for job with logger %s\n",
		    __func__, PSC_printTID(server->loggertid));
	    return -1;
	}

	mdbg(PSPMIX_LOG_VERBOSE, "%s: New PMIx jobserver started for job with"
	     " loggertid %s", __func__, PSC_printTID(server->loggertid));
	mdbg(PSPMIX_LOG_VERBOSE, ": %s\n", PSC_printTID(server->fwdata->tid));

	list_add_tail(&server->next, &pmixJobservers);

	// unset prototask, becomes invalid in the daemon once the hook returned
	server->prototask = NULL;
    } else {
	mdbg(PSPMIX_LOG_VERBOSE, "%s: Existing PMIx jobserver found for job"
	     " with loggertid %s", __func__, PSC_printTID(server->loggertid));
	mdbg(PSPMIX_LOG_VERBOSE, ": %s\n", PSC_printTID(server->fwdata->tid));

	// @todo do we need to inform the existing job server about the new
	//     task and resinfo ??? think we need to so it can resolve
	//     rank to node for each reservation
    }

    /* set jobserver tid in environment for the spawn forwarder */
    char buf[40];
    snprintf(buf, sizeof(buf), "%d", server->fwdata->tid);
    setenv("__PSPMIX_LOCAL_JOBSERVER_TID", buf, 1);

    return 0;
}

/**
 * @brief Hook function for PSIDHOOK_LOCALJOBREMOVED
 *
 * This hook is called before a local job gets removed
 *
 * In this function we do stop the PMIx jobserver.
 *
 * @param data Pointer to job structure to be removed.
 *
 * @return Returns 0 on success and -1 on error.
 */
static int hookLocalJobRemoved(void *data)
{
    PSjob_t *job = data;

    mdbg(PSPMIX_LOG_CALL, "%s() called for job with loggertid %s\n", __func__,
	    PSC_printTID(job->loggertid));

    // TODO look if this job is using PMIx

    /* is there a PMIx jobserver running for this job? */
    PspmixJobserver_t *server;
    server = findJobserver(job->loggertid);

    if (server == NULL) {
	mlog("%s: No existing PMIx jobserver found for job with loggertid %s."
		" (This is fine for jobs not using PMIx.)\n",
		__func__, PSC_printTID(job->loggertid));
	return -1;
    }

    /* jobserver found, stop it */
    stopJobserver(server);

    return 0;
}

/**
 * @brief Hook function for PSIDHOOK_NODE_DOWN
 *
 * This hook is called if a remote node disappeared
 *
 * In this function we do stop the PMIx jobserver of each job whose logger
 * was running on the disappeared node.
 *
 * @param nodeid ID of the disappeared node
 *
 * @return Returns 0 on success and -1 on error.
 */
static int hookNodeDown(void *data)
{
    PSnodes_ID_t *nodeid = data;

    mdbg(PSPMIX_LOG_CALL, "%s() called with nodeid %hd\n", __func__, *nodeid);

    PspmixJobserver_t *jobserver;
    list_t *j, *tmp;
    list_for_each_safe(j, tmp, &pmixJobservers) {
	jobserver = list_entry(j, PspmixJobserver_t, next);
	if (PSC_getID(jobserver->loggertid) == *nodeid) {
	    stopJobserver(jobserver);
	}
    }
    return 0;
}

void pspmix_initDaemonModule(void)
{
    PSIDhook_add(PSIDHOOK_RECV_SPAWNREQ, hookRecvSpawnReq);
    PSIDhook_add(PSIDHOOK_LOCALJOBREMOVED, hookLocalJobRemoved);
    PSIDhook_add(PSIDHOOK_NODE_DOWN, hookNodeDown);
    PSID_registerMsg(PSP_PLUG_PSPMIX, forwardPspmixMsg);
}

void pspmix_finalizeDaemonModule(void)
{
    PSIDhook_del(PSIDHOOK_RECV_SPAWNREQ, hookRecvSpawnReq);
    PSIDhook_del(PSIDHOOK_LOCALJOBREMOVED, hookLocalJobRemoved);
    PSIDhook_del(PSIDHOOK_NODE_DOWN, hookNodeDown);
    PSID_clearMsg(PSP_PLUG_PSPMIX, forwardPspmixMsg);
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
