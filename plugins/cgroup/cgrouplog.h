/*
 * ParaStation
 *
 * Copyright (C) 2016-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __CGROUP_LOG_H
#define __CGROUP_LOG_H

#include "logging.h"

extern logger_t *cgrouplogger;

#define cglog(...)  if (cgrouplogger) \
			    logger_print(cgrouplogger, __VA_ARGS__)
#define cgwarn(...) if (cgrouplogger) \
			    logger_warn(cgrouplogger, __VA_ARGS__)

typedef enum {
    CG_LOG_VERBOSE	= 0x00001, /**< Be verbose */
} Cgroup_log_types_t;

/**
 * @brief Init the logger facility.
 *
 * @return No return value.
 */
void initCgLogger(char *name, FILE *logfile);

/**
 * @brief Set the debug mask of the logger.
 *
 * @return No return value.
 */
void maskCgLogger(int32_t mask);

#endif  /* __CGROUP_LOG_H */
