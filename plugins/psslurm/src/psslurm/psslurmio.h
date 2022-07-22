/*
 * ParaStation
 *
 * Copyright (C) 2015-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_IO
#define __PS_SLURM_IO

#include <stddef.h>
#include <stdint.h>

#include "pluginforwarder.h"

#include "psslurmjob.h"
#include "psslurmstep.h"

/** Maximal number of concurrent sattach connections */
#define MAX_SATTACH_SOCKETS 30

/** Size of the Slurm I/O header */
#define SLURM_IO_HEAD_SIZE 3 * sizeof(uint16_t) + sizeof(uint32_t)

/** Slurm I/O header */
typedef struct {
    uint16_t type;	/**< I/O type */
    uint16_t grank;	/**< global task (global rank) ID */
    uint16_t lrank;	/**< local task (local rank) ID */
    uint32_t len;	/**< data length */
} IO_Slurm_Header_t;

/** Slurm I/O options */
typedef enum {
    IO_UNDEF = 0x05,	/**< I/O not defined */
    IO_SRUN,		/**< I/O via srun */
    IO_SRUN_RANK,	/**< I/O via srun to a single task/rank  */
    IO_GLOBAL_FILE,	/**< I/O to global file */
    IO_RANK_FILE,	/**< separate I/O file per rank via psslurm */
    IO_NODE_FILE,	/**< separate I/O file per node via psslurm */
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
void IO_init(void);

/**
 * @brief Finalize the I/O facility
 */
void IO_finalize(Forwarder_Data_t *fwdata);

/**
 * @brief Write a stdout or stderr message for a step
 *
 * @param fwdata The forwarder executing the step
 *
 * @param msg The message to write
 *
 * @param msgLen The length of the message
 *
 * @param grank The *global* rank of the message origin
 *
 * @param type The message type (stdout or stderr)
 */
void __IO_printStepMsg(Forwarder_Data_t *fwdata, char *msg, size_t msgLen,
		       uint32_t grank, uint8_t type, const char *caller,
		       const int line);

#define IO_printStepMsg(fwdata, msg, msgLen, rank, type) \
    __IO_printStepMsg(fwdata, msg, msgLen, rank, type, __func__, __LINE__)

/**
 * @brief Write a stdout or stderr message for a job
 *
 * @param fwdata The forwarder executing the job
 *
 * @param msg The message to write
 *
 * @param msgLen The length of the message
 *
 * @param type The message type (stdout or stderr)
 */
void IO_printJobMsg(Forwarder_Data_t *fwdata, char *msg, size_t msgLen,
		    uint8_t type);

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
void IO_redirectJob(Forwarder_Data_t *fwdata, Job_t *job);

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
 * @param grank The *global* rank (taskid) of the I/O channel
 *
 * @param type The I/O type of the channel
 */
void IO_closeChannel(Forwarder_Data_t *fwdata, uint32_t grank, uint8_t type);

/**
 * @brief Open I/O pipes for a step connected to the psilogger
 *
 * Open stdout, stderr and stdin pipes connected to the psilogger.
 *
 * @param fwdata The forwarder structure of the step
 *
 * @param step The step to open the pipes for
 */
void IO_openStepPipes(Forwarder_Data_t *fwdata, Step_t *step);

/**
 * @brief Open I/O pipes for a job
 *
 * @param fwdata The forwarder structure of the job
 *
 * @return Returns 1 on success otherwise 0 is returned
 */
int IO_openJobPipes(Forwarder_Data_t *fwdata);

/**
 * @brief Open I/O files for a job
 *
 * @param fwdata The forwarder structure of the job
 */
void IO_openJobIOfiles(Forwarder_Data_t *fwdata);

/**
 * @brief Forward I/O data for a job
 *
 * Read from pipes connected to stdout and stderr of
 * the job-script. The red data is written to the jobs
 * stdout and stderr files respectively. This forwarding
 * mechanism allows psslurm to inject additional messages
 * into the message stream.
 *
 * @param sock The socket holding new data to read
 *
 * @param data Pointer holding the forwarder structure
 *
 * @return Always returns 0
 */
int IO_forwardJobData(int sock, void *data);

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
 * @brief todo Verify that function can be safely removed with the
 * new I/O architecture
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

/**
 * @brief Replace various symbols for a step path
 *
 * This is a wrapper for @ref replaceSymbols().
 *
 * @param step The step to replace the symbols for
 *
 * @param rank The rank of the step
 *
 * @param path The string to replace the symbols in
 *
 * @param Returns a string holding the result or NULL on error. The caller
 * is responsible to free the allocated memory after use.
 */
char *IO_replaceStepSymbols(Step_t *step, int rank, char *path);

/**
 * @brief Replace various symbols for a job path
 *
 * This is a wrapper for @ref replaceSymbols().
 *
 * @param job The job to replace the symbols for
 *
 * @param path The string to replace the symbols in
 *
 * @param Returns a string holding the result or NULL on error. The caller
 * is responsible to free the allocated memory after use.
 */
char *IO_replaceJobSymbols(Job_t *job, char *path);

#endif  /* __PS_SLURM_IO */
