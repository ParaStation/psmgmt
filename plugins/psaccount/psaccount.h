/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_ACCOUNT
#define __PS_ACCOUNT

#include <stdbool.h>

/**
 * @brief Set general poll interval
 *
 * Set the plugin's general poll interval to @a poll seconds
 *
 * @param poll General poll interval to be set
 *
 * @return If @a poll is valid, return true; or false otherwise
 */
bool setMainTimer(int poll);

/**
 * @brief Get general poll interval
 *
 * @return Current general poll interval
 */
int getMainTimer(void);

#endif  /* __PS_ACCOUNT */
