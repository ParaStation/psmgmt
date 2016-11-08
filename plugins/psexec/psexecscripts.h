/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
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

#include "psexectypes.h"
#include "psnodes.h"

/**
 * @brief Add script information
 *
 * @doctodo
 *
 * @param pid
 *
 * @return
 */
Script_t *addScript(uint32_t id, pid_t pid, PSnodes_ID_t clnt, char *execName);

/**
 * @brief Find script information by its process ID
 *
 * Lookup the script information identified by its process ID @a pid
 * in the list of script information and return a pointer to the
 * corresponding structure.
 *
 * @param pid Process ID used to identify the script information
 *
 * @return Pointer to the script information structure or NULL
 */
Script_t *findScript(pid_t pid);

/**
 * @brief Find script information by its unique ID
 *
 * Lookup the script information identified by its unique ID @a uID in
 * the list of script information and return a pointer to the
 * corresponding structure.
 *
 * @param uID Unique ID used to identify the script information
 *
 * @return Pointer to the script information structure or NULL
 */
Script_t *findScriptByuID(uint16_t uID);

/**
 * @brief Delete script information identified by process ID
 *
 * Delete the script information identified by its process ID @a pid
 * from the list of script information and free all related memory.
 *
 * @param pid Process ID used to identify the script information
 *
 * @return Return true on success or false if the script was not found
 */
bool deleteScript(pid_t pid);

/**
 * @brief Delete script information identified by unique ID
 *
 * Delete the script information identified by its unique ID @a uID
 * from the list of script information and free all related memory.
 *
 * @param uID Unique ID used to identify the script information
 *
 * @return Return true on success or false if the script was not found
 */
bool deleteScriptByuID(uint16_t uID);

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
