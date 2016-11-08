/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>

#include "list.h"
#include "pluginmalloc.h"
#include "pluginenv.h"
#include "psexeclog.h"

#include "psexecscripts.h"

static LIST_HEAD(scriptList);

/** Unique ID of the next script to register */
static uint16_t nextUID = 42;

Script_t *addScript(uint32_t id, pid_t pid, PSnodes_ID_t origin, char *execName)
{
    Script_t *script = umalloc(sizeof(*script));

    if (!script) {
	mwarn(errno, "%s", __func__);
	return NULL;
    }

    script->id = id;
    script->pid = pid;
    script->origin = origin;
    script->cb = NULL;
    script->execName = ustrdup(execName);
    envInit(&script->env);
    script->uID = nextUID++;

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
    envDestroy(&script->env);
    list_del(&script->next);
    ufree(script);
}

Script_t *findScript(pid_t pid)
{
    list_t *s;
    list_for_each(s, &scriptList) {
	Script_t *script = list_entry(s, Script_t, next);

	if (script->pid == pid) return script;
    }
    return NULL;
}

Script_t *findScriptByuID(uint16_t uID)
{
    list_t *s;
    list_for_each(s, &scriptList) {
	Script_t *script = list_entry(s, Script_t, next);

	if (script->uID == uID) return script;
    }
    return NULL;
}

bool deleteScript(pid_t pid)
{
    Script_t *script = findScript(pid);

    if (pid > 0) kill(pid, SIGKILL);

    if (!script) return false;

    doDeleteScript(script);
    return true;
}

bool deleteScriptByuID(uint16_t uID)
{
    Script_t *script = findScriptByuID(uID);

    if (!script) return false;

    doDeleteScript(script);
    return true;
}

void clearScriptList(void)
{
    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &scriptList) {
	Script_t *script = list_entry(s, Script_t, next);
	if (script->pid > 0) kill(script->pid, SIGKILL);
	doDeleteScript(script);
    }
}
