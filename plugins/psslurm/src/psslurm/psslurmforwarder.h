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

#ifndef __PS_SLURM_FORWARDER
#define __PS_SLURM_FORWARDER

#include "psslurmjob.h"

void execUserStep(Step_t *step);
void execUserJob(Job_t *job);
char *replaceSymbols(uint32_t jobid, uint32_t stepid, char *hostname,
			int nodeid, char *username, uint32_t arrayJobId,
			int rank, char *path);
void printChildMessage(Forwarder_Data_t *fwdata, char *msg, int error);
int handleExecClient(void * data);
int handleForwarderInit(void * data);

#endif
