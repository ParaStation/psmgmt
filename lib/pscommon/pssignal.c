/*
 * ParaStation
 *
 * Copyright (C) 2015-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "list.h"
#include "psitems.h"
#include "pscommon.h"

#include "pssignal.h"

/** Flag to track initialization */
static bool initialized = false;

/** data structure to handle a pool of signal structures */
static PSitems_t sigPool;

void PSsignal_init(void)
{
    if (initialized) return;

    PSitems_init(&sigPool, sizeof(PSsignal_t), "PSsignal");
    initialized = true;
 }

PSsignal_t *PSsignal_get(void)
{
    if (!initialized) {
	PSC_log(-1, "%s: initialize before\n", __func__);
	return NULL;
    }

    PSsignal_t *sp = PSitems_getItem(&sigPool);
    if (!sp) return NULL;

    sp->tid = 0;
    sp->deleted = false;

    return sp;
}

void PSsignal_put(PSsignal_t *sp)
{
    PSitems_putItem(&sigPool, sp);
}

static bool relocSig(void *item)
{
    PSsignal_t *orig = item, *repl = PSsignal_get();

    if (!repl) return false;

    /* copy signal struct's content */
    repl->tid = orig->tid;
    repl->signal = orig->signal;
    repl->deleted = orig->deleted;

    /* tweak the list */
    __list_add(&repl->next, orig->next.prev, orig->next.next);

    return true;
}

void PSsignal_gc(void)
{
    if (!initialized) {
	PSC_log(-1, "%s: initialize before\n", __func__);
	return;
    }

    PSitems_gc(&sigPool, relocSig);
}

void PSsignal_printStat(void)
{
    if (!initialized) {
	PSC_log(-1, "%s: initialize before\n", __func__);
	return;
    }

    PSC_log(-1, "%s: Signals %d/%d (used/avail)\n", __func__,
	    PSitems_getUsed(&sigPool), PSitems_getAvail(&sigPool));
}
