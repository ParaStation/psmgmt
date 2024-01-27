/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_ENV
#define __PS_SLURM_ENV

#include <stdbool.h>
#include <stdint.h>

#include "list.h"
#include "psenv.h"
#include "pscpu.h"

#include "psslurmjob.h"
#include "psslurmjobcred.h"
#include "psslurmstep.h"

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
 * @brief Initialize the job/user specific jail environment
 *
 * Set variables used by the jail scripts.
 * This function is used by the psslurm forwarder.
 *
 * @param env		The job environment
 * @param user		The user running the job
 * @param stepcpus	CPUs to be used by the step
 * @param jobcpus	CPUs to be used by the job
 * @param gresList	GRes to limit devices
 * @param cred		job credential holding memory constrains
 * @param localNodeId   local node ID
 */
void setJailEnv(const env_t *env, const char *user, const PSCPU_set_t *stepcpus,
		const PSCPU_set_t *jobcpus, list_t *gresList, JobCred_t *cred,
		uint32_t localNodeId);

/**
 * @brief Initialize global jail environment
 *
 * The global jail configuration is set by psslurm at startup
 * in the main psid so all children may access it.
 */
void setGlobalJailEnvironment(void);

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
 * @brief Determine PMI layer to use
 *
 * Determine which PMI layer to utilize within the step @a step. For
 * this, the step's environment is investigated. Unless a different
 * PMI layer is chosen explicitly, the default (MPICH's Simple PMI)
 * will be used.
 *
 * @param step Step to investigate
 *
 * @return Return the PMI type chosen for @a step
 */
pmi_type_t getPMIType(Step_t *step);

/**
 * @brief Strip environment from user variables
 *
 * Remove all user environment variables which are not relevant
 * for spawning of processes via mpiexec and descendants.
 *
 * User variables will be transfered by srun and later merged back
 * into the environment in @a setRankEnv()
 *
 * @param env Environment to strip
 *
 * @param pmi_type Type of PMI to use
 */
void removeUserVars(env_t *env, pmi_type_t pmi_type);

/**
 * @brief Set SLURM_CONF environment
 *
 * Set SLURM_CONF environment variable in Slurm config-less mode.
 *
 * @param env The environment to change, if NULL setenv() is used
 * to change to current environment
 */
void setSlurmConfEnvVar(env_t *env);

#endif  /* __PS_SLURM_ENV */
