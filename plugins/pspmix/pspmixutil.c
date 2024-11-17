/*
 * ParaStation
 *
 * Copyright (C) 2022-2024 ParTec AG, Munich
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
	mlog("%s: Session: ID %s used %s\n", __func__,
	     PSC_printTID(session->ID), session->used ? "true" : "false");
	list_for_each(j, &session->jobs) {
	    PspmixJob_t *job = list_entry(j, PspmixJob_t, next);
	    mlog("%s:  - Job: ID %s used %s\n", __func__,
		 PSC_printTID(job->ID), job->used ? "true" : "false");
	    list_for_each(r, &job->resInfos) {
		PSresinfo_t *res = list_entry(r, PSresinfo_t, next);
		mlog("%s:    - Reservation: resID %d nEntries %u entries [",
		     __func__, res->resID, res->nEntries);
		for (unsigned int i = 0; i < res->nEntries; i++) {
		    mlog("(%hd:%d-%d)", res->entries[i].node,
			 res->entries[i].firstRank,
			 res->entries[i].lastRank);
		}
		mlog("]\n");
	    }
	}
    }
}

void __pspmix_printServer(PspmixServer_t *server, bool sessions,
			  const char *caller, const int line)
{
    mlog("%s@%d: Server: uid %d gid %d fwdata %p used %s timerId %d\n",
	 caller, line, server->uid, server->gid, server->fwdata,
	 server->used ? "true" : "false", server->timerId);
    if (server->fwdata) {
	mlog("%s@%d: Forwarder: pTitle %s jobID %s tid %s\n", caller, line,
	     server->fwdata->pTitle, server->fwdata->jobID,
	     PSC_printTID(server->fwdata->tid));
    }
    if (sessions) printSessions(server);
}

void pspmix_deleteJob(PspmixJob_t *job)
{
    if (!job) return;
    fdbg(PSPMIX_LOG_CALL, "ID %s\n", PSC_printTID(job->ID));

    list_del(&job->next);

    list_t *r, *tmp;
    list_for_each_safe(r, tmp, &job->resInfos) {
	PSresinfo_t *resInfo = list_entry(r, PSresinfo_t, next);
	list_del(&resInfo->next);
	free(resInfo->entries);
	free(resInfo);
    }

    ufree(job);
}

void __pspmix_deleteSession(PspmixSession_t *session, bool warn,
			  const char *caller, const int line)
{
    if (!session) return;
    fdbg(PSPMIX_LOG_CALL, "ID %s\n", PSC_printTID(session->ID));

    list_del(&session->next);

    if (!list_empty(&session->jobs) && warn) {
	mlog("%s@%d: jobs list not empty (ID %s)\n", caller, line,
	     PSC_printTID(session->ID));
    }

    list_t *j, *tmp;
    list_for_each_safe(j, tmp, &session->jobs) {
	PspmixJob_t *job = list_entry(j, PspmixJob_t, next);
	pspmix_deleteJob(job);
    }

    ufree(session->tmpdir);
    ufree(session);
}

void __pspmix_deleteServer(PspmixServer_t *server, bool warn,
			  const char *caller, const int line)
{
    fdbg(PSPMIX_LOG_CALL, "uid %d\n", server->uid);

    list_del(&server->next);

    if (!list_empty(&server->sessions) && warn) {
	mlog("%s@%d: sessions list not empty (uid %s)\n", caller, line,
	     PSC_printTID(server->uid));
    }

    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &server->sessions) {
	PspmixSession_t *session = list_entry(s, PspmixSession_t, next);
	__pspmix_deleteSession(session, warn, caller, line);
    }

    if (server->fwdata) server->fwdata->userData = NULL;
    free(server);
}
