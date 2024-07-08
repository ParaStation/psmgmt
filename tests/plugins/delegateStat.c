/*
 * ParaStation
 *
 * Copyright (C) 2017-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "list.h"
#include "pscommon.h"
#include "pscpu.h"
#include "pspartition.h"
#include "psreservation.h"
#include "psstrbuf.h"

#include "plugin.h"

#include "psidnodes.h"
#include "psidtask.h"

int requiredAPI = 112;

char name[] = "delegateStat";

int version = 100;

plugin_dep_t dependencies[] = {
    { NULL, 0 } };

char * help(char *key)
{
    return strdup("\tPlugin to peek into existing delegate tasks.\n"
		  "\tUse the plugin's show directive to take a peek\n"
		  "\tUse the plugin's set directive to reset resource usage\n");
}

char * show(char *key)
{
    char l[128];
    uint16_t nBytes = PSCPU_bytesForCPUs(PSIDnodes_getNumThrds(PSC_getMyID()));

    if (!key || !key[0]) {
	snprintf(l, sizeof(l), "\nUsage: 'plugin show %s key <tid>'\n", name);
	return strdup(l);
    }

    char *end;
    PStask_ID_t tid = strtol(key, &end, 0);
    if (*end) {
	snprintf(l, sizeof(l), "\nkey '%s' not a task ID\n", key);
	return strdup(l);
    }

    PStask_t *task = PStasklist_find(&managedTasks, tid);
    if (!task) {
	snprintf(l, sizeof(l), "\nno task for key %s (recognized as %s)\n",
		 key, PSC_printTID(tid));
	return strdup(l);
    }

    if (task->group != TG_DELEGATE) {
	snprintf(l, sizeof(l), "\ntask %s not delegate\n", PSC_printTID(tid));
	return strdup(l);
    }

    /* print partition */
    strbuf_t buf = strbufNew("\nPartition\n");
    if (task->partitionSize > 0 && task->partition) {
	for (unsigned int s = 0; s < task->partitionSize; s++) {
	    PSpart_slot_t *slt = &task->partition[s];
	    snprintf(l, sizeof(l), "\t%d\t%s\n", slt->node,
		     PSCPU_print_part(slt->CPUset, nBytes));
	    strbufAdd(buf, l);
	}
    } else {
	strbufAdd(buf, "\tnone\n");
    }

    /* print partThrds */
    strbufAdd(buf, "\nHW threads\n");
    if (task->totalThreads > 0 && task->partThrds) {
	unsigned int t;
	PSnodes_ID_t lastNode = -1;
	snprintf(l, sizeof(l), "\t%d of %u threads used:\n", task->usedThreads,
		 task->totalThreads);
	strbufAdd(buf, l);
	for (t=0; t<task->totalThreads; t++) {
	    PSpart_HWThread_t *hwThrd = &task->partThrds[t];
	    if (hwThrd->node != lastNode) {
		lastNode = hwThrd->node;
		snprintf(l, sizeof(l), "\n\t%d", hwThrd->node);
		strbufAdd(buf, l);
	    }
	    snprintf(l, sizeof(l), " %d(%d)", hwThrd->id, hwThrd->timesUsed);
	    strbufAdd(buf, l);
	}
	strbufAdd(buf, "\n");
    } else {
	strbufAdd(buf, "\tnone\n");
    }

    /* print reservations */
    strbufAdd(buf, "\nAssociated reservations\n");
    bool resFound = false;
    list_t *t;
    list_for_each(t, &managedTasks) {
	PStask_t *tsk = list_entry(t, PStask_t, next);
	if (tsk->delegate != task) continue;
	list_t *r;
	list_for_each(r, &tsk->reservations) {
	    PSrsrvtn_t *res = list_entry(r, PSrsrvtn_t, next);
	    resFound = true;
	    snprintf(l, sizeof(l), "\treservation %d @ %s", res->rid,
		     PSC_printTID(tsk->tid));
	    strbufAdd(buf, l);
	    snprintf(l, sizeof(l), " requested by %s \n\t\tholds %d slots"
		     " (%d used, %d released):\n", PSC_printTID(res->requester),
		     res->nSlots, res->nextSlot, res->relSlots);
	    strbufAdd(buf, l);
	}
    }
    if (!resFound) strbufAdd(buf, "\tnone\n");

    /* print pendingReleaseRes */
    snprintf(l, sizeof(l), "\nThere are %d pending RELEASERES messages\n",
	     task->pendingReleaseRes);
    strbufAdd(buf, l);

    return strbufSteal(buf);
}

char * set(char *key, char *val)
{
    char l[128];

    if (!key || !key[0]) {
	snprintf(l, sizeof(l), "\nUsage: 'plugin set %s <tid>' to reset"
		 " resource usage\n", name);
	return strdup(l);
    }

    char *end;
    PStask_ID_t tid = strtol(key, &end, 0);
    if (*end) {
	snprintf(l, sizeof(l), "\nkey '%s' not a task ID\n", key);
	return strdup(l);
    }

    PStask_t *task = PStasklist_find(&managedTasks, tid);
    if (!task) {
	snprintf(l, sizeof(l), "\nno task for key %s (recognized as %s)\n",
		 key, PSC_printTID(tid));
	return strdup(l);
    }

    if (task->group != TG_DELEGATE) {
	snprintf(l, sizeof(l), "\ntask %s not delegate\n", PSC_printTID(tid));
	return strdup(l);
    }

    strbuf_t buf = strbufNew(NULL);
    if (task->totalThreads > 0 && task->partThrds) {
	snprintf(l, sizeof(l), "\n%d of %u threads used:\n", task->usedThreads,
		 task->totalThreads);
	strbufAdd(buf, l);
	for (unsigned int t = 0; t < task->totalThreads; t++) {
	    PSpart_HWThread_t *hwThrd = &task->partThrds[t];
	    if (hwThrd->timesUsed) {
		snprintf(l, sizeof(l), "\tfree %d thread(s) of %d/%d\n",
			 hwThrd->timesUsed, hwThrd->node, hwThrd->id);
		strbufAdd(buf, l);
		task->usedThreads -= hwThrd->timesUsed;
		hwThrd->timesUsed = 0;
	    }
	}
	snprintf(l, sizeof(l), "%d of %u threads used\n", task->usedThreads,
		 task->totalThreads);
	strbufAdd(buf, l);
    } else {
	strbufAdd(buf, "\nno partition to reset\n");
    }

    return strbufSteal(buf);
}
