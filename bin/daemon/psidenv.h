/*
 * ParaStation
 *
 * Copyright (C) 2011-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2023-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Helper functions for environment handling
 */
#ifndef __PSIDENV_H
#define __PSIDENV_H

#include <stdbool.h>

#include "pstask.h"

/**
 * @brief Initialize environment stuff
 *
 * Initialize the environment handling framework. This also registers
 * the necessary message handlers.
 *
 * @return Return true on successful initialization or false on failure
 */
bool PSIDenv_init(void);

/**
 * @brief Send info on environment
 *
 * Send information on the environment variable @a key and its values
 * currently set in the local daemon. All information about a single
 * variable (i.e. name and value) is given back in a character
 * string. If @a key is pointing to a character string with value '*'
 * information on all variables is sent.
 *
 * @param dest Task ID of process waiting for answer
 *
 * @param key Name of the environment variable to send info about
 *
 * @return No return value
 */
void PSID_sendEnvList(PStask_ID_t dest, char *key);

#endif  /* __PSIDENV_H */
