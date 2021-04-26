/*
 * ParaStation
 *
 * Copyright (C) 2019-2021 ParTec Cluster Competence Center GmbH, Munich
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
    uint64_t in_max; 		/* TRes in max usage data */
    uint64_t in_max_nodeid; 	/* TRes in max usage data node id */
    uint64_t in_max_taskid; 	/* TRes in max usage data task id */
    uint64_t in_min; 		/* TRes in min usage data */
    uint64_t in_min_nodeid; 	/* TRes in min usage data node id */
    uint64_t in_min_taskid; 	/* TRes in min usage data task id */
    uint64_t in_tot; 		/* total in usage (megabytes) */
    uint64_t out_max; 		/* TRes out max usage data */
    uint64_t out_max_nodeid; 	/* TRes out max usage data node id */
    uint64_t out_max_taskid; 	/* TRes out max usage data task id */
    uint64_t out_min; 		/* TRes out min usage data */
    uint64_t out_min_nodeid; 	/* TRes out min usage data node id */
    uint64_t out_min_taskid; 	/* TRes out min usage data task id */
    uint64_t out_tot; 		/* total out usage (megabytes) */
} TRes_Entry_t;

typedef struct {
    uint32_t count; 		/* count of TRes in the usage array's */
    uint32_t *ids; 		/* array of tres_count of the TRes id's */
    uint64_t *in_max; 		/* TRes in max usage data */
    uint64_t *in_max_nodeid; 	/* TRes in max usage data node id */
    uint64_t *in_max_taskid; 	/* TRes in max usage data task id */
    uint64_t *in_min; 		/* TRes in min usage data */
    uint64_t *in_min_nodeid; 	/* TRes in min usage data node id */
    uint64_t *in_min_taskid; 	/* TRes in min usage data task id */
    uint64_t *in_tot; 		/* total in usage (megabytes) */
    uint64_t *out_max; 		/* TRes out max usage data */
    uint64_t *out_max_nodeid; 	/* TRes out max usage data node id */
    uint64_t *out_max_taskid; 	/* TRes out max usage data task id */
    uint64_t *out_min; 		/* TRes out min usage data */
    uint64_t *out_min_nodeid; 	/* TRes out min usage data node id */
    uint64_t *out_min_taskid; 	/* TRes out min usage data task id */
    uint64_t *out_tot; 		/* total out usage (megabytes) */
} TRes_t;

/**
 * @brief Get a new TRes structure
 *
 * Get a new TRes structure and initialize all fields to INFINITE64.
 * The memory is allocated using @ref umalloc(). The caller is responsible
 * to free the memory using @ref TRes_destroy().
 *
 * @param Returns a pointer to the new TRes structure
 */
TRes_t *TRes_new(void);

/**
 * @brief Reset a TRes entry
 *
 * Reset all values of the give TRes value back to INFINITE64.
 *
 * @param entry The entry to reset
 */
void TRes_reset_entry(TRes_Entry_t *entry);

/**
 * @brief Set an entry in a TRes structure
 *
 * @param tres The TRes structure to change
 *
 * @param id The id of the entry to set
 *
 * @param entry The entry holding the values to set
 *
 * @return Returns true on success and false on error
 */
bool TRes_set(TRes_t *tres, uint32_t id, TRes_Entry_t *entry);

/**
 * @brief Destroy a TRes structure
 *
 * Free all memory from a TRes structure
 */
void TRes_destroy(TRes_t *tres);

/**
 * @brief Print a TRes structure
 *
 * @param tres The TRes structure to print
 */
void TRes_print(TRes_t *tres);

/**
 * @brief Find a TRes ID from type/name
 *
 * @param type The type of the TRes ID
 *
 * @param name The optional name of the TRes ID
 *
 * @return Returns the TRes ID or NO_VAL on error
 */
uint32_t TRes_getID(const char *type, const char *name);

#endif  /* __PSSLURM_ACCOUNT */
