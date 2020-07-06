/*
 * ParaStation
 *
 * Copyright (C) 2015-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_PELOGUE
#define __PS_SLURM_PELOGUE

#include "psslurmalloc.h"
#include "peloguetypes.h"

/**
 * @brief Start a local epilogue for an allocation
 *
 * @param alloc The allocation to start the pelogue for
 *
 * @return Returns true on success and false otherwise
 */
bool startEpilogue(Alloc_t *alloc);

/**
 * @brief Handle hook PSIDHOOK_PELOGUE_START
 *
 * Save various information from the slurmctld prologue environment and
 * add an allocation.
 *
 * @param data Pointer to the PElogueChild structure
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
 * @param data Pointer to the PElogueChild structure
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
 * @brief Start a task prologue in PSIDHOOK_EXEC_CLIENT_USER
 *
 * This function is called right before starting the users executable. Thus
 * the task prologue is spawned directly without the use of a pluginforwarder.
 *
 * @param step The step to start a task prologue for
 *
 * @param task The PS task structure
 *
 * @return Returns 0 on success or -1 otherwise
 */
int startTaskPrologue(Step_t *step, PStask_t *task);

/**
 * @brief Finalize an epilogue on the allocation leader node
 *
 * If all sister nodes send the result of their epilogue
 * to mother superior the allocation will be freed.
 *
 * @param alloc The allocation of the epilogue
 *
 * @return Returns true if the epilogue is finished
 * or false otherwise.
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

#endif
