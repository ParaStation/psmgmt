/*
 *               ParaStation3
 * psienv.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psienv.h,v 1.4 2002/04/26 12:39:22 eicker Exp $
 *
 */
/**
 * @file
 * User-functions for interaction with the ParaStation environment.
 *
 * $Id: psienv.h,v 1.4 2002/04/26 12:39:22 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIENV_H
#define __PSIENV_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

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
 * @param name The name of the variable to be set.
 * @param value The value of the variable to be set.
 * @param overwrite Flag if overwriting is allowed.
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
 * @param buffer The buffer to pack the ParaStation Environment in.
 * @param size The size of @a buffer.
 *
 * @return On success, the number of used bytes in buffer is returned, or -1
 * if an error occurred (i.e. the buffer is to small).
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
 * Get a packed copy of the actual ParaStation Environment.
 *
 * @todo More info.
 *
 * @return On success, the number of used bytes in buffer is returned, or -1
 * if an error occurred (i.e. the buffer is to small).
 */
char ** dumpPSIEnv(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIENV_H */
