/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "psidtask.h"
#include "pscommon.h"
#include "psidnodes.h"
#include "psidstatus.h"
#include "psidcomm.h"
#include "psdaemonprotocol.h"
#include "psidpartition.h"

#include "pluginmalloc.h"
#include "pluginlog.h"

#include "pluginpartition.h"

/** Structure to hold a nodelist */
typedef struct {
    int size;             /**< Actual number of valid entries within nodes[] */
    int maxsize;          /**< Maximum number of entries within nodes[] */
    PSnodes_ID_t *nodes;  /**< ParaStation IDs of the requested nodes. */
} nodelist_t;

int isPSAdminUser(uid_t uid, gid_t gid)
{
    if (!PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_ADMUSER,
		(PSIDnodes_guid_t){.u=uid})
	    && !PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_ADMGROUP,
		(PSIDnodes_guid_t){.g=gid})) {
	return 0;
    }
    return 1;
}

void grantPartitionRequest(PSpart_HWThread_t *hwThreads, uint32_t numHWthreads,
				PStask_ID_t dest, PStask_t *task)
{
    PSpart_HWThread_t *threads;

    threads = malloc(numHWthreads * sizeof(*threads));
    if (!threads) {
	errno = ENOMEM;
	rejectPartitionRequest(dest);
	return;
    }
    memcpy(threads, hwThreads, numHWthreads * sizeof(*threads));

    /* save the request in the task structure */
    task->options |= PART_OPT_EXACT;
    task->partition = NULL;
    task->usedThreads = 0;
    task->activeChild = 0;
    task->partitionSize = 0;
    task->partThrds = threads;
    task->totalThreads = numHWthreads;

    /* generate slots from hw threads and register partition to master psid */
    PSIDpart_register(task);

    /* send OK to waiting mpiexec */
    DDTypedMsg_t msg = (DDTypedMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_PARTITIONRES,
		.dest = dest,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg) },
	    .type = 0};

    if ((sendMsg(&msg)) == -1 && errno != EWOULDBLOCK) {
	pluginwarn(errno, "%s: sendMsg() to '%s' failed ", __func__,
		    PSC_printTID(msg.header.dest));
    }
}

void rejectPartitionRequest(PStask_ID_t dest)
{
    DDTypedMsg_t msg = (DDTypedMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_PARTITIONRES,
		.dest = dest,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg) },
	    .type = errno};
    if ((sendMsg(&msg)) == -1 && errno != EWOULDBLOCK) {
	pluginwarn(errno, "%s: sendMsg() to '%s' failed ", __func__,
		    PSC_printTID(msg.header.dest));
    }
}
