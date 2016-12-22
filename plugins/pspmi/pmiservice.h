/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_PMI_SERVICE
#define __PS_PMI_SERVICE

#include <stdbool.h>
#include "pstask.h"

/**
 * @brief Send message to the deamon to spawn a service process to start
 * further compute processes.
 *
 * @param task Task structure describing the processes to spawn
 *
 * @return Returns true on success or false on error
 */
bool sendSpawnMessage(PStask_t *task);

#endif  /* __PS_PMI_SERVICE */
