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

#ifndef __PSEXEC__INTER
#define __PSEXEC__INTER

#include <stdint.h>

#include "pluginenv.h"
#include "pstask.h"
#include "psexecscripts.h"

int psExecStartScript(uint32_t id, char *execName, env_t *env,
			PSnodes_ID_t dest, psExec_Script_CB_t *cb);

int psExecSendScriptStart(uint16_t scriptID, PSnodes_ID_t dest);

int psExecStartLocalScript(uint16_t scriptID);

#endif
