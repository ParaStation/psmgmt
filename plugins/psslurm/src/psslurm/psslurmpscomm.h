/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
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
#include "peloguetypes.h"

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

/**
 * @brief Send a job exit message
 *
 * Inform sister nodes a job or step is finished.
 *
 * @param jobid Unique job identifier
 *
 * @param stepid Unique step identifier
 *
 * @param nrOfNodes The number of nodes in the nodelist
 *
 * @param nodes The nodes to inform
 */
void send_PS_JobExit(uint32_t jobid, uint32_t stepid, uint32_t nrOfNodes,
		     PSnodes_ID_t *nodes);

/**
 * @brief Send a job launch request
 *
 * Send a job launch request holding all information to create
 * a job structure. The job launch request is send from the mother superior to
 * all sister nodes of a job.
 *
 * @param job The job to send the request for
 */
void send_PS_JobLaunch(Job_t *job);

/**
 * @brief Send local epilogue launch
 *
 * Start a local epilogue on all nodes of the allocation.
 *
 * @param alloc The allocation to start the epilogue for
 */
void send_PS_EpilogueLaunch(Alloc_t *alloc);

/**
 * @brief Send result of local epilogue
 *
 * Send result of a local epilogue to allocation leader.
 *
 * @param alloc The allocation of the completed epilogue
 *
 * @param res The result of the epilogue
 */
void send_PS_EpilogueRes(Alloc_t *alloc, int16_t res);

/**
 * @brief Request the status of an epilogue
 *
 * @param alloc The allocation to request the status for
 */
void send_PS_EpilogueStateReq(Alloc_t *alloc);

/**
 * @brief Forward a Slurm message using RDP
 *
 * @param sMsg The Slurm message to forward
 *
 * @param nrOfNodes The number of nodes
 *
 * @param nodes The nodelist to forward the message to
 *
 * @return The total number of bytes sent is returned or
 * -1 if an error occurred.
 */
int forwardSlurmMsg(Slurm_Msg_t *sMsg, uint32_t nrOfNodes, PSnodes_ID_t *nodes);

/**
 * @brief Response for a forwarded message request
 *
 * Send the result of a forwarded message which was processed
 * locally back to the sender.
 *
 * @param The forwarded message to send the result for
 *
 * @return Returns the number of bytes send or -1 on error
 */
int send_PS_ForwardRes(Slurm_Msg_t *msg);

/**
 * @brief Send allocation state change
 *
 * Inform all nodes of an allocation that it changed
 * to a new state.
 *
 * @para alloc The allocation with the new state
 */
void send_PS_AllocState(Alloc_t *alloc);

/**
 * @brief Send pack information to pack leader
 *
 * Send pack information only known by the local mother superior
 * to the leader of the pack.
 *
 * @param step The step to send the information for
 *
 * @return Returns the number of bytes send or -1 on error
 */
int send_PS_PackInfo(Step_t *step);

/**
 * @brief Send pack exit status to pack follower
 *
 * This message is send from the pack leader (MS) node to the
 * pack follower (MS) nodes of a step. Every mother superior of a pack
 * has to send a step exit message to slurmctld. So the pack leader has
 * to distribute the compound exit status of mpiexec to all its followers.
 *
 * @param step The step to send the information for
 *
 * @param exitStatus The compound exit status of the step
 *
 * @return Returns the number of bytes send or -1 on error
 */
int send_PS_PackExit(Step_t *step, int32_t exitStatus);

/**
 * @brief Set a node offline using psexec
 *
 * Set a node in Slurm offline using psexec to call an offline-script.
 * The script is executed on the primary slurmctld node. If the execution
 * fails the offline-script is executed again on the backup slurmctld node.
 *
 * @param env The Slurm environment forwarded to the offline script
 *
 * @param id The jobid executed while the node was set offline
 *
 * @param host The host to set offline
 *
 * @param reason The reason why the node was set offline
 */
void setNodeOffline(env_t *env, uint32_t id, const char *host,
		    const char *reason);

void requeueBatchJob(Job_t *job, PSnodes_ID_t dest);

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
 * @brief Lookup hostname from PS node ID
 *
 * Lookup the hostname from a ParaStation node ID.
 * Warning: the function will *ONLY* work for compute nodes
 * configured in slurm.conf. The used hash table is build
 * at startup of the psslurm plugin.
 *
 * @param nodeID The PS node ID to lookup
 *
 * @return Returns the requested hostname or NULL on error
 */
const char *getSlurmHostbyNodeID(PSnodes_ID_t nodeID);

/**
 * @brief Lookup PS nodeID by hostname (address)
 *
 * Lookup the ParaStation node ID by hostname.
 * Warning: the function will *ONLY* work for compute nodes
 * configured in slurm.conf. The used lookup table is build
 * at startup of the psslurm plugin.
 *
 * @param addr The hostname to lookup
 *
 * @return Returns the requested nodeID or -1 on error
 */
PSnodes_ID_t getNodeIDbySlurmHost(const char *host);

/**
 * @brief Find the next index in a combined pack task array
 *
 * @param step The step to search
 *
 * @param last Shall be set to the last offset or -1 for the first
 * call
 *
 * @param offset Holds the array offset of the next index
 *
 * @param idx Holds the result (the next index)
 *
 * @return Returns true if the next index was found otherwise
 * false is returned. On error offset and idx might not be updated.
 */
bool findPackIndex(Step_t *step, int64_t last, int64_t *offset, uint32_t *idx);

/**
 * @brief Send pelogue stdout/stderr data
 *
 * @para alloc The allocation of the pelogue
 *
 * @param oeData The data to send
 */
void sendPElogueOE(Alloc_t *alloc, PElogue_OEdata_t *oeData);

/**
 * @brief Initialize PScomm facility
 *
 * Initialize the facility handling communication via psid. This
 * includes registering various psid messages and hooks
 *
 * @return On success true is returned or false otherwise
 */
bool initPScomm(void);

/**
 * @brief Finalize PScomm facility
 *
 * Initialize the facility handling communication via psid. This
 * includes cleaning up various psid messages and hooks. If @a verbose
 * is true, different problems when doing so are reported.
 *
 * @param verbose More verbose reports on problems
 *
 * @return No return value
 */
void finalizePScomm(bool verbose);

#endif
