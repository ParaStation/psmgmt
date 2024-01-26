/*
 * ParaStation
 *
 * Copyright (C) 2013-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PELOGUE__COMM
#define __PELOGUE__COMM

#include <stdbool.h>

#include "psenv.h"

#include "peloguejob.h"
#include "peloguetypes.h"

/**
 * @brief Start job's pelogues
 *
 * Tell all nodes associated to the job @a job to start a
 * corresponding prologue or epilogue depending on the type @a type.
 * The environment @a env will be used on the target node in order to
 * run the pelogue. The pelogue will be started @a rounds times in
 * order to enable for different types of pelogues (e.g. prologue and
 * prologue.user in PBS type of RMS). To actually start the specific
 * pelogue for a given round PSIDHOOK_PELOGUE_PREPARE shall be used.
 *
 * In order to trigger the start according messages will be sent to
 * the pelogue plugins of all involved nodes.
 *
 * @param job Job to be handled
 *
 * @param type Type of pelogue to start
 *
 * @param rounds Number of times the pelogue shall be started
 *
 * @param env Environment to use on the target node for the pelogue
 *
 * @return Returns 0 on succes and -1 on error.
 */
int sendPElogueStart(Job_t *job, PElogueType_t type, int rounds, env_t env);

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
 * @brief Tell job about finished pelogue
 *
 * Tell a job about the finalization of its pelogue @a child.
 *
 * @param child The pelogue that finished
 *
 * @return No return value
 */
void sendPElogueFinish(PElogueChild_t *child);

/**
 * @brief Initialize communication layer
 *
 * Initialize the plugin's communication layer. This will mainly
 * register handler and dropper for messages of type PSP_PLUG_PELOGUE.
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
