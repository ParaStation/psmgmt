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
 * @file Implementation of pspmix functions running in the plugin forwarder
 *       working as PMIx server
 */
#include "pspmixuserserver.h"

#include <stdio.h>

#include "list.h"
#include "pscommon.h"
#include "psidutil.h"

#include "pluginmalloc.h"

#include "pspmixlog.h"
#include "pspmixservice.h"
#include "pspmixtypes.h"
#include "pspmixutil.h"
#include "pspmixcomm.h"

PspmixServer_t *server = NULL;

/**
 * @brief Find job with given spawnertid
 *
 * @param spawnertid  TID of the spawner creating the job (unique ID)
 *
 * @return Returns the job or NULL if not in list
 */
static PspmixJob_t * findJob(PStask_ID_t spawnertid)
{
    if (!server) return NULL;

    list_t *s;
    list_for_each(s, &server->sessions) {
	PspmixSession_t *session = list_entry(s, PspmixSession_t, next);
	list_t *j;
	list_for_each(j, &session->jobs) {
	    PspmixJob_t *job = list_entry(j, PspmixJob_t, next);
	    if (job->spawnertid == spawnertid) return job;
	}
    }
    return NULL;
}

int pspmix_userserver_initialize(Forwarder_Data_t *fwdata)
{
    server = (PspmixServer_t *)fwdata->userData;

    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    /* there has to be a server object */
    if (!server) {
	mlog("%s: FATAL: no server object\n", __func__);
	return -1;
    }

    /* there must not be a session in the server object */
    if (!list_empty(&server->sessions)) {
	mlog("%s: FATAL: sessions list not empty\n", __func__);
	return -1;
    }

    char *clusterid = PSID_config->psiddomain;
    if (!clusterid || !clusterid[0]) clusterid = "ParaStationCluster";

    /* initialize service modules */
    if (!pspmix_service_init(server->uid, server->gid, clusterid)) {
	mlog("%s: Failed to initialize pmix service\n", __func__);
	return -1;
    }

    return 0;
}

static char * genSessionTmpdirName(PspmixSession_t *session)
{
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "/tmp/pspmix_%d/%s", session->server->uid,
	     PSC_printTID(session->ID));

    return ustrdup(tmp);
}

bool pspmix_userserver_addJob(PStask_ID_t sessID, PspmixJob_t *job)
{
    mdbg(PSPMIX_LOG_CALL, "%s(%s)\n", __func__,
	 pspmix_jobIDsStr(sessID, job->spawnertid));

    if (!server) {
	mlog("%s: FATAL: no server object\n", __func__);
	return false;
    }

    PspmixSession_t *session = findSessionInList(sessID, &server->sessions);
    if (!session) {
	session = ucalloc(sizeof(*session));
	if (!session) return false;
	session->server = server;
	session->ID = sessID;
	INIT_LIST_HEAD(&session->jobs);
	list_add_tail(&session->next, &server->sessions);
	session->tmpdir = genSessionTmpdirName(session);
	mdbg(PSPMIX_LOG_VERBOSE, "%s(uid %d): session created (ID %s"
	     " tmpdir %s)\n", __func__, server->uid, PSC_printTID(session->ID),
	     session->tmpdir);
    }

    job->session = session;
    list_add_tail(&job->next, &session->jobs);

    mdbg(PSPMIX_LOG_VERBOSE, "%s(uid %d): job added (%s)\n", __func__,
	 server->uid, pspmix_jobStr(job));

    if (!pspmix_service_registerNamespace(job)) {
	mlog("%s: creating namespace failed (%s)\n", __func__,
	     pspmix_jobStr(job));
	pspmix_deleteJob(job);
	if (list_empty(&session->jobs)) pspmix_deleteSession(session, true);
	return false;
    }

    if (mset(PSPMIX_LOG_VERBOSE)) pspmix_printServer(server, true);

    return true;
}

/**
 * @brief Terminate the Session
 *
 * Send first TERM and then KILL signal to all the processes in a
 * session.  This is done by simply signaling the session's logger and
 * relying on the SIGTERM / delayed SIGKILL mechanism in the psid
 *
 * @return No return value
 */
static void terminateSession(PspmixSession_t *session)
{
    mdbg(PSPMIX_LOG_CALL, "%s(ID %s)\n", __func__, PSC_printTID(session->ID));

    ulog("terminating session by signaling logger %s\n",
	 PSC_printTID(session->ID));

    pspmix_comm_sendSignal(session->ID, -1);
}

/**
 * @brief Terminate the SessionJob
 *
 * Send first TERM and then KILL signal to all the job's processes.
 *
 * @return No return value
 */
static void terminateJob(PspmixJob_t *job)
{
    mdbg(PSPMIX_LOG_CALL, "%s(%s)\n", __func__, pspmix_jobStr(job));

    /* TODO change for spawn support */

    ulog("only terminating sessions is supported at the moment\n");

    terminateSession(job->session);
}

bool pspmix_userserver_removeJob(PStask_ID_t spawnertid, bool abort)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    if (!server) {
	mlog("%s: FATAL: no server object\n", __func__);
	return false;
    }

    PspmixJob_t *job = findJob(spawnertid);
    if (!job) {
	ulog("job not found (spawner %s)\n", PSC_printTID(spawnertid));
	return false;
    }

    if (abort) terminateJob(job);

    if (!pspmix_service_removeNamespace(spawnertid)) {
	ulog("destroying namespace failed (%s)\n", pspmix_jobStr(job));
	return false;
    }

    PspmixSession_t *session = job->session;
    pspmix_deleteJob(job);

    mdbg(PSPMIX_LOG_VERBOSE, "%s(uid %d): job removed (job %s)\n", __func__,
	 server->uid, pspmix_jobIDsStr(session->ID, spawnertid));

    if (list_empty(&session->jobs)) pspmix_deleteSession(session, true);

    return true;
}

void pspmix_userserver_prepareLoop(Forwarder_Data_t *fwdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    if (!server) mlog("%s: FATAL: no server object\n", __func__);
}

void pspmix_userserver_finalize(Forwarder_Data_t *fwdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s()\n", __func__);

    pspmix_service_finalize();

    server = NULL;
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
