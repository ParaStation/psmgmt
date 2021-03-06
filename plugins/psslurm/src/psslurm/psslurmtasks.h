/*
 * ParaStation
 *
 * Copyright (C) 2017-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_PSSLURM_TASKS
#define __PS_PSSLURM_TASKS

#include "list.h"
#include "pstask.h"

typedef struct {
    list_t next;                /**< used to put into some task-lists */
    PStask_ID_t childTID;	/** PS task ID of the child */
    PStask_ID_t forwarderTID;	/** PS task ID of the psidforwarder */
    PStask_t *forwarder;	/** PS task structure of the psidforwarder */
    PStask_group_t childGroup;	/** PS task group of the child */
    int32_t childRank;		/** PS child rank */
    int exitCode;		/** exit code of the child */
    bool sentExit;		/** track the sending of the exit status */
} PS_Tasks_t;

/**
 * @brief Add a task to a task-list
 *
 * @param list The list to add the task
 *
 * @param childTID The PS task ID of the child
 *
 * @param forwarderTID The PS task ID of the psidforwarder
 *
 * @param forwarder The PS task structure of the psidforwarder
 *
 * @param childGroup The PS task group of the child
 *
 * @param childRank The PS child rank
 *
 * @return Returns a pointer to the added task
 */
PS_Tasks_t *addTask(list_t *list, PStask_ID_t childTID,
		    PStask_ID_t forwarderTID, PStask_t *forwarder,
		    PStask_group_t childGroup, int32_t rank);

/**
 * @brief Signal a list of tasks
 *
 * @param jobid The jobid of the task
 *
 * @param uid The userid of the task
 *
 * @param tasks The list of tasks to signal
 *
 * @param signal The signal to send
 *
 * @param group The group of the tasks to signal
 *
 * @return Returns the number of tasks which actually
 * got signaled
 */
int signalTasks(uint32_t jobid, uid_t uid, list_t *taskList, int signal,
		int32_t group);

/**
 * @brief Clear a list of tasks
 *
 * @param taskList The list of tasks to clear
 */
void clearTasks(list_t *taskList);

/**
 * @brief Send a signal to child
 *
 * Send a signal to a child and make sure that the signal is
 * to a single process and not the psid itself.
 *
 * @param pid The pid to send the signal to
 *
 * @param signal The signal to send
 *
 * @param uid The uid if the process owner
 *
 * @param return Returns 0 on success and -1 on error
 */
int killChild(pid_t pid, int signal, uid_t uid);

/**
 * @brief Find a task identified by its rank
 *
 * @param taskList The list of tasks to search
 *
 * @param rank The rank of the task to find
 *
 * @return Returns the found task or NULL on error
 */
PS_Tasks_t *findTaskByRank(list_t *taskList, int32_t rank);

/**
 * @brief Find a task identified by its forwarders TID
 *
 * @param taskList The list of tasks to search
 *
 * @param fwTID The task ID of the forwarder
 *
 * @return Returns the found task or NULL on error
 */
PS_Tasks_t *findTaskByFwd(list_t *taskList, PStask_ID_t fwTID);

/**
 * @brief Find a task identified by its child PID
 *
 * @param list_head The list of tasks to search
 *
 * @param childPid The PID of the task
 *
 * @return Returns the found task or NULL on error
 */
PS_Tasks_t *findTaskByChildPid(list_t *taskList, pid_t childPid);

/**
 * @brief Find a task identified by its child TID
 *
 * @param list_head The list of tasks to search
 *
 * @param childTID The TID of the task
 *
 * @return Returns the found task or NULL on error
 */
PS_Tasks_t *findTaskByChildTID(list_t *taskList, PStask_ID_t childTID);

/**
 * @brief Count the number of all tasks in a task-list
 *
 * @param list_head The list of tasks
 *
 * @return Returns the number of tasks in the list
 */
unsigned int countTasks(list_t *taskList);

/**
 * @brief Count the number of regular tasks in a task-list
 *
 * Count the number of regular tasks which have a non negative
 * rank.
 *
 * @param list_head The list of tasks
 *
 * @return Returns the number of regular tasks in the list
 */
unsigned int countRegTasks(list_t *taskList);

#endif  /* __PS_PSSLURM_TASKS */
