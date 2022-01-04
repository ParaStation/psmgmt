/*
 * ParaStation
 *
 * Copyright (C) 2016-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSEXEC__COMM
#define __PSEXEC__COMM

#include <stdbool.h>
#include <stdint.h>

#include "psnodes.h"
#include "psexecscripts.h"

/**
 * @brief Request to execute script remotely
 *
 * Send the request to execute the script @a script to the
 * destination node with ParaStation ID @a dest.
 *
 * @param script Description of the script to execute
 *
 * @param dest Destination node the script shall run on
 *
 * @return On success the total number of bytes sent to the
 * destination node is returned. Or -1 in case of error.
 */
int sendExecScript(Script_t *script, PSnodes_ID_t dest);

/**
 * @brief Request to execute script locally
 *
 * Execute the script @a script locally.
 *
 * @param script Description of the script to execute
 *
 * @return If the script is started successfully, 0 is
 * returned. Otherwise -1 is returned, unless a callback is associated
 * to the script that returns PSEXEC_CONT upon calling. In this latter
 * case, 0 is returned, too.
 */
int startLocalScript(Script_t *script);

/**
 * @brief Send result to initiator
 *
 * Send the exit code @a res to the initiator of the script @a script
 * if any.
 *
 * @param script Description of script to provide the exit-code for
 *
 * @param res Exit code of the script
 *
 * @param output Stdout/stderr of the script
 *
 * @return If an initiator was found and a message was sent
 * successfully, the total number of bytes sent to the destination
 * node is returned. Or -1 otherwise.
 */
int sendScriptResult(Script_t *script, int32_t res, char *output);

/**
 * @brief Initialize communication layer
 *
 * Initialize the plugin's communication layer. This will mainly
 * register handler and dropper for messages of type
 * PSP_PLUG_PSEXEC.
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


#endif  /* __PSEXEC__COMM */
