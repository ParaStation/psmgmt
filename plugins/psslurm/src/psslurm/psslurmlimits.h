/*
 * ParaStation
 *
 * Copyright (C) 2014-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_PSSLURM_LIMITS
#define __PS_PSSLURM_LIMITS

#include <stdbool.h>

#include "psenv.h"

/**
 * @brief Initialize resources limits from configuration
 *
 * @return Returns true on success or false otherwise
 */
bool initLimits(void);

/**
 * @brief Log all supported resource limits
 */
void printLimits(void);

/**
 * @brief Set resources limits from job environment
 *
 * @param env The environment holding rlimits to set
 *
 * @param psi If true set PSI rlimit environment variables
 */
void setRlimitsFromEnv(env_t env, bool psi);

/**
 * @brief Set default soft/hard rlimits from configuration
 */
void setDefaultRlimits(void);

#endif  /* __PS_PSSLURM_LIMITS */
