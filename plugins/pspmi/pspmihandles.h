/*
 * ParaStation
 *
 * Copyright (C) 2015-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_PMI_HANDLES
#define __PS_PMI_HANDLES

#include "pmitypes.h"

/*
 * This file contains definitions of function pointers for each of the
 * functions the pspmi plugin exports to foreign plugins like the
 * batch-system plugin psslurm. In order to initialize those handles
 * used within a foreign module, a corresponding call to @ref dlsym()
 * must be executed there.
 */

/* For documentation of the specific funtions refer to pmitypes.h */

psPmiSetFillSpawnTaskFunction_t *psPmiSetFillSpawnTaskFunction;
psPmiResetFillSpawnTaskFunction_t *psPmiResetFillSpawnTaskFunction;

#endif  /* __PS_PMI_HANDLES */
