/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psmomchild.h"

#include <stdio.h>
#include <signal.h>
#include <string.h>

#include "timer.h"
#include "pluginconfig.h"
#include "pluginmalloc.h"

#include "psmomconfig.h"
#include "psmomlog.h"

Child_t ChildList;

void initChildList()
{
    INIT_LIST_HEAD(&ChildList.list);
}

void clearChildList()
{
    list_t *pos, *tmp;
    Child_t *child;

    if (list_empty(&ChildList.list)) return;

    list_for_each_safe(pos, tmp, &ChildList.list) {
	if ((child = list_entry(pos, Child_t, list)) == NULL) {
	    return;
	}
	deleteChild(child->pid);
    }
    return;
}

char *childType2String(int type)
{
    switch (type) {
	case PSMOM_CHILD_PROLOGUE:
	    return "PROLOGUE";
	case PSMOM_CHILD_EPILOGUE:
	    return "EPILOGUE";
	case PSMOM_CHILD_JOBSCRIPT:
	    return "JOBSCRIPT";
	case PSMOM_CHILD_COPY:
	    return "COPY";
	case PSMOM_CHILD_INTERACTIVE:
	    return "INTERACTIVE";
    }
    return NULL;
}

Child_t *findChild(pid_t pid)
{
    list_t *pos, *tmp;
    Child_t *child;

    if (list_empty(&ChildList.list)) return NULL;

    list_for_each_safe(pos, tmp, &ChildList.list) {
	if ((child = list_entry(pos, Child_t, list)) == NULL) {
	    return NULL;
	}
	if (child->pid == pid) {
	    return child;
	}
    }
    return NULL;
}

Child_t *findChildByJobid(char *jobid, int type)
{
    list_t *pos, *tmp;
    Child_t *child;

    if (!jobid) {
	mlog("%s: invalid jobid\n", __func__);
	return NULL;
    }

    if (list_empty(&ChildList.list)) return NULL;

    list_for_each_safe(pos, tmp, &ChildList.list) {
	if ((child = list_entry(pos, Child_t, list)) == NULL) {
	    return NULL;
	}

	if (type == -1 || child->type == (unsigned int) type) {
	    if (!(strcmp(child->jobid, jobid))) {
		return child;
	    }
	}
    }
    return NULL;
}

/**
 * @brief Timeout handler for children.
 *
 * @param timerId The id of the timer which hit.
 *
 * @param data Pointer to the child structure.
 *
 * @return No return value.
 */
static void handleChildTimeout(int timerId, void *data)
{
    struct timeval childTimer = {0, 0};
    Child_t *child = data;
    int id;

    Timer_remove(timerId);
    child->childMonitorId = -1;

    /* kill the forwarder */
    if (child->killFlag) {

	if (child->c_pid == -1) {
	    mlog("%s: %s child for job '%s' : re-connect timeout, sending"
		" SIGKILL\n", __func__, childType2String(child->type),
		child->jobid);
	} else {
	    mlog("%s: %s child for job '%s' : walltime timeout, sending"
		" SIGKILL\n", __func__, childType2String(child->type),
		child->jobid);
	}

	kill(child->pid, SIGKILL);
	return;
    }

    child->killFlag = 1;

    /* set timer for hard killing via SIGKILL */
    childTimer.tv_sec = getConfValueL(&config, "TIMEOUT_CHILD_GRACE");

    id = Timer_registerEnhanced(&childTimer, handleChildTimeout, child);
    if (id == -1) {
	mlog("%s: register child monitor timer failed\n", __func__);
	child->childMonitorId = -1;
	kill(child->pid, SIGKILL);
    } else {
	child->childMonitorId = id;
    }

    /* log it */
    if (child->c_pid == -1) {
	mlog("%s: %s child for job %s: re-connect timeout, sending SIGTERM\n",
	     __func__, childType2String(child->type), child->jobid);
    } else {
	mlog("%s: %s child for job %s: walltime timeout, sending SIGTERM\n",
	     __func__, childType2String(child->type), child->jobid);
    }

    kill(child->pid, SIGTERM);
}

void setChildTimeout(Child_t *child, time_t timeout, int addGrace)
{
    struct timeval childTimer = {1,0};
    int id;

    if (child->childMonitorId != -1) {
	Timer_remove(child->childMonitorId);
	child->childMonitorId = -1;
    }

    childTimer.tv_sec = timeout;

    /* don't use invalid timeouts */
    if (timeout <= 0) return;

    /* get child grace time */
    if (addGrace) {
	int grace = 0;
	switch (child->type) {
	    case PSMOM_CHILD_PROLOGUE:
	    case PSMOM_CHILD_EPILOGUE:
		grace = getConfValueI(&config, "TIMEOUT_PE_GRACE");
		break;
	    case PSMOM_CHILD_JOBSCRIPT:
	    case PSMOM_CHILD_INTERACTIVE:
		grace = getConfValueI(&config, "TIME_OBIT");
		break;
	    case PSMOM_CHILD_COPY:
		grace = 3;
		break;
	}

	/* need a longer grace time, so the forwarder can cleanup properly */
	childTimer.tv_sec += (3 * grace);
    }

    id = Timer_registerEnhanced(&childTimer, handleChildTimeout, child);
    if (id == -1) {
	mlog("%s: register child monitor timer failed\n", __func__);
	child->childMonitorId = -1;
    } else {
	child->childMonitorId = id;
    }
}

Child_t *addChild(pid_t pid, PSMOM_child_types_t type, char *jobid)
{
    Child_t *child;
    long conTimeout;

    child = (Child_t *) umalloc(sizeof(Child_t));
    child->pid = pid;
    child->type = type;
    child->jobid = ustrdup(jobid);
    child->c_pid = -1;
    child->c_sid = -1;
    child->fw_timeout = -1;
    child->childMonitorId = -1;
    child->sharedComm = NULL;
    child->killFlag = 0;
    child->signalFlag = 0;

    gettimeofday(&child->start_time, 0);

    /* monitor every forwarder */
    conTimeout = getConfValueL(&config, "TIMEOUT_CHILD_CONNECT");
    setChildTimeout(child, conTimeout, 0);

    list_add_tail(&(child->list), &ChildList.list);
    return child;
}

int deleteChild(pid_t pid)
{
    Child_t *child;

    if ((child = findChild(pid)) == NULL) {
	return 0;
    }

    /* remove child timeout monitor */
    if (child->childMonitorId != -1) {
	Timer_remove(child->childMonitorId);
	child->childMonitorId = -1;
    }

    if (child->jobid) {
	ufree(child->jobid);
    }

    list_del(&child->list);
    ufree(child);
    return 1;
}
