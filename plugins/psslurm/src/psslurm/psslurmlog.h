/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSSLURM_LOG
#define __PSSLURM_LOG

#include <stdint.h>
#include <stdio.h>

#include "logging.h"  // IWYU pragma: export
#include "pluginlog.h"

extern logger_t *psslurmlogger;
extern FILE *psslurmlogfile;

#define mlog(...) if (psslurmlogger) \
	    logger_print(psslurmlogger, -1, __VA_ARGS__)
#define mwarn(...) if (psslurmlogger) \
	    logger_warn(psslurmlogger, -1, __VA_ARGS__)
#define mdbg(...) if (psslurmlogger) logger_print(psslurmlogger, __VA_ARGS__)

/**
 * @brief Print a log message with function prefix
 *
 * Print a log message and add a function name as prefix. The maximal message
 * length is MAX_FLOG_SIZE including the added function name. If the message
 * to print is larger then MAX_FLOG_SIZE it will be printed without prefix.
 *
 * @param func The function name to use as prefix
 *
 * @param key The key to use in order to decide if anything is put out
 *
 * @param format The format to be used in order to produce output. The
 * syntax of this parameter is according to the one defined for the
 * printf() family of functions from the C standard. This string will
 * also define the further parameters to be expected.
 */
#define flog(...) __Plugin_flog(psslurmlogger, __func__, -1, __VA_ARGS__)
#define fdbg(key, ...) __Plugin_flog(psslurmlogger, __func__, key, __VA_ARGS__)

void initLogger(char *name, FILE *logfile);
void maskLogger(int32_t mask);

typedef enum {
    PSSLURM_LOG_DEBUG    =      0x000010, /**< Debug */
    PSSLURM_LOG_WARN     =      0x000020, /**< Warnings */
    PSSLURM_LOG_PSCOMM   =      0x000040, /**< Daemon communication */
    PSSLURM_LOG_PROCESS  =      0x000080, /**< Process */
    PSSLURM_LOG_COMM     =      0x000100, /**< Slurm communication */
    PSSLURM_LOG_PELOG    =      0x000200, /**< Prologue/Epilogue */
    PSSLURM_LOG_JOB      =      0x000400, /**< Job */
    PSSLURM_LOG_ENV      =      0x000800, /**< Env */
    PSSLURM_LOG_PROTO    =      0x001000, /**< Protocol */
    PSSLURM_LOG_AUTH     =      0x002000, /**< Auth */
    PSSLURM_LOG_PART     =      0x004000, /**< Partition and Reservation */
    PSSLURM_LOG_GRES     =      0x008000, /**< Gres */
    PSSLURM_LOG_FWD      =      0x010000, /**< Msg forwarding */
    PSSLURM_LOG_IO       =      0x020000, /**< I/O */
    PSSLURM_LOG_ACC      =      0x040000, /**< Account */
    PSSLURM_LOG_IO_VERB  =      0x080000, /**< more verbose I/O */
    PSSLURM_LOG_PACK	 =	0x100000, /**< job pack */
    PSSLURM_LOG_SPANK	 =	0x200000, /**< spank */
    PSSLURM_LOG_SPLUGIN  =      0x400000, /**< slurm plugins */
} PSSLURM_log_types_t;

#endif  /* __PSSLURM_LOG */
