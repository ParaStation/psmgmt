/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "peloguechild.h"

/** List of all children */
static LIST_HEAD(childList);

char *childType2String(PElogueType_t type)
{
    switch (type) {
    case PELOGUE_PROLOGUE:
	return "PROLOGUE";
    case PELOGUE_EPILOGUE:
	return "EPILOGUE";
    default:
	return NULL;
    }
}

PElogueChild_t *addChild(char *plugin, char *jobid, PElogueType_t type)
{
    PElogueChild_t *child = malloc(sizeof(*child));

    if (child) {
	child->plugin = plugin;
	child->jobid = jobid;
	child->type = type;
	child->mainPElogue = -1;
	child->dirScripts = NULL;
	child->timeout = 0;
	child->rounds = 1;
	envInit(&child->env);
	child->uid = 0;
	child->gid = 0;
	child->startTime = 0;
	child->fwData = NULL;
	child->signalFlag = 0;
	child->exit = 0;

	list_add_tail(&child->next, &childList);
    }
    return child;
}

PElogueChild_t *findChild(const char *plugin, const char *jobid)
{
    list_t *c;
    list_for_each(c, &childList) {
	PElogueChild_t *child = list_entry(c, PElogueChild_t, next);

	if (!strcmp(child->plugin, plugin) && !strcmp(child->jobid, jobid)) {
	    return child;
	}
    }
    return NULL;
}

bool deleteChild(PElogueChild_t *child)
{
    if (!child) return false;

    if (child->plugin) free(child->plugin);
    if (child->jobid) free(child->jobid);
    if (child->dirScripts) free(child->dirScripts);
    envDestroy(&child->env);
    if (child->fwData) {
	/* detach from forwarder */
	child->fwData->callback = NULL;
	child->fwData->userData = NULL;
    }

    list_del(&child->next);
    free(child);

    return true;
}

void clearChildList(void)
{
    list_t *c, *tmp;
    list_for_each_safe(c, tmp, &childList) {
	PElogueChild_t *child = list_entry(c, PElogueChild_t, next);
	if (child->fwData && child->fwData->killSession) {
	    child->fwData->killSession(child->fwData->cSid, SIGKILL);
	}
	deleteChild(child);
    }
}
