/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSENV_H
#define __PSENV_H

#include <stdbool.h>
#include <stdint.h>

/** Environment context to be created via @ref envNew() */
typedef struct env * env_t;

/**
 * @brief Create an environment
 *
 * Create an environment context in order to hold an arbitrary number
 * of entries. If the NULL terminated array of string @a envArray is
 * given, the environment context will use (and possibly modify) this
 * array. This implies:
 *
 * 1. The initial setting of the environment content is provided
 * within @a envArray
 *
 * 2. The ownership of @a envArray is passed to the environment context
 *
 * 3. Any modification of @a envArray later on will directly influence
 * the content of the environment context created here
 *
 * If it is required to avoid these implications, it is suggested to
 * construct the environment via @ref envConstruct().
 *
 * @a envArray is expected to be NULL terminated and each string must
 * be of the form "<key>=<value>" representing individual environment
 * entries. Each key is expected to be unique throughout @a envArray.
 *
 * @param envArray NULL terminated array of strings defining the
 * environment; might be NULL
 *
 * @return Handle to the environment if it was successfully created or
 * NULL
 */
env_t envNew(char **envArray);

/**
 * @brief Check environment for initialization
 *
 * Check if the environment represented by @a env is initialized,
 * i.e. if @ref envNew() was called for this environment before.
 *
 * @param env Environment's handle
 *
 * @return Return true if the environment is initialized; or false
 * otherwise
 */
bool envInitialized(env_t env);

/**
 * @brief Get environment's size
 *
 * Get the actual size, i.e. the number of stored variables, of the
 * environment @a env.
 *
 * @param env Environment to investigate
 *
 * @return Size of the environment
 */
uint32_t envSize(env_t env);

/**
 * @brief Get value from environment
 *
 * Get the value indexed by the key @a name from the environment @a env.
 *
 * @param env Environment to search
 *
 * @param name Key of the entry to look up
 *
 * @return If an entry with key @a name is found, the corresponding
 * value is returned. Otherwise NULL is returned.
 */
char *envGet(const env_t env, const char *name);

/**
 * @brief Dump entry from environment
 *
 * Dump the entry at index @a idx from the environment @a env.  This
 * function is mainly used for putting whole environments into a
 * processes real environment via successiv calls of putenv().
 *
 * @param env Environment to search
 *
 * @param idx Index to look up
 *
 * @return If an environment variable is stored under the index @a
 * idx, a string of the format 'name=value' is returned. Otherwise
 * NULL is returned.
 */
char *envDumpIndex(const env_t env, uint32_t idx);

/**
 * @brief Add to environment
 *
 * Add an entry with key @a name and value @a val to the environment
 * @a env. If an entry with key @a name exists before, it will be
 * removed from the environment.
 *
 * @param env Environment to extend
 *
 * @param name Key of the entry to add
 *
 * @param val Value of the entry to add
 *
 * @return If the entry was added, true is returned. Otherwise false
 * is returned.
 */
bool envSet(env_t env, const char *name, const char *val);

/**
 * @brief Remove from environment
 *
 * Remove the entry indexed by the key @a name from the environment @a
 * env.
 *
 * @param env Environment to modify
 *
 * @param name Key of the entry to remove
 *
 * @return No return value
 */
void envUnset(env_t env, const char *name);

/**
 * @brief Steal strings from the environment
 *
 * Destroy the environment @a env but leave the actual strings
 * alone. For this, all memory occupied by the environment is
 * free()ed, but not the individual string's memory.
 *
 * This is meant to be used after all the environment has been
 * putenv()ed.
 *
 * @param env Environment to steal the strings from
 *
 * @return No return value
 */
void envSteal(env_t env);

/**
 * @brief Steal environment's string array
 *
 * Destroy the environment @a env but leave the actual string array
 * alone. For this, all memory occupied by the environment is
 * free()ed, but not the representing string array's memory that can
 * be accessed via envGetArray().
 *
 * This is meant to be used after a handle to the string array is
 * gained through envGetArray() and the string array shall be kept
 * outside of @a env on the long run.
 *
 * Instead of utilizing @ref envGetArray() before, the return value of
 * this function might be used directly. I.e.
 *
 * char **environ = envStealArray(env);
 *
 * is equivalent to:
 *
 * char **environ = envGetArray(env)
 * envStealArray(env)
 *
 * @param env Environment to steal the string array from
 *
 * @return Pointer to a NULL terminated string array or NULL if @a env
 * is still uninitialized or empty
 */
char **envStealArray(env_t env);

/**
 * @brief Clear environment
 *
 * Clear the environment @a env. For this, all entries are removed and
 * the corresponding memory is free()ed.
 *
 * @param env Environment to clear
 *
 * @param shred If true overwrite the memory with zeros before free
 *
 * @return No return value
 */
