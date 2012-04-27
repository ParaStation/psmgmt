/*
 *               ParaStation
 *
 * Copyright (C) 2012 ParTec Cluster Competence Center GmbH, Munich
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PS_MOM_HELPER
#define __PS_MOM_HELPER

#include <stdint.h>

#define urealloc(old, size) __urealloc(old, size, __func__, __LINE__)
#define umalloc(size) __umalloc(size, __func__, __LINE__)
#define ustrdup(s1) __ustrdup(s1, __func__, __LINE__)
#define ufree(ptr) __ufree(ptr, __func__, __LINE__)

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
char *__ustrdup(const char *s1, const char *func, const int line);

#endif
