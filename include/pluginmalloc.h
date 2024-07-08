/*
 * ParaStation
 *
 * Copyright (C) 2012-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PLUGIN_LIB_MALLOC
#define __PLUGIN_LIB_MALLOC

#include <stddef.h>

/**
 * @brief malloc() with error handling and logging.
 *
 * Call malloc() and handle errors by printing an error message and
 * calling exit(EXIT_FAILURE).
 *
 * In addition to that debug messages are created.
 *
 * @param size Size in bytes to allocate
 *
 * @param func Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returned is a pointer to the allocated memory.
 */
void *__umalloc(size_t size, const char *func, const int line);
#define umalloc(size) __umalloc(size, __func__, __LINE__)

/**
 * @brief realloc() with error handling and logging.
 *
 * Call realloc() and handle errors by printing an error message and
 * calling exit(EXIT_FAILURE).
 *
 * In addition to that debug messages are created.
 *
 * @param size Size in bytes to allocate
 *
 * @param func Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returned is a pointer to the allocated memory.
 */
void *__urealloc(void *old ,size_t size, const char *func, const int line);
#define urealloc(old, size) __urealloc(old, size, __func__, __LINE__)

/**
 * @brief Free memory using free() with logging.
 *
 * In addition to calling free() debug messages are created.
 *
 * @param ptr Pointer to the memory address to free
 *
 * @param func Funtion name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return No return value.
 */
void __ufree(void *ptr, const char *func, const int line);
#define ufree(ptr) __ufree(ptr, __func__, __LINE__)

/**
 * @brief Shred and free memory
 *
 * Zero the memory and free it with additional logging.
 *
 * @param ptr Pointer to the memory address to shred
 *
 * @param len The length of the data to shred
 *
 * @param func Funtion name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return No return value.
 */
void __mshred(void *ptr, size_t len, const char *func, const int line);
#define mshred(ptr, len) __mshred(ptr, len, __func__, __LINE__)

#define strShred(ptr) {	size_t len = 0; if (ptr) len = strlen(ptr); \
    __mshred(ptr, len, __func__, __LINE__); }

/**
 * @brief strdup() replacement using umalloc() and logging.
 *
 * This will create debug messages.
 *
 * @param s1 The string to duplicate
 *
 * @param func Funtion name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return No return value.
 */
char *__ustrdup(const char *s1, const char *func, const int line);
#define ustrdup(s1) __ustrdup(s1, __func__, __LINE__)

/**
 * @brief calloc() with error handling and logging.
 *
 * Call calloc() and handle errors by printing an error message and
 * calling exit(EXIT_FAILURE).
 *
 * In addition to that debug messages are created.
 *
 * @param size Size in bytes to allocate
 *
 * @param func Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returned is a pointer to the allocated memory.
 */
void *__ucalloc(size_t size, const char *func, const int line);
#define ucalloc(size) __ucalloc(size, __func__, __LINE__)

#endif   /* __PLUGIN_LIB_MALLOC */
