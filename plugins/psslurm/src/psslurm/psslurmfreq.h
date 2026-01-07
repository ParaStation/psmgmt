/*
 * ParaStation
 *
 * Copyright (C) 2025-2026 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_FREQ
#define __PS_SLURM_FREQ

#include <stdbool.h>
#include <stdint.h>

#include "psslurmstep.h"
#include "psslurmjob.h"

/**
 * @brief Adjust GPU frequencies allocated by given step
 *
 * @param step Step to adjust GPU frequencies for
 *
 * @return Returns true on success otherwise false is returned
 */
bool Freq_adjustStepGPUs(Step_t *step);

/**
 * @brief Adjust GPU frequencies allocated by given job
 *
 * @param job Job to adjust GPU frequencies for
 *
 * @return Returns true on success otherwise false is returned
 */
bool Freq_adjustJobGPUs(Job_t *job);

/**
 * @brief Reset GPU frequencies allocated by given step
 *
 * @param step Step to adjust GPU frequencies for
 *
 * @return Returns true on success otherwise false is returned
 */
bool Freq_resetStep(Step_t *step);

/**
 * @brief Reset GPU frequencies allocated by given job
 *
 * @param job Job to adjust GPU frequencies for
 *
 * @return Returns true on success otherwise false is returned
 */
bool Freq_resetJob(Job_t *job);

/**
 * @brief Parse Slurm TRes frequency string
 *
 * A TRes frequency string is a comma separate list of various
 * frequency values. This functions extracts the graphics and
 * memory frequency for GPUs. Frequencies might be specified
 * by a value in MHz or as abstract string including
 * "low, medium, high, highm1".
 *
 * @param freqStr TRes string to parse
 *
 * @param graFreq GPU graphics frequency
 *
 * @param memFreq GPU memory frequency
 *
 * @return Returns true on success otherwise false is returned
 */
bool Freq_gpuStr2Freq(char *freqStr, uint32_t *graFreq, uint32_t *memFreq);

#endif  /* __PS_SLURM_FREQ */
