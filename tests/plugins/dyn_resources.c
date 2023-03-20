/*
 * ParaStation
 *
 * Copyright (C) 2015-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "psattribute.h"
#include "pscpu.h"
#include "pspartition.h"
#include "psreservation.h"
#include "pstask.h"
#include "timer.h"

#include "plugin.h"
#include "psidhook.h"
#include "psidpartition.h"
#include "psidplugin.h"
#include "psidutil.h"

/* We'll need the dynamic resource management stuff */
int requiredAPI = 112;

char name[] = "dyn_resources";

int version = 100;

plugin_dep_t dependencies[] = {
    { NULL, 0 } };

static int resTimer = -1;

static PSrsrvtn_t *res = NULL;

/* My pool of threads. All reside on node 2 */
#define MAXTHREADS 4
#define THREAD_NODE 2

static int usedThreads[MAXTHREADS];

void provideSlots(void)
{
    static PSpart_slot_t slots[MAXTHREADS];
    int numSlot = 0, numThread = 0, s, min, max;
    PStask_ID_t tid;
    PSrsrvtn_ID_t rid;

    if (resTimer > -1) {
	Timer_remove(resTimer);
	PSID_log(-1, "%s: delete timer %d\n", name, resTimer);
	resTimer = -1;
    }

    if (!res) {
	PSID_log(-1, "%s: %s: No reservation\n", name, __func__);
	return;
    }

    min = res->nMin - res->nSlots;
    max = res->nMax - res->nSlots;

    slots[numSlot].node = THREAD_NODE;
    PSCPU_clrAll(slots[numSlot].CPUset);

    for (s = 0; s < MAXTHREADS && numSlot < max; s++) {
	if (usedThreads[s]) continue;
	PSID_log(-1, "%s: %s: add HW-thread (%d/%d) to slot\n", name, __func__,
		 THREAD_NODE, s);
	PSCPU_setCPU(slots[numSlot].CPUset, s);
	usedThreads[s] = 1;
	numThread++;
	if (numThread == res->tpp) {
	    numSlot++;
	    slots[numSlot].node = THREAD_NODE;
	    PSCPU_clrAll(slots[numSlot].CPUset);
	    numThread = 0;
	}
    }

    tid = res->task;
    rid = res->rid;

    res = NULL;

    if (numSlot >= min) {
	PSID_log(-1, "%s: %s: Got %d of [%d..%d] slots\n",
		 name, __func__, numSlot, min, max);
	PSIDpart_extendRes(tid, rid, numSlot, slots);
    } else {
	int frd = 0;
	for (s = 0; s < numSlot + 1; s++) {
	    int t;
	    for (t = 0; t < MAXTHREADS; t++) {
		if (PSCPU_isSet(slots[s].CPUset, t)) {
		    usedThreads[t] = 0;
		    frd++;
		}
	    }
	}
	PSID_log(-1, "%s: %s: Bookkeeping minus %d\n", name, __func__, frd);
	PSID_log(-1, "%s: %s: Only found %d of minimum %d slots\n",
		 name, __func__, numSlot, min);
	PSIDpart_extendRes(tid, rid, 0, NULL);
    }
}

int handleDynReservation(void *resPtr)
{
    struct timeval timeout = {1, 0};
    int min, max;

    if (!resPtr) {
	PSID_log(-1, "%s: %s: No reservation given\n", name, __func__);
	return 0;
    }

    if (res) {
	PSrsrvtn_t *r = resPtr;

	PSID_log(-1, "%s: %s: Pending reservation %#x\n", name, __func__,
		 res->rid);
	PSIDpart_extendRes(r->task, r->rid, 0, NULL);
	return 1;
    }

    /* Keep the reservation somewhere */
    res = resPtr;

    min = res->nMin - res->nSlots;
    if (min < 1) min = 1;
    max = res->nMax - res->nSlots;

    PSID_log(-1, "%s: %s: Try to reserve %d to %d slots of type '%s' with"
	     " %d threads for reservation ID %#x\n", name, __func__, min, max,
	     Attr_print(res->hwType), res->tpp, res->rid);

    if (min > MAXTHREADS) {
	/* Unsuccessful: No slots to be provided */
	PSIDpart_extendRes(res->task, res->rid, 0, NULL);
	res = NULL;
    } else {
	/* This timer mimicks waiting for the actual resource management */
	resTimer = Timer_register(&timeout, provideSlots);
	PSID_log(-1, "%s: timer %d\n", name, resTimer);
    }

    return 1;
}

int handleDynRelease(void *dynResPtr)
{
    PSrsrvtn_dynRes_t *dynRes = dynResPtr;

    if (!dynResPtr) {
	PSID_log(-1, "%s: %s: No to be released slot given\n", name, __func__);
	return 0;
    }

    PSID_log(-1, "%s: %s: release: (%d/%s) of reservation %#x\n",
	     name, __func__, dynRes->slot.node,
	     PSCPU_print_part(dynRes->slot.CPUset, 4), dynRes->rid);

    if (dynRes->slot.node == THREAD_NODE) {
	int t, freed = 0;
	for (t=0; t<MAXTHREADS; t++) {
	    if (PSCPU_isSet(dynRes->slot.CPUset, t)) {
		usedThreads[t] = 0;
		freed++;
	    }
	}
	PSID_log(-1, "%s: %s: free %d thread(s)\n", name, __func__, freed);
    }

    return 1;
}

static void unregisterHooks(void)
{
    /* unregister hooks */
    if (!PSIDhook_del(PSIDHOOK_XTND_PART_DYNAMIC, handleDynReservation)) {
	PSID_log(-1, "unregister 'PSIDHOOK_XTND_PART_DYNAMIC' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_RELS_PART_DYNAMIC, handleDynRelease)) {
	PSID_log(-1, "unregister 'PSIDHOOK_RELS_PART_DYNAMIC' failed\n");
    }
}

int initialize(FILE *logfile)
{
    /* register needed hooks */
    if (!PSIDhook_add(PSIDHOOK_XTND_PART_DYNAMIC, handleDynReservation)) {
	PSID_log(-1, "%s: 'PSIDHOOK_XTND_PART_DYNAMIC' registration failed\n",
		 name);
	goto INIT_ERROR;
    }

    if (!PSIDhook_add(PSIDHOOK_RELS_PART_DYNAMIC, handleDynRelease)) {
	PSID_log(-1, "%s: 'PSIDHOOK_RELS_PART_DYNAMIC' registration failed\n",
		 name);
	goto INIT_ERROR;
    }

    for (int t = 0; t < MAXTHREADS; t++) usedThreads[t] = 0;

    PSID_log(-1, "%s: (%i) successfully started\n", name, version);
    return 0;

INIT_ERROR:
    unregisterHooks();
    return 1;
}


void finalize(void)
{
    PSID_log(-1, "%s: %s\n", name, __func__);
    PSIDplugin_unload(name);
}

void cleanup(void)
{
    PSID_log(-1, "%s: %s\n", name, __func__);
    unregisterHooks();
    PSID_log(-1, "%s: Done\n", name);
}

char * help(char *key)
{
    char *helpText =
	"\tThis is some dummy plugin mimicking dynamic resource handling.\n"
	"\tIt will just add by chance some random resources to a given "
	"request.\n";

    return strdup(helpText);
}
