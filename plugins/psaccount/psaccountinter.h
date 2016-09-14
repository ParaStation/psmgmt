/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
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
psAccountReadProcStatInfo_t psAccountReadProcStatInfo;

psAccountFindDaemonProcs_t psAccountFindDaemonProcs;
psAccountSignalChildren_t psAccountSignalChildren;
psAccountSignalSession_t psAccountSignalSession;

#endif  /* __PS_ACCOUNT_INTER */
