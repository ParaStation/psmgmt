/*
 * ParaStation
 *
 * Copyright (C) 2015-2020 ParTec Cluster Competence Center GmbH, Munich
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
} IO_Slurm_Header_t;

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
 * @brief Initialize the I/O facility
 */
void IO_init();

/**
 * @brief Finalize the I/O facility
 */
void IO_finalize(Forwarder_Data_t *fwdata);

/**
 * @brief Write a stdout or stderr message
 *
 * @param fwdata The forwarder executing the step
 *
 * @param msg The message to write
 *
 * @param msgLen The length of the message
 *
 * @param rank The rank of the message origin
 *
 * @param type The message type (stdout or stderr)
 */
void IO_printChildMsg(Forwarder_Data_t *fwdata, char *msg, size_t msgLen,
		      uint32_t rank, uint8_t type);

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
void IO_redirectJob(Job_t *job);

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
void IO_redirectStep(Forwarder_Data_t *fwdata, Step_t *step);

/**
 * @brief Redirect stdin of a rank
 *
 * Redirect the stdin of a rank from a local file depending
 * on options specified by the user. Additionally various symbols in the
 * filename will be replaced. See @ref replaceSymbols() for a full list
 * of supported symbols.
 *
 * @param step The step of the rank
 *
 * @param rank The rank to redirect
 */
int IO_redirectRank(Step_t *step, int rank);

/**
 * @brief Close an I/O channel
 *
 * @param fwdata The forwarder associated with the I/O channel
 *
 * @param taskid The taskid of the I/O channel
 *
 * @param type The I/O type of the channel
 */
void IO_closeChannel(Forwarder_Data_t *fwdata, uint32_t taskid, uint8_t type);

/**
 * @brief Open I/O pipes for a step connected to the psilogger
 *
 * Open stdout, stderr and stdin pipes connected to the psilogger.
 *
 * @param fwdata The forwarder structure of the step
 *
 * @param step The step to open the pipes for
 */
void IO_openPipes(Forwarder_Data_t *fwdata, Step_t *step);

/**
 * @brief Attach an additional I/O connection to sattach
 *
 * @param step The step to open a new connection for
 *
 * @param ioAddr The address of sattach
 *
 * @param ioPort The I/O port of sattach
 *
 * @param ctlPort The control port of sattach
 *
 * @param sig The signature to validate the connection
 */
void IO_sattachTasks(Step_t *step, uint32_t ioAddr, uint16_t ioPort,
		     uint16_t ctlPort, char *sig);

/**
 * @brief doctodo
 */
int handleUserOE(int sock, void *data);

/**
 * @brief Convert a Slurm I/O option to string
 *
 * @param opt The I/O option to convert
 *
 * @return Returns the string representation of the
 * give Slurm I/O option.
 */
const char *IO_strOpt(int opt);

/**
 * @brief Convert a Slurm I/O type to string
 *
 * @param opt The I/O type to convert
 *
 * @return Returns the string representation of the
 * give Slurm I/O type.
 */
const char *IO_strType(int type);

#endif  /* __PS_SLURM_IO */
