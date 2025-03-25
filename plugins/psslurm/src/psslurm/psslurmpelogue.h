/*
 * ParaStation
 *
 * Copyright (C) 2015-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_PELOGUE
#define __PS_SLURM_PELOGUE

#include <stdbool.h>

#include "pstask.h"
#include "peloguetypes.h"

#include "psslurmalloc.h"
#include "psslurmstep.h"

/**
 * @brief Start a local prologue or epilogue for an allocation
 *
 * @param alloc Allocation to start the pelogue for
 *
 * @param type If true a prologue is started otherwise
 * an epilogue is started
 *
 * @return Returns true on success and false otherwise
 */
bool startPElogue(Alloc_t *alloc, PElogueType_t type);

/**
 * @brief Handle hook PSIDHOOK_PELOGUE_START
 *
 * Save various information from the slurmctld prologue environment and
 * add an allocation.
 *
 * @param data Pointer to PElogueChild structure
 *
 * @return Returns 0 on success or -1 otherwise
 */
int handleLocalPElogueStart(void *data);

/**
 * @brief Handle local prologue/epilogue finish
 *
 * This hook will be called every time a local prologue/epilogue
 * script finished executing. It is used to allow or revoke SSH
 * access to the local node, start step I/O forwarders and set the node
 * offline in case of an error.
 *
 * @param data Pointer to PElogueChild structure
 *
 * @return Always returns 0
 */
int handleLocalPElogueFinish(void *data);

/**
 * @brief Handle hook PSIDHOOK_PELOGUE_PREPARE
 *
 * Used for the spank hooks SPANK_JOB_PROLOG and SPANK_JOB_EPILOG.
 *
 * @param data PElogueChild structure
 *
 * @return Always returns 0
 */
int handlePEloguePrepare(void *data);

/**
 * @brief Start a task prologue or epilogue
 *
 * A task prologue is started in PSIDHOOK_EXEC_CLIENT_USER
 *
 * For a task prologue this function is called right before starting
 * the user's executable. The task prologues are executed in the same
 * process that will execv() the client executable without the use of
 * an additional pluginforwarder.
 *
 * First a task prologue that might be defined in slurm.conf utilizing
 * the TaskProlog option is executed. Then a task prologue defined via
 * srun's --task-prolog option that is part of @a step is started.
 *
 *
 * A task epilogue is started in PSIDHOOK_FRWRD_CLNT_RLS
 *
 * For a epilogue this function is called right after the user's
 * executable exited. The task epilogues are executed directly in the
 * psidforwarder process without the use of a pluginforwarder.
 *
 * First a task epilogue that might be defined in slurm.conf utilizing
 * the TaskEpilog option is executed. Then a task epilogue defined via
 * srun's --task-epilog option that is part of @a step is started.
 *
 *
 * @param step Step to start a task prologue/epilogue for
 *
 * @param task PS task structure
 *
 * @param type Specifies to execute a task prologue or epilogue
 *
 * @return No return value
 */
void startTaskPElogue(Step_t *step, PStask_t *task, PElogueType_t type);

/**
 * @brief Finalize an epilogue on the allocation leader node
 *
 * If all sister nodes send the result of their epilogue
 * to mother superior the allocation will be freed.
 *
 * @param alloc Allocation the epilogue was called for
 *
 * @return Returns true if the epilogue is finished or false
 * otherwise.
 */
bool finalizeEpilogue(Alloc_t *alloc);

/**
 * @brief Handle hook PSIDHOOK_PELOGUE_OE
 *
 * Used to handle stdout and stderr of prologue and epilogue scripts.
 *
 * @param data PElogue_OEdata_t structure
 *
 * @return Always returns 0
 */
int handlePelogueOE(void *data);

/**
 * @brief Handle hook PSIDHOOK_PELOGUE_GLOBAL
 *
 * Used to set nodes offline if the global slurmctld prologue failed.
 *
 * @param data PElogue_Global_Res_t structure
 *
 * @return Always returns 0
 */
int handlePelogueGlobal(void *data);

/**
 * @brief Handle hook PSIDHOOK_PELOGUE_DROP
 *
 * Handle a messages of type PSP_PLUG_PELOGUE to be dropped. This
 * might happen if pspelogue is aborted prematurely. For the time
 * being only sub-type PSP_PELOGUE_RESP is handled.
 *
 * @param data Message to be dropped
 *
 * @return Always returns 0
 */
int handlePelogueDrop(void *data);

#endif
