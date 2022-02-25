/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_ACCOUNT_HANDLES
#define __PS_ACCOUNT_HANDLES

#include "psaccounttypes.h"  // IWYU pragma: export

/*
 * This file contains definitions of function pointer for each of the
 * functions the psaccount plugin exports to foreign plugins like the
 * batch-system plugins psmom and psslurm. In order to initialize
 * those handles used within a foreign module, a corresponding
 * call to @ref dlsym() must be executed there.
 */

/* For documentation of the specific funtions refer to psaccounttypes.h */

psAccountRegisterJob_t *psAccountRegisterJob;
psAccountDelJob_t *psAccountDelJob;
psAccountUnregisterJob_t *psAccountUnregisterJob;

psAccountSwitchAccounting_t *psAccountSwitchAccounting;
psAccountSetGlobalCollect_t *psAccountSetGlobalCollect;

psAccountGetDataByLogger_t *psAccountGetDataByLogger;
psAccountGetDataByJob_t *psAccountGetDataByJob;
psAccountGetSessionInfos_t *psAccountGetSessionInfos;
psAccountIsDescendant_t *psAccountIsDescendant;
psAccountGetLoggerByClient_t *psAccountGetLoggerByClient;
psAccountGetPidsByLogger_t *psAccountGetPidsByLogger;
psAccountGetEnergy_t *psAccountGetEnergy;
psAccountGetIC_t *psAccountGetIC;

psAccountFindDaemonProcs_t *psAccountFindDaemonProcs;
psAccountSignalSession_t *psAccountSignalSession;

psAccountGetPoll_t *psAccountGetPoll;
psAccountSetPoll_t *psAccountSetPoll;

#endif  /* __PS_ACCOUNT_HANDLES */
