/*
 * ParaStation
 *
 * Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <signal.h>
#include <unistd.h>

#include "psslurmlog.h"

#include "pluginmalloc.h"
#include "psidtask.h"
#include "pscommon.h"

#include "psslurmtasks.h"

PS_Tasks_t *addTask(struct list_head *list, PStask_ID_t childTID,
		    PStask_ID_t forwarderTID, PStask_t *forwarder,
		    PStask_group_t childGroup, int32_t rank)
{
    PS_Tasks_t *task;

    task = (PS_Tasks_t *) umalloc(sizeof(PS_Tasks_t));
    task->childTID = childTID;
    task->forwarderTID = forwarderTID;
    task->forwarder = forwarder;
    task->childGroup = childGroup;
    task->childRank = rank;
    task->exitCode = 0;
    task->sentExit = 0;

    list_add_tail(&(task->list), list);

    return task;
}

static void deleteTask(PS_Tasks_t *task)
{
    if (!task) return;
    list_del(&task->list);
    ufree(task);
}

void clearTasks(struct list_head *taskList)
{
    list_t *pos, *tmp;

    if (!taskList) return;

    list_for_each_safe(pos, tmp, taskList) {
	PS_Tasks_t *task = list_entry(pos, PS_Tasks_t, list);
	deleteTask(task);
    }
}

PS_Tasks_t *findTaskByRank(struct list_head *taskList, int32_t rank)
{
    list_t *pos, *tmp;

    if (!taskList) return NULL;
    list_for_each_safe(pos, tmp, taskList) {
	PS_Tasks_t *task = list_entry(pos, PS_Tasks_t, list);
	if (task->childRank == rank) return task;
    }
    return NULL;
}

PS_Tasks_t *findTaskByForwarder(struct list_head *taskList, PStask_ID_t fwTID)
{
    list_t *pos, *tmp;

    if (!taskList) return NULL;
    list_for_each_safe(pos, tmp, taskList) {
	PS_Tasks_t *task = list_entry(pos, PS_Tasks_t, list);
	if (task->forwarderTID == fwTID) return task;
    }
    return NULL;
}

PS_Tasks_t *findTaskByChildPid(struct list_head *taskList, pid_t childPid)
{
    list_t *pos, *tmp;

    if (!taskList) return NULL;
    list_for_each_safe(pos, tmp, taskList) {
	PS_Tasks_t *task = list_entry(pos, PS_Tasks_t, list);
	if (PSC_getPID(task->childTID) == childPid) return task;
    }
    return NULL;
}

unsigned int countTasks(struct list_head *taskList)
{
    struct list_head *pos;
    unsigned int count = 0;

    if (!taskList) return 0;

    list_for_each(pos, taskList) count++;
    return count;
}

unsigned int countRegTasks(struct list_head *taskList)
{
    struct list_head *pos;
    unsigned int count = 0;

    if (!taskList) return 0;

    list_for_each(pos, taskList) {
	PS_Tasks_t *task;
	task = list_entry(pos, PS_Tasks_t, list);
	if (task->childRank <0) continue;
	count++;
    }
    return count;
}

int killChild(pid_t pid, int signal)
{
    if (!pid || pid < 0) return -1;
    if (pid == getpid()) return -1;

    return kill(pid, signal);
}

int signalTasks(uint32_t jobid, uid_t uid, PS_Tasks_t *tasks, int signal,
		int32_t group)
{
    list_t *pos, *tmp;
    PStask_t *child;
    int count = 0;

    list_for_each_safe(pos, tmp, &tasks->list) {
	PS_Tasks_t *task = list_entry(pos, PS_Tasks_t, list);

	if ((child = PStasklist_find(&managedTasks, task->childTID))) {
	    if (group > -1 && child->group != (PStask_group_t) group) continue;
	    if (child->rank < 0 && signal != SIGKILL) continue;

	    if (child->forwardertid == task->forwarderTID &&
		child->uid == uid) {
		mdbg(PSSLURM_LOG_PROCESS, "%s: rank '%i' kill(%i) signal '%i' "
			"group '%i' job '%u' \n", __func__, child->rank,
			PSC_getPID(child->tid), signal, child->group, jobid);
		killChild(PSC_getPID(child->tid), signal);
		count++;
	    }
	}
    }

    if (count) {
	mlog("%s: killed %i processes with signal '%i' of job '%u'\n", __func__,
	    count, signal, jobid);
    }

    return count;
}
