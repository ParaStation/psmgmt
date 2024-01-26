/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psexecinter.h"

#include <stdint.h>
#include <stdlib.h>

#include "psenv.h"
#include "psnodes.h"

#include "psexecscripts.h"
#include "psexeccomm.h"

int psExecStartScript(uint32_t id, char *execName, env_t *env,
		      PSnodes_ID_t dest, psExec_Script_CB_t *cb)
{
    return psExecStartScriptEx(id, execName, NULL, env, dest, cb);
}

int psExecStartScriptEx(uint32_t id, char *execName, char *execPath,
			env_t *env, PSnodes_ID_t dest, psExec_Script_CB_t *cb)
{
    Script_t *script = addScript(id, execName, execPath, cb);

    /* equip local delegate */
    envClone(env, &script->env, NULL);

    int ret = sendExecScript(script, dest);

    if (ret == -1) deleteScript(script);

    return ret;
}

int psExecSendScriptStart(uint16_t uID, PSnodes_ID_t dest)
{
    Script_t *script = findScriptByuID(uID);

    if (!script) return -1;

    return sendExecScript(script, dest);
}

int psExecStartLocalScript(uint16_t uID)
{
    Script_t *script = findScriptByuID(uID);

    if (!script) return -1;

    return startLocalScript(script);
}
