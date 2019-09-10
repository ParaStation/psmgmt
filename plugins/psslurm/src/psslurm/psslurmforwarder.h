/*
 * ParaStation
 *
 * Copyright (C) 2014-2019 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_SLURM_FORWARDER
#define __PS_SLURM_FORWARDER

#include "psslurmjob.h"
#include "psslurmbcast.h"

typedef enum {
    CMD_PRINT_CHILD_MSG = 100,
    CMD_ENABLE_SRUN_IO,
    CMD_FW_FINALIZE,
    CMD_REATTACH_TASKS,
    CMD_INFO_TASKS,
    CMD_STEP_TIMEOUT,
    CMD_BROKE_IO_CON,
} PSSLURM_Fw_Cmds_t;

/**
 * @brief Execute a step forwarder
 *
 * @param step The step to execute the forwarder for
 *
 * @return Returns true on success otherwise false is returned
 */
bool execStep(Step_t *step);

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
 * @brief Execute a step I/O forwarder
 *
 * @param step The step to execute the forwarder for
 *
 * @return Returns true on success otherwise false is returned
 */
bool execStepIO(Step_t *step);

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
 * @param data Task structure of the client
 *
 * @return Always returns 0
 */
int handleForwarderInit(void * data);

/**
 * @brief Handle hook PSIDHOOK_FRWRD_CLIENT_STAT
 *
 * Used to execute the task epilogue.
 *
 * @param data Task structure of the client
 *
 * @return Always returns 0
 */
int handleForwarderClientStatus(void * data);

#endif
