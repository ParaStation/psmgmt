/*
 * ParaStation
 *
 * Copyright (C) 2019 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

/**
 * @file Implementation of small common pspmix helper functions
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "pstask.h"

#include "pspmixlog.h"

#include "pspmixcommon.h"

/* descide if this job wants to use PMIx */
bool __pspmix_common_usePMIx(PStask_t *task, const char* func) {
    for (uint32_t i = 0; i < task->envSize; i++) {
	if (strncmp(task->environ[i], "__PMIX_NODELIST=", 16) == 0) {
	    mdbg(PSPMIX_LOG_VERBOSE, "%s: PMIx support requested by mpiexec.\n",
		    __func__);
	    return true;
	}
    }
    mdbg(PSPMIX_LOG_VERBOSE, "%s: No PMIx support requested by mpiexec.\n",
	    func);
    return false;
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
