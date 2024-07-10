/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "psaccounthistory.h"

#define HIST_SIZE 10

static PStask_ID_t savedJobs[HIST_SIZE];

static int idx = -1;

void saveHist(PStask_ID_t tid)
{
    if (idx < 0) for (int i = 0; i < HIST_SIZE; i++) savedJobs[i] = -1;

    idx = (idx+1) % HIST_SIZE;
    savedJobs[idx] = tid;
}

bool findHist(PStask_ID_t tid)
{
    if (idx < 0) return false;

    for (int i = 0; i < HIST_SIZE; i++) if (savedJobs[i] == tid) return true;

    return false;
}
