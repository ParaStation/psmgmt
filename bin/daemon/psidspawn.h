/*
 *               ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2007 ParTec Cluster Competence Center GmbH, Munich
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
 * Handle the message @a msg of type PSP_CD_SPAWNREQUEST. These are
 * replaced by PSP_CD_SPAWNREQ messages in later versions of the
 * protocol. For reasons of compatibility the old requests are still
 * supported.
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
 * @brief Handle a PSP_CD_SPAWNREQ message.
 *
 * Handle the message @a msg of type PSP_CD_SPAWNREQ. These replace
 * the PSP_CD_SPAWNREQUEST messages of earlier protocol versions.
 *
 * Spawn a process as described within a series of messages. Depending
 * on the subtype of the current message @a msg, either the @ref
 * PStask_t structure contained is extracted or the argv or
 * environment parts are decoded and added to the corresponding task
 * structure. After receiving the last part of the environment the
 * actual task is created.
 *
 * If called on the node of the initiating task, various
 * tests are undertaken in order to determine the spawn to be
 * allowed. If all tests pass, the message is forwarded to the
 * target-node where the process to spawn is created.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_SPAWNREQ(DDTypedBufferMsg_t *msg);

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

/**
 * @brief Handle a PSP_DD_CHILDDEAD message.
 *
 * Handle the message @a msg of type PSP_DD_CHILDDEAD.
 *
 * This type of message is created by the forwarder process to inform
 * the local daemon on the dead of the controlled client process. This
 * might result in sending pending signals, deregistering the task,
 * etc. Additionally the message will be forwarded to the daemon
 * controlling the parent task in order to take according measures.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_CHILDDEAD(DDErrorMsg_t *msg);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDSPAWN_H */
