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

#ifndef __PSEXEC__COMM
#define __PSEXEC__COMM

#include "psidcomm.h"
#include "pluginenv.h"
#include "psexecscripts.h"

void handlePsExecMsg(DDTypedBufferMsg_t *msg);
void handleDroppedMsg(DDTypedBufferMsg_t *msg);
int sendScriptExec(Script_t *script, PSnodes_ID_t dest);
int startLocalScript(Script_t *script);

#endif
