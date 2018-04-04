/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_PSSLURM_LIMITS
#define __PS_PSSLURM_LIMITS

#include <sys/resource.h>

#include "psenv.h"

/** @doctodo */
typedef struct {
    rlim_t limit;
    char *name;
    char *psname;
    int propagate;
} Limits_t;

/**
 * @doctodo
 */
int initLimits(void);

/**
 * @doctodo
 *
 * @return No return value
 */
void printLimits(void);

/**
 * @doctodo
 *
 * @return No return value
 */
void setRlimitsFromEnv(env_t *env, int psi);

/**
 * @doctodo
 *
 * @return No return value
 */
void setDefaultRlimits(void);

#endif  /* __PS_PSSLURM_LIMITS */
