/*
 * ParaStation
 *
 * Copyright (C) 2010-2017 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_MOM_PARTITION
#define __PS_MOM_PARTITION

/**
 * @brief Handle a create partition message.
 *
 * @param msg The message to handle.
 *
 * @return Returns 0 if the partition request is valid or 1 otherwise.
 */
int handleCreatePart(void *msg);

/**
 * @brief Handle a create partition nodelist message.
 *
 * @param msg The message to handle.
 *
 * @return No return value.
 */
int handleCreatePartNL(void *msg);

/**
 * @brief Handle a PS spawn request message
 *
 * Catch all new spawn requests and inject the PBS_JOBCOOKIE and PBS_JOBID into
 * the environment of the new spawned processes.
 *
 * @param msg Pointer to message to handle
 *
 * @return true if the message was fully handled; or false otherwise
 */
bool handlePSSpawnReq(DDTypedBufferMsg_t *msg);

#endif
