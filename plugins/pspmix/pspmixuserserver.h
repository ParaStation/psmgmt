/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Definitions of functions running in the plugin forwarder working as
 *       PMIx user server
 */
#ifndef __PS_PMIX_USERSERVER
#define __PS_PMIX_USERSERVER

#include <stdbool.h>

#include "pstask.h"
#include "pluginforwarder.h"

#include "pspmixtypes.h"

/** All info on the local PMIx server available within its plugin forwarder */
extern PspmixServer_t *server;

/**
 * @brief Function called to initialize the plugin forwarder
 *
 * @param fwdata  the forwarders user data containing the server struct
 */
int pspmix_userserver_initialize(Forwarder_Data_t *fwdata);

/**
 * @brief Add a job to this server
 *
 * @param sessID     logger's TID to identify session to add the job to
 * @param job        the job to add to the server (takes ownership)
 *                   (needs not to have the session set, yet)
 */
bool pspmix_userserver_addJob(PStask_ID_t sessID, PspmixJob_t *job);

/**
 * @brief Remove a job from this server
 *
 * @param spawnertid  spawner identifying the job to remove
 */
bool pspmix_userserver_removeJob(PStask_ID_t spawnertid);

/**
 * @brief Function called to prepare the plugin forwarder loop
 *
 * @param fwdata  the forwarders user data containing the server struct (unused)
 */
void pspmix_userserver_prepareLoop(Forwarder_Data_t *fwdata);

/**
 * @brief Function called to finalize the plugin forwarder
 *
 * @param fwdata  the forwarders user data containing the server struct (unused)
 */
void pspmix_userserver_finalize(Forwarder_Data_t *fwdata);

#endif  /* __PS_PMIX_USERSERVER */

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
