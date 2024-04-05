/*
 * ParaStation
 *
 * Copyright (C) 2016-2017 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/*
 * This small library is a collection of functions for easy handling
 * of dynamic NULL terminated string vectors.
 */
#ifndef __PSSTRV_H
#define __PSSTRV_H

#include <stdint.h>
#include <stdbool.h>

/** String vector to be handled by strv* functions */
typedef struct {
    char **strings;    /**< Array of strings */
    uint32_t cnt;      /**< Current number of strings in array */
    uint32_t size;     /**< Current maximum size incl. NULL (do not use) */
} strv_t;

/**
 * @brief Initialize string vector
 *
 * This function has to be called for @a strv before it can be passed
 * to any other strv* function.
 *
 * @a initstrv might hold the initial content to be copied into @a
 * strv or NULL if @a strv shall be started empty. Only the actual
 * array is copied, the strings are not. If @a initcount is 0, @a
 * initstrv is assumed to be NULL terminated.
 *
 * @param strv The string vector to initialize
 *
 * @param initstrv Array to set initially or NULL
 *
 * @param initcount Length of initstrv or 0 if initstrv is NULL terminated
 *
 * @return No return value
 */
void strvInit(strv_t *strv, char **initstrv, uint32_t initcount);

/**
 * @brief Check string vector for initialization
 *
 * Check if the string vector represented by @a strv is initialized,
 * i.e. if @ref strvNew() was called for this string vector before.
 *
 * @param strv String vector's handle
 *
 * @return Return true if the string vector is initialized; or false
 * otherwise
 */
bool strvInitialized(const strv_t *strv);

/**
 * @brief Get string vector's size
 *
 * Get the actual size, i.e. the number of stored strings, of the
 * string vector @a strv.
 *
 * @param strv String vector to investigate
 *
 * @return Size of the string vector
 */
uint32_t strvSize(strv_t *strv);

/**
 * @brief Add string to string vector
 *
 * Append the string @a str to the string vector @a strv. The string
 * vector @a strv must be initialized via @ref strvInit() before.
 *
 * @a strv will not be extended by @a str itself but by a copy of the
 * string created utilizing @ref strdup(). Thus, "ownership" of @a str
 * remains where it belonged.
 *
 * @param strv String vector to be extended
 *
 * @param str String to add
 *
 * @return If @a str was appended, true is returned; or false in case
 * of error
 */
bool strvAdd(strv_t *strv, const char *str);

/**
 * @brief Link string to string vector
 *
 * Append the string @a str itself to the string vector @a strv. The
 * string vector @a strv must be initialized via @ref strvInit()
 * before.
 *
 * @attention The pointer @a str to the string pointer is stored
 * directly, i.e. the "ownership" of @a str is transferred to @a
 * strv. If this is not intended consider to use of @ref strvAdd.
 *
 * @param strv String vector to be extended
 *
 * @param str String to add
 *
 * @return If @a str was appended, true is returned; or false in case
 * of error
 */
bool strvLink(strv_t *strv, const char *str);

/**
 * @brief Access string vector's string array
 *
 * Get a handle on a string array representing the string vector @a
 * strv. The string array is NULL terminated and remains in the
 * ownership of @a strv, i.e.
 *
 * - it will get obsolete as soon as @a strv is destroyed
 *
 * - any modification of this array will immediately affect @a strv
 *
 * - any modifications of @a strv will immediately change the returned
 *   string array
 *
 * Thus, if it is required to modify the returned string array or to
 * rely on the content of it on the long run, it is advised to either
 * steal it from @a strv utilizing strvStealArray().
 *
 * The main purpose of this function is to feed the content of @a strv
 * into functions like @ref addStringArrayToMsg() or exec().
 *
 * @param strv String vector to get a string array handle on
 *
 * @return Pointer to a NULL terminated string array or NULL if @a strv
 * is still uninitialized or empty
 */
char **strvGetArray(strv_t *strv);

/**
 * Destroy string vector
 *
 * Destroy the string vector @a strv. All memory used by the string
 * vector itself and the containing strings is invalidated and
 * free()ed.
 *
 * @param strv The string vector to be destroy
 *
 * @return No return value
 */
void strvDestroy(strv_t *strv);

/**
 * @brief Steal string vector's strings
 *
 * Destroy the string vector @a strv but leave the actual strings
 * alone. For this, all memory occupied by the string vector is
 * free()ed, but not the individual string's memory.
 *
 * This is meant to be used after all the string vector content has been
 * used otherwise, i.e. transfered ownership to a different control'
 *
 * @param strv String vector to steal the strings from
 *
 * @return No return value
 */
void strvSteal(strv_t *strv);

/**
 * @brief Steal string vector's string array
 *
 * Destroy the string vector @a strv but leave the actual string array
 * alone. For this, all memory occupied by the string vector is
 * free()ed, but not the representing string array's memory that can
 * be accessed via strvGetArray().
 *
 * This is meant to be used after a handle to the string array is
 * gained through strvGetArray() and the string array shall be kept
 * outside of @a strv on the long run.
 *
 * Instead of utilizing @ref strvGetArray() before, the return value of
 * this function might be used directly. I.e.
 *
 * char **argv = strvStealArray(args);
 *
 * is equivalent to:
 *
 * char **argv = strvGetArray(args)
 * strvStealArray(args)
 *
 * @param strv String vector to steal the string array from
 *
 * @return Pointer to a NULL terminated string array or NULL if @a strv
 * is still uninitialized or empty
 */
char **strvStealArray(strv_t *strv);

#endif  /* __PSSTRV_H */
