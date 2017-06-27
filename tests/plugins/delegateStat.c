/*
 * ParaStation
 *
 * Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdlib.h>

#include "pscommon.h"
#include "pscpu.h"
#include "psidutil.h"
#include "psidplugin.h"
#include "psidtask.h"
#include "psidnodes.h"

#include "plugin.h"
#include "pluginmalloc.h"

int requiredAPI = 112;

char name[] = "delegateStat";

int version = 100;

plugin_dep_t dependencies[] = {
    { NULL, 0 } };

char * help(void)
{
    return strdup("\tPlugin to peek into existing delegate tasks.\n"
		  "\tUse the plugin's show directive to take a peek\n"
		  "\tUse the plugin's set directive to reset resource usage\n");
}

char * show(char *key)
{
    PStask_ID_t tid;
    PStask_t *task;
    char *buf = NULL, *end;
    size_t bufSize = 0;
    bool resFound = false;
    char l[128];
    uint16_t nBytes = PSCPU_bytesForCPUs(PSIDnodes_getVirtCPUs(PSC_getMyID()));

    if (!key || !key[0]) {
	snprintf(l, sizeof(l), "\nUsage: 'plugin show %s key <tid>'\n", name);
	return strdup(l);
    }

    tid = strtol(key, &end, 0);
    if (*end) {
	snprintf(l, sizeof(l), "\nkey '%s' not a task ID\n", key);
	return strdup(l);
    }

    task = PStasklist_find(&managedTasks, tid);
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
    str2Buf("\nPartition\n", &buf, &bufSize);
    if (task->partitionSize > 0 && task->partition) {
	unsigned int s;
	for (s=0; s < task->partitionSize; s++) {
	    PSpart_slot_t *slt = &task->partition[s];
	    snprintf(l, sizeof(l), "\t%d\t%s\n", slt->node,
		     PSCPU_print_part(slt->CPUset, nBytes));
	    str2Buf(l, &buf, &bufSize);
	}
    } else {
	str2Buf("\tnone\n", &buf, &bufSize);
    }

    /* print partThrds */
    str2Buf("\nHW threads\n", &buf, &bufSize);
    if (task->totalThreads > 0 && task->partThrds) {
	unsigned int t;
	PSnodes_ID_t lastNode = -1;
	snprintf(l, sizeof(l), "\t%u of %u threads used:\n", task->usedThreads,
		 task->totalThreads);
	str2Buf(l, &buf, &bufSize);
	for (t=0; t<task->totalThreads; t++) {
	    PSpart_HWThread_t *hwThrd = &task->partThrds[t];
	    if (hwThrd->node != lastNode) {
		lastNode = hwThrd->node;
		snprintf(l, sizeof(l), "\n\t%d", hwThrd->node);
		str2Buf(l, &buf, &bufSize);
	    }
	    snprintf(l, sizeof(l), " %d(%d)", hwThrd->id, hwThrd->timesUsed);
	    str2Buf(l, &buf, &bufSize);
	}
	str2Buf("\n", &buf, &bufSize);
    } else {
	str2Buf("\tnone\n", &buf, &bufSize);
    }

    /* print reservations */
    str2Buf("\nAssociated reservations\n", &buf, &bufSize);
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
	    str2Buf(l, &buf, &bufSize);
	    snprintf(l, sizeof(l), " requested by %s \n\t\tholds %d slots"
		     " (%d used, %d released):\n", PSC_printTID(res->requester),
		     res->nSlots, res->nextSlot, res->relSlots);
	    str2Buf(l, &buf, &bufSize);
	}
    }
    if (!resFound) str2Buf("\tnone\n", &buf, &bufSize);

    /* print pendingReleaseRes */
    snprintf(l, sizeof(l), "\nThere are %d pending RELEASERES messages\n",
	     task->pendingReleaseRes);
    str2Buf(l, &buf, &bufSize);

    return buf;
}

char * set(char *key, char *val)
{
    PStask_ID_t tid;
    PStask_t *task;
    char *buf = NULL, *end;
    size_t bufSize = 0;
    char l[128];

    if (!key || !key[0]) {
	snprintf(l, sizeof(l), "\nUsage: 'plugin set %s <tid>' to reset"
		 " resource usage\n", name);
	return strdup(l);
    }

    tid = strtol(key, &end, 0);
    if (*end) {
	snprintf(l, sizeof(l), "\nkey '%s' not a task ID\n", key);
	return strdup(l);
    }

    task = PStasklist_find(&managedTasks, tid);
    if (!task) {
	snprintf(l, sizeof(l), "\nno task for key %s (recognized as %s)\n",
		 key, PSC_printTID(tid));
	return strdup(l);
    }

    if (task->group != TG_DELEGATE) {
	snprintf(l, sizeof(l), "\ntask %s not delegate\n", PSC_printTID(tid));
	return strdup(l);
    }

    if (task->totalThreads > 0 && task->partThrds) {
	unsigned int t;
	snprintf(l, sizeof(l), "\n%u of %u threads used:\n", task->usedThreads,
		 task->totalThreads);
	str2Buf(l, &buf, &bufSize);
	for (t=0; t<task->totalThreads; t++) {
	    PSpart_HWThread_t *hwThrd = &task->partThrds[t];
	    if (hwThrd->timesUsed) {
		snprintf(l, sizeof(l), "\tfree %u thread(s) of %d/%d\n",
			 hwThrd->timesUsed, hwThrd->node, hwThrd->id);
		str2Buf(l, &buf, &bufSize);
		task->usedThreads -= hwThrd->timesUsed;
		hwThrd->timesUsed = 0;
	    }
	}
	snprintf(l, sizeof(l), "%u of %u threads used\n", task->usedThreads,
		 task->totalThreads);
	str2Buf(l, &buf, &bufSize);
    } else {
	str2Buf("\nno partition to reset\n", &buf, &bufSize);
    }

    return buf;
}
