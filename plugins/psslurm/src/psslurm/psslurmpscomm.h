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

#ifndef __PS_SLURM_PSCOMM
#define __PS_SLURM_PSCOMM

#include "psprotocol.h"
#include "pslog.h"
#include "psslurmjob.h"

#include "psslurmcomm.h"

int handleCreatePart(void *msg);

int handleCreatePartNL(void *msg);

int handleNodeDown(void *nodeID);

void handleDroppedMsg(DDTypedBufferMsg_t *msg);

void handlePsslurmMsg(DDTypedBufferMsg_t *msg);

void callbackPElogue(char *jobid, int exit_status, int timeout);

void handleChildBornMsg(DDErrorMsg_t *msg);

void send_PS_SignalTasks(Step_t *step, int signal, PStask_group_t group);

void send_PS_JobExit(uint32_t jobid, uint32_t stepid, uint32_t nrOfNodes,
			PSnodes_ID_t *nodes);

void send_PS_JobLaunch(Job_t *job);

void send_PS_JobState(uint32_t jobid, PStask_ID_t dest);

void forwardSlurmMsg(Slurm_Msg_t *sMsg, Connection_Forward_t *fw);

void send_PS_ForwardRes(Slurm_Msg_t *msg, PS_DataBuffer_t *body);

void handleCCMsg(PSLog_Msg_t *msg);

void handleCCError(PSLog_Msg_t *msg);

#endif
