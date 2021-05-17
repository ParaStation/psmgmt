/*
 * ParaStation
 *
 * Copyright (C) 2012-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PS_RESPORT_LOG
#define __PS_RESPORT_LOG

#include "logging.h"

extern logger_t *psresportlogger;

#define mlog(...)  if (psresportlogger) \
			    logger_print(psresportlogger, -1, __VA_ARGS__)
#define mwarn(...) if (psresportlogger) \
			    logger_warn(psresportlogger, -1, __VA_ARGS__)
#define mdbg(...)  if (psresportlogger) \
			    logger_print(psresportlogger, __VA_ARGS__)

typedef enum {
    RP_LOG_VERBOSE	= 0x000010, /**< Be verbose */
    RP_LOG_DEBUG	= 0x000020, /**< Log debug messages */
    RP_LOG_UMALLOC	= 0x000040, /**< Log memory allocation infos */
} PSResport_log_types_t;

/**
 * @brief Init the logger facility.
 *
 * @return No return value.
 */
void initLogger(char *name, FILE *logfile);

/**
 * @brief Set the debug mask of the logger.
 *
 * @return No return value.
 */
void maskLogger(int32_t mask);

#endif
