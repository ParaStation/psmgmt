/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_PMI_TYPES
#define __PS_PMI_TYPES

#include "pstask.h"
#include "pluginspawn.h"

/** Connection type of pmi */
typedef enum {
    PMI_DISABLED = 0,
    PMI_OVER_TCP,
    PMI_OVER_UNIX,
    PMI_FAILED,
} PMItype_t;

/**
 * @brief Set task preparation function
 *
 * Register the new task preparation function @a spawnFunc to the
 * pspmi module. This function will be used within future PMI spawn
 * requests in order to prepare the task structure of the helper tasks
 * used to realize the actual spawn action.
 *
 * @param spawnFunc New preparation function to use
 *
 * @return No return value
 */
typedef void(psPmiSetFillSpawnTaskFunction_t)(fillerFunc_t spawnFunc);

/**
 * @brief Reset task preparation function
 *
 * Reset pspmi's task preparation function to the default one
 * utilizing mpiexec as a helper.
 *
 * @return No return value
 */
typedef void(psPmiResetFillSpawnTaskFunction_t)(void);

#endif  /* __PS_PMI_TYPES */
