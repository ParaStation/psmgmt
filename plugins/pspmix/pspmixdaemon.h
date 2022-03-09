/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Definitions of the pspmix daemon module functions
 */
#ifndef __PS_PMIX_DAEMON
#define __PS_PMIX_DAEMON

#include <sys/types.h>

#include "pstaskid.h"

/**
 * @brief Initialize the daemon module
 *
 * Initialize the daemon module of the pspmix plugin.
 *
 * @return No return value
 */
void pspmix_initDaemonModule(void);

/**
 * @brief Finalize the daemon module
 *
 * Finalize the daemon module the pspmix plugin.
 *
 * @return No return value
 */
void pspmix_finalizeDaemonModule(void);

/**
 * @brief Find TID of the server of the passed user
 *
 * @param uid  ID of the user
 *
 * @return The TID of the server or -1 on error
 */
PStask_ID_t pspmix_daemon_getServerTID(uid_t uid);

#endif  /* __PS_PMIX_DAEMON */
