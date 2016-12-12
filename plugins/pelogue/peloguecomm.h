/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PELOGUE__COMM
#define __PELOGUE__COMM

#include <stdbool.h>

#include "pluginenv.h"
#include "peloguejob.h"

/**
 * @brief Start job's pelogues
 *
 * Tell all nodes associated to the job @a job to start a
 * corresponding prologue or epilogue depending on the flag @a
 * prologue. The environment @a env will be used on the target node in
 * order to run the pelogue.
 *
 * In order to trigger the start messages will be sent to the pelogue
 * plugins of all involved nodes.
 *
 * @param job Job to be handled
 *
 * @param prologue Flag to mark the start of prologue or epilogue
 *
 * @param env Environment to use on the target node for the pelogue
 *
 * @return No return value
 */
void sendPElogueStart(Job_t *job, bool prologue, env_t *env);

/**
 * @brief Signal job's pelogues
 *
 * Send the signal @a sig to all pelogues associated to the
 * job @a job. @a reason is mentioned within the corresponding log
 * messages.
 *
 * In order to deliver the signal messages will be sent to the pelogue
 * plugins of all involved nodes.
 *
 * @param job Job to be handled
 *
 * @param sig Signal to send to the job's pelogues
 *
 * @param reason Reason to be mentioned in the logs
 *
 * @return No return value
 */
void sendPElogueSignal(Job_t *job, int sig, char *reason);

/**
 * @brief Print statistics on pelogues
 *
 * Put information on plugin's success statistics into the buffer @a
 * buf. Upon return @a bufSize indicates the current size of @a buf.
 *
 * @param buf Buffer to write all information to
 *
 * @param bufSize Size of the buffer
 *
 * @return Pointer to buffer with updated statistics information
 */
char *printCommStatistics(char *buf, size_t *bufSize);

/**
 * @brief Initialize communication layer
 *
 * Initialize the plugin's communication layer. This will mainly
 * register handler and dropper for messages of type
 * PSP_CC_PLUG_PELOGUE.
 *
 * @return On success true is returned. Or false in case of an error
 */
bool initComm(void);

/**
 * @brief Finalize communication layer
 *
 * Finalize the plugin's communication layer. This will unregister the
 * handler and dropper registered by @ref initComm().
 *
 * @return No return value
 */
void finalizeComm(void);

#endif  /* __PELOGUE__COMM */
