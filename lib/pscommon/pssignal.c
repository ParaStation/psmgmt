/*
 * ParaStation
 *
 * Copyright (C) 2015-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pssignal.h"

#include <errno.h>
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

void PSsignal_clearList(list_t *list)
{
    list_t *s, *tmp;
    list_for_each_safe(s, tmp, list) {
	PSsignal_t *signal = list_entry(s, PSsignal_t, next);
	list_del(&signal->next);
	PSsignal_put(signal);
    }
}

void PSsignal_cloneList(list_t *cloneList, list_t *origList)
{
    PSC_log(PSC_LOG_TASK, "%s(%p)\n", __func__, origList);

    PSsignal_clearList(cloneList);

    list_t *s;
    list_for_each(s, origList) {
	PSsignal_t *origSig = list_entry(s, PSsignal_t, next);
	PSsignal_t *cloneSig = PSsignal_get();
	if (!cloneSig) {
	    PSsignal_clearList(cloneList);
	    PSC_warn(-1, ENOMEM, "%s()", __func__);
	    break;
	}

	cloneSig->tid = origSig->tid;
	cloneSig->signal = origSig->signal;
	list_add_tail(&cloneSig->next, cloneList);
    }
}
