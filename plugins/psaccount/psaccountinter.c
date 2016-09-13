/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdbool.h>
#include <string.h>

#include "pstaskid.h"
#include "pluginmalloc.h"

#include "psaccountcomm.h"
#include "psaccountclient.h"
#include "psaccountjob.h"
#include "psaccountlog.h"
#include "psaccountproc.h"

#include "psaccountinter.h"

int psAccountSwitchAccounting(PStask_ID_t clientTID, int enable)
{
    return switchAccounting(clientTID, !!enable);
}

int psAccountGetDataByLogger(PStask_ID_t logger, AccountDataExt_t *accData)
{
    memset(accData, 0, sizeof(*accData));
    return aggregateDataByLogger(logger, accData);
}

void psAccountGetPidsByLogger(PStask_ID_t loggerTID, pid_t **pids,
			      uint32_t *count)
{
    return getPidsByLogger(loggerTID, pids, count);
}

int psAccountGetJobData(pid_t jobscript, AccountDataExt_t *accData)
{
    Client_t *client;
    Job_t *job;
    list_t *pos, *tmp;

    memset(accData, 0, sizeof(*accData));

    if (!(client = findClientByPID(jobscript))) {
	mlog("%s: getting account info by client '%i' failed\n", __func__,
		jobscript);
	return 0;
    }

    /* find the parallel job */
    if ((job = findJobByJobscript(jobscript))) {

	list_for_each_safe(pos, tmp, &JobList.list) {
	    if (!(job = list_entry(pos, Job_t, list))) break;

	    if (job->jobscript == jobscript) {

		if (!(aggregateDataByLogger(job->logger, accData))) {
		    mlog("%s: getting account info by jobscript '%i' failed\n",
			    __func__, jobscript);
		    continue;
		}
	    }
	}
    }

    /* add the jobscript */
    addClientToAggData(client, accData);

    return 1;
}

int psAccountSignalAllChildren(pid_t mypid, pid_t child, pid_t pgroup, int sig)
{
    return signalChildren(mypid, child, pgroup, sig);
}

int psAccountsendSignal2Session(pid_t session, int sig)
{
    return signalSession(session, sig);
}

void psAccountisChildofParent(pid_t parent, pid_t child)
{
    /* we need up2date information */
    updateProcSnapshot();

    isDescendant(parent, child);
}

void psAccountGetSessionInfos(int *count, char *buf, size_t bufsize,
				int *userCount)
{
    getSessionInfo(count, buf, bufsize, userCount);
}

void psAccountFindDaemonProcs(uid_t uid, int kill, int warn)
{
    findDaemonProcesses(uid, !!kill, !!warn);
}

int psAccountreadProcStatInfo(pid_t pid, ProcStat_t *pS)
{
    return readProcStatInfo(pid, pS);
}

void psAccountRegisterJob(pid_t jsPid, char *jobid)
{
    PStask_ID_t taskID;
    Client_t *client;

    /* monitor the JS */
    taskID = PSC_getTID(PSC_getMyID(), jsPid);
    client = addClient(taskID, ACC_CHILD_JOBSCRIPT);
    client->jobid = ustrdup(jobid);
}

void psAccountDelJob(PStask_ID_t loggerTID)
{
    deleteJob(loggerTID);
    deleteClient(loggerTID);
}

void psAccountUnregisterJob(pid_t jsPid)
{
    list_t *pos, *tmp;
    PStask_ID_t taskID;
    Job_t *job;

    /* stop accounting of dead jobscript */
    taskID = PSC_getTID(PSC_getMyID(), jsPid);
    deleteClient(taskID);

    list_for_each_safe(pos, tmp, &JobList.list) {
	if (!(job = list_entry(pos, Job_t, list))) break;

	if (job->jobscript == jsPid) deleteJob(job->logger);
    }
}

void psAccountSetGlobalCollect(int active)
{
    globalCollectMode = !!active;
}

PStask_ID_t psAccountgetLoggerByClientPID(pid_t pid)
{
    return getLoggerByClientPID(pid);
}
