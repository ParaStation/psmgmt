/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

/**
 * @file Implementation of pspmix functions running in the plugin forwarder
 *       working as PMIx server
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include "list.h"
#include "pstask.h"

#include "pspmixlog.h"
#include "pspmixservice.h"
#include "pspmixcomm.h"

#include "pspmixjobserver.h"

static PspmixJobserver_t *server = NULL;

/**
 * @brief Find reservation in the server object
 *
 * @param resID  reservation id
 *
 * @return Returns the reservation or NULL if not in list
 */
static PSresinfo_t* findReservation(PSrsrvtn_ID_t resID)
{
    if (!server) return NULL;

    return findReservationInList(resID, &server->resInfos);
}

int pspmix_jobserver_initialize(Forwarder_Data_t *fwdata)
{
    server = fwdata->userData;

    mdbg(PSPMIX_LOG_CALL, "%s() called\n", __func__);

    PStask_t *prototask;
    prototask = server->prototask;

    /* there has to be a resInfo in the list */
    if (list_empty(&server->resInfos)) {
	mlog("%s: FATAL: no reservation in server's list\n", __func__);
	return -1;
    }

    PSresinfo_t *resInfo;
    resInfo = findReservation(prototask->resID);

    if (resInfo == NULL) {
	mlog("%s: FATAL: Reservation for initial spawn not found (resID %d)\n",
		__func__, prototask->resID);
	return -1;
    }


    if (mset(PSPMIX_LOG_VERBOSE)) {
	list_t *r;
	list_for_each(r, &server->resInfos) {
	    PSresinfo_t *res = list_entry(r, PSresinfo_t, next);
	    mlog("%s: Reservation: resID %d nEntries %u entries [",
		    __func__, res->resID, res->nEntries);
	    for (unsigned int i = 0; i < res->nEntries; i++) {
		mlog("(%hd:%d-%d)", res->entries[i].node,
			res->entries[i].firstrank,
			res->entries[i].lastrank);
	    }
	    mlog("]\n");
	}
    }

    /* initialize service modules */
    if (!pspmix_service_init(prototask->loggertid, prototask->uid,
		prototask->gid)) {
	mlog("%s: Failed to initialize pmix service\n", __func__);
	return -1;
    }

    /* register initial namespace */
    if (!pspmix_service_registerNamespace(prototask, server->resInfos)) {
	mlog("%s: Failed to register initial namespace\n", __func__);
	return -1;
    }

    return 1;
}

void pspmix_jobserver_prepareLoop(Forwarder_Data_t *fwdata)
{
    mdbg(PSPMIX_LOG_CALL, "%s called\n", __func__);
}

void pspmix_jobserver_finalize(Forwarder_Data_t *fwdata)
{
    pspmix_service_finalize();

    server = NULL;
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
