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

#ifndef __PSEXEC__SCRIPTS
#define __PSEXEC__SCRIPTS

#include "pluginenv.h"
#include "pstask.h"
#include "psexectypes.h"

Script_t ScriptList;

void initScriptList();
int deleteScript(pid_t pid);
void clearScriptList();
Script_t *findScript(pid_t pid);
Script_t *findScriptByuID(uint16_t uID);
Script_t *addScript(uint32_t id, pid_t pid, PSnodes_ID_t client, char *execName);
int deleteScriptByuID(uint16_t uID);

#endif
