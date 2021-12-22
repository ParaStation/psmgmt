/*
 * ParaStation
 *
 * Copyright (C) 2015-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pssignal.h"

#include <stddef.h>

#include "list.h"
#include "psitems.h"
#include "pscommon.h"

/** data structure to handle a pool of signal structures */
static PSitems_t sigPool = NULL;

void PSsignal_init(void)
{
    if (PSitems_isInitialized(sigPool)) return;
    sigPool = PSitems_new(sizeof(PSsignal_t), "PSsignal");
 }

PSsignal_t *PSsignal_get(void)
{
    PSsignal_t *sp = PSitems_getItem(sigPool);
    if (!sp) return NULL;

    sp->tid = 0;
    sp->deleted = false;

    return sp;
}

void PSsignal_put(PSsignal_t *sp)
{
    PSitems_putItem(sigPool, sp);
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
    PSitems_gc(sigPool, relocSig);
}

void PSsignal_printStat(void)
{
    PSC_log(-1, "%s: Signals %d/%d (used/avail)", __func__,
	    PSitems_getUsed(sigPool), PSitems_getAvail(sigPool));
    PSC_log(-1, "\t%d/%d (gets/grows)\n", PSitems_getUtilization(sigPool),
	    PSitems_getDynamics(sigPool));
}
