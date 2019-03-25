/*
 * ParaStation
 *
 * Copyright (C) 2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

/**
 * @file Implementation of pspmix functions running in the daemon
 *
 * Two jobs are done inside the daemon:
 * 1. Start the PMIx Jobserver as plugin forwarder
 * 2. Forward plugin messages
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "pstask.h"
#include "psidhook.h"
#include "pscommon.h"
#include "pluginmalloc.h"
#include "psidcomm.h"
#include "psidclient.h"
#include "psidspawn.h"
#include "pspluginprotocol.h"
#include "pluginforwarder.h"

#include "pspmixtypes.h"
#include "pspmixlog.h"
#include "pspmixcommon.h"
#include "pspmixjobserver.h"
#include "pspmixcomm.h"

#include "pspmixdaemon.h"

/** The list of running PMIx jobservers on this node */
LIST_HEAD(pmixJobservers);

/**
 * @brief Find local PMIx jobserver for job with logger tid
 *
 * @param loggertid tid of the logger
 *
 * @return Returns the job or NULL if not in list
 */
static PspmixJobserver_t* findJobserver(PStask_ID_t loggertid)
{
    PspmixJobserver_t *jobserver;
    list_t *j;
    list_for_each(j, &pmixJobservers) {
	jobserver = list_entry(j, PspmixJobserver_t, next);
	if (jobserver->loggertid == loggertid) {
	    return jobserver;
	}
    }
    return NULL;
}

void setTargetToPmixJobserver(DDTypedBufferMsg_t *msg) {

    //TODO support more than one jobserver in the list
    PspmixJobserver_t *server;
    list_t *s;
    int count = 0;
    list_for_each(s, &pmixJobservers) {
	count++;
	server = list_entry(s, PspmixJobserver_t, next);
    }

    if (count == 0) {
	mlog("%s: UNEXPECTED: No jobserver found.\n", __func__);
	return;
    }

    if (count != 1) {
	mlog("%s: Currently there is only one jobserver per node supported"
		" (found %d).\n", __func__, count);
	return;
    }

    msg->header.dest = server->fwdata->tid;

    mdbg(PSPMIX_LOG_COMM, "%s: setting destination %s\n", __func__,
	    PSC_printTID(msg->header.dest));
}

/*
 * @brief Forward messages of type PSP_CC_PLUG_PSPMIX in the main daemon.
 *
 * This function is registered in the daemon and used for messages comming
 * from the client forwarder and from other deamons and thus from PMIx
 * jobservers running there.
 *
 * @param fw     forwarder data
 *
 * @return Returns 1 if the type is known, 0 if not
 */
static void forwardPspmixMsg(DDMsg_t *vmsg)
{
    char sender[40], dest[40];

    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    DDTypedBufferMsg_t *msg = (DDTypedBufferMsg_t *)vmsg;

    strcpy(sender, PSC_printTID(msg->header.sender));
    strcpy(dest, PSC_printTID(msg->header.dest));

    mdbg(PSPMIX_LOG_COMM, "%s: msg: type %s (%i) length %hu [%s->%s]\n",
	    __func__, pspmix_getMsgTypeString(msg->type), msg->type,
	    msg->header.len, sender, dest);

    if (PSC_getID(msg->header.dest) == PSC_getMyID()) {
	/* destination is local */
	switch(msg->type) {
	    case PSPMIX_FENCE_IN:
	    case PSPMIX_MODEX_DATA_REQ:
		setTargetToPmixJobserver(msg);
		break;
	    default:
		break;
	}
	PSIDclient_send(vmsg);
    } else {
	/* destination is remote host */
	sendMsg(vmsg);
    }
}

/**
 * @brief Forward messages of type PSP_CC_PLUG_PSPMIX in the main daemon.
 *
 * This function is used to forward messages comming from the local PMIx
 * job server in the daemon.
 *
 * @param vmsg   message received
 * @param fw     the plugin forwarder hosting the PMIx jobserver
 *
 * @return Returns 1 if the type is known, 0 if not
 */
static int forwardPspmixFwMsg(PSLog_Msg_t *tmpmsg, ForwarderData_t *fw) {

    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    DDMsg_t *vmsg = (DDMsg_t *)tmpmsg; /* HACK */

    if (vmsg->type != PSP_CC_PLUG_PSPMIX) return 0;

    forwardPspmixMsg(vmsg);

    return 1;
}

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
static int jobserverTerminated_cb(int32_t exit_status, Forwarder_Data_t *fw)
{
    mlog("%s() called\n", __func__);

#if 0
    PspmixJobserver_t *curserver, *server = NULL;
    list_t *s;
    list_for_each(s, &pmixJobservers) {
	curserver = list_entry(s, PspmixJobserver_t, next);
	if (curserver->fwdata == fw) {
	    server = curserver;
	    break;
	}
    }

    /* remove server object from list */
    list_del(&server->next);
    ufree(server);
#endif

    return 0;
}

/*
 * @brief Start the pmix server process for a job
 *
 * Start a pluginforwarder as pmix server handling all processes of the job
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
	mlog("%s: starting pspmix JobServer for job '%s' failed\n", __func__,
		jobid);
	return false;
    }

    server->fwdata = fwdata;

    return true;
}

/*
 * @brief Stop the pmix server process for a job
 *
 * Stop the pluginforwarder working as pmix job server and removes it from
 * the list of jobservers.
 */
