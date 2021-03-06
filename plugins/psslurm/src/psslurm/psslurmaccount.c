/*
 * ParaStation
 *
 * Copyright (C) 2019 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <string.h>

#include "psslurmaccount.h"
#include "psslurmlog.h"
#include "psslurmproto.h"

#include "pluginmalloc.h"
#include "slurmcommon.h"

#define INF2Z(num) (num == INFINITE64) ? (0) : (num)

TRes_t *TRes_new(void)
{
    TRes_t *tres = umalloc(sizeof(*tres));
    if (tresDBconfig) {
	tres->count = (TRES_TOTAL_CNT > tresDBconfig->count) ?
			TRES_TOTAL_CNT : tresDBconfig->count;
    } else {
	tres->count = TRES_TOTAL_CNT;
    }

    tres->ids = umalloc(sizeof(uint32_t) * tres->count);

    tres->in_max = umalloc(sizeof(uint64_t) * tres->count);
    tres->in_max_nodeid = umalloc(sizeof(uint64_t) * tres->count);
    tres->in_max_taskid = umalloc(sizeof(uint64_t) * tres->count);
    tres->in_min = umalloc(sizeof(uint64_t) * tres->count);
    tres->in_min_nodeid = umalloc(sizeof(uint64_t) * tres->count);
    tres->in_min_taskid = umalloc(sizeof(uint64_t) * tres->count);
    tres->in_tot = umalloc(sizeof(uint64_t) * tres->count);

    tres->out_max = umalloc(sizeof(uint64_t) * tres->count);
    tres->out_max_nodeid = umalloc(sizeof(uint64_t) * tres->count);
    tres->out_max_taskid = umalloc(sizeof(uint64_t) * tres->count);
    tres->out_min = umalloc(sizeof(uint64_t) * tres->count);
    tres->out_min_nodeid = umalloc(sizeof(uint64_t) * tres->count);
    tres->out_min_taskid = umalloc(sizeof(uint64_t) * tres->count);
    tres->out_tot = umalloc(sizeof(uint64_t) * tres->count);

    uint32_t i;
    for (i=0; i<tres->count; i++) {
	if (tresDBconfig && tresDBconfig->count>i) {
	    tres->ids[i] = tresDBconfig->entry[i].id;
	} else {
	    tres->ids[i] = i;
	}

	tres->in_max[i] = INFINITE64;
	tres->in_max_nodeid[i] = INFINITE64;
	tres->in_max_taskid[i] = INFINITE64;
	tres->in_min[i] = INFINITE64;
	tres->in_min_nodeid[i] = INFINITE64;
	tres->in_min_taskid[i] = INFINITE64;
	tres->in_tot[i] = INFINITE64;

	tres->out_max[i] = INFINITE64;
	tres->out_max_nodeid[i] = INFINITE64;
	tres->out_max_taskid[i] = INFINITE64;
	tres->out_min[i] = INFINITE64;
	tres->out_min_nodeid[i] = INFINITE64;
	tres->out_min_taskid[i] = INFINITE64;
	tres->out_tot[i] = INFINITE64;
    }

    return tres;
}

void TRes_reset_entry(TRes_Entry_t *entry)
{
    entry->in_max = INFINITE64;
    entry->in_max_nodeid = INFINITE64;
    entry->in_max_taskid = INFINITE64;
    entry->in_min = INFINITE64;
    entry->in_min_nodeid = INFINITE64;
    entry->in_min_taskid = INFINITE64;
    entry->in_tot = INFINITE64;

    entry->out_max = INFINITE64;
    entry->out_max_nodeid = INFINITE64;
    entry->out_max_taskid = INFINITE64;
    entry->out_min = INFINITE64;
    entry->out_min_nodeid = INFINITE64;
    entry->out_min_taskid = INFINITE64;
    entry->out_tot = INFINITE64;
}

bool TRes_set(TRes_t *tres, uint32_t id, TRes_Entry_t *entry)
{
    uint32_t i;

    if (tresDBconfig && tresDBconfig->count>id) {
	id = tresDBconfig->entry[id].id;
    }

    for (i=0; i<tres->count; i++) {
	if (tres->ids[i] == id) {
	    tres->in_max[i] = entry->in_max;
	    tres->in_max_nodeid[i] = entry->in_max_nodeid;
	    tres->in_max_taskid[i] = entry->in_max_taskid;
	    tres->in_min[i] = entry->in_min;
	    tres->in_min_nodeid[i] = entry->in_min_nodeid;
	    tres->in_min_taskid[i] = entry->in_min_taskid;
	    tres->in_tot[i] = entry->in_tot;

	    tres->out_max[i] = entry->out_max;
	    tres->out_max_nodeid[i] = entry->out_max_nodeid;
	    tres->out_max_taskid[i] = entry->out_max_taskid;
	    tres->out_min[i] = entry->out_min;
	    tres->out_min_nodeid[i] = entry->out_min_nodeid;
	    tres->out_min_taskid[i] = entry->out_min_taskid;
	    tres->out_tot[i] = entry->out_tot;

	    return true;
	}
    }
    return false;
}

static const char *TRes_ID2Str(uint16_t ID)
{
    static char buf[64];

    if (tresDBconfig) {
	uint32_t i;
	for (i=0; i<tresDBconfig->count; i++) {
	    if (tresDBconfig->entry[i].id == ID) {
		return tresDBconfig->entry[i].type;
	    }
	}
    }

    switch (ID) {
	case TRES_CPU:
	    return "TRES_CPU";
	case TRES_MEM:
	    return "TRES_MEM";
	case TRES_ENERGY:
	    return "TRES_ENERGY";
	case TRES_NODE:
	    return "TRES_NODE";
	case TRES_BILLING:
	    return "TRES_BILLING";
	case TRES_FS_DISK:
	    return "TRES_FS_DISK";
	case TRES_VMEM:
	    return "TRES_VMEM";
	case TRES_PAGES:
	    return "TRES_PAGES";
	case TRES_TOTAL_CNT:
	    return "TRES_TOTAL_CNT";
	default:
	    snprintf(buf, sizeof(buf), "%u <Unknown>", ID);
	    return buf;
    }
}

void TRes_print(TRes_t *tres)
{
    uint32_t i;
    for (i=0; i<tres->count; i++) {
	flog("%s id %u in_max %zu in_max_nodeid %zu in_max_taskid %zu\n",
	     TRes_ID2Str(tres->ids[i]), tres->ids[i], INF2Z(tres->in_max[i]),
	     INF2Z(tres->in_max_nodeid[i]), INF2Z(tres->in_max_taskid[i]));
	flog("%s id %u in_min %zu in_min_nodeid %zu in_min_taskid %zu in_tot "
	     "%zu\n", TRes_ID2Str(tres->ids[i]), tres->ids[i],
	     INF2Z(tres->in_min[i]), INF2Z(tres->in_min_nodeid[i]),
	     INF2Z(tres->in_min_taskid[i]), INF2Z(tres->in_tot[i]));
	flog("%s id %u out_max %zu out_max_nodeid %zu out_max_taskid %zu\n",
	     TRes_ID2Str(tres->ids[i]), tres->ids[i], INF2Z(tres->out_max[i]),
	     INF2Z(tres->out_max_nodeid[i]), INF2Z(tres->out_max_taskid[i]));
	flog("%s id %u out_min %zu out_min_nodeid %zu out_min_taskid %zu "
	     "out_tot %zu\n", TRes_ID2Str(tres->ids[i]), tres->ids[i],
	     INF2Z(tres->out_min[i]), INF2Z(tres->out_min_nodeid[i]),
	     INF2Z(tres->out_min_taskid[i]), INF2Z(tres->out_tot[i]));
    }
}

void TRes_destroy(TRes_t *tres)
{
    ufree(tres->ids);

    ufree(tres->in_max);
    ufree(tres->in_max_nodeid);
    ufree(tres->in_max_taskid);
    ufree(tres->in_min);
    ufree(tres->in_min_nodeid);
    ufree(tres->in_min_taskid);
    ufree(tres->in_tot);

    ufree(tres->out_max);
    ufree(tres->out_max_nodeid);
    ufree(tres->out_max_taskid);
    ufree(tres->out_min);
    ufree(tres->out_min_nodeid);
    ufree(tres->out_min_taskid);
    ufree(tres->out_tot);

    ufree(tres);
}
