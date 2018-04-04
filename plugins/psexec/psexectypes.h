/*
 * ParaStation
 *
 * Copyright (C) 2016-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSEXEC__TYPES
#define __PSEXEC__TYPES

#include <stdint.h>

#include "psenv.h"
#include "psnodes.h"

/** Magic value to keep script information alive callback */
#define PSEXEC_CONT 2

/**
 * @brief Script callback
 *
 * Callback initiated upon execution of a script finished. The exit
 * value of the script is passed in @a res. In order to identify the
 * calling script @a id as given during starting the script via @ref
 * psExecStartScript() and the destination node @a dest are
 * provided. In order to be able to re-start the very same script on a
 * remote node via @ref psExecSendScriptStart() or locally via @ref
 * psExecStartLocalScript() its unique ID is given within @a uid.
 *
 * @param id ID originally passed to @ref psExecStartScript() in order
 * to help the callback to identify the reason for being called.
 *
 * @param res Exit value of the script calling back
 *
 * @param dest Destination node the script ran on
 *
 * @param uid Unique ID identifying the script information while
 * re-executing the script via @ref psExecSendScriptStart() or @ref
 * psExecStartLocalScript().
 *
 * @return If the callback has initiated an additional round of
 * execution for the given script, it shall return PSEXEC_CONT in
 * order to preserve the corresponding script information. Otherwise
 * the return value is ignored.
 */
typedef int(psExec_Script_CB_t)(uint32_t id, int32_t res, PSnodes_ID_t dest,
				uint16_t uid);

/**
 * @brief Request to execute a script
 *
 * Request to execute the script identified by its executable @a
 * execName on the node with ParaStation ID @a dest. The environment
 * as seen by the script upon execution is provided within @a env. The
 * callback @a cb will be executed upon finalization of the script's
 * execution. @a id is passed to this callback in order to identify
 * the reason of the calling back.
 *
 * In order to execute the script a corresponding script information
 * is created and equipped with a (node-local) unique ID. This unique
 * ID will be passed to the callback in order to identify this script
 * information for re-execution via @ref psExecSendScriptStart() or
 * @ref psExecStartLocalScript().
 *
 * @a id ID to be presented as the first argument to the callback @a
 * cb in order to help to identifying the reason for calling back
 *
 * @param execName Filename of the script to execute
 *
 * @param env Environment as it will be seen by the script
 *
 * @param dest Destination node the script shall run on
 *
 * @param cb Callback executed on finalization of the script
 *
 * @return On success the total number of bytes sent to the
 * destination node is returned. Or -1 in case of error.
 */
typedef int (psExecStartScript_t)(uint32_t id, char *execName, env_t *env,
				  PSnodes_ID_t dest, psExec_Script_CB_t *cb);

/**
 * @brief Request to execute existing script remotely
 *
 * Send the request to execute the script identified by its unique ID
 * @a uid to the destination node with ParaStation ID @a
 * dest.
 *
 * Prerquisite for this is that the corresponding script information
 * was created via calling @ref psExecStartScript() before. Typically
 * this function is called from within the callback registered there.
 *
 * @param uid Unique ID of the script to execute
 *
 * @param dest Destination node the script shall run on
 *
 * @return On success the total number of bytes sent to the
 * destination node is returned. Or -1 in case of error.
 */
typedef int(psExecSendScriptStart_t)(uint16_t uID, PSnodes_ID_t dest);

/**
 * @brief Request to execute existing script locally
 *
 * Execute the script identified by its unique ID @a uid locally.
 *
 * Prerquisite for this is that the corresponding script information
 * was created via calling @ref psExecStartScript() before. Typically
 * this function is called from within the callback registered there.
 *
 * @param uid Unique ID of the script to execute
 *
 * @return If the script is started successfully, 0 is
 * returned. Otherwise -1 is returned, unless a callback is associated
 * to the script that returns PSEXEC_CONT upon calling. In this latter
 * case, 0 is returned, too.
 */
typedef int(psExecStartLocalScript_t)(uint16_t uID);


#endif  /* __PSEXEC__TYPES */
