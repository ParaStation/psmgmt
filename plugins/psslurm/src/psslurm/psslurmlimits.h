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
void setRlimitsFromEnv(env_t *env, int psi);
void setHardRlimits();

#endif
