/*
 * ParaStation
 *
 * Copyright (C) 2014-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_SLURM_FORWARDER
#define __PS_SLURM_FORWARDER

#include "psslurmalloc.h"
#include "psslurmjob.h"
#include "psslurmbcast.h"

/**
 * @brief Execute a step leader forwarder
 *
 * The step leader forwarder is started on the mother superior
 * of the step. The main purpose of the forwarder is to start
 * the mpiexec executable to spawn all compute processes.
 * Additional tasks include handling of node local I/O streams
 * and calling various spank hooks.
 *
 * @param step The step to execute the forwarder for
 *
 * @return Returns true on success otherwise false is returned
 */
bool execStepLeader(Step_t *step);

/**
 * @brief Execute a batch job forwarder
 *
 * @param job The job to execute the forwarder for
 *
 * @return Returns true on success otherwise false is returned
 */
bool execBatchJob(Job_t *job);

/**
 * @brief Execute a BCast forwarder
 *
 * @param bcast The BCast structure to execute the forwarder for
 *
 * @return Returns true on success otherwise false is returned
 */
bool execBCast(BCast_t *bcast);

/**
 * @brief Execute a step follower forwarder
 *
 * The step follower forwarder is started on the all nodes
 * of the step with the exception of the mother superior.
 * The main purpose of the forwarder is handling of node
 * local I/O streams. Additionally various spank hooks are
 * called.
 *
 * @param step The step to execute the forwarder for
 *
 * @return Returns true on success otherwise false is returned
 */
bool execStepFollower(Step_t *step);

/**
 * @brief Execute an epilogue finalize forwarder
 *
 * The epilogue finalize forwarder is started on the mother
 * superior of an allocation. The forwarder executes a
 * finalize script which is used to compress log files.
 *
 * @param alloc The allocation to execute the forwarder for
 *
 * @return Returns true on success otherwise false is returned
 */
bool execEpilogueFin(Alloc_t *alloc);

/**
 * @brief Handle hook PSIDHOOK_EXEC_CLIENT
 *
 * Used to set Slurm rlimits in child processes.
 *
 * @param data Task structure of the client
 *
 * @return Always returns 0
 */
int handleExecClient(void *data);

/**
 * @brief Handle hook PSIDHOOK_EXEC_CLIENT_USER
 *
 * Used to change the child's environment, set memory binding,
 * execute the task prologue and setup the ptrace interface.
 *
 * Additionally the spank hook SPANK_TASK_INIT is called.
 *
 * @param data Task structure of the client
 *
 * @return Returns 0 on success or -1 otherwise
 */
int handleExecClientUser(void * data);

/**
 * @brief Handle hook PSIDHOOK_FRWRD_INIT
 *
 * Used to initialize the PMI interface and setup the ptrace interface.
 *
 * Additionally the spank hook SPANK_TASK_POST_FORK is called.
 *
 * @param data Task structure of the client
 *
 * @return Always returns 0
 */
int handleForwarderInit(void * data);

/**
 * @brief Handle hook PSIDHOOK_FRWRD_CLNT_RLS
 *
 * Used to execute the task epilogue.
 *
 * @param data Task structure of the client
 *
 * @return Always returns 0
 */
int handleForwarderClientStatus(void * data);

/**
 * @brief Find a step using the passed environment
 *
 * Find step structure by using the values of SLURM_STEPID and SLURM_JOBID
 * in the passed environment. If NULL is passed as environment or one of the
 * variables is not found, the values used are 0 as jobid and
 * SLURM_BATCH_SCRIPT as stepid.
 *
 * @param jobid_out Holds the jobid of the step on return if not NULL
 *
 * @param stepid_out Holds the stepid of the step on return if not NULL
 *
 * @return On success the found step is returned or NULL otherwise
 */
Step_t * __findStepByEnv(char **environ, uint32_t *jobid_out,
			 uint32_t *stepid_out, bool isAdmin,
			 const char *func, const int line);

#define findStepByEnv(environ, jobid_out, stepid_out, isAdmin) \
	    __findStepByEnv(environ, jobid_out, stepid_out, isAdmin, \
			    __func__, __LINE__)

/**
 * @brief Handle hook PSIDHOOK_FRWRD_CLNT_RES
 *
 * Used to forward the client exit status in spank hook SPANK_TASK_EXIT.
 *
 * @param data Exit status of the client
 *
 * @return Always returns 0
 */
int handleFwRes(void * data);

#endif
