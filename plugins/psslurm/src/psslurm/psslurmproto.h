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

#ifndef __PS_SLURM_PROTO
#define __PS_SLURM_PROTO

#include "psslurmjob.h"
#include "psslurmcomm.h"

void sendNodeRegStatus(uint32_t status, int protoVersion);
void getNodesFromSlurmHL(char *slurmNodes, uint32_t *nrOfNodes,
			    PSnodes_ID_t **nodes);
int getSlurmMsgHeader(Slurm_Msg_t *sMsg, Connection_Forward_t *fw);

#define sendSlurmRC(sMsg, rc) __sendSlurmRC(sMsg, rc, __func__, __LINE__)
int __sendSlurmRC(Slurm_Msg_t *sMsg, uint32_t rc,
		    const char *func, const int line);

#define sendSlurmReply(sMsg, type, body) \
		    __sendSlurmReply(sMsg, type, body, __func__, __LINE__)
int __sendSlurmReply(Slurm_Msg_t *sMsg, slurm_msg_type_t type,
		    PS_DataBuffer_t *body, const char *func, const int line);

int writeJobscript(Job_t *job, char *script);
int handleSlurmdMsg(Slurm_Msg_t *msg);

int sendTaskPids(Step_t *step);
void sendTaskExit(Step_t *step, int exit_status);
void sendStepExit(Step_t *step, int exit_status);
void sendJobExit(Job_t *job, uint32_t exit);
void sendEpilogueComplete(uint32_t jobid, uint32_t rc);
void startPElogue(uint32_t jobid, uid_t uid, gid_t gid, uint32_t nrOfNodes,
		    PSnodes_ID_t *nodes, env_t *env, env_t *spankenv,
		    int step, int prologue);
int addSlurmAccData(uint8_t accType, pid_t childPid, PStask_ID_t loggerTID,
			PS_DataBuffer_t *data, PSnodes_ID_t *nodes,
			uint32_t nrOfNodes);
void addSlurmPids(PStask_ID_t loggerTID, PS_DataBuffer_t *data);

int getSlurmNodeID(PSnodes_ID_t psNodeID, PSnodes_ID_t *nodes,
		    uint32_t nrOfNodes);
uint32_t getMyNodeIndex(PSnodes_ID_t *nodes, uint32_t nrOfNodes);
uint32_t getLocalRankID(uint32_t rank, Step_t *step, uint32_t nodeId);

#endif
