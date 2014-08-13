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

#ifndef __PS_SLURM_AUTH
#define __PS_SLURM_AUTH

#include "psslurmjob.h"
#include "plugincomm.h"
#include "psslurmcomm.h"

void addSlurmAuth(PS_DataBuffer_t *data);
int testMungeAuth(char **ptr, Slurm_msg_header_t *msgHead);
JobCred_t *getJobCred(char **ptr, uint16_t version);
int checkJobCred(Job_t *job);
int checkStepCred(Step_t *step);
int checkAuthorizedUser(uid_t user, uid_t test);

#endif
