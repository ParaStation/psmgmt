/*
 * ParaStation
 *
 * Copyright (C) 2015-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdlib.h>

#include "pscommon.h"
#include "list.h"

#include "psreservation.h"

/** Flag to track initialization */
static bool initialized = false;

/** data structure to handle a pool of reservations */
static PSitems_t PSrsrvtns;

PSrsrvtn_t *PSrsrvtn_get(void)
{
    if (!initialized) {
	PSC_log(-1, "%s: initialize before\n", __func__);
	return NULL;
    }

    PSrsrvtn_t *rp = PSitems_getItem(&PSrsrvtns);
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
    rp->nSlots = 0;
    rp->slots = NULL;
    rp->nextSlot = 0;
    rp->relSlots = 0;
    rp->checked = 0;
    rp->dynSent = 0;

    return rp;
}

void PSrsrvtn_put(PSrsrvtn_t *rp)
{
    if (!initialized) {
	PSC_log(-1, "%s: initialize before\n", __func__);
	return;
    }

    if (rp->slots) {
	PSC_log(-1, "%s: still slots in registration %#x\n", __func__, rp->rid);
	free(rp->slots);
    }

    PSitems_putItem(&PSrsrvtns, rp);
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
    if (!initialized) {
	PSC_log(-1, "%s: initialize before\n", __func__);
	return;
    }

    PSitems_gc(&PSrsrvtns, relocPSrsrvtn);
}

void PSrsrvtn_printStat(void)
{
    if (!initialized) {
	PSC_log(-1, "%s: initialize before\n", __func__);
	return;
    }

    PSC_log(-1, "%s: Reservations %d/%d (used/avail)\n", __func__,
	    PSitems_getUsed(&PSrsrvtns), PSitems_getAvail(&PSrsrvtns));
}

void PSrsrvtn_clearMem(void)
{
    if (!initialized) {
	PSC_log(-1, "%s: initialize before\n", __func__);
	return;
    }

    PSitems_clearMem(&PSrsrvtns);
}

void PSrsrvtn_init(void)
{
    if (initialized) return;

    PSitems_init(&PSrsrvtns, sizeof(PSrsrvtn_t), "PSrsrvtns");
    initialized = true;
}
