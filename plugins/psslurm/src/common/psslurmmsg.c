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

#include <stdlib.h>
#include <stdio.h>

#include "psslurmmsg.h"

char *msg2Str(PSP_PSSLURM_t type)
{
    switch(type) {
	case PSP_QUEUE:
	    return "QUEUE";
	case PSP_QUEUE_RES:
	    return "QUEUE_RES";
	case PSP_START:
	    return "START";
	case PSP_START_RES:
	    return "START_RES";
	case PSP_DELETE:
	    return "DELETE";
	case PSP_DELETE_RES:
	    return "DELETE_RES";
	case PSP_PROLOGUE_START:
	    return "PROLOGUE_START";
	case PSP_PROLOGUE_RES:
	    return "PROLOGUE_RES";
	case PSP_EPILOGUE_START:
	    return "EPILOGUE_START";
	case PSP_EPILOGUE_RES:
	    return "EPILOGUE_RES";
	case PSP_JOB_INFO:
	    return "JOB_INFO";
	case PSP_JOB_INFO_RES:
	    return "JOB_INFO_RES";
	case PSP_TASK_IDS:
	    return "PSP_TASK_IDS";
	case PSP_REMOTE_JOB:
	    return "PSP_REMOTE_JOB";
	case PSP_SIGNAL_TASKS:
	    return "PSP_SIGNAL_TASKS";
	case PSP_JOB_EXIT:
	    return "PSP_JOB_EXIT";
	case PSP_JOB_LAUNCH:
	    return "PSP_JOB_LAUNCH";
	case PSP_JOB_STATE_REQ:
	    return "PSP_JOB_STATE_REQ";
	case PSP_JOB_STATE_RES:
	    return "PSP_JOB_STATE_RES";
    }
    return NULL;
}
