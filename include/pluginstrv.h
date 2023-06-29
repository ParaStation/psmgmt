/*
 * ParaStation
 *
 * Copyright (C) 2016-2017 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/*
 * This small library is a collection of functions for easy handling of dynamic
 * NULL terminated string vectors.
 * After each function called on a strv_t *strv:
 * - strv->strings is a pointer to a NULL terminated vector of strings or NULL
 * - strv->count contains the number of entries in the strings vector
 */
#ifndef __PLUGIN_LIB_STRV
#define __PLUGIN_LIB_STRV

#include <stdint.h>
#include <stdbool.h>

/** String vector to be handled by strv* functions */
typedef struct {
    char **strings;    /**< Array of strings */
    uint32_t count;    /**< Current number of strings in array */
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
 * @param func Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return No return value
 */
void __strvInit(strv_t *strv, char **initstrv, uint32_t initcount,
		const char *func, const int line);
#define strvInit(strv, initstrv, initcount) \
    __strvInit(strv, initstrv, initcount, __func__, __LINE__)

/**
 * @brief Add string to string vector
 *
 * Append the string @a str to the string vector @a strv. The string
 * vector @a strv has to be initialized via @ref strvInit() before.
 *
 * @a strv will not be extended by @a str itself but by a copy of the
 * string created utilizing @ref strdup(). Thus, "ownership" of @a str
 * remains where it belonged.
 *
 * @param strv String vector to be extended
 *
 * @param str String to add
 *
 * @param func Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return No return value
 */
void __strvAdd(strv_t *strv, const char *str, const char *func, const int line);
#define strvAdd(strv, str) __strvAdd(strv, str, __func__, __LINE__)

/**
 * @brief Link string to string vector
 *
 * Append the string @a str itself to the string vector @a strv. The
 * string vector @a strv has to be initialized via @ref strvInit()
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
 * @param func Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return No return value
 */
void __strvLink(strv_t *strv, const char *str, const char *func, const int line);
#define strvLink(strv, str) __strvLink(strv, str, __func__, __LINE__)

/**
 * Destroys string vector
 *
 * Destroy the string vector @a strv. All memory used by the string
 * vector itself and the containing strings is invalidated and
 * free()ed.
 *
 * @param strv The string vector to be destroy
 *
 * @param func Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return No return value
 */
void __strvDestroy(strv_t *strv, const char *func, const int line);
#define strvDestroy(strv) __strvDestroy(strv, __func__, __LINE__)

/**
 * Steal string vector
 *
 * Destroy the string vector @a strv. All memory used by the string
 * vector itself is invalidated and free()ed. Nevertheless, memory
 * used by the strings within the vector is left untouched.
 *
 * With the @a array switch, you can choose to steal the strings array and the
 * contained strings (true), or to only steal the contained strings (false).
 *
 * @attention This function especially frees strv->strings iff array == false.
 *
 * @attention You cannot access any pointer in @a strv after calling this
 * function, so you need to copy strv->strings beforehand iff array == true or
 * the memory pointed to by it will be leaked.
 *
 * @param strv   The string vector to be destroy
 *
 * @param sarray Flag to free or not free the strings array
 *
 * @param func   Function name of the calling function
 *
 * @param line   Line number where this function is called
 *
 * @return No return value
 */
void __strvSteal(strv_t *strv, bool sarray, const char *func, const int line);
#define strvSteal(strv, sarray) __strvSteal(strv, sarray, __func__, __LINE__)

#endif  /* __PLUGIN_LIB_STRV */
