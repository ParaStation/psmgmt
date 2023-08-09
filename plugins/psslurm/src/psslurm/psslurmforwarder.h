/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_FORWARDER
#define __PS_SLURM_FORWARDER

#include <stdbool.h>

#include "pluginforwarder.h"
#include "pluginstrv.h"

#include "psslurmalloc.h"
#include "psslurmbcast.h"
#include "psslurmenv.h"
#include "psslurmjob.h"
#include "psslurmstep.h"

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
 * @brief Build up starter argument vector
 *
 * Build up the argument vector of the helper utilized in order to
 * start the actual tasks. Usually mpiexec or one of its descendants
 * is used and the actual argument vector is mostly based on the
 * information found in the forwarder data @a fwData. Additionally,
 * the content of the argument vector will be influence by the chosen
 * PMI type @a pmiType. The argument vector will be created in @a
 * argV. The caller is responsible for cleaning up the dynamic memory
 * in @a argV when it is no longer required.
 *
 * @param fwData Forwarder data of the step to investigate
 *
 * @param argV Argument vector to build
 *
 * @param pmiType Flag to signal which PMI type to use
 *
 * @return No return value
 */
void buildStartArgv(Forwarder_Data_t *fwData, strv_t *argV, pmi_type_t pmiType);

/**
 * @brief Handle hook PSIDHOOK_EXEC_FORWARDER
 *
 * Used to prepare the psidforwarders environment
 *
 * @param data Task structure of the client
 *
 * @return Always returns 0
 */
int handleHookExecFW(void *data);

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
 * @brief Handle hook PSIDHOOK_FRWRD_CLNT_RES
 *
 * Used to forward the client exit status in spank hook SPANK_TASK_EXIT.
 *
 * @param data Exit status of the client
 *
 * @return Always returns 0
 */
int handleFwRes(void * data);

/**
 * @brief Handle hook PSIDHOOK_EXEC_CLIENT_PREP
 *
 * Used to call Spank hook SPANK_TASK_INIT.
 *
 * @param data Task structure of the client
 *
 * @return Returns 0 on success or -1 otherwise
 */
int handleExecClientPrep(void *data);

/**
 * @brief Handle hook PSIDHOOK_LAST_CHILD_GONE
 *
 * Tell KVS provider resulting from a spawn to cease operation.
 *
 * @param data Points to task structure without children left
 *
 * @return Returns -1 in case of error (data == NULL) or 0 otherwise
 */
int handleLastChildGone(void *data);

/**
 * @brief Handle hook PSIDHOOK_LAST_RESRELEASED
 *
 * Tell step forwarders resulting from a spawn to possibly cease
 * operation.
 *
 * @param data Points to reservation structure with all utilized
 * resources released
 *
 * @return Returns -1 in case of error (data == NULL) or 0 otherwise
 */
int handleResReleased(void *data);


#endif
