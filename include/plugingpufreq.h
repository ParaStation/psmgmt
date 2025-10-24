/*
 * ParaStation
 *
 * Copyright (C) 2025-2026 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PLUGIN_LIB_GPUFREQ
#define __PLUGIN_LIB_GPUFREQ

#include <stdbool.h>
#include <stdint.h>

#include <pscpu.h>

/** list of supported GPU frequency ranges */
typedef enum {
    GPU_FREQ_FLAG     = 0x80000000,
    GPU_FREQ_LOW      = 0x80000001,
    GPU_FREQ_MEDIUM   = 0x80000002,
    GPU_FREQ_HIGH     = 0x80000003,
    GPU_FREQ_SEC_HIGH = 0x80000004,
} GPUfreq_range_t;

/** list of supported frequency types */
typedef enum {
    GPU_FREQ_TYPE_GRA = 1,
    GPU_FREQ_TYPE_MEM
} GPUfreq_type_t;

/**
 * @brief Callback used by @ref GPUfreq_init() to indicate the
 * result of the initialization process.
 */
typedef void GPUfreq_initCB_t(bool);

/**
 * @brief Test if the GPU frequency facility was successfully initialized
 *
 * @return Returns true on success otherwise false is returned
 */
bool GPUfreq_isInitialized(void);

/**
 * @brief Initialize the GPU frequency facility
 *
 * Collect various information about the GPUs capabilities including
 * current and available graphics and memory frequencies. The data is
 * collected by calls to the GPU frequency script which normally uses vendor
 * specific command line utilities as source of information.
 *
 * @param freqScript Path to frequency script or NULL to use the default
 *
 * @param cb Callback which returns the result of the initialization process
 */
void GPUfreq_init(const char *freqScript, GPUfreq_initCB_t *cb);

/**
 * @brief Finalize the GPU frequency facility
 */
void GPUfreq_finalize(void);

/**
 * @brief Reset the given GPUs to their default frequencies
 *
 * The default is set by @ref GPUfreq_init() from the current
 * frequencies.
 *
 * @param set Set of hardware threads to operate on
 *
 * @param freqType Frequency type to reset (graphics or memory)
 *
 * @return Returns true on success otherwise false is returned
 */
bool GPUfreq_resetFreq(PSCPU_set_t set, int freqType);

/**
 * @brief Set graphics frequency for selected GPUs
 *
 * @param set Set of GPUs to operate on
 *
 * @param setSize Size of @a set
 *
 * @param graFreq New graphics frequency to set
 *
 * @return Returns true on success otherwise false is returned
 */
bool GPUfreq_setGraFreq(PSCPU_set_t set, uint16_t setSize, uint32_t graFreq);

/**
 * @brief Set memory frequency for selected GPUs
 *
 * @param set Set of GPUs to operate on
 *
 * @param setSize Size of @a set
 *
 * @param memFreq New memory frequency to set
 *
 * @return Returns true on success otherwise false is returned
 */
bool GPUfreq_setMemFreq(PSCPU_set_t set, uint16_t setSize, uint32_t memFreq);

#endif  /* __PLUGIN_LIB_GPUFREQ */
