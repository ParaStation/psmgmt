/*
 * ParaStation
 *
 * Copyright (C) 2014-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSSLURM_PROTO
#define __PSSLURM_PROTO

#include "psslurmtypes.h"
#include "psslurmjob.h"
#include "psslurmcomm.h"
#include "psaccounttypes.h"

typedef struct {
    uint32_t jobid;
    uint32_t stepid;
    uint32_t jobstate;
    uid_t uid;
    char *nodes;
    env_t pelogueEnv;
    env_t spankEnv;
    time_t startTime;
} Req_Terminate_Job_t;

typedef struct {
    uint32_t cpuload;
    uint64_t freemem;
} Resp_Ping_t;

typedef struct {
    time_t now;
    time_t startTime;
    uint32_t status;
    char *nodeName;
    char *arch;
    char *sysname;
    uint16_t cpus;
    uint16_t boards;
    uint16_t sockets;
    uint16_t coresPerSocket;
    uint16_t threadsPerCore;
    uint64_t realMem;
    uint32_t tmpDisk;
    uint32_t uptime;
    uint32_t config;
    uint32_t cpuload;
    uint64_t freemem;
    uint32_t jobInfoCount;
    uint32_t *jobids;
    uint32_t *stepids;
    int protoVersion;
    char verStr[64];
} Resp_Node_Reg_Status_t;

typedef struct {
    AccountDataExt_t *accData;
    uint8_t type;
    bool empty;
    uint32_t nrOfNodes;
    PSnodes_ID_t *nodes;
} SlurmAccData_t;

void sendNodeRegStatus(uint32_t status, int protoVersion);
void getNodesFromSlurmHL(char *slurmNodes, uint32_t *nrOfNodes,
			    PSnodes_ID_t **nodes, uint32_t *localId);
int getSlurmMsgHeader(Slurm_Msg_t *sMsg, Connection_Forward_t *fw);

#define sendSlurmRC(sMsg, rc) __sendSlurmRC(sMsg, rc, __func__, __LINE__)
int __sendSlurmRC(Slurm_Msg_t *sMsg, uint32_t rc,
		    const char *func, const int line);

int __sendSlurmReply(Slurm_Msg_t *sMsg, slurm_msg_type_t type,
			const char *func, const int line);
#define sendSlurmReply(sMsg, type) \
		    __sendSlurmReply(sMsg, type, __func__, __LINE__)

int sendTaskPids(Step_t *step);
void sendLaunchTasksFailed(Step_t *step, uint32_t error);
void sendTaskExit(Step_t *step, int *ctlPort, int *ctlAddr);
void sendStepExit(Step_t *step, int exit_status);
void sendJobExit(Job_t *job, uint32_t exit);
void sendEpilogueComplete(uint32_t jobid, uint32_t rc);
int addSlurmAccData(uint8_t accType, pid_t childPid, PStask_ID_t loggerTID,
			PS_DataBuffer_t *data, PSnodes_ID_t *nodes,
			uint32_t nrOfNodes);
void addSlurmPids(PStask_ID_t loggerTID, PS_DataBuffer_t *data);

int getSlurmNodeID(PSnodes_ID_t psNodeID, PSnodes_ID_t *nodes,
		    uint32_t nrOfNodes);
uint32_t getLocalRankID(uint32_t rank, Step_t *step, uint32_t nodeId);

/**
 * @brief Register message handler function
 *
 * Register the function @a handler to handle all messages of type @a
 * msgType. If @a handler is NULL, all messages of type @a msgType
 * will be silently ignored in the future.
 *
 * @param msgType The message-type to handle.
 *
 * @param handler The function to call whenever a message of type @a
 * msgType has to be handled.
 *
 * @return If a handler for this message-type was registered before,
 * the corresponding function pointer is returned. If this is the
 * first handler registered for this message-type, NULL is returned.
 */
slurmdHandlerFunc_t registerSlurmdMsg(int msgType, slurmdHandlerFunc_t handler);

/**
 * @brief Unregister message handler function
 *
 * Unregister the message-type @a msgType such that it will not be
 * handled in the future. This includes end of silent ignore of this
 * message-type.
 *
 * @param msgType The message-type not to handle any longer.
 *
 * @return If a handler for this message-type was registered before,
 * the corresponding function pointer is returned. If no handler was
 * registered or the message-type was unknown before, NULL is
 * returned.
  */
slurmdHandlerFunc_t clearSlurmdMsg(int msgType);

/**
 * @brief Central protocol switch.
 *
 * Handle the message @a msg according to its message-type. The
 * handler associated to the message-type might be registered via @ref
 * registerSlurmdMsg() and unregistered via @ref clearSlurmdMsg().
 *
 * @param msg The message to handle.
 *
 * @return Always return 0
 */
int handleSlurmdMsg(Slurm_Msg_t *msg);

void initSlurmdProto(void);

void clearSlurmdProto(void);

#endif /* __PSSLURM_PROTO */
