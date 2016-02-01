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

#include <stdio.h>
#include <stdlib.h>

#include "psexeccomm.h"
#include "psexeclog.h"

#include "psexecinter.h"

int psExecStartScript(uint32_t id, char *execName, env_t *env,
			PSnodes_ID_t dest, psExec_Script_CB_t *cb)
{
    Script_t *script;
    int ret;

    script = addScript(id, -1, -1, execName);
    script->cb = cb;
    envClone(env, &script->env, NULL);

    ret = sendScriptExec(script, dest);

    if (ret == -1) {
	deleteScriptByuID(script->uID);
    }

    return ret;
}

int psExecSendScriptStart(uint16_t scriptID, PSnodes_ID_t dest)
{
    Script_t *script;

    if ((script = findScriptByuID(scriptID))) {
	return sendScriptExec(script, dest);
    }
    return  -1;
}

int psExecStartLocalScript(uint16_t scriptID)
{
    Script_t *script;

    if ((script = findScriptByuID(scriptID))) {
	return startLocalScript(script);
    }
    return  -1;
}
