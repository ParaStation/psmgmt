/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PS_PSSLURM_MSG
#define __PS_PSSLURM_MSG

typedef enum {
    PSP_QUEUE = 3,
    PSP_QUEUE_RES,
    PSP_START,
    PSP_START_RES,
    PSP_DELETE,
    PSP_DELETE_RES,
    PSP_PROLOGUE_START,
    PSP_PROLOGUE_RES,
    PSP_EPILOGUE_START,
    PSP_EPILOGUE_RES,
    PSP_JOB_INFO,
    PSP_JOB_INFO_RES,
    PSP_TASK_IDS,
    PSP_REMOTE_JOB,
} PSP_PSSLURM_t;

char *msg2Str(PSP_PSSLURM_t type);

#endif
