/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PS_PSSLURM_LOG
#define __PS_PSSLURM_LOG

#include "logging.h"

extern logger_t *psslurmlogger;
extern FILE *psslurmlogfile;

#define mlog(...) if (psslurmlogger) logger_print(psslurmlogger, -1, __VA_ARGS__)
#define mwarn(...) if (psslurmlogger) logger_warn(psslurmlogger, -1, __VA_ARGS__)
#define mdbg(...) if (psslurmlogger) logger_print(psslurmlogger, __VA_ARGS__)

void initLogger(char *name, FILE *logfile);
void maskLogger(int32_t mask);

typedef enum {
    PSSLURM_LOG_DEBUG    =	0x000010, /**< Debug */
    PSSLURM_LOG_WARN     =	0x000020, /**< Warnings */
    PSSLURM_LOG_PSCOMM   =	0x000040, /**< Warnings */
    PSSLURM_LOG_PROCESS  =	0x000080, /**< Process */
    PSSLURM_LOG_COMM	 =	0x000100, /**< Slurm communication */
    PSSLURM_LOG_PSIDCOM  =	0x000200, /**< Warnings */
    PSSLURM_LOG_JOB	 =	0x000400, /**< Job */
    PSSLURM_LOG_ENV	 =	0x000800, /**< Env */
    PSSLURM_LOG_PROTO	 =	0x001000, /**< Protocol */
    PSSLURM_LOG_AUTH	 =	0x002000, /**< Auth */
    PSSLURM_LOG_PART	 =	0x004000, /**< Partition */
    PSSLURM_LOG_GRES	 =	0x008000, /**< Gres */
} PSSLURM_log_types_t;


#endif
