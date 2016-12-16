/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PELOGUE_HANDLES
#define __PELOGUE_HANDLES

#include <stdbool.h>

#include "pluginconfig.h"
#include "peloguetypes.h"

/*
 * This file contains definitions of function pointer for each of the
 * functions the pelogue plugin exports to foreign plugins like the
 * batch-system plugins psmom and psslurm. In order to initialize
 * those handles used within a foreign module, a corresponding
 * call to @ref dlsym() must be executed there.
 */

/* For documentation of the specific funtions refer to peloguetypes.h */

psPelogueAddPluginConfig_t *psPelogueAddPluginConfig;
psPelogueDelPluginConfig_t *psPelogueDelPluginConfig;

psPelogueAddJob_t *psPelogueAddJob;
psPelogueStartPE_t *psPelogueStartPE;
sPelogueSignalPE_t *psPelogueSignalPE;
psPelogueDeleteJob_t *psPelogueDeleteJob;

#endif  /* __PELOGUE_HANDLES */
