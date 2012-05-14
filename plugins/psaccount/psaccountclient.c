/*
 * ParaStation
 *
 * Copyright (C) 2010-2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdlib.h>
#include <stdio.h>

#include "psaccountcollect.h"
#include "psaccountlog.h"
#include "psaccountproc.h"
#include "helper.h"
#include "psaccount.h"

#include "psaccountclient.h"

void initAccClientList()
{
    INIT_LIST_HEAD(&AccClientList.list);
}

/**
 * @brief Search an account client.
 *
 * Find an account client by the client TID or by the logger TID.
 *
 * @return Return the found client, or NULL on error and if no client
 * was found.
 */
static Client_t *findAccClient(PStask_ID_t clientTID, PStask_ID_t loggerTID,
				pid_t clientPID)
{
    struct list_head *pos;
    Client_t *client;

    if (list_empty(&AccClientList.list)) return NULL;

    list_for_each(pos, &AccClientList.list) {
	if ((client = list_entry(pos, Client_t, list)) == NULL) {
	    return NULL;
	}
	if (clientTID != -1) {
	    if (client->taskid == clientTID) return client;
	}
	if (loggerTID != -1) {
	    if (client->logger == loggerTID) return client;
	}
	if (clientPID != -1) {
	    if (client->pid == clientPID) return client;
	}
    }
    return NULL;
}

Client_t *findAccClientByClientTID(PStask_ID_t clientTID)
{
    return findAccClient(clientTID, -1, -1);
}

Client_t *findAccClientByLogger(PStask_ID_t loggerTID)
{
    return findAccClient(-1, loggerTID, -1);
}

Client_t *findAccClientByClientPID(pid_t clientPID)
{
    return findAccClient(-1, -1, clientPID);
}

/**
 * @brief Try to find the jobscript for a logger.
 *
 * The logger (mpiexec) process must be a child of a jobscript
 * which was started by the psmom. This functions tries to find
 * the correct jobscript for a logger using the /proc filesystem
 * parent-child realtions.
 *
 * @param The logger TaskID to find the jobscript for.
 *
 * @return On success the found jobscript is returned. On error
 * NULL is returned.
 */
static Client_t *findJobscriptByLogger(PStask_ID_t logger)
{
    struct list_head *pos;
    Client_t *jobscript;

    if (list_empty(&AccClientList.list)) return NULL;

    list_for_each(pos, &AccClientList.list) {
	if ((jobscript = list_entry(pos, Client_t, list)) == NULL) {
	    return NULL;
	}
	if (jobscript->type == ACC_CHILD_JOBSCRIPT &&
	    jobscript->logger == -1) {
	    /* check if the jobscript is a parent of logger */
	    if ((isChildofParent(jobscript->pid, PSC_getPID(logger)))) {
		jobscript->logger = logger;
		return jobscript;
	    } else {
		/*
		mlog("%s: js %i not parent of logger %i\n", __func__, jobscript->pid,
		    PSC_getPID(logger));
		*/
	    }
	}
    }
    return NULL;
}

Client_t *findJobscriptInClients(Job_t *job)
{
    struct list_head *pos;
    Client_t *client, *js;

    if (list_empty(&AccClientList.list)) return NULL;

    list_for_each(pos, &AccClientList.list) {
	if ((client = list_entry(pos, Client_t, list)) == NULL) {
	    return NULL;
	}
	if (client->job == job && client->type == ACC_CHILD_PSIDCHILD) {
	    if ((js = findJobscriptByLogger(client->logger))) return js;
	}
    }
    return NULL;
}

int getAccountInfoByLogger(PStask_ID_t logger, psaccAccountInfo_t *accData)
{
    struct list_head *pos;
    Client_t *client;

    if (list_empty(&AccClientList.list)) return false;

    list_for_each(pos, &AccClientList.list) {
	if ((client = list_entry(pos, Client_t, list)) == NULL) {
	    return true;
	}
	if (client->logger == logger) {
	    /* calculate used cputime */
	    accData->cputime +=
		client->rusage.ru_utime.tv_sec +
		1.0e-6 * client->rusage.ru_utime.tv_usec +
		client->rusage.ru_stime.tv_sec +
		1.0e-6 * client->rusage.ru_stime.tv_usec;
	    accData->mem += client->data.maxRss * pageSize;
	    accData->vmem += client->data.maxVsize;
	    accData->utime += client->data.cutime;
	    accData->stime += client->data.cstime;
	    accData->count++;
	}
    }
    return true;
}

Client_t *addAccClient(PStask_ID_t taskid, PS_Acct_job_types_t type)
{
    Client_t *client;

    client = (Client_t *) umalloc(sizeof(Client_t), __func__);
    client->taskid = taskid;
    client->pid = PSC_getPID(taskid);
    client->status = -1;
    client->logger = -1;
    client->doAccounting = 1;
    client->type = type;
    client->job = NULL;

    client->rusage.ru_utime.tv_sec = 0;
    client->rusage.ru_utime.tv_usec = 0;
    client->rusage.ru_stime.tv_sec = 0;
    client->rusage.ru_stime.tv_usec = 0;

    client->data.session = 0;
    client->data.pgroup = 0;
    client->data.maxThreads = 0;
    client->data.maxRss = 0;
    client->data.maxVsize = 0;
    client->data.avgThreads = 0;
    client->data.avgThreadsCount = 0;
    client->data.avgVsize = 0;
    client->data.avgVsizeCount = 0;
    client->data.avgRss = 0;
    client->data.avgRssCount = 0;
    client->data.cutime = 0;
    client->data.cstime = 0;

    list_add_tail(&(client->list), &AccClientList.list);
    return client;
}

/**
 * @brief Delete an account client.
 *
 * Delete an account client identified by its TaskID.
 *
 * @param tid The taskID of the client to delete.
 *
 * @return Returns 1 on success and 0 on error.
 */
static int deleteAccClient(PStask_ID_t tid)
{
    Client_t *client;

    if ((client = findAccClientByClientTID(tid)) == NULL) {
	return 0;
    }

    list_del(&client->list);
    free(client);
    return 1;
}

void deleteAllAccClientsByLogger(PStask_ID_t loggerTID)
{
    Client_t *client;

    while ((client = findAccClientByLogger(loggerTID))) {
	deleteAccClient(client->taskid);
    }
}

int haveActiveAccClients()
{
    struct list_head *pos;
    Client_t *client;

    if (list_empty(&AccClientList.list)) return 0;

    list_for_each(pos, &AccClientList.list) {
	if ((client = list_entry(pos, Client_t, list)) == NULL) {
	    return 0;
	}
	if (client->doAccounting) {
	    return 1;
	}
    }
    return 0;
}

void clearAllAccClients()
{
    list_t *pos, *tmp;
    Client_t *client;

    if (list_empty(&AccClientList.list)) return;

    list_for_each_safe(pos, tmp, &AccClientList.list) {
	if ((client = list_entry(pos, Client_t, list)) == NULL) {
	    return;
	}
	if (!(deleteAccClient(client->taskid))) {
	    mlog("%s: deleting acc client '%i' failed\n", __func__,
		client->pid);
	}
    }
    return;
}

void updateAllAccClients(Job_t *job)
{
    struct list_head *pos;
    Client_t *client;

    if (list_empty(&AccClientList.list)) return;

    list_for_each(pos, &AccClientList.list) {
	if ((client = list_entry(pos, Client_t, list)) == NULL) {
	    return;
	}
	if (client->doAccounting) {
	    if (job) {
		if (client->job == job) {
		    updateAccountData(client);
		}
	    } else {
		updateAccountData(client);
	    }
	}
    }
}
