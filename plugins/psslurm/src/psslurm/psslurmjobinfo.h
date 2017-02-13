/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PS_SLURM_JOB_INFO
#define __PS_SLURM_JOB_INFO

#include <stdint.h>
#include "pstask.h"

typedef struct {
    uint32_t jobid;
    uint32_t stepid;
    uint32_t timeout;
    PStask_ID_t logger;
    PStask_ID_t mother;
    struct list_head list;  /* the jobinfo list header */
} JobInfo_t;

JobInfo_t JobInfoList;

void initJobInfo(void);
JobInfo_t *addJobInfo(uint32_t jobid, uint32_t stepid, PStask_ID_t mother);
JobInfo_t *findJobInfoByLogger(PStask_ID_t logger);
int deleteJobInfo(uint32_t jobid, uint32_t stepid);
void clearJobInfoList(void);

#endif
