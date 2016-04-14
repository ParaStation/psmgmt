/*
 * ParaStation
 *
 * Copyright (C) 2015 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Stephan Krempel <krempel@par-tec.com>
 *
 */

#ifndef __PS_PMI_HANDLES
#define __PS_PMI_HANDLES

#include "pstask.h"
#include "pmiclientspawn.h"

/**
 * @brief Set the function to handle spawn requests.
 *
 * @param spawnFunc  function to use.
 */
void (*psPmiSetFillSpawnTaskFunction)(
	int (*spawnFunc)(SpawnRequest_t *req, int usize, PStask_t *task));

/**
 * @brief Reset the function to handle spawn requests to default.
 */
void (*psPmiResetFillSpawnTaskFunction)(void);

#endif
