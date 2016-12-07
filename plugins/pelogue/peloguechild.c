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
#include <sys/time.h>

#include "pluginmalloc.h"

#include "peloguechild.h"

/** List of all children */
static LIST_HEAD(childList);

char *childType2String(PELOGUE_child_types_t type)
{
    switch (type) {
    case PELOGUE_CHILD_PROLOGUE:
	return "PROLOGUE";
    case PELOGUE_CHILD_EPILOGUE:
	return "EPILOGUE";
    default:
	return NULL;
    }
}

Child_t *findChild(const char *plugin, const char *jobid)
{
    list_t *c;
    list_for_each(c, &childList) {
	Child_t *child = list_entry(c, Child_t, next);

	if (!(strcmp(child->plugin, plugin) &&
	    !(strcmp(child->jobid, jobid)))) {
	    return child;
	}
    }
    return NULL;
}

Child_t *addChild(const char *plugin, char *jobid, Forwarder_Data_t *fwdata,
		  PELOGUE_child_types_t type)
{
    Child_t *child = malloc(sizeof(*child));

    if (child) {
	child->jobid = ustrdup(jobid);
	child->plugin = ustrdup(plugin);
	child->type = type;
	child->signalFlag = 0;
	child->fwdata = fwdata;
	gettimeofday(&child->start_time, 0);

	list_add_tail(&child->next, &childList);
    }
    return child;
}

static void doDeleteChild(Child_t *child)
{
    if (child->plugin) free(child->plugin);
    if (child->jobid) free(child->jobid);
    list_del(&child->next);
    free(child);
}

bool deleteChild(const char *plugin, const char *jobid)
{
    Child_t *child = findChild(plugin, jobid);

    if (!child) return false;

    doDeleteChild(child);

    return true;
}

void clearChildList(void)
{
    list_t *c, *tmp;
    list_for_each_safe(c, tmp, &childList) {
	Child_t *child = list_entry(c, Child_t, next);
	doDeleteChild(child);
    }
}
