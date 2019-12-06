/*
 * ParaStation
 *
 * Copyright (C) 2017-2019 ParTec Cluster Competence Center GmbH, Munich
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

PS_Tasks_t *addTask(list_t *list, PStask_ID_t childTID,
		    PStask_ID_t forwarderTID, PStask_t *forwarder,
		    PStask_group_t childGroup, int32_t rank)
{
    PS_Tasks_t *task = umalloc(sizeof(*task));

    task->childTID = childTID;
    task->forwarderTID = forwarderTID;
    task->forwarder = forwarder;
    task->childGroup = childGroup;
    task->childRank = rank;
    task->exitCode = 0;
    task->sentExit = 0;

    fdbg(PSSLURM_LOG_PROCESS, "childTID %s ", PSC_printTID(childTID));
    mdbg(PSSLURM_LOG_PROCESS, "forwarderTID %s childGroup %i rank %i\n",
	 PSC_printTID(forwarderTID), childGroup, rank);

    list_add_tail(&task->next, list);

    return task;
}

static void deleteTask(PS_Tasks_t *task)
{
    if (!task) return;
    list_del(&task->next);
    ufree(task);
}

void clearTasks(list_t *taskList)
{
    if (!taskList) return;
    list_t *t, *tmp;
    list_for_each_safe(t, tmp, taskList) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	deleteTask(task);
    }
}

PS_Tasks_t *findTaskByRank(list_t *taskList, int32_t rank)
{
    if (!taskList) return NULL;
    list_t *t;
    list_for_each(t, taskList) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	if (task->childRank == rank) return task;
    }
    return NULL;
}

PS_Tasks_t *findTaskByFwd(list_t *taskList, PStask_ID_t fwTID)
{
    if (!taskList) return NULL;
    list_t *t;
    list_for_each(t, taskList) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	if (task->forwarderTID == fwTID) return task;
    }
    return NULL;
}

PS_Tasks_t *findTaskByChildPid(list_t *taskList, pid_t childPid)
{
    if (!taskList) return NULL;
    list_t *t;
    list_for_each(t, taskList) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	if (PSC_getPID(task->childTID) == childPid) return task;
    }
    return NULL;
}

PS_Tasks_t *findTaskByChildTID(list_t *taskList, pid_t childTID)
{
    if (!taskList) return NULL;
    list_t *t;
    list_for_each(t, taskList) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	if (task->childTID == childTID) return task;
    }
    return NULL;
}

unsigned int countTasks(list_t *taskList)
{
    unsigned int count = 0;
    if (!taskList) return 0;
    list_t *t;
    list_for_each(t, taskList) count++;
    return count;
}

unsigned int countRegTasks(list_t *taskList)
{
    unsigned int count = 0;
    list_t *t;

    if (!taskList) return 0;
    list_for_each(t, taskList) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	if (task->childRank < 0) continue;
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

int signalTasks(uint32_t jobid, uid_t uid, list_t *taskList, int signal,
		int32_t group)
{
    list_t *t, *tmp;
    int count = 0;

    list_for_each_safe(t, tmp, taskList) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	PStask_t *child = PStasklist_find(&managedTasks, task->childTID);
	if (child) {
	    if (group > -1 && child->group != (PStask_group_t) group) continue;
	    if (child->rank < 0 && signal != SIGKILL) continue;

	    if (child->rank == task->childRank && child->forwarder &&
		child->forwarder->tid == task->forwarderTID &&
		child->uid == uid) {
		mdbg(PSSLURM_LOG_PROCESS, "%s: rank %i kill(%i) signal %i "
		     "group %i job %u \n", __func__, child->rank,
		     PSC_getPID(child->tid), signal, child->group, jobid);
		killChild(PSC_getPID(child->tid), signal);
		count++;
	    }
	}
    }

    if (count) {
	mlog("%s: killed %i processes with signal %i of job %u\n", __func__,
	     count, signal, jobid);
    }

    return count;
}
