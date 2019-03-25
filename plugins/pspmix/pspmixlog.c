/*
 * ParaStation
 *
 * Copyright (C) 2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <pthread.h>

#include "pluginlog.h"

#include "pspmixtypes.h"

#include "pspmixlog.h"

logger_t *pmixlogger = NULL;
FILE *pmixlogfile = NULL;
pthread_mutex_t __mlock = PTHREAD_MUTEX_INITIALIZER;

const char *pspmix_getMsgTypeString(PSP_PSPMIX_t type)
{
    switch(type) {
	case PSPMIX_REGISTER_CLIENT:
	    return "PSPMIX_REGISTER_CLIENT";
	case PSPMIX_CLIENT_PMIX_ENV:
	    return "PSPMIX_CLIENT_PMIX_ENV";
	case PSPMIX_FENCE_IN:
	    return "PSPMIX_FENCE_IN";
	case PSPMIX_FENCE_OUT:
	    return "PSPMIX_FENCE_OUT";
	case PSPMIX_MODEX_DATA_REQ:
	    return "PSPMIX_MODEX_DATA_REQ";
	case PSPMIX_MODEX_DATA_RES:
	    return "PSPMIX_MODEX_DATA_RES";
	default:
	    return "<unknown>";
    }
}

void pspmix_initLogger(FILE *logfile)
{
    pmixlogger = logger_init("pspmix", logfile);
    initPluginLogger(NULL, logfile);
    pmixlogfile = logfile;
}

void pspmix_maskLogger(int32_t mask)
{
    logger_setMask(pmixlogger, mask);
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
