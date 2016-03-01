/*
 * ParaStation
 *
 * Copyright (C) 2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PS_ACCOUNT_HISTORY
#define __PS_ACCOUNT_HISTORY

#include "pscommon.h"

void initHist(void);

void saveHist(PStask_ID_t tid);

int findHist(PStask_ID_t tid);

void clearHist(void);

#endif
