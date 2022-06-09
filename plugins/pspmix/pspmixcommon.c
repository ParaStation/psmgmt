/*
 * ParaStation
 *
 * Copyright (C) 2019-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Implementation of small common pspmix helper functions
 */
#include "pspmixcommon.h"

#include "pspmixlog.h"
#include "pspmixconfig.h"

/* decide if this job wants to use PMIx
 * Relies on __USE_PMIX set in hookRecvSpawnReq() in pspmixdaemon.c */
bool __pspmix_common_usePMIx(const env_t *env, const char* func) {
    if (envGet(env, "__USE_PMIX")) {
	mdbg(PSPMIX_LOG_VERBOSE, "%s: PMIx support requested by mpiexec\n",
	     func);
	return true;
    }
    mdbg(PSPMIX_LOG_VERBOSE, "%s: No PMIx support requested by mpiexec\n", func);

    if (getConfValueI(&config, "SUPPORT_MPI_SINGLETON")) {
	mdbg(PSPMIX_LOG_VERBOSE, "%s: Singleton support enabled\n", func);
    }

    return false;
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
