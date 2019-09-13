/*
 * ParaStation
 *
 * Copyright (C) 2015-2019 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_IO
#define __PS_SLURM_IO

#include "pslog.h"
#include "psslurmjob.h"
#include "psslurmtasks.h"

/** Maximal number of concurrent sattach connections */
#define MAX_SATTACH_SOCKETS 30

/** Size of the Slurm I/O header */
#define SLURM_IO_HEAD_SIZE 3 * sizeof(uint16_t) + sizeof(uint32_t)

/** Slurm I/O header */
typedef struct {
    uint16_t type;	/**< I/O type */
    uint16_t gtid;	/**< global task ID */
    uint16_t ltid;	/**< local task ID */
    uint32_t len;	/**< data length */
} Slurm_IO_Header_t;

/** Slurm I/O options */
typedef enum {
    IO_UNDEF = 0x05,	/**< I/O not defined */
    IO_SRUN,		/**< I/O via srun */
    IO_SRUN_RANK,	/**< I/O via srun to a single task/rank  */
    IO_GLOBAL_FILE,	/**< I/O to global file */
    IO_RANK_FILE,	/**< separate I/O file per rank */
    IO_NODE_FILE,	/**< separate I/O file per node */
} IO_Opt_t;

/** Used to track the state of an I/O connection */
typedef enum {
    IO_CON_NORM = 1,	/**< default state */
    IO_CON_ERROR,	/**< new error occurred */
    IO_CON_BROKE,	/**< connection broken */
} IO_Con_State_t;

/**
 * @brief Write a stdout or stderr message
 *
 * @param msg The message to write
 *
 * @param msgLen The length of the message
 *
 * @param taskid The task ID of the message origin
 *
 * @param type The message type (stdout or stderr)
 *
 * @param fwdata The forwarder executing the step
 *
 * @param step The step the message origin belongs to
 *
 * @param lrank The step dependent local rank of the message origin
 */
void writeIOmsg(char *msg, uint32_t msgLen, uint32_t taskid, uint8_t type,
	        Forwarder_Data_t *fwdata, Step_t *step, uint32_t lrank);

/**
 * @brief Redirect I/O of a job
 *
 * Redirect the stdout/stderr and stdin of a job to local files depending
 * on options specified by the user. Additionally various symbols in the
 * filename will be replaced. See @ref replaceSymbols() for a full list
 * of supported symbols.
 *
 * @param job The job to redirect
 */
void redirectJobOutput(Job_t *job);

/**
 * @brief Redirect I/O of a step
 *
 * Redirect the stdout/stderr and stdin of a step to local files depending
 * on options specified by the user. Additionally various symbols in the
 * filename will be replaced. See @ref replaceSymbols() for a full list
 * of supported symbols.
 *
 * @param fwdata The forwarder executing the step to redirect
 *
 * @param step The step to redirect
 */
void redirectStepIO(Forwarder_Data_t *fwdata, Step_t *step);

int redirectIORank(Step_t *step, int rank);

/**
 * @brief Open I/O pipes for a step connected to the psilogger
 *
 * Open stdout, stderr and stdin pipes connected to the psilogger.
 *
 * @param fwdata The forwarder structure of the step
 *
 * @param step The step to open the pipes for
 */
void openStepIOpipes(Forwarder_Data_t *fwdata, Step_t *step);

void sendEnableSrunIO(Step_t *step);

void printChildMessage(Step_t *step, char *msg, uint32_t msgLen,
		       uint8_t type, int32_t rank);

int handleUserOE(int sock, void *data);

int setFilePermissions(Job_t *job);

void stepFinalize(Forwarder_Data_t *fwData);

int stepForwarderMsg(PSLog_Msg_t *msg, Forwarder_Data_t *fwData);

void sendFWfinMessage(Forwarder_Data_t *fwdata, PSLog_Msg_t *msg);

void reattachTasks(Forwarder_Data_t *fwdata, uint32_t addr,
		    uint16_t ioPort, uint16_t ctlPort, char *sig);

void sendFWtaskInfo(Forwarder_Data_t *fwdata, PS_Tasks_t *task);

void initStepIO(Step_t *step);

void sendStepTimeout(Forwarder_Data_t *fwdata);

int hookFWmsg(PSLog_Msg_t *msg, Forwarder_Data_t *fwData);

void sendBrokeIOcon(Step_t *step);

/**
 * @brief Convert a Slurm I/O option to string
 *
 * @param opt The I/O option to convert
 *
 * @return Returns the string representation of the
 * give Slurm I/O option.
 */
const char *strIOopt(int opt);

/**
 * @brief Convert a Slurm I/O type to string
 *
 * @param opt The I/O type to convert
 *
 * @return Returns the string representation of the
 * give Slurm I/O type.
 */
const char *strIOtype(int type);

#endif  /* __PS_SLURM_IO */
