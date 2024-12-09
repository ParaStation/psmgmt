/*
 * ParaStation
 *
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_ACCOUNT_FILESYSTEM
#define __PS_ACCOUNT_FILESYSTEM

#include <stdbool.h>
#include <stdint.h>

#include "psaccounttypes.h"

/**
 * @brief Initialize filesystem monitoring
 *
 * @return Returns true on success or false otherwise
 */
bool FS_init(void);

/**
 * @brief Stop filesystem monitoring
 */
void FS_stopScript(void);

/**
 * @brief Cleanup filesystem monitoring memory
 */
void FS_cleanup(void);

/**
 * @brief Get current filesystem data
 *
 * @return Returns a pointer to the current filesystem data
 */
psAccountFS_t *FS_getData(void);

/**
 * @brief Set filesystem poll time
 *
 * @param poll The new poll time in seconds
 *
 * @return Returns true on success or false otherwise
 */
bool FS_setPoll(uint32_t poll);

/**
 * @brief Get filesystem poll time
 *
 * @param Returns the current poll time in seconds
 */
uint32_t FS_getPoll(void);

/**
 * @brief Start the filesystem monitor script
 *
 * @return Returns true on success or false otherwise
 */
bool FS_startScript(void);

/**
 * @brief Set/unset environment variable for filesystem script
 *
 * @param action Specifies if a variable should be added or removed
 *
 * @param name Name of environment variable to set/unset
 *
 * @param val Value to be set; ignored in case of variable unset
 *
 * @return Returns true on success or false otherwise
 */
bool FS_ctlEnv(psAccountCtl_t action, const char *name, const char *val);

/**
 * @brief Show environment variable(s) for filesystem script
 *
 * Provide a string containing name and value of the environment
 * variable @a name stored in the filesystem script's environment or
 * all environment variables of this environment if @a name is NULL or
 * of length 0.
 *
 * @param name Name of the environment variable to print; all
 * environment variables are printed if @a name is NULL or of length 0
 *
 * @return Return the string describing the requested information
 */
char *FS_showEnv(const char *name);

#endif  /* __PS_ACCOUNT_FILESYSTEM */
