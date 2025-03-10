/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_ACCOUNT_SCRIPT
#define __PS_ACCOUNT_SCRIPT

#include <stdbool.h>
#include <stdint.h>

#include "list.h"
#include "pluginforwarder.h"
#include "psenv.h"
#include "psaccounttypes.h"

/** the callback function handling the stdout/stderr of the script */
typedef void scriptDataHandler_t(char *);

/** structure holding all information of a collect script */
typedef struct {
    list_t next;
    char *path;
    scriptDataHandler_t *func;
    uint32_t poll;
    Forwarder_Data_t *fwdata;
    env_t env;
    bool shutdown;
} Collect_Script_t;

/**
 * @brief Start a new collect script
 *
 * @param title The name of the script
 *
 * @param path The absolute path to the collect script
 *
 * @param func The callback function handling the script output
 *
 * @param poll The time to wait between repeading calls of the script
 *
 * @param env Additional scripts environment variables or NULL
 *
 * @return Returns a structure to the started script or NULL on error
 */
Collect_Script_t *Script_start(char *title, char *path,
			       scriptDataHandler_t *func, uint32_t poll,
			       env_t env);

/**
 * @brief Finalize a collect script
 *
 * @param script The structure of the collect script to stop
 */
void Script_finalize(Collect_Script_t *script);

/**
 * @brief Set poll time in seconds
 *
 * @param script The collect script
 *
 * @param poll The new poll time in seconds
 *
 * @return Returns true on success or false otherwise
 */
bool Script_setPollTime(Collect_Script_t *script, uint32_t poll);

/**
 * @brief Validate a collect script
 *
 * @spath The absolute path to the script
 */
bool Script_test(char *spath, char *title);

/**
 * @brief Set/unset environment variable for collect script
 *
 * @param script Collect script to modify
 *
 * @param action Specifies if a variable should be added or removed
 *
 * @param name Name of the environment variable to set/unset
 *
 * @param val Value to be set; ignored in case of variable unset
 *
 * @return Returns true on success or false otherwise
 */
bool Script_ctlEnv(Collect_Script_t *script, psAccountCtl_t action,
		   const char *name, const char *val);

/**
 * @brief Show environment variable(s) for collect script
 *
 * Print the environment variable @a name or all environment variables
 * of @a env if @a name is NULL or of length 0 to a string buffer and
 * return the stolen string.
 *
 * @param env Environment to investigate
 *
 * @param name Name of the environment variable to print; all
 * environment variables are printed if @a name is NULL or of length 0
 *
 * @return Return the string describing the requested information
 */
char *Script_showEnv(env_t env, const char *name);

/**
 * @brief Finalize all active collect scripts
 *
 * @return No return value
 */
void Script_finalizeAll(void);

/**
 * @brief Hard cleanup of all scripts still active
 *
 * @return No return value
 */
void Script_cleanup(void);

#endif  /* __PS_ACCOUNT_SCRIPT */
