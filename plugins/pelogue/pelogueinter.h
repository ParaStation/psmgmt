/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PELOGUE_INTER
#define __PELOGUE_INTER

#include "peloguetypes.h"

psPelogueAddPluginConfig_t psPelogueAddPluginConfig;
psPelogueDelPluginConfig_t psPelogueDelPluginConfig;

psPelogueAddJob_t psPelogueAddJob;
psPelogueStartPE_t psPelogueStartPE;
psPelogueSignalPE_t psPelogueSignalPE;
psPelogueDeleteJob_t psPelogueDeleteJob;

#endif  /* __PELOGUE_INTER */
