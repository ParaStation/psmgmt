/*
 * ParaStation
 *
 * Copyright (C) 2001-2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
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

#include <stdlib.h>

/**
 * @brief Initialize the ParaStation Environment.
 *
 * Initialize the ParaStation Environment, i.e. clear all variables.
 *
 * @return No return value.
 */
void clearPSIEnv(void);

/**
 * @brief Change or add a ParaStation Environment variable.
 *
 * Adds the variable @a name to the ParaStation Environment with the value
 * @a value, if @a name does not already exist. If @a name does exist in the
 * ParaStation Environment, then its value is changed to @a value if
 * @a overwrite is non-zero; if @a overwrite is zero, then the valye of
 * @a name is not changed.
 *
 *
 * @param name The name of the variable to be set.
 *
 * @param value The value of the variable to be set.
 *
 * @param overwrite Flag if overwriting is allowed.
 *
 *
 * @return On success, 0 is returned, or -1 if an error occured.
 *
 * @see getPSIEnv()
 */
int setPSIEnv(const char *name, const char *value, int overwrite);

/**
 * @brief Delete a ParaStation Environment variable.
 *
 * Deletes the variable @a name from the ParaStation Environment.
 *
 * @param name The name of the variable to be removed.
 *
 * @return No return value.
 */
void unsetPSIEnv(const char *name);

/**
 * @brief Change or add a ParaStation Environment variable.
 *
 * Adds or changes the value of ParaStation Environment variables. The
 * argument @a string is of the form 'name=value'. If name does not already
 * exist in the ParaStation Environment, then @a string is added. If name
 * does exist, then the value of name in the ParaStation Environment is
 * changed to value.
 *
 * @param string A character string of the form 'name=value'.
 *
 * @return On success, 0 is returned, or -1 if an error occured.
 *
 * @see getPSIEnv()
 */
int putPSIEnv(const char *string);

/**
 * @brief Lookup the variable @a name in the ParaStation Environment.
 *
 * Find the variable @a name within the ParaStation Environment and return
 * the corresponding value, set by PSI_setenv("name=value").
 *
 * @param name The name of the environment variable to be looked up.
 *
 * @return On success, a pointer to the corresponding value is returned, or
 * NULL if an error occured.
 *
 * @see setPSIEnv()
 */
char* getPSIEnv(const char *name);

/**
 * @brief Pack the ParaStation Environment.
 *
 * Pack the ParaStation Environment into buffer @a buffer, so it can be sent
 * in a single message.
 *
 *
 * @param buffer The buffer to pack the ParaStation Environment in.
 *
 * @param size The size of @a buffer.
 *
 *
 * @return On success, the number of used bytes in buffer is returned, or -1
 * if an error occurred (i.e. the buffer is too small).
 */
int packPSIEnv(char *buffer, size_t size);

/**
 * @brief Get the number variables in the ParaStation Environment.
 *
 * Get the number variables in the ParaStation Environment.
 *
 * @return On success, the number of variables in the ParaStation Environment
 * is returned, or -1 if an error occurred.
 */
int numPSIEnv(void);

/**
 * @brief Get a packed copy of the ParaStation Environment.
 *
 * Get a packed copy of the actual ParaStation Environment. This will
 * contain all environment variables and its values.
 *
 * The packed is allocated by malloc() and might be free()ed part by
 * part. As a first step an index of pointers to char with size
 * numPSIEnv() is allocated, then all environment variables and their
 * values are strdup()ed to this structure. Thus firstly each element
 * of the index has to be free()ed before the index itself is
 * free()ed.
 *
 * @return A compressed form of the ParaStation environment is
 * returned. If something went wrong within the creation of the
 * compressed copy, NULL is returned.
 */
char ** dumpPSIEnv(void);

#endif /* __PSIENV_H */
