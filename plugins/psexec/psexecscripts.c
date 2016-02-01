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
#include <list.h>
#include <signal.h>

#include "psexeclog.h"
#include "pluginmalloc.h"

#include "psexecscripts.h"


void initScriptList()
{
    INIT_LIST_HEAD(&ScriptList.list);
}

static void doDeleteScript(Script_t *script)
{
    ufree(script->execName);
    envDestroy(&script->env);
    list_del(&script->list);
    ufree(script);
}

int deleteScript(pid_t pid)
{
    Script_t *script;

    if (pid >0) kill(pid, SIGKILL);

    if (!(script = findScript(pid))) return 0;

    doDeleteScript(script);
    return 1;
}

int deleteScriptByuID(uint16_t uID)
{
    Script_t *script;

    if (!(script = findScriptByuID(uID))) return 0;

    doDeleteScript(script);
    return 1;
}

void clearScriptList()
{
    list_t *pos, *tmp;
    Script_t *script;

    list_for_each_safe(pos, tmp, &ScriptList.list) {
	if (!(script = list_entry(pos, Script_t, list))) return;

	deleteScript(script->pid);
    }
}

Script_t *findScriptByuID(uint16_t uID)
{
    list_t *pos, *tmp;
    Script_t *script;

    list_for_each_safe(pos, tmp, &ScriptList.list) {
	if (!(script = list_entry(pos, Script_t, list))) return NULL;

	if (script->uID == uID) return script;
    }
    return NULL;
}

Script_t *findScript(pid_t pid)
{
    list_t *pos, *tmp;
    Script_t *script;

    list_for_each_safe(pos, tmp, &ScriptList.list) {
	if (!(script = list_entry(pos, Script_t, list))) return NULL;

	if (script->pid == pid) return script;
    }
    return NULL;
}

Script_t *addScript(uint32_t id, pid_t pid, PSnodes_ID_t origin, char *execName)
{
    struct timespec tp;
    Script_t *script;

    script = (Script_t *) umalloc(sizeof(Script_t));
    script->id = id;
    script->pid = pid;
    script->origin = origin;
    script->cb = NULL;
    script->execName = strdup(execName);
    envInit(&script->env);

    if (!(clock_gettime(CLOCK_REALTIME, &tp))) {
	script->uID = (uint16_t) tp.tv_nsec;
    } else {
	script->uID = (uint16_t) time(NULL);
    }

    list_add_tail(&(script->list), &ScriptList.list);
    return script;
}
