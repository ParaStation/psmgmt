/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PLUGIN_LIB_ENV
#define __PLUGIN_LIB_ENV

#include <stdint.h>
#include <stdbool.h>

/** Structure holding an environment */
typedef struct {
    char **vars;	/**< Array of variables */
    uint32_t cnt;       /**< Number of used elements in @ref vars */
    uint32_t size;      /**< Total amount of elements in @ref vars */
} env_t;

/**
 * @brief Initialize environment
 *
 * Initialize the environment structure @a env.
 *
 * @param env Environment to initialize
 *
 * @return No return value
 */
void envInit(env_t *env);

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
char *envGet(env_t *env, const char *name);

/**
 * @brief Get entry from environment
 *
 * Get the entry at index @a idx from the environment @a env.
 *
 * @param env Environment to search
 *
 * @param idx Index to look up
 *
 * @return If the index is value the full entry of the form
 * <key>=<value> is returned. Otherwise NULL is returned.
 */
char *envGetIndex(env_t *env, uint32_t idx);

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
 * @param func Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return If the entry was added, true is returned. Otherwise false
 * is returned.
 */
bool __envSet(env_t *env, const char *name, const char *val, const char *func,
	      const int line);

#define envSet(env, name, val) __envSet(env, name, val, __func__, __LINE__)

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
 * @param func Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return No return value
 */
void __envUnset(env_t *env, const char *name, const char *func, const int line);

#define envUnset(env, name) __envUnset(env, name, __func__, __LINE__)

/**
 * @brief Clear environment
 *
 * Clear the environment @a env. For this, all entries are removed and
 * the corresponding memory is free()ed.
 *
 * @param env Environment to clear
 *
 * @param func Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return No return value
 */
void __envDestroy(env_t *env, const char *func, const int line);

#define envDestroy(env) __envDestroy(env, __func__, __LINE__)

/**
 * @brief Put into environment
 *
 * Add the string @a envString to the environment @a env. @a envString
 * is expected to be of the form <name>=<value>. If an entry with key
 * <name> exists before, it will be removed from the environment.
 *
 * @param env Environment to extend
 *
 * @param envString Character string of the form <name>=<value> to be
 * added to the environment.
 *
 * @param func Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return If the entry was added, true is returned. Otherwise false
 * is returned.
 */
bool __envPut(env_t *env, const char *envString, const char *func,
	     const int line);

#define envPut(env, envstring) __envPut(env, envstring, __func__, __LINE__)

/**
 * @brief Get integer from environment
 *
 * Get an unsigned integer indexed by the key @a name from the
 * environment @a env and return it in @a val.
 *
 * @param env Environment to search
 *
 * @param name Key of the entry to look up
 *
 * @param val Pointer to the integer value upon return
 *
 * @return If an entry with key @a name is found and the corresponding
 * value could be converted to an unsigned integer true is
 * returned. Otherwise false is returned.
 */
bool envGetUint32(env_t *env, const char *name, uint32_t *val);

/**
 * @brief Remove from environment
 *
 * Remove the entry indexed by @a idx from the environment @a env.
 *
 * @param env Environment to modify
 *
 * @param idx Index of the entry to remove
 *
 * @param func Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return No return value
 */
void __envUnsetIndex(env_t *env, uint32_t idx, const char *func,
		     const int line);

#define envUnsetIndex(env, idx) __envUnsetIndex(env, idx, __func__, __LINE__)

/**
 * @brief Clone environment
 *
 * Clone the environment @a env into a new environment @a clone.
 *
 * If @a filter is given, only those elements of @a env that match the
 * filter are cloned into the new environment. Therefore, the key of
 * each entry of the environment to clone is tested against each
 * element of the filter-array. @a filter consists of a series of
 * strings that shall either exactly match a key or -- if the string's
 * last character is '*' -- match the beginning of a key.
 *
 * If @a filter is NULL, all elements of @a env are cloned.
 *
 * @param env Environment to clone
 *
 * @param clone Cloned environment upon return
 *
 * @param filter Array of strings to match those elements of @a env to
 * clone
 *
 * @param func Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return No return value
 */
void __envClone(env_t *env, env_t *clone, char **filter, const char *func,
		const int line);

#define envClone(env, clone, filter) __envClone(env, clone, filter,	\
						__func__, __LINE__)

/**
 * @brief Concatenate two environments
 *
 * Add the elements of environment @a env2 to the environment @a env1.
 *
 * If @a filter is given, only those elements of @a env2 that match
 * the filter are added to @a env1. Therefore, the key of each entry
 * of @a env2 is tested against each element of the filter-array. @a
 * filter consists of a series of strings that shall either exactly
 * match a key or -- if the string's last character is '*' -- match
 * the beginning of a key.
 *
 * If @a filter is NULL, all elements of @a env2 are added.
 *
 * @param env1 Environment to extend
 *
 * @param env2 Environment to add to @a env1
 *
 * @param filter Array of strings to match those elements of @a env2 to
 * be added to @a env1
 *
 * @param func Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return No return value
 */
void __envCat(env_t *env1, env_t *env2, char **filter, const char *func,
	      const int line);

#define envCat(env1, env2, filter) __envCat(env1, env2, filter, \
					    __func__, __LINE__)

#endif  /* __PLUGIN_LIB_ENV */
