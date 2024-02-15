/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __JAIL_LOG_H
#define __JAIL_LOG_H

#include <stdint.h>
#include <stdio.h>

#include "logging.h"

extern logger_t jaillogger;

#define jlog(...) logger_print(jaillogger, __VA_ARGS__)
#define jwarn(...) logger_warn(jaillogger, __VA_ARGS__)

typedef enum {
    J_LOG_VERBOSE	= 0x00001, /**< Be verbose */
} Jail_log_types_t;

/**
 * @brief Init logging facility
 *
 * Init jail plugin's logging facility. If the filehandle @a logfile
 * is different from NULL, the corresponding file will be used for
 * logging. Otherwise the syslog facility is used.
 *
 * @param name Tag used for logging
 *
 * @param logfile File to use for logging
 *
 * @return No return value
 */
void initLogger(char *name, FILE *logfile);

/**
 * @brief Set logger's debug mask
 *
 * Set the logger's debug mask to @a mask. @a mask is expected to be a
 * bit-field of type @ref Jail_log_types_t.
 *
 * @param mask Bit-field of type @ref Jail_log_types_t
 *
 * @return No return value
 */
void maskLogger(int32_t mask);

/**
 * @brief Finalize logging facility
 *
 * Finalize jail plugin's logging facility.
 *
 * @return No return value
 */
void finalizeLogger(void);

#endif  /* __JAIL_LOG_H */
