/*
 * ParaStation
 *
 * Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <signal.h>

#include "psslurmtasks.h"
#include "pluginmalloc.h"

#include "psslurmbcast.h"

/** List of all bcasts */
static LIST_HEAD(BCastList);

BCast_t *addBCast(void)
{
    BCast_t *bcast;

    bcast = (BCast_t *) ucalloc(sizeof(BCast_t));

    initSlurmMsg(&bcast->msg);

    list_add_tail(&(bcast->list), &BCastList);

    return bcast;
}

void deleteBCast(BCast_t *bcast)
{
    list_del(&bcast->list);
    freeSlurmMsg(&bcast->msg);
    ufree(bcast->username);
    ufree(bcast->fileName);
    ufree(bcast->block);
    ufree(bcast->sig);
    ufree(bcast);
}

void clearBCastByJobid(uint32_t jobid)
{
    list_t *pos, *tmp;

    list_for_each_safe(pos, tmp, &BCastList) {
	BCast_t *bcast = list_entry(pos, BCast_t, list);
	if (bcast->jobid == jobid) {
	    if (bcast->fwdata) {
		killChild(PSC_getPID(bcast->fwdata->tid), SIGKILL);
	    } else {
		deleteBCast(bcast);
	    }
	}
    }
}

void clearBCastList(void)
{
    list_t *pos, *tmp;

    list_for_each_safe(pos, tmp, &BCastList) {
	BCast_t *bcast = list_entry(pos, BCast_t, list);
	deleteBCast(bcast);
    }
}

BCast_t *findBCast(uint32_t jobid, char *fileName, uint32_t blockNum)
{
    list_t *pos, *tmp;

    list_for_each_safe(pos, tmp, &BCastList) {
	BCast_t *bcast = list_entry(pos, BCast_t, list);
	if (blockNum > 0 && blockNum != bcast->blockNumber) continue;
	if (bcast->jobid == jobid &&
	    !strcmp(bcast->fileName, fileName)) return bcast;
    }
    return NULL;
}
