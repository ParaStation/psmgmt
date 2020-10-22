/*
 * ParaStation
 *
 * Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __NODEINFO_LOG_H
#define __NODEINFO_LOG_H

#include "logging.h"

extern logger_t *nodeInfoLogger;

#define mlog(...)  if (nodeInfoLogger) \
	logger_print(nodeInfoLogger, -1, __VA_ARGS__)
#define mdbg(...)  if (nodeInfoLogger)			\
	logger_print(nodeInfoLogger, __VA_ARGS__)
#define mwarn(...) if (nodeInfoLogger) \
	logger_warn(nodeInfoLogger, __VA_ARGS__)

typedef enum {
    NODEINFO_LOG_VERBOSE = 0x00001, /**< Be verbose */
} NodeInfo_log_types_t;

/**
 * @brief Init the logger facility
 *
 * @return No return value
 */
void initNodeInfoLogger(char *name);

/**
 * @brief Set the debug mask of the logger
 *
 * @return No return value
 */
void maskNodeInfoLogger(int32_t mask);

#endif  /* __NODEINFO_LOG_H */
