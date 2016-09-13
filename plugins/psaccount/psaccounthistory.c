/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>

#include "psaccountlog.h"
#include "pluginmalloc.h"

#include "psaccounthistory.h"

#define HIST_SIZE 10

static PStask_ID_t *savedJobs = NULL;

static int idx = 0;

void initHist(void)
{
    int i;

    savedJobs = umalloc(sizeof(PStask_ID_t) * HIST_SIZE);

    for (i=0; i<HIST_SIZE; i++) {
	savedJobs[i] = -1;
    }
}

void saveHist(PStask_ID_t tid)
{
    if (idx >= HIST_SIZE) idx = 0;

    savedJobs[idx++] = tid;
}

int findHist(PStask_ID_t tid)
{
    int i;

    for (i=0; i<HIST_SIZE; i++) {
	if (savedJobs[i] == tid) return 1;
    }

    return 0;
}

void clearHist(void)
{
    ufree(savedJobs);
}
