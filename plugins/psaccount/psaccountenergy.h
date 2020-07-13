/*
 * ParaStation
 *
 * Copyright (C) 2019-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_ACCOUNT_ENERGY
#define __PS_ACCOUNT_ENERGY

#include "psaccounttypes.h"

/**
 * @brief Initialize energy monitoring
 *
 * @return Returns true on success or false otherwise
 */
bool energyInit(void);

/**
 * @brief Update node energy and power consumption
 *
 * @return Returns true on success or false otherwise
 */
bool energyUpdate(void);

/**
 * @brief Get current energy and power data
 *
 * @return Returns a pointer to the current energy data
 */
psAccountEnergy_t *energyGetData(void);

/**
 * @brief Finalize energy monitoring
 */
void energyFinalize(void);

#endif  /* __PS_ACCOUNT_ENERGY */
