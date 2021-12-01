/*
 * ParaStation
 *
 * Copyright (C) 2014-2017 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSPAM_HANDLES
#define __PSPAM_HANDLES

#include "pspamtypes.h"  // IWYU pragma: export

/*
 * This file contains definitions of function pointer for each of the
 * functions the pspam plugin exports to foreign plugins like the
 * batch-system plugins psmom and psslurm. In order to initialize
 * those handles used within a foreign module, a corresponding call to
 * @ref dlsym() must be executed there.
 */

/* For documentation of the specific funtions refer to pspamtypes.h */

psPamAddUser_t *psPamAddUser;
psPamSetState_t *psPamSetState;
psPamDeleteUser_t *psPamDeleteUser;
psPamFindSessionForPID_t *psPamFindSessionForPID;

#endif
