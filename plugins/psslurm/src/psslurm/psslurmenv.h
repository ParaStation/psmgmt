/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_ENV
#define __PS_SLURM_ENV

#include "psenv.h"
#include "pscpu.h"

#include "psslurmjob.h"

/** Type to distinguish between different PMI environments */
typedef enum pmi_type {
	PMI_TYPE_NONE,
	PMI_TYPE_DEFAULT,
	PMI_TYPE_PMIX
} pmi_type_t;

/** environment filter for prologue/epilogue execution */
extern char **envFilter;

/**
 * @brief Initialize the environment filter
 *
 * Initialize the environment filter from the psslurm configuration.
 * It will be applied to the environment of prologue and epilogue
 * scripts.
 *
 * @return Returns true on success and false on error
 */
bool initEnvFilter(void);

/**
 * @brief Free all memory used by the environment filter
 */
void freeEnvFilter(void);

/**
 * @brief Initialize the jail environment
 *
 * Set variables used by the jail scripts.
 * This function is used by the psslurm forwarder.
 *
 * @param env       The job environment
 * @param user      The user running the job
 * @param stepcpus  CPUs to be used by the step
 * @param jobcpus   CPUs to be used by the job
 */
void setJailEnv(const env_t *env, const char *user,
	const PSCPU_set_t *stepcpus, const PSCPU_set_t *jobcpus);

/**
 * @brief Initialize a job environment
 *
 * Do initial setup of a job environment at an early stage.
 * Selected variables will be used for the prologue and epilogue.
 *
 * @param job The job holding the environment to initialize
 */
void initJobEnv(Job_t *job);

/**
 * @brief Setup environment for a batch job
 *
 * Set missing environment variables missing for the execution
 * of a job. This includes the PID of the job forwarder.
 *
 * @param job The job to setup the environment for
 */
void setJobEnv(Job_t *job);

/**
 * @brief Setup environment for a step
 *
 * Set common environment variables for all ranks in a step.
 *
 * @param step The step to setup the environment for
 */
void setStepEnv(Step_t *step);

/**
 * @brief Setup an environment for a specific rank
 *
 * @param rank The rank to use
 *
 * @param step The step holding the environment to setup
 */
void setRankEnv(int32_t rank, Step_t *step);

/**
 * @brief Strip environment from user variables
 *
 * Remove all user environment variables which are not relevant
 * for spawning of processes via mpiexec.
 *
 * @param env The environment to strip
 *
 * @param pmi_type Type of PMI to use
 */
void removeUserVars(env_t *env, pmi_type_t pmi_type);

#endif  /* __PS_SLURM_ENV */
