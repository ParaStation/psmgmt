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

/** String vector context to be created via @ref strvNew() */
typedef struct strv * strv_t;

/**
 * @brief Create a string vector
 *
 * Create a string vector context in order to hold an arbitrary number
 * of strings. If the NULL terminated array of strings @a strArray is
 * given, the string vector context will use (and possibly modify)
 * this array. This implies:
 *
 * 1. The initial setting of the string vector content is provided
 * within @a strArray
 *
 * 2. The ownership of @a strArray is passed to the string vector
 * context
 *
 * 3. Any modification of @a strArray later on will directly influence
 * the content of the string vector context created here
 *
 * If it is required to avoid these implications, it is suggested to
 * construct the string vector via @ref strvConstruct().
 *
 * @a strArray is expected to be NULL terminated.
 *
 * @param strArray NULL terminated array of strings defining the
 * string vector; might be NULL
 *
 * @return Handle to the string vector if it was successfully created
 * or NULL
 */
strv_t strvNew(char **strArray);

/**
 * @brief Construct a string vector
 *
 * Construct a string vector context from the NULL terminated array of
 * string @a strArray. In contrast to @ref strvNew(), @a strArray
 * itself is not part of the newly constructed context. Nevertheless,
 * the strings the elements of @a strArray are pointing to will still
 * be referenced. This implies:
 *
 * 1. The initial setting of the string vector content is provided
 * within @a strArray
 *
 * 2. The ownership of @a strArray remains at the caller
 *
 * 3. Any modification of the strings, the elements of @a strArray are
 * pointing to later on will directly influence the content of the
 * string vector context created here
 *
 * @a strArray is expected to be NULL terminated.
 *
 * @param strArray NULL terminated array of strings defining the
 * string vector; if NULL, no string vector context will be created
 *
 * @return Handle to the string vector if it was successfully
 * constructed or NULL
 */
strv_t strvConstruct(char **strArray);

/**
 * @brief Clone string vector
 *
 * Clone the string vector @a strv into a new string vector context.
 *
 * @param strv String vector to clone
 *
 * @return If the string vector was successfully cloned, the handle to
 * the cloned string vectory is returned; or NULL in case of error
 */
strv_t strvClone(const strv_t strv);

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
bool strvInitialized(const strv_t strv);

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
uint32_t strvSize(strv_t strv);

/**
 * @brief Add string to string vector
 *
 * Append the string @a str to the string vector @a strv.
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
bool strvAdd(strv_t strv, const char *str);

/**
 * @brief Link string to string vector
 *
 * Append the string @a str itself to the string vector @a strv.
 *
 * @attention The pointer @a str to the string is stored directly,
 * i.e. the "ownership" of @a str is transferred to @a strv. If this
 * is not intended, consider to use of @ref strvAdd.
 *
 * @param strv String vector to be extended
 *
 * @param str String to add
 *
 * @return If @a str was appended, true is returned; or false in case
 * of error
 */
bool strvLink(strv_t strv, char *str);

/**
 * @brief Replace string in string vector
 *
 * Replace the string at index @a idx in the string vector @a strv by
 * the replacement string @a str. Memory occupied at index @a idx in
 * the string vector @a strv will be released.
 *
 * @param strv String vector to be modified
 *
 * @param idx Index to be replaced
 *
 * @param str Replacement to be inserted
 *
 * @return If the string was successfully replaced, true is returned;
 * or false in case of error
 */
bool strvReplace(strv_t strv, uint32_t idx, const char *str);

/**
 * @brief Get string at index
 *
 * Get the string at index @a idx in the string vector @a strv.
 *
 * @param strv String vector to be looked up
 *
 * @param idx Index to be replaced
 *
 * @return If a string is available at index @a idx, this string is
 * returned; or NULL otherwise
 */
char *strvGet(const strv_t strv, uint32_t idx);

/**
 * @brief Append string vector
 *
 * Append the strings of string vector @a src to the string vector @a
 * dst. The strings added to @a dst will be copies of the strings
 * contained in @a src, i.e. @a src will remain untouched.
 *
 * @a dst is expected to be initialized, or the call will fail. If @a
 * src is empty or uninitialized, @a dst will be left untouched.
 *
 * @param dst String vector to extend
 *
 * @param src String vector to append to @a dst
 *
 * @return If @a src was successfully appended, true is returned; or
 * false in case of error. In the latter case @a dst might be modified
 * upon return and contain parts of @a src.
 */
bool strvAppend(strv_t dst, strv_t src);

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
 * rely on the content of it on the long run, it is advised to steal
 * it from @a strv utilizing strvStealArray().
 *
 * The main purpose of this function is to feed the content of @a strv
 * into functions like @ref addStringArrayToMsg() or exec().
 *
 * @param strv String vector to get a string array handle on
 *
 * @return Pointer to a NULL terminated string array or NULL if @a strv
 * is still uninitialized or empty
 */
char **strvGetArray(strv_t strv);

/**
 * @brief Destroy string vector
 *
 * Destroy the string vector @a strv. All memory used by the string
 * vector itself and the containing strings is invalidated and
 * free()ed.
 *
 * @param strv String vector to destroy
 *
 * @param shred If true overwrite memory with zeros before free
 *
 * @return No return value
 */
void __strvDestroy(strv_t strv, bool shred);

#define strvDestroy(strv) __strvDestroy(strv, false);
#define strvShred(strv) __strvDestroy(strv, true);

/**
 * @brief Steal string vector's strings
 *
 * Destroy the string vector @a strv but leave the actual strings
 * alone. For this, all memory occupied by the string vector is
 * free()ed, but not the individual string's memory.
 *
 * This is meant to be used after all the string vector content has been
 * used otherwise, i.e. transferred ownership to a different control.
 *
 * @param strv String vector to steal the strings from
 *
 * @return No return value
 */
void strvSteal(strv_t strv);

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
char **strvStealArray(strv_t strv);

#endif  /* __PSSTRV_H */
