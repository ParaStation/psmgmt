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

#ifndef __PS_PSSLURM_LIMITS
#define __PS_PSSLURM_LIMITS

#include <sys/resource.h>
#include "pluginenv.h"

typedef struct {
    rlim_t limit;
    char *name;
    char *psname;
    int propagate;
} Limits_t;

int initLimits();
void printLimits();
void setRLimits(env_t *env, int psi);

/**
 * @brief Set default ulimits from configuration.
 *
 * @param limits A comma separated string with limits to set.
 *
 * @param soft Flag which should be set to 1 for setting soft limits and
 * 0 for setting hard limits.
 *
 * @return No return value.
 */
void setDefaultRlimits(char *limits, int soft);

#endif
