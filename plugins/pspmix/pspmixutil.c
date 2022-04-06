/*
 * ParaStation
 *
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pspmixutil.h"

#include <stdlib.h>

#include "list.h"
#include "pscommon.h"

#include "psidsession.h"

#include "pluginforwarder.h"
#include "pluginmalloc.h"

#include "pspmixlog.h"

/**
 * Print sessions managed by server using mlog()
 *
 * Prints all sessions, jobs, and reservations in the server object
 *
 * @param server  server to print
 */
static void printSessions(PspmixServer_t *server)
{
    list_t *s, *j, *r;
    list_for_each(s, &server->sessions) {
	PspmixSession_t *session = list_entry(s, PspmixSession_t, next);
	mlog("%s: Session: logger %s used %s remove %s\n", __func__,
	     PSC_printTID(session->loggertid),
	     session->used ? "true" : "false",
	     session->remove ? "true" : "false");
	list_for_each(j, &session->jobs) {
	    PspmixJob_t *job = list_entry(j, PspmixJob_t, next);
	    mlog("%s:  - Job: spawner %s used %s remove %s\n",
		 __func__, PSC_printTID(job->spawnertid),
		 job->used ? "true" : "false",
		 job->remove ? "true" : "false");
	    list_for_each(r, &job->resInfos) {
		PSresinfo_t *res = list_entry(r, PSresinfo_t, next);
		mlog("%s:    - Reservation: resID %d nEntries %u entries [",
		     __func__, res->resID, res->nEntries);
		for (unsigned int i = 0; i < res->nEntries; i++) {
		    mlog("(%hd:%d-%d)", res->entries[i].node,
			 res->entries[i].firstrank,
			 res->entries[i].lastrank);
		}
		mlog("]\n");
	    }
	}
    }
}

void __pspmix_printServer(PspmixServer_t *server, bool sessions,
			  const char *caller, const int line)
{
    mlog("%s(%s:%d): Server: uid %d gid %d fwdata %p used %s timerId %d\n",
	 __func__, caller, line, server->uid, server->gid, server->fwdata,
	 server->used ? "true" : "false", server->timerId);
    if (server->fwdata) {
	mlog("%s: Forwarder: pTitle %s jobID %s tid %s\n", __func__,
		server->fwdata->pTitle, server->fwdata->jobID,
		PSC_printTID(server->fwdata->tid));
    }
    if (sessions) printSessions(server);
}

void pspmix_deleteJob(PspmixJob_t *job)
{
    if (!job) return;
    mdbg(PSPMIX_LOG_CALL, "%s(spawner %s) called\n", __func__,
	 PSC_printTID(job->spawnertid));

    list_del(&job->next);

    list_t *r, *tmp;
    list_for_each_safe(r, tmp, &job->resInfos) {
	PSresinfo_t *resInfo = list_entry(r, PSresinfo_t, next);
	list_del(&resInfo->next);
	free(resInfo);
    }

    ufree(job);
}

void __pspmix_deleteSession(PspmixSession_t *session, bool warn,
			  const char *caller, const int line)
{
    if (!session) return;
    mdbg(PSPMIX_LOG_CALL, "%s(logger %s) called\n", __func__,
	 PSC_printTID(session->loggertid));

    list_del(&session->next);

    if (!list_empty(&session->jobs) && warn) {
	mlog("%s(%s@%d): jobs list not empty (logger %s)\n", __func__,
	     caller, line, PSC_printTID(session->loggertid));
    }

    list_t *j, *tmp;
    list_for_each_safe(j, tmp, &session->jobs) {
	PspmixJob_t *job = list_entry(j, PspmixJob_t, next);
	pspmix_deleteJob(job);
    }

    ufree(session);
}

void __pspmix_deleteServer(PspmixServer_t *server, bool warn,
			  const char *caller, const int line)
{
    mdbg(PSPMIX_LOG_CALL, "%s(uid %d) called\n", __func__, server->uid);

    list_del(&server->next);

    if (!list_empty(&server->sessions) && warn) {
	mlog("%s(%s@%d): sessions list not empty (uid %s)\n", __func__,
	     caller, line, PSC_printTID(server->uid));
    }

    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &server->sessions) {
	PspmixSession_t *session = list_entry(s, PspmixSession_t, next);
	__pspmix_deleteSession(session, warn, caller, line);
    }

    if (server->fwdata) server->fwdata->userData = NULL;
    free(server);
}
