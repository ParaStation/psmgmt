/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_ACCOUNT_SCRIPT
#define __PS_ACCOUNT_SCRIPT

#include "pluginforwarder.h"

/** the callback function handling the stdout/stderr of the script */
typedef void scriptDataHandler_t(char *);

/** structure holding all information of a collect script */
typedef struct {
    char *path;
    scriptDataHandler_t *func;
    uint32_t poll;
    Forwarder_Data_t *fwdata;
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
 * @return Returns a structure to the started script or NULL on error
 */
Collect_Script_t *Script_start(char *title, char *path,
			       scriptDataHandler_t *func, uint32_t poll);

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
 * @return Returns true on success otherwise false is returned
 */
bool Script_setPollTime(Collect_Script_t *script, uint32_t poll);

/**
 * @brief Validate a collect script
 *
 * @spath The absolute path to the script
 */
bool Script_test(char *spath, char *title);

#endif  /* __PS_ACCOUNT_SCRIPT */
