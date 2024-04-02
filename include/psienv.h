/*
 * ParaStation
 *
 * Copyright (C) 2001-2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file User-functions for interaction with the ParaStation
 * environment
 */
#ifndef __PSIENV_H
#define __PSIENV_H

#include <stdbool.h>
#include <stdlib.h>

#include "psenv.h"

/**
 * @brief Initialize the ParaStation Environment.
 *
 * Initialize the ParaStation Environment, i.e. clear all variables.
 *
 * @return No return value
 */
void clearPSIEnv(void);

/**
 * @brief Change or add a ParaStation Environment variable
 *
 * Set the variable @a name in the ParaStation Environment to the
 * value @a val.
 *
 * @param name Variable name to be set
 *
 * @param val Value the variable shall be set to
 *
 * @return On success, true is returned; or false if an error occurred
 *
 * @see getPSIEnv()
 */
bool setPSIEnv(const char *name, const char *val);

/**
 * @brief Delete a ParaStation Environment variable
 *
 * Delete the variable @a name from the ParaStation Environment.
 *
 * @param name Variable name to be removed
 *
 * @return No return value
 */
void unsetPSIEnv(const char *name);

/**
 * @brief Change or add a ParaStation Environment variable
 *
 * Adds or changes the value of ParaStation Environment variables. The
 * argument @a string is of the form 'name=value'. If name does not already
 * exist in the ParaStation Environment, then @a string is added. If name
 * does exist, then the value of name in the ParaStation Environment is
 * changed to value.
 *
 * @param string Character string of the form 'name=value'
 *
 * @return On success, true is returned; or false if an error occurred
 *
 * @see getPSIEnv()
 */
bool putPSIEnv(const char *string);

/**
 * @brief Lookup the variable @a name in the ParaStation Environment
 *
 * Find the variable @a name within the ParaStation Environment and return
 * the corresponding value.
 *
 * @param name Variable name to be looked up
 *
 * @return On success, a pointer to the corresponding value is returned; or
 * NULL if an error occured
 *
 * @see setPSIEnv(), putPSIEnv()
 */
char *getPSIEnv(const char *name);

/**
 * @brief Get the number variables in the ParaStation Environment
 *
 * Get the number variables in the ParaStation Environment.
 *
 * @return On success, the number of variables in the ParaStation Environment
 * is returned; or -1 if an error occurred
 */
int numPSIEnv(void);

/**
 * @brief Get a packed copy of the ParaStation Environment
 *
 * Get a copy of the actual ParaStation Environment. This will
 * contain all environment variables and its values.
 *
 * @return A compressed form of the ParaStation environment is
 * returned; if something went wrong during the creation of the
 * compressed copy, NULL is returned
 */
char **dumpPSIEnv(void);

#endif /* __PSIENV_H */
