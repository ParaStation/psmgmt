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

#ifndef __PS_ACCOUNT_INTER
#define __PS_ACCOUNT_INTER

#include "psaccounttypes.h"

psAccountRegisterJob_t psAccountRegisterJob;
psAccountDelJob_t psAccountDelJob;
psAccountUnregisterJob_t psAccountUnregisterJob;

psAccountSwitchAccounting_t psAccountSwitchAccounting;
psAccountSetGlobalCollect_t psAccountSetGlobalCollect;

psAccountGetDataByLogger_t psAccountGetDataByLogger;
psAccountGetDataByJob_t psAccountGetDataByJob;
psAccountGetSessionInfos_t psAccountGetSessionInfos;
psAccountIsDescendant_t psAccountIsDescendant;
psAccountGetLoggerByClient_t psAccountGetLoggerByClient;
psAccountGetPidsByLogger_t psAccountGetPidsByLogger;

psAccountFindDaemonProcs_t psAccountFindDaemonProcs;
psAccountSignalSession_t psAccountSignalSession;
psAccountGetLocalInfo_t psAccountGetLocalInfo;
psAccountCtlScript_t psAccountCtlScript;

psAccountGetPoll_t psAccountGetPoll;
psAccountSetPoll_t psAccountSetPoll;

#endif  /* __PS_ACCOUNT_INTER */
