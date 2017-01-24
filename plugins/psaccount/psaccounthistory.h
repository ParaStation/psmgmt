/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_ACCOUNT_HISTORY
#define __PS_ACCOUNT_HISTORY

#include <stdbool.h>

#include "pstaskid.h"

/**
 * @brief Add task to history
 *
 * Add the task ID @a tid to the history of tasks.
 *
 * @param tid Task ID to add to history
 *
 * @return No return value
 */
void saveHist(PStask_ID_t tid);

/**
 * @brief Find task in history
 *
 * Search for the task ID @a tid in the history of tasks.
 *
 * @param tid Task ID to search for
 *
 * @return if the task was found, return true,  or false otherwise.
 */
bool findHist(PStask_ID_t tid);

#endif  /* __PS_ACCOUNT_HISTORY */
