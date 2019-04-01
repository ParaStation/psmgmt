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
#include "psslurmtypes.h"

typedef enum {
    A_INIT = 0x020,
    A_PROLOGUE_FINISH = 0x022,
    A_RUNNING,
    A_EPILOGUE,
    A_EPILOGUE_FINISH,
    A_EXIT
} AllocState_t;

/**
 * @brief Add a new allocation
 *
 * @param id unique allocation identifier
 *
 * @param packID unique pack identifier
 *
 * @param slurmHosts Slurm compressed host-list
 *
 * @param env environment variables
 *
 * @param uid user ID of the allocation owner
 *
 * @param gid group of the allocation owner
 *
 * @param username username of the allocation owner
 *
 * @return Returns the newly created allocation or a existing allocation
 * with the given @a id
 */
Alloc_t *addAlloc(uint32_t id, uint32_t packID, char *slurmHosts, env_t *env,
		  uid_t uid, gid_t gid, char *username);

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
 * @param visitor Visitor function to be called for each allocation
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
 * @brief Find an allocation
 *
 * @param id unique allocation identifier
 *
 * @return Returns the requested allocation or NULL on error
 */
Alloc_t *findAlloc(uint32_t id);

/**
 * @brief Find an allocation by pack ID
 *
 * @param id unique pack identifier
 *
 * @return Returns the requested allocation or NULL on error
 */
Alloc_t *findAllocByPackID(uint32_t id);

/**
 * @brief Delete an allocation
 *
 * Delete an allocation and its corresponding job and steps.
 *
 * @param id unique allocation identifier
 *
 * @return Returns true on success or false on error
 */
bool deleteAlloc(uint32_t id);

/**
 * @brief Delete all remaining allocations
 */
void clearAllocList(void);

/**
 * @brief Count all allocations
 *
 * @return Returns the number of allocations
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

/**
 * @brief Signal an allocation
 *
 * Send a signal to an allocation including a corresponding job and all steps
 * with the given @a id. All matching steps will be signaled if they are not in
 * state JOB_COMPLETE.  The @reqUID must have the appropriate permissions to
 * send the signal.
 *
 * @param id The allocation ID to send the signal to
 *
 * @param signal The signal to send
 *
 * @param reqUID The UID of the requesting process
 *
 * @return Returns the number of tasks which were signaled or -1
 *  if the @a reqUID is not permitted to signal the tasks
 */
int signalAlloc(uint32_t id, int signal, uid_t reqUID);

/**
 * @brief Convert an allocation state to string
 *
 * @param state The state to convert
 *
 * @return Returns the state as string representation
 */
const char *strAllocState(AllocState_t state);

/**
 * @brief Test if local node is the leader of an allocation
 *
 * @param alloc The allocation to test
 *
 * @return Returns true if the local node is the leader otherwise
 * false
 */
bool isAllocLeader(Alloc_t *alloc);

#endif  /* __PS_PSSLURM_ALLOC */
