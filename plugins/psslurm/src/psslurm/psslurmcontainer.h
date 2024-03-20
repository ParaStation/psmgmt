/*
 * ParaStation
 *
 * Copyright (C) 2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_CONTAINER
#define __PS_SLURM_CONTAINER

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#include "pstask.h"
#include "psenv.h"

#include "psslurmjob.h"
#include "psslurmcontainertype.h"

/**
 * @brief Destroy a container and free used memory
 *
 * Cleanup files and free used memory of a container.  Before destroying
 * the container it is supposed to be halted using @ref Container_stop().
 *
 * @param ct Container to destroy
 *
 * @return Returns true on success otherwise false is returned
 */
bool Container_destroy(Slurm_Container_t *ct);

/**
 * @brief Create a new container
 *
 * @param bundle Path to OCI bundle
 *
 * @param jobid Unique job identifier
 *
 * @param stepid Unique step identifier
 *
 * @param username Username of job/step owner
 *
 * @param uid User ID of job/step owner
 *
 * @param gid Group ID of job/step owner
 *
 * @return Returns the new created container on success otherwise
 * NULL is returned
 */
Slurm_Container_t *Container_new(const char *bundle, uint32_t jobid,
				 uint32_t stepid, const char *username,
				 uid_t uid, gid_t gid);

/**
 * @brief Initialize a container for a given job
 *
 * Called in batch forwarder as root to initialize an OCI container for
 * a job.
 *
 * @param job Job executed in container to initialize
 */
void Container_jobInit(Job_t *job);

/**
 * @brief Initialize a container for a given task
 *
 * Called in PSIDHOOK_EXEC_CLIENT as root to initialize an OCI container for
 * a task.
 *
 * @param ct Container to initialize
 *
 * @param task Task to be executed inside the container
 *
 * @param tty Allocate a tty for the task if true
 *
 * @return Returns true on success otherwise false is returned
 */
bool Container_taskInit(Slurm_Container_t *ct, PStask_t *task, bool tty);

/**
 * @brief Let runtime start an OCI container
 *
 * Called in PSIDHOOK_EXEC_CLIENT_EXEC hook as job owner or in the jobscript
 * forwarder and does *not* return.
 *
 * It starts a job/task inside a container which was initialized
 * using @ref Container_taskInit() or @ref Container_jobInit().
 *
 * @param ct Container to be execute
 * */
__attribute__ ((noreturn))
void Container_run(Slurm_Container_t *ct);

/**
 * @brief Let container runtime stop and remove the container
 *
 * @param ct Container to stop
 */
void Container_stop(Slurm_Container_t *ct);

#endif  /* __PS_SLURM_CONTAINER */
