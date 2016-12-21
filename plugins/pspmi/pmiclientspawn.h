/*
 * ParaStation
 *
 * Copyright (C) 2015 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_PMI_CLIENT_SPAWN
#define __PS_PMI_CLIENT_SPAWN

#include "pmitypes.h"

/**
 * @brief Initialize empty spawn request
 *
 * @param num Number of single spawns to have space in the request
 *
 * @return SpawnRequest_t structure, NULL on error with errno set
 */
SpawnRequest_t *initSpawnRequest(int num);

/**
 * @brief Copy spawn request and all its contents
 *
 * @param req Spawn request to copy
 *
 * @return Returns a duplicate of the spawn request
 */
SpawnRequest_t *copySpawnRequest(SpawnRequest_t *req);

/**
 * @brief Free spawn request
 *
 * @param req Spawn request to free
 */
void freeSpawnRequest(SpawnRequest_t *req);

#endif  /* __PS_PMI_CLIENT_SPAWN */
