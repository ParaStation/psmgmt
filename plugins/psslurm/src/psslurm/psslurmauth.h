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

#ifndef __PS_SLURM_AUTH
#define __PS_SLURM_AUTH

#include "psslurmjob.h"
#include "plugincomm.h"
#include "psslurmcomm.h"

void addSlurmAuth(PS_DataBuffer_t *data);
int testMungeAuth(char **ptr, Slurm_Msg_Header_t *msgHead);
JobCred_t *getJobCred(Gres_Cred_t *gres, char **ptr, uint16_t version);
void deleteJobCred(JobCred_t *cred);
int checkJobCred(Job_t *job);
int checkStepCred(Step_t *step);
int checkBCastCred(char **ptr, BCast_t *bcast);
int checkAuthorizedUser(uid_t user, uid_t test);

#endif
