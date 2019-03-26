/*
 * ParaStation
 *
 * Copyright (C) 2018-2019 ParTec Cluster Competence Center GmbH, Munich
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


#endif  /* __PS_PMIX_DAEMON */
