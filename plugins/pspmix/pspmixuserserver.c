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
#include <sys/stat.h>

#include "list.h"
#include "pscommon.h"
#include "psidutil.h"

#include "pluginhelper.h"
#include "pluginmalloc.h"

#include "pspmixlog.h"
#include "pspmixservice.h"
#include "pspmixtypes.h"
#include "pspmixutil.h"
#include "pspmixcomm.h"

PspmixServer_t *server = NULL;

/**
 * @brief Find job with given ID
 *
 * ATTENTION: Special care is needed every time a job reference is used. The
 * namespace in pspmixservice.c contains a reference to the corresponing job
 * to be used read only there. It is used to access the job's ID and
 * environment. As long as the job is created before the namespace and the
 * reference is removed before the job is deleted, everything is fine as long
 * as the job is never changed in between. So the job objects are considered to
 * be IMMUTABLE.
 *
 * @param jobID Job's unique ID (TID of the spawner creating the job)
 *
 * @return Returns the job or NULL if not in list
 */
static PspmixJob_t * findJob(PStask_ID_t jobID)
{
    if (!server) return NULL;

    list_t *s;
    list_for_each(s, &server->sessions) {
	PspmixSession_t *session = list_entry(s, PspmixSession_t, next);
	list_t *j;
	list_for_each(j, &session->jobs) {
	    PspmixJob_t *job = list_entry(j, PspmixJob_t, next);
	    if (job->ID == jobID) return job;
	}
    }

    return NULL;
}

int pspmix_userserver_initialize(Forwarder_Data_t *fwdata)
{
    server = (PspmixServer_t *)fwdata->userData;

    fdbg(PSPMIX_LOG_CALL, "\n");

    /* there has to be a server object */
    if (!server) {
	flog("FATAL: no server object\n");
	return -1;
    }

    /* there must not be a session in the server object */
    if (!list_empty(&server->sessions)) {
	flog("FATAL: sessions list not empty\n");
	return -1;
    }

    /* generate server namespace name and rank */
    snprintf(server->nspace, sizeof(server->nspace), "pspmix_%d", server->uid);
    server->rank = PSC_getMyID();

    /* fill root for all temporary directories */
    snprintf(server->tmproot, sizeof(server->tmproot), "/tmp/pspmix_%d",
	     server->uid);
    mkDir(server->tmproot, S_IRWXU, server->uid, server->gid);

    /* create top level temporary directory */
    if (!mkDir(server->tmproot, 0755, server->uid, server->gid)) {
	flog("failed to create session's tempdir '%s'\n", server->tmproot);
    }

    char *clusterid = PSID_config->psiddomain;
    if (!clusterid || !clusterid[0]) clusterid = "ParaStationCluster";

    /* initialize service modules */
    if (!pspmix_service_init(server, clusterid)) {
	flog("failed to initialize service module for UID %d\n", server->uid);
	return -1;
    }

    /* always print UID to have the PID <-> UID mapping in syslog */
    flog("server for UID %d initialized\n", server->uid);

    return 0;
}

static char * genSessionTmpdirName(PspmixSession_t *session)
{
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%s/%#.8x", server->tmproot, session->ID);

    return ustrdup(tmp);
}

bool pspmix_userserver_addJob(PStask_ID_t sessID, PspmixJob_t *job)
{
    fdbg(PSPMIX_LOG_CALL, "sessID %s\n", pspmix_jobIDsStr(sessID, job->ID));

    if (!server) {
	flog("FATAL: no server object\n");
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
	fdbg(PSPMIX_LOG_VERBOSE, "uid %d: session created (ID %s tmpdir %s)\n",
	     server->uid, PSC_printTID(session->ID), session->tmpdir);

	/* create session's temporary directory */
	if (!mkDir(session->tmpdir, 0755, server->uid, server->gid)) {
	    flog("failed to create session's tempdir '%s'\n", session->tmpdir);
	}
    }

    job->session = session;
    list_add_tail(&job->next, &session->jobs);

    fdbg(PSPMIX_LOG_VERBOSE, "uid %d: new job %s\n",
	 server->uid, pspmix_jobStr(job));

    if (!pspmix_service_registerNamespace(job)) {
	flog("failed to create namespace '%s'\n", pspmix_jobStr(job));
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
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
static void terminateSession(PspmixSession_t *session)
{
    fdbg(PSPMIX_LOG_CALL, "ID %s\n", PSC_printTID(session->ID));

    flog("terminating session by sending signal to logger %s\n",
	 PSC_printTID(session->ID));

    pspmix_comm_sendSignal(session->ID, -1);
}
#pragma GCC diagnostic pop

bool pspmix_userserver_removeJob(PStask_ID_t jobID)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

    if (!server) {
	flog("FATAL: no server object\n");
	return false;
    }

    PspmixJob_t *job = findJob(jobID);
    if (!job) {
	flog("job not found (ID %s)\n", PSC_printTID(jobID));
	return false;
    }

    /* always remove namespace first, so there is no reference to job left */
    if (!pspmix_service_removeNamespace(jobID)) {
	flog("destroying namespace failed (%s)\n", pspmix_jobStr(job));
	return false;
    }

    PspmixSession_t *session = job->session;
    pspmix_deleteJob(job);

    mdbg(PSPMIX_LOG_VERBOSE, "%s(uid %d): job removed (job %s)\n", __func__,
	 server->uid, pspmix_jobIDsStr(session->ID, jobID));

    if (list_empty(&session->jobs)) {
	/* remove session's tempdir */
	removeDir(session->tmpdir, true);
	pspmix_deleteSession(session, true);
    }

    return true;
}

void pspmix_userserver_prepareLoop(Forwarder_Data_t *fwdata)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

    if (!server) flog("FATAL: no server object\n");
}

void pspmix_userserver_finalize(Forwarder_Data_t *fwdata)
{
    fdbg(PSPMIX_LOG_CALL, "\n");

    pspmix_service_finalize();

    /* remove root of all temporary directories */
    removeDir(server->tmproot, true);

    flog("server for UID %d finalized\n", server->uid);

    server = NULL;
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
