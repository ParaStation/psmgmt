/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Stephan Krempel <krempel@par-tec.com>
 *
 */

/*
 * This small library is a collection of functions for easy handling of dynamic
 * NULL terminated string vectors.
 * After each function called on a strv_t *strv:
 * - strv->strings is a pointer to a NULL terminated vector of strings
 * - strv->count contains the number of entries in the strings vector
 */

#ifndef __PS_PLUGINSTRV_H
#define __PS_PLUGINSTRV_H

/**
 * String vector to be handled by strv* functions
 */
typedef struct {
    char **strings;    /**< Array of strings */
    size_t count;      /**< Current size */
    size_t size;       /**< Current maximum size (do not use) */
} strv_t;

/**
 * Initialize a string vector.
 *
 * This function has to be called for strv before you can pass it to any other
 * strv* function.
 *
 * You can give an array of strings to be set initially. This array is copied,
 *  the strings are not. If count is 0, initstrv is assumed to be NULL
 *  terminated.
 *
 * @param strv       The string vector to initialize.
 *
 * @param initstrv   Array to set initially or NULL
 *
 * @param initcount  Length of initstrv or 0 if initstrv is NULL terminated.
 */
#define strvInit(strv, initstrv, initcount) \
    __strvInit(strv, initstrv, initcount, __func__, __LINE__)
void __strvInit(strv_t *strv, const char **initstrv, const size_t initcount,
        const char *func, const int line);

/**
 * Adds a string to the string vector.
 *
 * The vector has to be initialized by @a strvInit().
 *
 * ATTENTION: The string pointer is stored directly, so you have to do strdup
 *  by your own if intended.
 *
 * @param strv  The initialized string vector
 *
 * @param str   The string to add
 *
 * @return 0 on success, -1 else
 */
#define strvAdd(strv, str) __strvAdd(strv, str, __func__, __LINE__)
int __strvAdd(strv_t *strv, char *str, const char *func, const int line);

/**
 * Destroys the string vector, you cannot use strv in any way afterwards.
 *
 * To use strv again you have to call @a strvInit(strv) again.
 *
 * ATTENTION: This function especially frees strv->strings.
 *
 * @param strv  The string vector to destroy
 */
#define strvDestroy(strv) __strvDestroy(strv, __func__, __LINE__)
void __strvDestroy(strv_t *strv, const char *func, const int line);

#endif

/* vim: set ts=9 sw=4 tw=0 sts=4 noet:*/
