/*
 * ParaStation
 *
 * Copyright (C) 2014-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_SLURM_PSCOMM
#define __PS_SLURM_PSCOMM

#include "list.h"

#include "psprotocol.h"
#include "pslog.h"
#include "psslurmjob.h"

#include "psslurmcomm.h"

/**
 * Release delayed spawns buffered for a specific jobstep. This is
 * done once the corresponding step has been created.
 *
 * @param  jobid   JodID
 * @param  stepid  StepID
 */
void releaseDelayedSpawns(uint32_t jobid, uint32_t stepid);

/**
 * Cleanup all delayed spawns for a specific jobstep.
 * This is done as part of the cleanup if a step failed.
 *
 * @param  jobid   JodID
 * @param  stepid  StepID
 */
void cleanupDelayedSpawns(uint32_t jobid, uint32_t stepid);

int handleCreatePart(void *msg);

int handleCreatePartNL(void *msg);

int handleNodeDown(void *nodeID);

void handleDroppedMsg(DDTypedBufferMsg_t *msg);

void handlePsslurmMsg(DDTypedBufferMsg_t *msg);

void handleChildBornMsg(DDErrorMsg_t *msg);

void send_PS_SignalTasks(Step_t *step, int signal, PStask_group_t group);

void send_PS_JobExit(uint32_t jobid, uint32_t stepid, uint32_t nrOfNodes,
			PSnodes_ID_t *nodes);

void send_PS_JobLaunch(Job_t *job);

void send_PS_JobState(uint32_t jobid, PStask_ID_t dest);

void forwardSlurmMsg(Slurm_Msg_t *sMsg, Connection_Forward_t *fw);

int send_PS_ForwardRes(Slurm_Msg_t *msg);

void handleCCMsg(PSLog_Msg_t *msg);

void handleSpawnFailed(DDErrorMsg_t *msg);

void handleSpawnReq(DDTypedBufferMsg_t *msg);

void setNodeOffline(env_t *env, uint32_t id, PSnodes_ID_t dest,
			const char *host, char *reason);

void requeueBatchJob(Job_t *job, PSnodes_ID_t dest);

void send_PS_AllocLaunch(Alloc_t *alloc);

void send_PS_AllocState(Alloc_t *alloc);

#endif
