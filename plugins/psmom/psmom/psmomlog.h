/*
 * ParaStation
 *
 * Copyright (C) 2010-2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_MOM_LOG
#define __PS_MOM_LOG

#include "logging.h"

extern logger_t *psmomlogger;
extern FILE *psmomlogfile;

#define mlog(...) if (psmomlogger) logger_print(psmomlogger, -1, __VA_ARGS__)
#define mwarn(...) if (psmomlogger) logger_warn(psmomlogger, -1, __VA_ARGS__)
#define mdbg(...) if (psmomlogger) logger_print(psmomlogger, __VA_ARGS__)

void initLogger(char *name, FILE *logfile);
void maskLogger(int32_t mask);

typedef enum {
    PSMOM_LOG_PSCOM =	0x000010, /**< Communication via psid */
    PSMOM_LOG_RPP =	0x000020, /**< RPP protocol */
    PSMOM_LOG_TCP =	0x000040, /**< TCP protocol */
    PSMOM_LOG_PTM =	0x000080, /**< TM Messages */
    PSMOM_LOG_PRM =	0x000100, /**< RM Messages */
    PSMOM_LOG_PIS =	0x000200, /**< IS Messages */
    PSMOM_LOG_CONVERT = 0x000400, /**< Data converstion stuff */
    PSMOM_LOG_VERBOSE = 0x000800, /**< Other verbose stuff */
    PSMOM_LOG_STRUCT  = 0x001000, /**< Reading/writing of data structures */
    PSMOM_LOG_PROCESS = 0x002000, /**< Process creating/death */
    PSMOM_LOG_PELOGUE = 0x004000, /**< Prologue/epilogue scripts */
    PSMOM_LOG_JOB     = 0x008000, /**< Job information */
    PSMOM_LOG_WARN    = 0x010000, /**< Warnings */
    PSMOM_LOG_LOCAL   = 0x020000, /**< Local communication */
    PSMOM_LOG_MALLOC  = 0x040000, /**< Memory Allocation */
    PSMOM_LOG_ACC     = 0x080000, /**< Accounting Infos */
    PSMOM_LOG_OBIT    = 0x100000, /**< Detailed job obit log */
} PSMOM_log_types_t;


#endif  /* __PS_MOM_LOG */
