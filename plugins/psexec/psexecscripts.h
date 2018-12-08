/*
 * ParaStation
 *
 * Copyright (C) 2016-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSEXEC__SCRIPTS
#define __PSEXEC__SCRIPTS

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "list_t.h"
#include "psenv.h"
#include "pstaskid.h"

#include "psexectypes.h"

/** Structure holding all information on a given script to execute */
typedef struct {
    list_t next;            /**< used to put into list of script */
    uint32_t id;            /**< ID helping to identify callback reason */
    uint16_t uID;           /**< node-local unique ID */
    pid_t pid;              /**< process id of running script or 0 */
    env_t env;              /**< environment as seen by the script */
    PStask_ID_t initiator;  /**< task ID or -1 for local delegate */
    psExec_Script_CB_t *cb; /**< callback called upon script's finalization */
    char *execName;         /**< script's filename relative to SCRIPT_DIR */
    char *execPath;         /**< optional script's path to use */
} Script_t;

/**
 * @brief Add script information
 *
 * Create a new script information filled with the caller provided ID
 * @a id, the name of the script to execute @a execName and the
 * callback @a cb to be executed upon finalization of the script's
 * execution. Furthermore add this to the list of script information.
 *
 * @a id will be presented as the first argument to the callback @a cb
 * in order to help the caller of this function to identify the reason
 * for the callback.
 *
 * @param id ID helping the caller to identify the callback reason
 *
 * @param execName Filename of the script to execute
 *
 * @param execPath Optional path of the script to execute
 *
 * @param cb Callback executed on finalization of the script
 *
 * @return On success the new script information is returned or NULL
 * in case of error,i.e. insufficient memory.
 */
Script_t *addScript(uint32_t id, char *execName, char *execPath,
		    psExec_Script_CB_t *cb);

/**
 * @brief Find script information by its unique ID
 *
 * Look up the script information identified by its unique ID @a uID
 * in the list of script information and return a pointer to the
 * corresponding structure.
 *
 * @param uID Unique ID used to identify the script information
 *
 * @return Pointer to the script information structure or NULL
 */
Script_t *findScriptByuID(uint16_t uID);

/**
 * @brief Delete script information
 *
 * Delete the script information @a script from the list of script
 * information and free all related memory.
 *
 * @param script Script information to delete
 *
 * @return Return true on success or false if the script was not found
 */
bool deleteScript(Script_t *script);

/**
 * @brief Eliminate all script information
 *
 * Remove all script information from the list and free all related
 * memory.
 *
 * @return No return value
 */
void clearScriptList(void);

#endif  /* __PSEXEC__SCRIPTS */
