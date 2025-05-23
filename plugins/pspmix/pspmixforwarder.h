/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Definitions of the pspmix forwarder module functions
 */
#ifndef __PS_PMIX_FORWARDER
#define __PS_PMIX_FORWARDER

#include "pslog.h"
#include "psidforwarder.h"

/* only works in psid forwarder processes (#214) */
#define elog(...) PSIDfwd_printMsgf(STDERR, __VA_ARGS__)

/**
 * @brief Initialize the forwarder module
 *
 * Initialize the forwarder module of the pspmix plugin.
 *
 * @return No return value
 */
void pspmix_initForwarderModule(void);

/**
 * @brief Finalize the forwarder module
 *
 * Finalize the forwarder module the pspmix plugin.
 *
 * @return No return value
 */
void pspmix_finalizeForwarderModule(void);

#endif  /* __PS_PMIX_FORWARDER */

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
