/*
 * ParaStation
 *
 * Copyright (C) 2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_PMIX_HANDLES
#define __PS_PMIX_HANDLES

#include "pspmixtypes.h"

/*
 * This file contains definitions of function pointers for each of the
 * functions the pspmix plugin exports to foreign plugins like the
 * batch-system plugin psslurm. In order to initialize those handles
 * used within a foreign module, a corresponding call to @ref dlsym()
 * must be executed there.
 */

/* For documentation of the specific funtions refer to pmitypes.h */

psPmixSetFillSpawnTaskFunction_t *psPmixSetFillSpawnTaskFunction;
psPmixResetFillSpawnTaskFunction_t *psPmixResetFillSpawnTaskFunction;

#endif  /* __PS_PMIX_HANDLES */
