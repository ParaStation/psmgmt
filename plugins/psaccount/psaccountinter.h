/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PS_ACCOUNT_INTER
#define __PS_ACCOUNT_INTER

#include "psidcomm.h"
#include "pspluginprotocol.h"
#include "psaccountclient.h"
#include "psaccountproc.h"

typedef enum {
    PSP_ACCOUNT_FORWARD_START = 0x00000,
    PSP_ACCOUNT_FORWARD_END,
    PSP_ACCOUNT_DATA_UPDATE,
    PSP_ACCOUNT_ENABLE_UPDATE,
    PSP_ACCOUNT_DISABLE_UPDATE,
    PSP_ACCOUNT_AGG_DATA_UPDATE,
    PSP_ACCOUNT_AGG_DATA_FINISH,
} PSP_PSAccount_t;

extern int globalCollectMode;

/**
 * @brief Global message switch for inter account messages.
 *
 * @msg The message to handle.
 *
 * @return No return value.
 */
void handleInterAccount(DDTypedBufferMsg_t *msg);

void sendAggregatedData(AccountDataExt_t *aggData, PStask_ID_t loggerTID);

void sendAggDataFinish(PStask_ID_t loggerTID);

/**
 * @brief Enable the global collection of accounting data.
 *
 * This function is called by the psmom to enable
 * the global collection of accounting data. This way all
 * psaccount plugins will automatic forward all information
 * to the node were the logger is executed.
 *
 * @param active If flag is 1 the global collect mode is switched
 * on. If the flag is 0 it is swichted off.
 *
 * @return No return value.
 */
void psAccountSetGlobalCollect(int active);

/**
 * @brief Get account info for a jobscript.
 *
 * @param jobscript The jobscript to get the info for.
 *
 * @param accData A pointer to an accountInfo structure which will receive the
 * requested information.
 *
 * @return Returns 1 on success and 0 on error.
 */
int psAccountGetJobInfo(pid_t jobscript, psaccAccountInfo_t *accData);

/**
 * @brief Wrapper for the getSessionInformation() function.
 */
void psAccountGetSessionInfos(int *count, char *buf, size_t bufsize,
				int *userCount);

/**
 * @brief Wrapper for the sendSignal2Session() function.
 */
int psAccountsendSignal2Session(pid_t session, int sig);

/**
 * @brief Wrapper for the sendSignal2AllChildren() function.
 */
int psAccountSignalAllChildren(pid_t mypid, pid_t child, pid_t pgroup, int sig);

/**
 * @brief Wrapper for the isChildofParent() function.
 */
void psAccountisChildofParent(pid_t parent, pid_t child);

/**
 * @brief Wrapper for the findDaemonProcesses() function.
 */
void psAccountFindDaemonProcs(uid_t uid, int kill, int warn);

/**
 * @brief Find a account client by its pid and return the corresponding logger.
 *
 * @param pid The pid of the client to find.
 *
 * @return Returns the pid of the clients logger or -1 on error.
 */
PStask_ID_t psAccountgetLoggerByClientPID(pid_t pid);

/**
 * @brief Wrapper for the readProcStatInfo() function.
 */
int psAccountreadProcStatInfo(pid_t pid, ProcStat_t *pS);

/**
 * @brief Wrapper for getAccountDataByLogger().
 */
int psAccountGetDataByLogger(PStask_ID_t logger, AccountDataExt_t *accData);

/**
 * @brief Wrapper for  getPidsByLogger().
 */
int psAccountGetPidsByLogger(PStask_ID_t loggerTID, pid_t **pids,
				uint32_t *count);

int psAccountSwitchAccounting(PStask_ID_t clientTID, int enable);

int psAccountGetJobData(pid_t jobscript, AccountDataExt_t *accData);

void psAccountRegisterJob(pid_t jsPid, char *jobid);

void psAccountDelJob(PStask_ID_t loggerTID);

void psAccountUnregisterJob(pid_t jsPid);

#endif
