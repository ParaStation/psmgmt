/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <string.h>

#include "pluginmalloc.h"

#include "psaccountclient.h"
#include "psaccountcomm.h"
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
    Client_t *jsClient = findClientByPID(jobscript);
    Job_t *job = findJobByJobscript(jobscript);

    memset(accData, 0, sizeof(*accData));

    if (!jsClient) {
	mlog("%s: getting account info by client '%i' failed\n", __func__,
	     jobscript);
	return false;
    }

    /* search all parallel jobs and calc data */
    if (job) collectDataByJobscript(jobscript, accData);

    /* add the jobscript */
    addClientToAggData(jsClient, accData);

    return true;
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
    PStask_ID_t taskID = PSC_getTID(-1, jsPid);

    /* stop accounting of dead jobscript */
    deleteClient(taskID);
    deleteJobsByJobscript(jsPid);
}

void psAccountSetGlobalCollect(int active)
{
    globalCollectMode = !!active;
}

PStask_ID_t psAccountgetLoggerByClientPID(pid_t pid)
{
    return getLoggerByClientPID(pid);
}
