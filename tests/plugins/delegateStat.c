/*
 * ParaStation
 *
 * Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>

#include "psidutil.h"
#include "psidplugin.h"
#include "psidtask.h"

#include "plugin.h"
#include "pluginmalloc.h"

int requiredAPI = 112;

char name[] = "delegateStat";

int version = 100;

plugin_dep_t dependencies[] = {
    { NULL, 0 } };

//int initialize(void){}

//void finalize(void){}


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
    char *buf = NULL;
    size_t bufSize = 0;

    char l[128];

    if (!key[0]) {
	snprintf(l, sizeof(l), "\nUsage: 'plugin show %s key <tid>'\n", name);
	return strdup(l);
    }

    if (sscanf(key, "%u", &tid) != 1) {
	snprintf(l, sizeof(l), "\nkey '%s' not a task ID\n", key);
	return strdup(l);
    }

    task = PStasklist_find(&managedTasks, tid);
    if (!task) {
	snprintf(l, sizeof(l), "\nno task %s\n", PSC_printTID(tid));
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
		     PSCPU_print(slt->CPUset));
	    str2Buf(l, &buf, &bufSize);
	}
    } else {
	str2Buf("\tnone\n", &buf, &bufSize);
    }

    /* print partThrds */
    str2Buf("\nHW threads", &buf, &bufSize);
    if (task->totalThreads > 0 && task->partThrds) {
	unsigned int t;
	PSnodes_ID_t lastNode = -1;
	snprintf(l, sizeof(l), "%u of %u threads used:\n", task->usedThreads,
		 task->totalThreads);
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
	str2Buf("\n\tnone\n", &buf, &bufSize);
    }

    /* print reservations */
    str2Buf("\nReservations\n", &buf, &bufSize);
    if (list_empty(&task->reservations)) {
	str2Buf("\tnone\n", &buf, &bufSize);
    } else {
	list_t *r;
	list_for_each(r, &task->reservations) {
	    PSrsrvtn_t *res = list_entry(r, PSrsrvtn_t, next);
	    snprintf(l, sizeof(l), "\treservation %d requested by %s holds %d"
		     " slots (%d used, %d released):\n", res->rid,
		     PSC_printTID(res->requester), res->nSlots, res->nextSlot,
		     res->relSlots);
	    str2Buf(l, &buf, &bufSize);
	    int s;
	    for (s = 0; s < res->nSlots; s++) {
		PSpart_slot_t *slt = &res->slots[s];
		snprintf(l, sizeof(l), "\t%d\t%s\n", slt->node,
			 PSCPU_print(slt->CPUset));
		str2Buf(l, &buf, &bufSize);
	    }
	}
    }


    /* print spawnNodes */
    /* @todo */

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
    char l[128];

    if (!key[0]) {
	snprintf(l, sizeof(l), "\nUsage: 'plugin set %s <tid>' to reset"
		 " resource usage\n", name);
	return strdup(l);
    }

    if (sscanf(key, "%i", &tid) != 1) {
	snprintf(l, sizeof(l), "\nkey '%s' not a task ID\n", key);
	return strdup(l);
    }

    task = PStasklist_find(&managedTasks, tid);
    if (!task) {
	snprintf(l, sizeof(l), "\nno task %s\n", PSC_printTID(tid));
	return strdup(l);
    }

    if (task->group != TG_DELEGATE) {
	snprintf(l, sizeof(l), "\ntask %s not delegate\n", PSC_printTID(tid));
	return strdup(l);
    }

    snprintf(l, sizeof(l), "\n\tNot yet implemented\n");

    return strdup(l);
}
