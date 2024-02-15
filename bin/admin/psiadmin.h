/*
 * ParaStation
 *
 * Copyright (C) 2016-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSIADMIN_H
#define __PSIADMIN_H

#include "logging.h"

/** Logger used for error messages within psiadmin. */
extern logger_t PSIadm_logger;

/**
 * Print log messages via psiadmins's logging facility
 *
 * This is a wrapper to @ref logger_print().
 *
 * @see logger_print()
 */
#define PSIadm_log(...) logger_print(PSIadm_logger, __VA_ARGS__)

/**
 * Print warn messages via psiadmin's logging facility
 *
 * This is a wrapper to @ref logger_warn().
 *
 * @see logger_warn()
 */
#define PSIadm_warn(...) logger_warn(PSIadm_logger, __VA_ARGS__)

/**
 * Print warn messages via psiadmin's logging facility and exit
 *
 * This is a wrapper to @ref logger_exit().
 *
 * @see logger_exit()
 */
#define PSIadm_exit(...) logger_exit(PSIadm_logger, __VA_ARGS__)

#endif /* __PSIADMIN_H */
