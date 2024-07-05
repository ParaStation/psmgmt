/*
 * ParaStation
 *
 * Copyright (C) 2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/*
 * This small library is a collection of functions for easy handling
 * of dynamic growing buffers used to host strings.
 */
#ifndef __PSSTRBUF_H
#define __PSSTRBUF_H

#include <stdbool.h>
#include <stdint.h>

/** String buffer context to be created via @ref strbufNew() */
typedef struct strbuf * strbuf_t;

/**
 * @brief Create a string buffer
 *
 * Create a string buffer context in order to hold a buffer to contain
 * a string that might grow dynamically. If the NULL terminated string
 * @a str is given, the newly created string buffer will be pre-filled
 * with a copy of @a str.
 *
 * @a str String to pre-fill the new string buffer context with
 *
 * @return Handle to the string buffer context if it was successfully
 * created or NULL
 */
strbuf_t strbufNew(const char *str);

/**
 * @brief Check string buffer for initialization
 *
 * Check if the string buffer represented by @a strbuf is initialized,
 * i.e. if @ref strbufNew() was called for this string buffer before.
 *
 * @param strbuf String buffer handle
 *
 * @return Return true if the string buffer is initialized; or false
 * otherwise
 */
bool strbufInitialized(const strbuf_t strbuf);

/**
 * @brief Get string buffer's length
 *
 * Get the actual length, i.e. the length of the representing string
 * including the trailing '\0' byte, for the string buffer @a strbuf.
 *
 * @param strbuf String buffer to investigate
 *
 * @return Length of the string buffer
 */
uint32_t strbufLen(strbuf_t strbuf);

/**
 * @brief Get string buffer's size
 *
 * Get the actual size, i.e. the size of the memory region allocated
 * to hold the representing string, for the string buffer @a strbuf.
 *
 * @param strbuf String buffer to investigate
 *
 * @return Size of the string buffer
 */
uint32_t strbufSize(strbuf_t strbuf);

/**
 * @brief Append string to string buffer
 *
 * Append the string @a str to the content of the string buffer @a
 * strbuf. If required, @a strbuf will grow dynamically.
 *
 * @param strbuf String buffer to extend
 *
 * @param str String to add
 *
 * @return Return true if the string buffer could be expanded and @a
 * str was added; or false otherwise
 */
bool strbufAdd(strbuf_t strbuf, const char *str);

/**
 * @brief Access string buffer's string
 *
 * Get a handle on the string representing the string buffer @a
 * strbufv. The string is \0 terminated and remains in the ownership
 * of @a strbuf, i.e.
 *
 * - it will get obsolete as soon as @a strbuf is destroyed
 *
 * - any modification of this array will immediately affect @a strbbuf
 *
 * - any modifications of @a strbuf will immediately change the returned
 *   string
 *
 * Thus, if it is required to modify the returned string or to rely on
 * the content of it on the long run, it is advised to steal it from
 * @a strbuf utilizing strbufSteal().
 *
 * @param strbuf String buffer to get a string handle on
 *
 * @return Pointer to a \0 terminated string or NULL if @a strbuf is
 * still uninitialized or empty
 */
char *strbufStr(strbuf_t strbuf);

/**
 * @brief Steal string buffer's string
 *
 * Destroy the string buffer @a strbuf but leave the actual string
 * alone. For this, all memory occupied by the string buffer is
 * free()ed, but not the representing string that can be accessed via
 * @ref strbufStr().
 *
 * This is meant to be used after a handle to the string is gained
 * through @ref strbufStr() and the string shall be kept outside of @a
 * strbuf on the long run.
 *
 * Instead of utilizing @ref strbufStr() before, the return value of
 * this function might be used directly. I.e.
 *
 * char *str = strbufSteal(strbuf);
 *
 * is equivalent to:
 *
 * char *str = strbufStr(strbuf)
 * strbufSteal(strbuf)
 *
 * @param strbuf String buffer to steal the string from
 *
 * @return Pointer to a NULL terminated string or NULL if @a strbuf is
 * still uninitialized or empty
 */
char *strbufSteal(strbuf_t strbuf);

/**
 * @brief Destroy string buffer
 *
 * Destroy the string buffer @a strbuf. All memory used by the string
 * buffer itself and the containing string is invalidated and
 * free()ed.
 *
 * @param strbuf String buffer to destroy
 *
 * @return No return value
 */
void strbufDestroy(strbuf_t strbuf);

#endif  /* __PSSTRBUF_H */
