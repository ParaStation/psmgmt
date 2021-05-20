/*
 * ParaStation
 *
 * Copyright (C) 2017-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSSLURM_HANDLES
#define __PSSLURM_HANDLES

#include "psslurmtypes.h"

/*
 * This file contains definitions of function pointer for each of the
 * functions the psslurm plugin exports to foreign plugins. In order
 * to initialize those handles used within a foreign module, a
 * corresponding call to @ref dlsym() must be executed there.
 */

/* For documentation of the specific funtions refer to psslurmtypes.h */

psSlurmRegMsgHandler_t *psSlurmRegMsgHandler;
psSlurmClrMsgHandler_t *psSlurmClrMsgHandler;

psSlurmDupMsg_t *psSlurmDupMsg;
psSlurmReleaseMsg_t *psSlurmReleaseMsg;

#ifdef HAVE_SPANK
psSpankSetenv_t *psSpankSetenv;
psSpankGetenv_t *psSpankGetenv;
psSpankUnsetenv_t *psSpankUnsetenv;
psSpankGetItem_t *psSpankGetItem;
psSpankSymbolSup_t *psSpankSymbolSup;
psSpankGetContext_t *psSpankGetContext;
psSpankOptRegister_t *psSpankOptRegister;
psSpankOptGet_t *psSpankOptGet;
#endif

#endif /* __PSSLURM_HANDLES */
