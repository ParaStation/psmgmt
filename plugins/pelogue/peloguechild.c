/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
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

#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <string.h>

#include "peloguelog.h"
#include "pelogueconfig.h"

#include "pluginmalloc.h"
#include "timer.h"

#include "peloguechild.h"

void initChildList(void)
{
    INIT_LIST_HEAD(&ChildList.list);
}

void clearChildList(void)
{
    list_t *pos, *tmp;
    Child_t *child;

    list_for_each_safe(pos, tmp, &ChildList.list) {
	if (!(child = list_entry(pos, Child_t, list))) return;

	deleteChild(child->plugin, child->jobid);
    }
}

char *childType2String(int type)
{
    switch (type) {
	case PELOGUE_CHILD_PROLOGUE:
	    return "PROLOGUE";
	case PELOGUE_CHILD_EPILOGUE:
	    return "EPILOGUE";
    }
    return NULL;
}

Child_t *findChild(const char *plugin, const char *jobid)
{
    list_t *pos, *tmp;
    Child_t *child;

    list_for_each_safe(pos, tmp, &ChildList.list) {
	if (!(child = list_entry(pos, Child_t, list))) return NULL;

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
    Child_t *child;

    child = (Child_t *) umalloc(sizeof(Child_t));
    child->jobid = ustrdup(jobid);
    child->plugin = ustrdup(plugin);
    child->type = type;
    child->signalFlag = 0;
    child->fwdata = fwdata;

    gettimeofday(&child->start_time, 0);

    list_add_tail(&(child->list), &ChildList.list);
    return child;
}

int deleteChild(const char *plugin, const char *jobid)
{
    Child_t *child;

    if (!(child = findChild(plugin, jobid))) return 0;

    ufree(child->plugin);
    ufree(child->jobid);

    list_del(&child->list);
    ufree(child);
    return 1;
}
