/*
 * ParaStation
 *
 * Copyright (C) 2014 - 2015 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PS_SLURM_FORWARDER
#define __PS_SLURM_FORWARDER

#include "psslurmjob.h"

#define CMD_PRINT_CHILD_MSG 100
#define CMD_ENABLE_SRUN_IO  101
#define CMD_FW_FINALIZE	    102

int execUserStep(Step_t *step);
int execUserJob(Job_t *job);
int execUserBCast(BCast_t *bcast);
int execStepFWIO(Step_t *step);
int handleExecClient(void * data);
int handleForwarderInit(void * data);

#endif
