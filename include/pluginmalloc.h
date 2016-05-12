/*
 * ParaStation
 *
 * Copyright (C) 2012-2016 ParTec Cluster Competence Center GmbH, Munich
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
 * @brief Save a string into a buffer that dynamically grows if needed
 *
 * Copy the string @a strSave into the dynamic buffer @a buffer is
 * pointing to. If required, this buffer will be grown using @ref
 * __umalloc() and @ref __urealloc. The current size of the buffer is
 * traced in @a bufSize.
 *
 * In addition to that debug messages are created.
 *
 * @param strSave The string to write to the buffer
 *
 * @param buffer The buffer to write the string to
 *
 * @param bufSize The current size of the buffer
 *
 * @param func Funtion name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returns a pointer to the buffer.
 */
char *__str2Buf(char *strSave, char **buffer, size_t *bufSize, const char *func,
		const int line);
#define str2Buf(strSave, buffer, bufSize) \
    __str2Buf(strSave, buffer, bufSize, __func__, __LINE__)

/**
 * @doctodo
 */
char *__strn2Buf(char *strSave, size_t lenSave, char **buffer, size_t *bufSize,
		 const char *func, const int line);
#define strn2Buf(strSave, lenSave, buffer, bufSize) \
    __strn2Buf(strSave, lenSave, buffer, bufSize, __func__, __LINE__)

#endif   /* __PLUGIN_LIB_MALLOC */
