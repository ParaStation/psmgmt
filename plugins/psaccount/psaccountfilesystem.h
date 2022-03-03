/*
 * ParaStation
 *
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_ACCOUNT_FILESYSTEM
#define __PS_ACCOUNT_FILESYSTEM

/**
 * @brief Initialize filesystem monitoring
 *
 * @return Returns true on success or false otherwise
 */
bool FS_init(void);

/**
 * @brief Finalize filesystem monitoring
 */
void FS_finalize(void);

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
 * @return Returns true on success otherwise false is returned
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
 * @return Returns true on success otherwise false is returned
 */
bool FS_startScript(void);

#endif  /* __PS_ACCOUNT_FILESYSTEM */
