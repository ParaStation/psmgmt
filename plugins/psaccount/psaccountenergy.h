/*
 * ParaStation
 *
 * Copyright (C) 2019-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_ACCOUNT_ENERGY
#define __PS_ACCOUNT_ENERGY

#include <stdbool.h>
#include <stdint.h>

#include "psaccounttypes.h"

/**
 * @brief Initialize energy monitoring
 *
 * @return Returns true on success or false otherwise
 */
bool Energy_init(void);

/**
 * @brief Update node energy and power consumption
 *
 * @return Returns true on success or false otherwise
 */
bool Energy_update(void);

/**
 * @brief Get current energy and power data
 *
 * @return Returns a pointer to the current energy data
 */
psAccountEnergy_t *Energy_getData(void);

/**
 * @brief Stop energy monitoring
 */
void Energy_stopScript(void);

/**
 * @brief Finalize energy monitoring
 */
void Energy_finalize(void);

/**
 * @brief Set energy poll time
 *
 * @param poll The new poll time in seconds
 *
 * @return Returns true on success or false otherwise
 */
bool Energy_setPoll(uint32_t poll);

/**
 * @brief Get energy poll time
 *
 * @param Returns the current poll time in seconds
 */
uint32_t Energy_getPoll(void);

/**
 * @brief Start the energy monitor script
 *
 * @return Returns true on success or false otherwise
 */
bool Energy_startScript(void);

/**
 * @brief Set/unset environment variable for energy script
 *
 * @param envStr Environment variable to set/unset
 *
 * @param action Specifies if a variable should be added or removed
 *
 * @return Returns true on success or false otherwise
 */
bool Energy_ctlEnv(psAccountCtl_t action, const char *envStr);

#endif  /* __PS_ACCOUNT_ENERGY */
