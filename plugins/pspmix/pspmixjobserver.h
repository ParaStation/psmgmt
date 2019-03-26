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
 * @file Definitions of functions running in the plugin forwarder working as
 *       PMIx job server
 */

#ifndef __PS_PMIX_JOBSERVER
#define __PS_PMIX_JOBSERVER

/**
 * @brief Function called to initialize the plugin forwarder
 */
int pspmix_jobserver_initialize(Forwarder_Data_t *fwdata);

/**
 * @brief Function called to prepare the plugin forwarder loop
 */
void pspmix_jobserver_prepareLoop(Forwarder_Data_t *fwdata);

/**
 * @brief Function called to finalize the plugin forwarder
 */
void pspmix_jobserver_finalize(Forwarder_Data_t *fwdata);

#endif  /* __PS_PMIX_JOBSERVER */

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
