/*
 * ParaStation
 *
 * Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_FW_COMM
#define __PS_SLURM_FW_COMM

#include <stdint.h>

#include "pslog.h"
#include "pluginforwarder.h"

#include "psslurmjob.h"
#include "psslurmstep.h"
#include "psslurmtasks.h"

/**
 * @brief Handle a message from step forwarder sendto mother
 *
 * @param msg The message to handle
 *
 * @param fwdata The forwarder management structure
 *
 * @return Returns 1 on success and 0 otherwise
 */
int fwCMD_handleFwStepMsg(PSLog_Msg_t *msg, Forwarder_Data_t *fwdata);

/**
 * @brief Handle a message from mother sendto step forwarder
 *
 * @param msg The message to handle
 *
 * @param fwdata The forwarder management structure
 *
 * @return Returns 1 on success and 0 otherwise
 */
int fwCMD_handleMthrStepMsg(PSLog_Msg_t *msg, Forwarder_Data_t *fwdata);

/**
 * @brief Handle a message from mother sendto job forwarder
 *
 * @param msg The message to handle
 *
 * @param fwdata The forwarder management structure
 *
 * @return Returns 1 on success and 0 otherwise
 */
int fwCMD_handleMthrJobMsg(PSLog_Msg_t *msg, Forwarder_Data_t *fwdata);

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
 * @brief Send CMD_PRINT_CHILD_MSG to a forwarder
 *
 * Print a message for a job or a step using the forwarder. The parameters
 * @ref job and @ref step are mutually exclusive.
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
 * @param rank The rank of the message origin (only
 * used for a step)
 */
void fwCMD_printMsg(Job_t *job, Step_t *step, char *plMsg, uint32_t msgLen,
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

#endif  /* __PS_SLURM_FW_COMM */
