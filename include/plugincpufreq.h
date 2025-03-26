/*
 * ParaStation
 *
 * Copyright (C) 2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PLUGIN_LIB_CPUFREQ
#define __PLUGIN_LIB_CPUFREQ

#include <stdbool.h>
#include <stdint.h>

#include <pscpu.h>

/** used as an index for Command_Map */
typedef enum {
    CMD_LIST_CPUS = 0,
    CMD_GET_AVAIL_GOV,
    CMD_GET_AVAIL_FREQ,
    CMD_GET_FREQ,
    CMD_GET_CUR_GOV,
    CMD_SET_MIN_FREQ,
    CMD_SET_MAX_FREQ,
    CMD_SET_GOV,
} Script_CMDs_t;

/** list of supported CPU governors */
typedef enum {
    GOV_UNDEFINED	= 0x00,
    GOV_CONSERVATIVE	= 0x01,
    GOV_ONDEMAND	= 0x02,
    GOV_PERFORMANCE	= 0x04,
    GOV_POWERSAVE	= 0x08,
    GOV_USERSPACE	= 0x10,
    GOV_SCHEDUTIL	= 0x20,
} CPUfreq_governors_t;

/**
 * @brief Test if the CPU frequency facility was successfully initialized
 *
 * @return Returns true on success otherwise false is returned
 */
bool CPUfreq_isInitialized(void);

/**
 * @brief Initialize the CPU frequency facility
 *
 * Collect various information about the CPU scaling capabilities including
 * current, available and default governors and frequencies. The data is
 * collected by calls to the CPU frequency script which normally uses the
 * sys-filesystem as source of information.
 *
 * @param cpuSysPath Path in the sys-filesystem which holds CPU frequency
 * configuration or NULL to use the default
 *
 * @return Returns true on success otherwise false is returned
 */
bool CPUfreq_init(const char *cpuSysPath);

/**
 * @brief Finalize the CPU frequency facility
 */
void CPUfreq_finalize(void);

/**
 * @brief Reset the given CPUs to their default governor
 *
 * The default is set by @ref CPUfreq_init() from the current
 * governor.
 *
 * @return Returns true on success otherwise false is returned
 */
bool CPUfreq_resetGov(PSCPU_set_t set, uint16_t setSize);

/**
 * @brief Reset the given CPUs to their default minimum frequency
 *
 * The default is set by @ref CPUfreq_init() from the current
 * minimum frequency.
 *
 * @return Returns true on success otherwise false is returned
 */
bool CPUfreq_resetMinFreq(PSCPU_set_t set, uint16_t setSize);

/**
 * @brief Reset the given CPUs to their default maximum frequency
 *
 * The default is set by @ref CPUfreq_init() from the current
 * maximum frequency.
 *
 * @return Returns true on success otherwise false is returned
 */
bool CPUfreq_resetMaxFreq(PSCPU_set_t set, uint16_t setSize);

/**
 * @brief Reset governor, minimum and maximum CPU frequency
 *
 * Reset all scaling parameters for all CPUs. The default values
 * were set by @ref CPUfreq_init().
 *
 * @return Returns true on success otherwise false is returned
 */
bool CPUfreq_resetAll(void);

/**
 * @brief Set CPU governor for selected CPUs
 *
 * @param set Set of all CPUs to change
 *
 * @param setSize Size of the CPU set
 *
 * @return Returns true on success otherwise false is returned
 */
bool CPUfreq_setGov(PSCPU_set_t set, uint16_t setSize,
		    CPUfreq_governors_t newGov);

/**
 * @brief Set CPU frequency for selected CPUs
 *
 * @param set Set of all CPUs to change
 *
 * @param setSize Size of the CPU set
 *
 * @param newFreq New frequency to set
 *
 * @param cmd Command to execute to change the desired frequency
 *
 * @return Returns true on success otherwise false is returned
 */
bool CPUfreq_setFreq(PSCPU_set_t set, uint16_t setSize, uint32_t newFreq,
		     Script_CMDs_t cmd);

#define CPUfreq_setMinFreq(set, setSize, newFreq) \
    CPUfreq_setFreq(set, setSize, newFreq, CMD_SET_MIN_FREQ)

#define CPUfreq_setMaxFreq(set, setSize, newFreq) \
    CPUfreq_setFreq(set, setSize, newFreq, CMD_SET_MAX_FREQ)

/**
 * @brief Convert a governor name to its binary representation
 *
 * If the first letters of @a govName match a known governor the
 * rest of the string will be ignored.
 *
 * @param govName Name of the governor to convert
 *
 * @return Returns the requested governor on success or 0
 * on error
 */
CPUfreq_governors_t CPUfreq_str2Gov(char *govName);

/**
 * @brief Convert a governor to its string representation
 *
 * @param gov Governor to convert
 *
 * Returns the name of the governor on success or "unkonwn"
 * on error
 */
char *CPUfreq_gov2Str(CPUfreq_governors_t gov);

#endif  /* __PLUGIN_LIB_CPUFREQ */
