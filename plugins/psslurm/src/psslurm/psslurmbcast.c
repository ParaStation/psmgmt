/*
 * ParaStation
 *
 * Copyright (C) 2017-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
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
    BCast_t *bcast = ucalloc(sizeof(BCast_t));

    initSlurmMsg(&bcast->msg);

    list_add_tail(&(bcast->next), &BCastList);

    return bcast;
}

void deleteBCast(BCast_t *bcast)
{
    list_del(&bcast->next);
    freeSlurmMsg(&bcast->msg);
    ufree(bcast->username);
    ufree(bcast->fileName);
    ufree(bcast->block);
    ufree(bcast->sig);
    ufree(bcast);
}

void clearBCastByJobid(uint32_t jobid)
{
    list_t *b, *tmp;
    list_for_each_safe(b, tmp, &BCastList) {
	BCast_t *bcast = list_entry(b, BCast_t, next);
	if (bcast->jobid == jobid) {
	    if (bcast->fwdata) {
		killChild(PSC_getPID(bcast->fwdata->tid), SIGKILL, bcast->uid);
	    } else {
		deleteBCast(bcast);
	    }
	}
    }
}

void clearBCastList(void)
{
    list_t *b, *tmp;
    list_for_each_safe(b, tmp, &BCastList) {
	BCast_t *bcast = list_entry(b, BCast_t, next);
	deleteBCast(bcast);
    }
}

BCast_t *findBCast(uint32_t jobid, char *fileName, uint32_t blockNum)
{
    list_t *b;
    list_for_each(b, &BCastList) {
	BCast_t *bcast = list_entry(b, BCast_t, next);
	if (blockNum > 0 && blockNum != bcast->blockNumber) continue;
	if (bcast->jobid == jobid &&
	    !strcmp(bcast->fileName, fileName)) return bcast;
    }
    return NULL;
}

void freeBCastCred(BCast_Cred_t *cred)
{
    ufree(cred->username);
    ufree(cred->gids);
    ufree(cred->hostlist);
    ufree(cred->sig);
}
