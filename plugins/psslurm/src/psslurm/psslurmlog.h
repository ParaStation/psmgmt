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
#include "pluginlog.h"  // IWYU pragma: keep

extern logger_t *psslurmlogger;
extern FILE *psslurmlogfile;

#define mlog(...) if (psslurmlogger)			\
	logger_print(psslurmlogger, -1, __VA_ARGS__)
#define mwarn(...) if (psslurmlogger)			\
	logger_warn(psslurmlogger, -1, __VA_ARGS__)
#define mdbg(...) if (psslurmlogger) logger_print(psslurmlogger, __VA_ARGS__)

#define flog(...) if (psslurmlogger)					\
	logger_funcprint(psslurmlogger, __func__, -1, __VA_ARGS__)
#define fdbg(key, ...) if (psslurmlogger)				\
	logger_funcprint(psslurmlogger, __func__, key, __VA_ARGS__)

void initLogger(char *name, FILE *logfile);
void maskLogger(int32_t mask);

typedef enum {
    PSSLURM_LOG_DEBUG    =      0x0000010, /**< Debug */
    PSSLURM_LOG_WARN     =      0x0000020, /**< Warnings */
    PSSLURM_LOG_PSCOMM   =      0x0000040, /**< Daemon communication */
    PSSLURM_LOG_PROCESS  =      0x0000080, /**< Process */
    PSSLURM_LOG_COMM     =      0x0000100, /**< Slurm communication */
    PSSLURM_LOG_PELOG    =      0x0000200, /**< Prologue/Epilogue */
    PSSLURM_LOG_JOB      =      0x0000400, /**< Job */
    PSSLURM_LOG_ENV      =      0x0000800, /**< Env */
    PSSLURM_LOG_PROTO    =      0x0001000, /**< Protocol */
    PSSLURM_LOG_AUTH     =      0x0002000, /**< Auth */
    PSSLURM_LOG_PART     =      0x0004000, /**< Partition and Reservation */
    PSSLURM_LOG_GRES     =      0x0008000, /**< Gres */
    PSSLURM_LOG_FWD      =      0x0010000, /**< Msg forwarding */
    PSSLURM_LOG_IO       =      0x0020000, /**< I/O */
    PSSLURM_LOG_ACC      =      0x0040000, /**< Account */
    PSSLURM_LOG_IO_VERB  =      0x0080000, /**< more verbose I/O */
    PSSLURM_LOG_PACK	 =	0x0100000, /**< job pack */
    PSSLURM_LOG_SPANK	 =	0x0200000, /**< spank */
    PSSLURM_LOG_SPLUGIN  =      0x0400000, /**< slurm plugins */
    PSSLURM_LOG_TOPO     =      0x0800000, /**< Topology */
    PSSLURM_LOG_JAIL     =      0x1000000, /**< jail/cgroup */
} PSSLURM_log_types_t;

#endif  /* __PSSLURM_LOG */
