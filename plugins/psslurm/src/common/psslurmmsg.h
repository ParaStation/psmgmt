/*
 * ParaStation
 *
 * Copyright (C) 2014 - 2015 ParTec Cluster Competence Center GmbH, Munich
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
    PSP_QUEUE = 3,	    /* obsolete */
    PSP_QUEUE_RES,	    /* obsolete */
    PSP_START,		    /* obsolete */
    PSP_START_RES,	    /* obsolete */
    PSP_DELETE,		    /* obsolete */
    PSP_DELETE_RES,	    /* obsolete */
    PSP_PROLOGUE_START,	    /* obsolete */
    PSP_PROLOGUE_RES,	    /* obsolete */
    PSP_EPILOGUE_START,	    /* obsolete */
    PSP_EPILOGUE_RES,	    /* obsolete */
    PSP_JOB_INFO,	    /* obsolete */
    PSP_JOB_INFO_RES,	    /* obsolete */
    PSP_TASK_IDS,	    /* obsolete */
    PSP_REMOTE_JOB,	    /* obsolete */
    PSP_SIGNAL_TASKS,
    PSP_JOB_EXIT,
    PSP_JOB_LAUNCH,
    PSP_JOB_STATE_REQ,
    PSP_JOB_STATE_RES,
    PSP_FORWARD_SMSG,
    PSP_FORWARD_SMSG_RES,
    PSP_LAUNCH_TASKS,
} PSP_PSSLURM_t;

char *msg2Str(PSP_PSSLURM_t type);

#endif
