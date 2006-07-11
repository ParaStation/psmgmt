/*
 *               ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2006 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * \file
 * Spawning of client processes and forwarding for the ParaStation daemon
 *
 * $Id$
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDSPAWN_H
#define __PSIDSPAWN_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

#include "pstask.h"
#include "psprotocol.h"

/**
 * @brief Handle a PSP_CD_SPAWNREQUEST message.
 *
 * Handle the message @a msg of type PSP_CD_SPAWNREQUEST.
 *
 * Spawn a process as described with @a msg. Therefor a @ref PStask_t
 * structure is extracted from @a msg. If called on the node of the
 * initiating task, various tests are undertaken in order to determine
 * the spawn to be allowed. If all tests pass, the message is
 * forwarded to the target-node where the process to spawn is created.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_SPAWNREQUEST(DDBufferMsg_t *msg);

/**
 * @brief Handle a PSP_CD_SPAWNSUCCESS message.
 *
 * Handle the message @a msg of type PSP_CD_SPAWNSUCCESS.
 *
 * Register the spawned process to its parent task and forward the
 * message to the initiating process.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_SPAWNSUCCESS(DDErrorMsg_t *msg);

/**
 * @brief Handle a PSP_CD_SPAWNFAILED message.
 *
 * Handle the message @a msg of type PSP_CD_SPAWNFAILED.
 *
 * This just forwards the message to the initiating process.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_SPAWNFAILED(DDErrorMsg_t *msg);

/**
 * @brief Handle a PSP_CD_SPAWNFINISH message.
 *
 * Handle the message @a msg of type PSP_CD_SPAWNFINISH.
 *
 * This just forwards the message to the initiating process.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_SPAWNFINISH(DDMsg_t *msg);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDSPAWN_H */