static void stopJobserver(PspmixJobserver_t *server)
{
    mdbg(PSPMIX_LOG_CALL, "%s() called for job with loggertid %s\n", __func__,
	    PSC_printTID(server->loggertid));

    shutdownForwarder(server->fwdata);

    list_del(&server->next);
    ufree(server);

    mdbg(PSPMIX_LOG_VERBOSE, "%s: PMIx jobserver stopped for job with"
		" loggertid %s\n", __func__, PSC_printTID(server->loggertid));

}

/**
 * @brief Hook function for PSIDHOOK_RECV_SPAWNREQ
 *
 * This hook is called after receiving a spawn request message
 *
 * In this function we do start the pmix jobserver.
 *
 * @param data Pointer to task structure to be spawned.
 *
 * @return Returns 0 on success and -1 on error.
 */
static int hookRecvSpawnReq(void *data)
{
    char buf[40];

    PStask_t *task = data;

    /* leave all special task groups alone */
    if (task->group != TG_ANY) return 0;

    mdbg(PSPMIX_LOG_CALL, "%s() called with task group TG_ANY\n", __func__);

    /* descide if this job wants to use PMIx */
    if (!pspmix_common_usePMIx(task)) return 0;

    /* find job */
    PSjob_t *job;
    job = PSID_findJobByLoggerTid(task->loggertid);

    if (job == NULL) {
	mlog("%s: Could not find job with loggertid %s\n", __func__,
		PSC_printTID(task->loggertid));
	return -1;
    }

    if (list_empty(&job->resInfos)) {
	mlog("%s: No reservation in job with loggertid %s\n", __func__,
		PSC_printTID(task->loggertid));
    }
    
    if (job->resInfos.next->next != &job->resInfos) {
	// more than one reservation
	//TODO implement respawn
	mlog("%s: SORRY: Respawning tasks not yet implemented in pspmix.\n",
		__func__);
	return -1;
    }

    /* is there already a PMIx jobserver running for this job? */
    PspmixJobserver_t *server;
    server = findJobserver(job->loggertid);

    if (server != NULL) {
	strcpy(buf, PSC_printTID(server->loggertid));
	mdbg(PSPMIX_LOG_VERBOSE, "%s: Existing PMIx jobserver found for job"
		" with loggertid %s: %s\n", __func__, buf,
		PSC_printTID(server->fwdata->tid));

	//TODO do we need to inform the existing job server about the new 
	//     task and resinfo ??? think we need to so it can resolve
	//     rank to node for each reservation

	goto setenv;
    }

    /* No jobserver found, start one */
    server = ucalloc(sizeof(*server));
    server->loggertid = task->loggertid;

    // XXX what needs to be copied when cleanup daemon stuff in jobserver_init?

    /* remember first task of the jobserver */
    server->task = task;

    /* copy stuff from job */
    server->resInfos = job->resInfos;

    startJobserver(server);

    strcpy(buf, PSC_printTID(server->loggertid));
    mdbg(PSPMIX_LOG_VERBOSE, "%s: New PMIx jobserver started for job with"
		" loggertid %s: %s\n", __func__, buf,
		PSC_printTID(server->fwdata->tid));

    list_add_tail(&server->next, &pmixJobservers);

setenv:

    /* set jobserver tid in environment for the spawn forwarder */
    snprintf(buf, sizeof(buf), "%u", server->fwdata->tid);
    setenv("__PSPMIX_LOCAL_JOBSERVER_TID", buf, 1);

    return 0;
}

/**
 * @brief Hook function for PSIDHOOK_LOCALJOBREMOVED
 *
 * This hook is called before a local job gets removed
 *
 * In this function we do stop the pmix jobserver.
 *
 * @param data Pointer to job structure to be removed.
 *
 * @return Returns 0 on success and -1 on error.
 */
static int hookLocalJobRemoved(void *data)
{
    PSjob_t *job = data;

    /* is there a PMIx jobserver running for this job? */
    PspmixJobserver_t *server;
    server = findJobserver(job->loggertid);

    if (server == NULL) {
	mlog("%s: No existing PMIx jobserver found for job with loggertid %s\n",
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
 * In this function we do stop the pmix jobserver of each job whose logger
 * was running on the disappeared node.
 *
 * @param nodeid ID of the disappeared node
 *
 * @return Returns 0 on success and -1 on error.
 */
static int hookNodeDown(void *data)
{
    PSnodes_ID_t *nodeid = data;

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
    PSID_registerMsg(PSP_CC_PLUG_PSPMIX, (handlerFunc_t) forwardPspmixMsg);
}

void pspmix_finalizeDaemonModule(void)
{
    PSIDhook_del(PSIDHOOK_RECV_SPAWNREQ, hookRecvSpawnReq);
    PSIDhook_del(PSIDHOOK_LOCALJOBREMOVED, hookLocalJobRemoved);
    PSIDhook_del(PSIDHOOK_NODE_DOWN, hookNodeDown);
    PSID_clearMsg(PSP_CC_PLUG_PSPMIX);
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
