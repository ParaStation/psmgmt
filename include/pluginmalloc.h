/*
 * ParaStation
 *
 * Copyright (C) 2012 - 2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PLUGIN_MALLOC_HELPER
#define __PLUGIN_MALLOC_HELPER

/**
 * @brief Malloc() with error handling and logging.
 *
 * Call malloc() and handle errors.
 *
 * @param size Size in bytes to allocate.
 *
 * @param func Function name of the calling function.
 *
 * @return Returned is a pointer to the allocated memory.
 */
#define umalloc(size) __umalloc(size, __func__, __LINE__)
void *__umalloc(size_t size, const char *func, const int line);

/**
 * @brief Realloc() with error handling and logging.
 *
 * Call realloc() and handle errors.
 *
 * @param size Size in bytes to allocate.
 *
 * @param func Function name of the calling function.
 *
 * @return Returned is a pointer to the allocated memory.
 */
#define urealloc(old, size) __urealloc(old, size, __func__, __LINE__)
void *__urealloc(void *old ,size_t size, const char *func, const int line);

/**
 * @brief Free memory using free() with logging.
 *
 * @param ptr Pointer to the memory address to free.
 *
 * @param func Funtion name of the calling function.
 *
 * @return No return value.
 */
#define ufree(ptr) __ufree(ptr, __func__, __LINE__)
void __ufree(void *ptr, const char *func, const int line);

/**
 * @brief Strdup() replacement using umalloc() and logging.
 *
 * @param s1 The string to duplicate.
 *
 * @param func Funtion name of the calling function.
 *
 * @return No return value.
 */
#define ustrdup(s1) __ustrdup(s1, __func__, __LINE__)
char *__ustrdup(const char *s1, const char *func, const int line);

/**
 * @brief Save a string into a buffer and let it dynamically grow if needed.
 *
 * @param strSave The string to write to the buffer.
 *
 * @param buffer The buffer to write the string to.
 *
 * @param bufSize The current size of the buffer.
 *
 * @return Returns a pointer to the buffer.
 */
char *str2Buf(char *strSave, char *buffer, size_t *bufSize);

#endif
