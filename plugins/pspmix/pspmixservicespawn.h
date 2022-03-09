/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_PMIX_SERVICE_SPAWN
#define __PS_PMIX_SERVICE_SPAWN

#include "pspmixtypes.h"

/**
 * @todo document
 */
void pspmix_setFillSpawnTaskFunction(fillerFunc_t spawnFunc);

/**
 * @todo document
 */
void pspmix_resetFillSpawnTaskFunction(void);

fillerFunc_t * pspmix_getFillTaskFunction(void);

#endif  /* __PS_PMIX_SERVICE_SPAWN */

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
