/*
 * ParaStation
 *
 * Copyright (C) 2019 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PSSLURM_ACCOUNT
#define __PSSLURM_ACCOUNT

#include <stdint.h>
#include <stdbool.h>

enum {
    TRES_CPU = 0,
    TRES_MEM,
    TRES_ENERGY,
    TRES_NODE,
    TRES_BILLING,
    TRES_FS_DISK,
    TRES_VMEM,
    TRES_PAGES,
    TRES_TOTAL_CNT
};

typedef struct {
    uint64_t in_max; 		/* tres in max usage data */
    uint64_t in_max_nodeid; 	/* tres in max usage data node id */
    uint64_t in_max_taskid; 	/* tres in max usage data task id */
    uint64_t in_min; 		/* tres in min usage data */
    uint64_t in_min_nodeid; 	/* tres in min usage data node id */
    uint64_t in_min_taskid; 	/* tres in min usage data task id */
    uint64_t in_tot; 		/* total in usage (megabytes) */
    uint64_t out_max; 		/* tres out max usage data */
    uint64_t out_max_nodeid; 	/* tres out max usage data node id */
    uint64_t out_max_taskid; 	/* tres out max usage data task id */
    uint64_t out_min; 		/* tres out min usage data */
    uint64_t out_min_nodeid; 	/* tres out min usage data node id */
    uint64_t out_min_taskid; 	/* tres out min usage data task id */
    uint64_t out_tot; 		/* total out usage (megabytes) */
} TRes_Entry_t;

typedef struct {
    uint32_t count; 		/* count of tres in the usage array's */
    uint32_t *ids; 		/* array of tres_count of the tres id's */
    uint64_t *in_max; 		/* tres in max usage data */
    uint64_t *in_max_nodeid; 	/* tres in max usage data node id */
    uint64_t *in_max_taskid; 	/* tres in max usage data task id */
    uint64_t *in_min; 		/* tres in min usage data */
    uint64_t *in_min_nodeid; 	/* tres in min usage data node id */
    uint64_t *in_min_taskid; 	/* tres in min usage data task id */
    uint64_t *in_tot; 		/* total in usage (megabytes) */
    uint64_t *out_max; 		/* tres out max usage data */
    uint64_t *out_max_nodeid; 	/* tres out max usage data node id */
    uint64_t *out_max_taskid; 	/* tres out max usage data task id */
    uint64_t *out_min; 		/* tres out min usage data */
    uint64_t *out_min_nodeid; 	/* tres out min usage data node id */
    uint64_t *out_min_taskid; 	/* tres out min usage data task id */
    uint64_t *out_tot; 		/* total out usage (megabytes) */
} TRes_t;

TRes_t *TRes_new(void);

void TRes_reset_entry(TRes_Entry_t *entry);

bool TRes_set(TRes_t *tres, uint32_t id, TRes_Entry_t *entry);

void TRes_destroy(TRes_t *tres);

void TRes_print(TRes_t *tres);

const char *TRes_ID2Str(uint16_t ID);

#endif
