/*
 * ParaStation
 *
 * Copyright (C) 2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PAMSERVICE_HANDLES
#define __PAMSERVICE_HANDLES

#include "pamservice_types.h"  // IWYU pragma: export

/*
 * This file contains definitions of function pointer for each of the
 * functions the pamservice plugin exports to foreign plugins like
 * psslurm. In order to initialize those handles used within a foreign
 * module, a corresponding call to @ref dlsym() must be executed
 * there.
 */

/* For documentation of the specific funtions refer to pamservice_types.h */

pamserviceStartService_t *pamserviceStartService;
pamserviceOpenSession_t *pamserviceOpenSession;
pamserviceStopService_t *pamserviceStopService;

#endif /* __PAMSERVICE_HANDLES */