void __envDestroy(env_t env, bool shred);

#define envDestroy(env) __envDestroy(env, false);
#define envShred(env) __envDestroy(env, true);

/**
 * @brief Add string to environment
 *
 * Add the string @a envStr to the environment @a env. @a envStr is
 * expected to be of the form <name>=<value>. If an entry with key
 * <name> exists before, it will be replaced in the environment.
 *
 * Note: Unlike putenv(), this function stores a copy of the passed string.
 *
 * @param env Environment to extend
 *
 * @param envStr Character string of the form <name>=<value> to be
 * added to the environment
 *
 * @return If the entry was added, true is returned; otherwise false
 * is returned
 */
bool envAdd(env_t env, const char *envStr);

/**
 * @brief Construct environment from array
 *
 * Construct a new environment from the NULL terminated array of
 * string @a envArray.
 *
 * If @a filter is given, only those elements of @a envArray that
 * match the filter are added to the new environment. For this, each
 * element is passed to the filter function @a filter and only added
 * if this returns true.
 *
 * If @a filter is NULL, all elements of @a envArray are added to the
 * environment.
 *
 * @a envArray is expected to be NULL terminated and each string must
 * be of the form "<key>=<value>" representing individual environment
 * entries. Each key is expected to be unique throughout @a envArray.
 *
 * @param envArray NULL terminated array of character strings to create
 * the new environment from
 *
 * @param filter Function filtering the elements of @a envArray to put
 * into the environment
 *
 * @return If the environments was successfully constructed, the
 * handle to this new environment is returned; or NULL in case of
 * error
 */
env_t envConstruct(char **envArray, bool filter(const char *));

/**
 * @brief Access environment's string array
 *
 * Get a handle on a string array representing the environment @a
 * env. The string array is NULL terminated and remains in the
 * ownership of @a env, i.e.
 *
 * - it will get obsolete as soon as @a env is destroyed
 *
 * - any modification of this array will immediately affect @a env
 *
 * - any modifications of @a env will immediately change the returned
 *   string array
 *
 * Thus, if it is required to modify the returned string array or to
 * rely on the content of it on the long run, it is advised to either
 * steal it from @a env utilizing envStealArray() or to work on a
 * clone of @a env created via @ref envClone().
 *
 * The main purpose of this function is to feed the content of @a env
 * into functions like @ref addStringArrayToMsg() or execve().
 *
 * @param env Environment to get a string array handle on
 *
 * @return Pointer to a NULL terminated string array or NULL if @a env
 * is still uninitialized or empty
 */
char **envGetArray(env_t env);

/**
 * @brief Clone environment
 *
 * Clone the environment @a env into a new environment.
 *
 * If @a filter is given, only those elements of @a env that match the
 * filter are cloned. For this, each element is passed to the filter
 * function @a filter and only added if this returns true.
 *
 * If @a filter is NULL, all elements of @a env are cloned.
 *
 * @param env Environment to clone
 *
 * @param filter Function filtering the elements of @a env to clone
 *
 * @return If the environments was successfully cloned, the handle to
 * the cloned environment is returned; or NULL in case of error
 */
env_t envClone(const env_t env, bool filter(const char *));

/**
 * @brief Concatenate two environments
 *
 * Add the elements of environment @a src to the environment @a dst.
 *
 * If @a filter is given, only those elements of @a src that match the
 * filter are added to @a dst. For this, each element is passed to the
 * filter function @a filter and only added if this returns true.
 *
 * If @a filter is NULL, all elements of @a src are added.
 *
 * @param dst Environment to extend
 *
 * @param src Environment to add to @a dst
 *
 * @param filter Function filtering the @a src elements
 *
 * @return If both environments were successfully concatenated, true
 * is returned. Or false in case of error. In the latter case @a dst
 * might be modified upon return and contain parts of @a src.
 */
bool envCat(env_t dst, const env_t src, bool filter(const char *));

/**
 * @brief Evict elements from environment
 *
 * Evict all elements from the environment @a env that match the
 * filter.  For this, each element is passed to the filter function @a
 * filter together with some extra information @a info is pointing
 * to. If the filter returns true, the element is evicted from @a env.
 *
 * @param env Environment to filter
 *
 * @param filter Function filtering the elements
 *
 * @param info Extra information to be passed to the filter function
 *
 * @return No return value
 */
void envEvict(env_t env, bool filter(const char *, void *), void *info);

/**
 * @brief Create an environment from comma separated string
 *
 * Create an environment context and initialize it with the content
 * provided in @a string. @a string is expected to contain a
 * comma-separated list of <key>=<value> entries.
 *
 * @param string Character string holding the environment
 *
 * @return Handle to the environment if it was successfully created or
 * NULL
 */
env_t envFromString(const char *string);

#endif  /* __PSENV_H */
