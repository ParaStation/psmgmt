/*
 * ParaStation
 *
 * Copyright (C) 2010-2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PS_MOM_PARTITION
#define __PS_MOM_PARTITION

/**
 * @brief Handle a create partion message.
 *
 * @param msg The message to handle.
 *
 * @return Returns 0 if the partion request is valid or 1 otherwise.
 */
int handleCreatePart(void *msg);

/**
 * @brief Handle a create partion nodelist message.
 *
 * @param msg The message to handle.
 *
 * @return No return value.
 */
int handleCreatePartNL(void *msg);

/**
 * @brief Test if a user is member of the PS admin users/group.
 *
 * @param uid The uid of the user to test.
 *
 * @param gid The gid of the user to test.
 *
 * @return Returns 1 if the user is member of the PS admin group or 0 otherwise.
 */
int isPSAdminUser(uid_t uid, gid_t gid);

/**
 * @brief Handle a PS spawn request message.
 *
 * Catch all new spawn requests and inject the PBS_JOBCOOKIE and PBS_JOBID into
 * the environment of the new spawned processes.
 *
 * @param msg The message to handle.
 *
 * @return No return value.
 */
void handlePSSpawnReq(DDTypedBufferMsg_t *msg);

#endif
