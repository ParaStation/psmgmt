/*
 * ParaStation
 *
 * Copyright (C) 2015-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_SPAWN
#define __PS_SLURM_SPAWN

#include "pstask.h"
#include "pmitypes.h"

#include "psslurmstep.h"

/**
 * @brief Initialize the spawn facility
 *
 * @param jobstep The step of the facility to initialize
 */
void initSpawnFacility(Step_t *jobstep);

/**
 *  @brief Fills the passed task structure to spawn processes using srun
 *
 *  @param req    spawn request
 *
 *  @param usize  universe size
 *
 *  @param task   task structure to adjust
 *
 *  @return Returns 1 on success, 0 on error, -1 on not responsible
 */
int fillSpawnTaskWithSrun(SpawnRequest_t *req, int usize, PStask_t *task);

#endif  /* __PS_SLURM_SPAWN */
