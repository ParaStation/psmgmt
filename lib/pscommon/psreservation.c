/*
 * ParaStation
 *
 * Copyright (C) 2015-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psreservation.h"

#include <stdlib.h>

#include "pscommon.h"
#include "list.h"

/** data structure to handle a pool of reservations */
static PSitems_t rsrvtnPool = NULL;

PSrsrvtn_t *PSrsrvtn_get(void)
{
    PSrsrvtn_t *rp = PSitems_getItem(rsrvtnPool);
    if (!rp) return NULL;

    rp->state = RES_USED;
    rp->task = 0;
    rp->requester = 0;
    rp->nMin = 0;
    rp->nMax = 0;
    rp->ppn = 0;
    rp->tpp = 1;
    rp->hwType = 0;
    rp->options = 0;
    rp->rid = 0;
    rp->firstRank = 0;
    rp->rankOffset = 0;
    rp->nSlots = 0;
    rp->slots = NULL;
    rp->nextSlot = 0;
    rp->relSlots = 0;
    rp->checked = false;
    rp->dynSent = false;

    return rp;
}

void PSrsrvtn_put(PSrsrvtn_t *rp)
{
    if (rp->slots) {
	PSC_flog("still slots in registration %#x\n", rp->rid);
	free(rp->slots);
    }

    PSitems_putItem(rsrvtnPool, rp);
}

static bool relocPSrsrvtn(void *item)
{
    PSrsrvtn_t *orig = item, *repl = PSrsrvtn_get();

    if (!repl) return false;

    /* copy reservation struct's content */
    repl->task = orig->task;
    repl->requester = orig->requester;
    repl->nMin = orig->nMin;
    repl->nMax = orig->nMax;
    repl->ppn = orig->ppn;
    repl->tpp = orig->tpp;
    repl->hwType = orig->hwType;
    repl->options = orig->options;
    repl->rid = orig->rid;
    repl->firstRank = orig->firstRank;
    repl->rankOffset = orig->rankOffset;
    repl->nSlots = orig->nSlots;
    repl->slots = orig->slots;
    orig->slots = NULL;
    repl->nextSlot = orig->nextSlot;
    repl->relSlots = orig->relSlots;
    repl->checked = orig->checked;
    repl->dynSent = orig->dynSent;


    /* tweak the list */
    __list_add(&repl->next, orig->next.prev, orig->next.next);

    return true;
}

void PSrsrvtn_gc(void)
{
    PSitems_gc(rsrvtnPool, relocPSrsrvtn);
}

void PSrsrvtn_printStat(void)
{
    PSC_flog("Reservations %d/%d (used/avail)", PSitems_getUsed(rsrvtnPool),
	     PSitems_getAvail(rsrvtnPool));
    PSC_log(-1, "\t%d/%d (gets/grows)\n", PSitems_getUtilization(rsrvtnPool),
	    PSitems_getDynamics(rsrvtnPool));
}

void PSrsrvtn_clearMem(void)
{
    PSitems_clearMem(rsrvtnPool);
    rsrvtnPool = NULL;
}

void PSrsrvtn_init(void)
{
    if (PSitems_isInitialized(rsrvtnPool)) return;
    rsrvtnPool = PSitems_new(sizeof(PSrsrvtn_t), "rsrvtnPool");
}
