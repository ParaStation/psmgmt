/*
 * ParaStation
 *
 * Copyright (C) 2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_PSSLURM_ALLOC
#define __PS_PSSLURM_ALLOC

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "list.h"
#include "psnodes.h"
#include "psenv.h"
#include "pstaskid.h"

typedef enum {
    A_INIT = 0x010,
    A_PROLOGUE,
    A_RUNNING,
    A_EPILOGUE
} AllocState_t;

typedef struct {
    list_t next;              /**< used to put into some allocation-lists */
    uint32_t id;
    uid_t uid;
    gid_t gid;
    uint32_t nrOfNodes;
    PSnodes_ID_t *nodes;
    char *slurmHosts;	      /* Slurm compressed hostlist (SLURM_NODELIST) */
    env_t env;
    env_t spankenv;
    uint8_t terminate;
    int state;
    char *username;
    time_t firstKillRequest;
    PStask_ID_t motherSup;
    time_t start_time;        /**< time the allocation started */
    uint32_t localNodeId;
} Alloc_t;

/**
 * @doctodo
 */
Alloc_t *addAlloc(uint32_t id, uint32_t nrOfNodes, char *slurmHosts,
		       env_t *env, env_t *spankenv, uid_t uid, gid_t gid,
		       char *username);

/**
 * @brief Visitor function
 *
 * Visitor function used by @ref traverseAllocs() in order to visit
 * each allocation currently registered.
 *
 * The parameters are as follows: @a allocation points to the allocation to
 * visit. @a info points to the additional information passed to @ref
 * traverseAllocs() in order to be forwarded to each allocation.
 *
 * If the visitor function returns true the traversal will be
 * interrupted and @ref traverseAllocs() will return to its calling
 * function.
 */
typedef bool AllocVisitor_t(Alloc_t *alloc, const void *info);

/**
 * @brief Traverse all allocations
 *
 * Traverse all allocations by calling @a visitor for each of the registered
 * allocations. In addition to a pointer to the current allocation @a info is
 * passed as additional information to @a visitor.
 *
 * If @a visitor returns true, the traversal will be stopped
 * immediately and true is returned to the calling function.
 *
 * @param visitor Visitor function to be called for each alloc
 *
 * @param info Additional information to be passed to @a visitor while
 * visiting the allocations
 *
 * @return If the visitor returns true, traversal will be stopped and
 * true is returned. If no visitor returned true during the traversal
 * false is returned.
 */
bool traverseAllocs(AllocVisitor_t visitor, const void *info);

/**
 * @doctodo
 */
Alloc_t *findAlloc(uint32_t id);

/**
 * @doctodo
 */
int deleteAlloc(uint32_t id);

/**
 * @doctodo
 */
void clearAllocList(void);

/**
 * @doctodo
 */
int countAllocs(void);

/**
 * @brief Send a signal to all allocations
 *
 * Send a signal to all allocations. All tasks of the allocations
 * will be signaled if the job-state is not JOB_COMPLETE. The signals are
 * send with the UID of root.
 *
 * @param signal The signal to send
 *
 * @return Returns the number of tasks signaled.
 */
int signalAllocs(int signal);

#endif  /* __PS_PSSLURM_ALLOC */
