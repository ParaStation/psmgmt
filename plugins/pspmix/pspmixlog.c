/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pspmixlog.h"

#include "pscommon.h"
#include "pluginlog.h"

logger_t *pmixlogger = NULL;
pthread_mutex_t __mlock = PTHREAD_MUTEX_INITIALIZER;

const char *pspmix_getMsgTypeString(PSP_PSPMIX_t type)
{
    switch(type) {
	case PSPMIX_ADD_JOB:
	    return "PSPMIX_ADD_JOB";
	case PSPMIX_REMOVE_JOB:
	    return "PSPMIX_REMOVE_JOB";
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
	case PSPMIX_CLIENT_INIT:
	    return "PSPMIX_CLIENT_INIT";
	case PSPMIX_CLIENT_INIT_RES:
	    return "PSPMIX_CLIENT_INIT_RES";
	case PSPMIX_CLIENT_FINALIZE:
	    return "PSPMIX_CLIENT_FINALIZE";
	case PSPMIX_CLIENT_FINALIZE_RES:
	    return "PSPMIX_CLIENT_FINALIZE_RES";
	case PSPMIX_FENCE_DATA:
	    return "PSPMIX_FENCE_DATA";
	case PSPMIX_CLIENT_SPAWN:
	    return "PSPMIX_CLIENT_SPAWN";
	case PSPMIX_CLIENT_SPAWN_RES:
	    return "PSPMIX_CLIENT_SPAWN_RES";
	case PSPMIX_SPAWN_INFO:
	    return "PSPMIX_SPAWN_INFO";
	case PSPMIX_CLIENT_STATUS:
	    return "PSPMIX_CLIENT_STATUS";
	case PSPMIX_TERM_CLIENTS:
	    return "PSPMIX_TERM_CLIENTS";
	default:
	{
	    static char buf[32];
	    snprintf(buf, sizeof(buf), "<unknown> (%d)", type);
	    return buf;
	}
    }
}

const char *pspmix_jobStr(PspmixJob_t *job)
{
    return pspmix_jobIDsStr(job->session->ID, job->ID);
}

const char *pspmix_jobIDsStr(PStask_ID_t sessID, PStask_ID_t jobID)
{
    static char str[64];
    int n = snprintf(str, sizeof(str), "session %s", PSC_printTID(sessID));
    n += snprintf(str+n, sizeof(str) - n, " job %s", PSC_printTID(jobID));

    return str;
}

void pspmix_initLogger(char *name, FILE *logfile)
{
    pmixlogger = logger_init(name, logfile);
    initPluginLogger(name, logfile);
}

void pspmix_maskLogger(int32_t mask)
{
    logger_setMask(pmixlogger, mask);
}

void pspmix_finalizeLogger(void)
{
    logger_finalize(pmixlogger);
    pmixlogger = NULL;
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
