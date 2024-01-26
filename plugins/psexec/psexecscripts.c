/*
 * ParaStation
 *
 * Copyright (C) 2016-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psexecscripts.h"

#include <stdio.h>
#include <signal.h>
#include <errno.h>

#include "pluginmalloc.h"

#include "psidscripts.h"
#include "psidsignal.h"

#include "psexeccomm.h"
#include "psexeclog.h"

static LIST_HEAD(scriptList);

/** node-local unique ID of the next script to register */
static uint16_t nextUID = 42;

Script_t *addScript(uint32_t id, char *execName, char *execPath,
		    psExec_Script_CB_t *cb)
{
    Script_t *script = umalloc(sizeof(*script));
    if (!script) {
	mwarn(errno, "%s", __func__);
	return NULL;
    }

    *script = (Script_t){
	.id = id,
	.pid = 0,
	.initiator = -1,
	.cb = cb,
	.execName = ustrdup(execName),
	.execPath = ustrdup(execPath),
	.uID = nextUID++, };

    envInit(&script->env);

    list_add_tail(&script->next, &scriptList);

    return script;
}

/**
 * @brief Delete script information
 *
 * Remove the script information @a script from the list and free()
 * all related memory.
 *
 * @param script Script information to delete
 *
 * @return No return value
 */
static void doDeleteScript(Script_t *script)
{
    ufree(script->execName);
    ufree(script->execPath);
    envDestroy(&script->env);
    list_del(&script->next);
    ufree(script);
}

Script_t *findScriptByuID(uint16_t uID)
{
    list_t *s;
    list_for_each(s, &scriptList) {
	Script_t *script = list_entry(s, Script_t, next);

	/* check for script->pid to ensure we return a local delegate */
	if (script->uID == uID && !script->pid) return script;
    }
    return NULL;
}

bool deleteScript(Script_t *script)
{
    if (!script) return false;
    if (script->pid) {
	pskill(script->pid, SIGKILL, 0);
	PSID_cancelCB(script->pid);
    }
    doDeleteScript(script);
    return true;
}

void clearScriptList(void)
{
    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &scriptList) {
	Script_t *script = list_entry(s, Script_t, next);
	if (script->pid) {
	    pskill(script->pid, SIGKILL, 0);
	    PSID_cancelCB(script->pid);
	    char output[] = "";
	    if (script->initiator != -1) sendScriptResult(script, -1, output);
	}
	doDeleteScript(script);
    }
}
