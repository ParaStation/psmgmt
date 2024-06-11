/*
 * ParaStation
 *
 * Copyright (C) 2019-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Implementation of small common pspmix helper functions
 */
#include "pspmixcommon.h"

#include "pluginconfig.h"

#include "pspmixlog.h"
#include "pspmixconfig.h"

/* decide if this job wants to use PMIx */
bool __pspmix_common_usePMIx(const env_t env, const char* func) {
    /* PMIX_JOB_SIZE is set by mpiexec iff called with '--pmix' and by the
     * spawning forwarder in case of PMIx_Spawn() */
    if (envGet(env, "PMIX_JOB_SIZE")) {
	mdbg(PSPMIX_LOG_VERBOSE, "%s: PMIx support requested by mpiexec\n",
	     func);
	return true;
    }
    mdbg(PSPMIX_LOG_VERBOSE, "%s: No PMIx support requested by mpiexec\n", func);

    if (getConfValueI(config, "SUPPORT_MPI_SINGLETON")) {
	mdbg(PSPMIX_LOG_VERBOSE, "%s: Singleton support enabled\n", func);
    }

    return false;
}

uint32_t getResSize(PSresinfo_t *resInfo)
{
    /* reservations are always compact, there are no gaps in the ranks used
     * by the entries, so this calculation is sufficient */
    return resInfo->maxRank - resInfo->minRank + 1;
}
/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
