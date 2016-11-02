/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PSEXEC__HANDELS
#define __PSEXEC__HANDELS

#include <stdint.h>

#include "pluginenv.h"
#include "pstask.h"
#include "psexecscripts.h"

int (*psExecStartScript)(uint32_t, char *, env_t *, PSnodes_ID_t,
			psExec_Script_CB_t *);

int (*psExecSendScriptStart)(uint16_t, PSnodes_ID_t);

int (*psExecStartLocalScript)(uint16_t);

#endif
