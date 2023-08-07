/*
 * ParaStation
 *
 * Copyright (C) 2017-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmtasks.h"

#include <signal.h>
#include <unistd.h>

#include "pscommon.h"
#include "pluginmalloc.h"
#include "psidsignal.h"
#include "psidtask.h"

#include "psslurmlog.h"
#include "psslurmstep.h"

PS_Tasks_t *addTask(list_t *list, PStask_ID_t childTID,
		    PStask_ID_t forwarderTID, PStask_t *forwarder,
		    PStask_group_t childGroup,
		    int32_t jobRank, int32_t globalRank)
{
    PS_Tasks_t *task = ucalloc(sizeof(*task));

    task->childTID = childTID;
    task->forwarderTID = forwarderTID;
    task->forwarder = forwarder;
    task->childGroup = childGroup;
    task->jobRank = jobRank;
    task->globalRank = globalRank;
    task->exitCode = 0;
    task->sentExit = 0;

    fdbg(PSSLURM_LOG_PROCESS, "childTID %s ", PSC_printTID(childTID));
    mdbg(PSSLURM_LOG_PROCESS, "forwarderTID %s childGroup %i jobRank %d"
	 " globalRank %d\n", PSC_printTID(forwarderTID), childGroup, jobRank,
	globalRank);

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

PS_Tasks_t *findTaskByJobRank(list_t *taskList, int32_t rank)
{
    if (!taskList) return NULL;
    list_t *t;
    list_for_each(t, taskList) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	if (task->jobRank == rank) return task;
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
	if (task->jobRank < 0) continue;
	count++;
    }
    return count;
}

int killChild(pid_t pid, int signal, uid_t uid)
{
    if (!pid || pid == -1) return -1;
    if (pid == getpid()) return -1;
    if ((signal == SIGKILL || signal == SIGTERM) && pid > 1) pid *= -1;

    return pskill(pid, signal, uid);
}

int __signalTasks(uint32_t jobid, uint32_t stepid, uid_t uid, list_t *taskList,
		  int signal, int32_t group, const char *caller, const int line)
{
    list_t *t, *tmp;
    int count = 0;
    Step_t s = {
	.jobid = jobid,
	.stepid = stepid };

    list_for_each_safe(t, tmp, taskList) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);
	PStask_t *child = PStasklist_find(&managedTasks, task->childTID);
	if (child) {
	    if (group > -1 && child->group != (PStask_group_t) group) continue;
	    if (child->rank < 0 && signal != SIGKILL) continue;

	    if (child->forwarder
		&& child->forwarder->tid == task->forwarderTID
		&& child->uid == uid) {
		fdbg(PSSLURM_LOG_PROCESS, "(%s:%i) rank %i/%i kill(%i) "
		     "signal %i group %i %s\n", caller, line, task->jobRank,
		     child->rank, PSC_getPID(child->tid), signal, child->group,
		     Step_strID(&s));
		PSID_kill(PSC_getPID(child->tid), signal, child->uid);
		count++;
	    }
	}
    }

    if (count) {
	flog("(%s:%u) killed %i processes with signal %i of %s\n", caller, line,
	     count, signal, Step_strID(&s));
    }

    return count;
}
