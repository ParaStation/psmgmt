/*
 * ParaStation
 *
 * Copyright (C) 2010-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psaccountinter.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include "pscommon.h"
#include "pluginmalloc.h"

#include "psaccount.h"
#include "psaccountclient.h"
#include "psaccountcomm.h"
#include "psaccountenergy.h"
#include "psaccountjob.h"
#include "psaccountlog.h"
#include "psaccountproc.h"
#include "psaccountinterconnect.h"
#include "psaccountfilesystem.h"

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

int psAccountSignalSession(pid_t session, int sig)
{
    mdbg(PSACC_LOG_SIGNAL, "%s(session %d sig %d)\n", __func__, session, sig);
    initProcPool(); // Just in case we are called within a forwarder
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

void psAccountGetLocalInfo(psAccountInfo_t *info)
{
    memset(info, 0, sizeof(*info));

    psAccountEnergy_t *eSrc = Energy_getData();
    memcpy(&info->energy, eSrc, sizeof(*eSrc));

    psAccountIC_t *icSrc = IC_getData();
    memcpy(&info->interconnect, icSrc, sizeof(*icSrc));

    psAccountFS_t *fsSrc = FS_getData();
    memcpy(&info->filesytem, fsSrc, sizeof(*fsSrc));
}

int psAccountGetPoll(psAccountOpt_t type)
{
    switch (type) {
	case PSACCOUNT_OPT_MAIN:
	    return getMainTimer();
	case PSACCOUNT_OPT_IC:
	    return IC_getPoll();
	case PSACCOUNT_OPT_ENERGY:
	    return Energy_getPoll();
	case PSACCOUNT_OPT_FS:
	    return FS_getPoll();
    }

    flog("invalid option %i\n", type);
    return -1;
}

bool psAccountSetPoll(psAccountOpt_t type, int poll)
{
    switch (type) {
	case PSACCOUNT_OPT_MAIN:
	    return setMainTimer(poll);
	case PSACCOUNT_OPT_IC:
	    return IC_setPoll(poll);
	case PSACCOUNT_OPT_ENERGY:
	    return Energy_setPoll(poll);
	case PSACCOUNT_OPT_FS:
	    return FS_setPoll(poll);
    }

    flog("invalid option %i\n", type);
    return false;
}

bool psAccountCtlScript(psAccountCtl_t action, psAccountOpt_t type)
{
    switch(type) {
    case PSACCOUNT_OPT_IC:
	switch (action) {
	case PSACCOUNT_SCRIPT_START:
	    return IC_startScript();
	case PSACCOUNT_SCRIPT_STOP:
	    IC_finalize();
	    return true;
	default:
	    flog("invalid interconnect action %i\n", action);
	}
	break;
    case PSACCOUNT_OPT_ENERGY:
	switch (action) {
	case PSACCOUNT_SCRIPT_START:
	    return Energy_startScript();
	case PSACCOUNT_SCRIPT_STOP:
	    Energy_finalize();
	    return true;
	default:
	    flog("invalid energy action %i\n", action);
	}
	break;
    case PSACCOUNT_OPT_FS:
	switch (action) {
	case PSACCOUNT_SCRIPT_START:
	    return FS_startScript();
	case PSACCOUNT_SCRIPT_STOP:
	    FS_finalize();
	    return true;
	default:
	    flog("invalid filesystem action %i\n", action);
	}
	break;
    default:
	flog("invalid type %i (action %i)\n", type, action);
    }

    return false;
}

bool psAccountScriptEnv(psAccountCtl_t action, psAccountOpt_t type,
			char *envStr)
{
    if (!envStr) {
	flog("called with invalid envStr\n");
	return false;
    }

    switch(type) {
	case PSACCOUNT_OPT_IC:
	    return IC_ctlEnv(action, envStr);
	case PSACCOUNT_OPT_ENERGY:
	    return Energy_ctlEnv(action, envStr);
	case PSACCOUNT_OPT_FS:
	    return FS_ctlEnv(action, envStr);
	default:
	    flog("invalid action %i or type %i\n", action, type);
	    return false;
    }

    return false;
}
