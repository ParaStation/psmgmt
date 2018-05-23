/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_PSCOMM
#define __PS_SLURM_PSCOMM

#include <stdbool.h>

#include "list.h"

#include "psprotocol.h"
#include "pslog.h"
#include "psslurmjob.h"
#include "psslurmalloc.h"

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

void send_PS_SignalTasks(Step_t *step, uint32_t signal, PStask_group_t group);

void send_PS_JobExit(uint32_t jobid, uint32_t stepid, uint32_t nrOfNodes,
			PSnodes_ID_t *nodes);

void send_PS_JobLaunch(Job_t *job);

void send_PS_JobState(uint32_t jobid, PStask_ID_t dest);

void forwardSlurmMsg(Slurm_Msg_t *sMsg, Msg_Forward_t *fw);

int send_PS_ForwardRes(Slurm_Msg_t *msg);

void setNodeOffline(env_t *env, uint32_t id, PSnodes_ID_t dest,
			const char *host, char *reason);

void requeueBatchJob(Job_t *job, PSnodes_ID_t dest);

void send_PS_AllocLaunch(Alloc_t *alloc);

void send_PS_AllocState(Alloc_t *alloc);

/**
 * @brief Send pack information to leader ms
 *
 * Send pack information only known by the local mother superior
 * to the leader of the pack.
 *
 * @param step The step to send the information for
 */
void send_PS_PackInfo(Step_t *step);

/**
 * @brief Free all unhandled cached messages for a step
 *
 * @param jobid The jobid of the step
 *
 * @param stepid The stepid of the step
 */
void deleteCachedMsg(uint32_t jobid, uint32_t stepid);

/**
 * @brief Handle all cached messages for a step
 *
 * @param step The step to handle
 */
void handleCachedMsg(Step_t *step);

/**
 * @brief Find PS nodeID by hostname (address)
 *
 * @param addr The hostname to lookup
 *
 * @return Returns the requested nodeID or -1 on error
 */
PSnodes_ID_t getNodeIDbyHost(char *host);

/**
 * @brief Initialzie PScomm facility
 *
 * Initialize the facility handling communication via psid. This
 * includes registering various psid messages and hooks
 *
 * @return On success true is returned of false otherwise
 */
bool initPScomm(void);

/**
 * @brief Finalize PScomm facility
 *
 * Initialize the facility handling communication via psid. This
 * includes cleaning up various psid messages and hooks. If @a verbose
 * is true, diferent problems when doing so are reported.
 *
 * @param verbose More verbose reports on problems
 *
 * @return No return value
 */
void finalizePScomm(bool verbose);

#endif
