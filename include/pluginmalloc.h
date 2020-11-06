/*
 * ParaStation
 *
 * Copyright (C) 2012-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PLUGIN_LIB_MALLOC
#define __PLUGIN_LIB_MALLOC

#include <stddef.h>

typedef struct {
    char *buf;
    size_t size;
    size_t strLen;
} StrBuffer_t;

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
 * Copy the string @a str into the dynamic buffer @a strBuf is
 * pointing to. If required, this buffer will be grown using @ref
 * __umalloc() and @ref __urealloc(). The current size of the buffer
 * is tracked internally within the string buffer @a strBuf. This improves
 * performance in comparison to @ref str2Buf() and @ref strn2Buf() and is
 * the preferred choice. The user is responsible to free the buffer after
 * use with @ref freeStrBuf().
 *
 * @param str The string to write to the buffer
 *
 * @param strBuf The buffer to write the string to
 *
 * @param func Funtion name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returns a pointer to the buffer.
 */
char *__addStrBuf(const char *str, StrBuffer_t *strBuf, const char *func,
		  const int line);
#define addStrBuf(str, strBuf) \
    __addStrBuf(str, strBuf, __func__, __LINE__)

/**
 * @brief Free memory used by a string buffer
 *
 * @param strBuf Pointer to the string buffer to free
 */
void __freeStrBuf(StrBuffer_t *strBuf, const char *func, const int line);
#define freeStrBuf(strBuf) \
    __freeStrBuf(strBuf, __func__, __LINE__)

/**
 * @brief Save a string into a buffer that dynamically grows if needed
 *
 * If possible @ref addStrBuf() should be used instead.
 *
 * Copy the string @a str into the dynamic buffer @a buffer is
 * pointing to. If required, this buffer will be grown using @ref
 * __umalloc() and @ref __urealloc(). The current size of the buffer
 * is tracked in @a bufSize. This is a wrapper to @ref strn2Buf().
 *
 * @param str The string to write to the buffer
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
char *__str2Buf(const char *str, char **buffer, size_t *bufSize,
		const char *func, const int line);
#define str2Buf(str, buffer, bufSize) \
    __str2Buf(str, buffer, bufSize, __func__, __LINE__)

/**
 * @brief Save a string into a buffer that dynamically grows if needed
 *
 * If possible @ref addStrBuf() should be used instead.
 *
 * Copy @a lenStr bytes from the string @a str into the dynamic buffer
 * @a buffer is pointing to. If required, this buffer will be grown
 * using @ref __umalloc() and @ref __urealloc(). The current size of
 * the buffer is traced in @a bufSize.
 *
 * @param str The string to write to the buffer
 *
 * @param lenStr The number of bytes of @a str to write to the buffer
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
char *__strn2Buf(const char *str, size_t lenStr, char **buffer, size_t *bufSize,
		 const char *func, const int line);
#define strn2Buf(str, lenStr, buffer, bufSize) \
    __strn2Buf(str, lenStr, buffer, bufSize, __func__, __LINE__)

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
