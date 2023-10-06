/*
 * ParaStation
 *
 * Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_FW_COMM
#define __PS_SLURM_FW_COMM

#include <stdbool.h>
#include <stdint.h>

#include "pslog.h"
#include "psprotocol.h"
#include "pluginforwarder.h"

#include "psslurmjob.h"
#include "psslurmstep.h"
#include "psslurmtasks.h"

/** holding messages forwarded to user after forwarder started */
typedef struct {
    list_t next;		/**< used to put into some msg-list */
    char *msg;			/**< the actual message to send */
    uint32_t msgLen;		/**< length of the message */
    uint8_t type;		/**< msg type (stdout/stderr ) */
    int32_t rank;		/**< sender (Slurm) rank */
} FwUserMsgBuf_t;

/**
 * @brief Handle a message from step forwarder sendto mother
 *
 * @param msg The message to handle
 *
 * @param fwdata The forwarder management structure
 *
 * @return Return true if message was handled or false otherwise
 */
bool fwCMD_handleFwStepMsg(DDTypedBufferMsg_t *msg, Forwarder_Data_t *fwdata);

/**
 * @brief Handle a message from mother sendto step forwarder
 *
 * @param msg The message to handle
 *
 * @param fwdata The forwarder management structure
 *
 * @return Return true if message was handled or false otherwise
 */
bool fwCMD_handleMthrStepMsg(DDTypedBufferMsg_t *msg, Forwarder_Data_t *fwdata);

/**
 * @brief Handle a message from mother sendto job forwarder
 *
 * @param msg The message to handle
 *
 * @param fwdata The forwarder management structure
 *
 * @return Return true if message was handled or false otherwise
 */
bool fwCMD_handleMthrJobMsg(DDTypedBufferMsg_t *msg, Forwarder_Data_t *fwdata);

/**
 * @brief Send CMD_STEP_TIMEOUT to a forwarder
 *
 * @param fwdata The forwarder management structure
 */
void fwCMD_stepTimeout(Forwarder_Data_t *fwdata);

/**
 * @brief Send CMD_BROKE_IO_CON to a forwarder
 *
 * @param step The step to set the broken connection flag
 */
void fwCMD_brokeIOcon(Step_t *step);

/**
 * @brief Send CMD_FW_FINALIZE to a forwarder
 *
 * @param fwdata The forwarder management structure
 *
 * @param msg The PSLog finalize message to forward
 */
void fwCMD_finalize(Forwarder_Data_t *fwdata, PSLog_Msg_t *msg);

/**
 * @brief Clear job/step forwarder message queue
 *
 * @param queue The queue to clear
 */
void clearFwMsgQueue(list_t *queue);

/**
 * @brief Send CMD_PRINT_CHILD_MSG to a forwarder
 *
 * Print a message for a job or a step using its forwarder. The
 * parameters @a job and @a step are mutually exclusive.
 *
 * If no matching forwarder is present in the given job/step, the
 * message is queued and delivered after the corresponding forwarder
 * was started. The queued messages are saved in the job/step
 * structure's member fwMsgQueue and must be removed via @ref
 * clearFwMsgQueue() when the job/step is purged.
 *
 * The function is able to handle negative ranks from psid service
 * processes (e.g. psilogger). Since srun only accepts I/O messages
 * from known ranks, a negative rank will be mapped to the first node
 * local rank of the step.  It is *important* to note that for this
 * mapping to work the @ref step->localNodeId has to be initialized
 * before use!
 *
 * @param job The job to print the message for
 *
 * @param step The step to print the message for
 *
 * @param msg The message to print
 *
 * @param msgLen The length of the message
 *
 * @param type The message type (stdout or stderr)
 *
 * @param rank The rank of the message origin (only used for a step)
 *
 * @return Retuns 0 on success and -1 on error. If the messages was
 * queued and waiting for delivery or the forwarder is already gone 1
 * is returned.
 */
int fwCMD_printMsg(Job_t *job, Step_t *step, char *plMsg, uint32_t msgLen,
		   uint8_t type, int32_t rank);

/**
 * @brief Send CMD_REATTACH_TASKS to a forwarder
 *
 * @param fwdata The forwarder management structure
 *
 * @param addr The address of sattach
 *
 * @param ioPort The I/O port of sattach
 *
 * @param ctlPort The control port of sattach
 *
 * @param sig The signature to validate the connection
 */
void fwCMD_reattachTasks(Forwarder_Data_t *fwdata, uint32_t addr,
			 uint16_t ioPort, uint16_t ctlPort, char *sig);

/**
 * @brief Send CMD_INFO_TASKS to a forwarder
 *
 * @param fwdata The forwarder management structure
 *
 * @param task The task to send info about
 */
void fwCMD_taskInfo(Forwarder_Data_t *fwdata, PS_Tasks_t *task);

/**
 * @brief Send CMD_ENABLE_SRUN_IO to a forwarder
 *
 * @param step The step to enable the srun communication for
 */
void fwCMD_enableSrunIO(Step_t *step);

/**
 * @brief Send CMD_PRINT_CHILD_MSG to a forwarder
 *
 * @param The step to print the message for
 *
 * @param lmsg The PSLog message to print
 *
 * @param senderRank The origin of the message
 */
void fwCMD_msgSrunProxy(Step_t *step, PSLog_Msg_t *lmsg, int32_t senderRank);

/**
 * @brief Send CMD_SETENV to mother
 *
 * Let SPANK hooks running in the psslurm forwarder modify the step
 * environment of the mother psid. Therefore the environment can be
 * changed before the user processes will inherit them.
 *
 * @param step The step which holds the environment to modify
 *
 * @param var The name of the environment variable to set
 *
 * @param val The value to set
 */
void fwCMD_setEnv(Step_t *step, const char *var, const char *val);

/**
 * @brief Send CMD_UNSETENV to mother
 *
 * Let SPANK hooks running in the psslurm forwarder modify the step
 * environment of the mother psid. Therefore the environment can be
 * changed before the user processes will inherit them.
 *
 * @param step The step which holds the environment to modify
 *
 * @param var The name of the environment variable to unset
 */
void fwCMD_unsetEnv(Step_t *step, const char *var);

/**
 * @brief Send CMD_INIT_COMPLETE to mother
 *
 * Inform the mother psid that the psslurmstep forwarder
 * successfully complete the initialization phase including
 * SPANK hooks.
 *
 * @param step The step the forwarder is responsible for
 */
void fwCMD_initComplete(Step_t *step);

#endif  /* __PS_SLURM_FW_COMM */
