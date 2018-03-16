/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_ENV
#define __PS_SLURM_ENV

#include "psslurmjob.h"

extern char **envFilter;

int initEnvFilter(void);
void freeEnvFilter(void);
void setBatchEnv(Job_t *job);
void setStepEnv(Step_t *step);
void setSlurmJobEnv(Job_t *job);
void setRankEnv(int32_t rank, Step_t *step);
void removeUserVars(env_t *env);

#endif
