/*
 * ParaStation
 *
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_ACCOUNT_INTERCONNECT
#define __PS_ACCOUNT_INTERCONNECT

#include <stdbool.h>
#include <stdint.h>

#include "psaccounttypes.h"

/**
 * @brief Initialize interconnect monitoring
 *
 * If a vaild poll interval is configured the monitoring script
 * is started.
 *
 * @return Returns true on success or false otherwise
 */
bool IC_init(void);

/**
 * @brief Stop interconnect monitoring
 */
void IC_stopScript(void);

/**
 * @brief Cleanup interconnect monitoring memory
 */
void IC_cleanup(void);

/**
 * @brief Get current interconnect data
 *
 * @return Returns a pointer to the current interconnect data
 */
psAccountIC_t *IC_getData(void);

/**
 * @brief Set interconnect poll time
 *
 * @param poll The new poll time in seconds
 *
 * @return Returns true on success otherwise false is returned
 */
bool IC_setPoll(uint32_t poll);

/**
 * @brief Get interconnect poll time
 *
 * @param Returns the current poll time in seconds
 */
uint32_t IC_getPoll(void);

/**
 * @brief Start the interconnect monitor script
 *
 * @return Returns true on success or false otherwise
 */
bool IC_startScript(void);

/**
 * @brief Set/unset environment variable for interconnect script
 *
 * @param action Specifies if a variable should be added or removed
 *
 * @param name Name of environment variable to set/unset
 *
 * @param val Value to be set; ignored in case of variable unset
 *
 * @return Returns true on success or false otherwise
 */
bool IC_ctlEnv(psAccountCtl_t action, const char *name, const char *val);

#endif  /* __PS_ACCOUNT_INTERCONNECT */
