/*
 * ParaStation
 *
 * Copyright (C) 2010-2017 ParTec Cluster Competence Center GmbH, Munich
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

int psAccountSwitchAccounting(PStask_ID_t clientTID, bool enable)
{
    return switchAccounting(clientTID, enable);
}

void psAccountGetPidsByLogger(PStask_ID_t logger, pid_t **pids, uint32_t *cnt)
{
    getPidsByLogger(logger, pids, cnt);
}

bool psAccountGetDataByLogger(PStask_ID_t logger, AccountDataExt_t *accData)
{
    memset(accData, 0, sizeof(*accData));
    return aggregateDataByLogger(logger, accData);
}

bool psAccountGetDataByJob(pid_t jobscript, AccountDataExt_t *accData)
{
    return getDataByJob(jobscript, accData);
}

int psAccountSignalChildren(pid_t mypid, pid_t child, pid_t pgroup, int sig)
{
    mdbg(PSACC_LOG_SIGNAL, "%s(mypid %d child %d pgroup %d sig %d)\n", __func__,
	 mypid, child, pgroup, sig);
    return signalChildren(mypid, child, pgroup, sig);
}

int psAccountSignalSession(pid_t session, int sig)
{
    mdbg(PSACC_LOG_SIGNAL, "%s(session %d sig %d)\n", __func__, session, sig);
    return signalSession(session, sig);
}

bool psAccountIsDescendant(pid_t parent, pid_t child)
{
    /* we need up2date information */
    updateProcSnapshot();

    return isDescendant(parent, child);
}

void psAccountGetSessionInfos(int *count, char *buf, size_t bufsize,
			       int *userCount)
{
    getSessionInfo(count, buf, bufsize, userCount);
}

void psAccountFindDaemonProcs(uid_t uid, bool kill, bool warn)
{
    mdbg(PSACC_LOG_SIGNAL, "%s(uid %d kill %d warn %d)\n", __func__,
	 uid, kill, warn);
    findDaemonProcs(uid, kill, warn);
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

void psAccountSetGlobalCollect(bool active)
{
    globalCollectMode = active;
}

PStask_ID_t psAccountGetLoggerByClient(pid_t pid)
{
    return getLoggerByClientPID(pid);
}
