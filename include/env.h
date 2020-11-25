/*
 * ParaStation
 *
 * Copyright (C) 2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Simple environment handling
 */
#ifndef _ENV_H_
#define _ENV_H_

/** The structure holding the environment */
typedef struct env_fields_s{
    char **vars;    /**< Array of variables */
    int cnt;        /**< Actual size */
    int size;       /**< Actual maximum size */
}env_fields_t;

/**
 * @brief Init an environment
 *
 * Initialzie the environment @a env. This mainly resets the contenv
 * of @a env, i.e. if @a env is initialized more than once this may
 * lead to a memory leak.
 *
 * @param env The environment to initialize.
 *
 * @return No return value.
*/
void env_init(env_fields_t *env);

/**
 * @brief Get environment's size.
 *
 * Get the actual size, i.e. the number of stored variables, of the
 * environment @a env.
 *
 * @param env The environment the size should be determined from.
 *
 * @return The size of the environment is returned.
 */
int env_size(env_fields_t *env);

/**
 * @brief Get a variables index.
 *
 * Get the index of the variable with name @a name within the
 * environment @a env. The returned index is in between 0 and size-1,
 * where size is the actual size of the environment.
 *
 * @param env The environment to search in.
 *
 * @param name The name of the variable to search for. It must not
 * contain the character '='.
 *
 * @return On success, i.e. if the variable was found, its index is
 * returned. Or -1, if it was not found or an error occured.
 */
int env_index(env_fields_t *env, const char *name);

/**
 * @brief Set a variable.
 *
 * Set the variable with name @a name to the value @a val within the
 * environment @a env.
 *
 * If a variable with name @a name is already set, it will be removed
 * using the env_unset() function before registering the variable to
 * its new value.
 *
 * If the actual maximum size of the environment is not sufficient, it
 * will be expanded on the fly.
 *
 * @param env The environment to act on.
 *
 * @param name The name of the variable to set. It must not contain
 * the character '='.
 *
 * @param val The value to be assigned to the variable.
 *
 * @return On success, i.e. if the variable could be registered, 0 is
 * returned. Or -1, if an error occured.
 */
int env_set(env_fields_t *env, const char *name, const char *val);

/**
 * @brief Set a variable.
 *
 * Set the variable with name @a name to the value @a val within the
 * environment @a env.
 *
 * If a variable with name @a name is already set, it will be removed
 * using the env_unset() function before registering the variable to
 * its new value.
 *
 * If the actual maximum size of the environment is not sufficient, it
 * will be expanded on the fly.
 *
 * @param env The environment to act on.
 *
 * @param name The name of the variable to set. It must not contain
 * the character '='.
 *
 * @param val The value to be assigned to the variable.
 *
 * @param index The integer will receive the index in the environment.
 *
 * @return On success, i.e. if the variable could be registered, 0 is
 * returned. Or -1, if an error occured.
 */
int env_setIdx(env_fields_t *env, const char *name, const char *val, int *index);

/**
 * @brief Get a variable.
 *
 * Get the value of the variable with name @a name within the
 * environment @a env. The variable has to be registered to the
 * environment before using the env_set() function.
 *
 * @param env The environment to look at.
 *
 * @param name The name of the variable to lookup. It must not contain
 * the character '='.
 *
 * @return On success, i.e. if the variable could be found, a pointer
 * to the value is returned. Or NULL if an error occured.
 */
char *env_get(env_fields_t *env, const char *name);

/**
 * @brief Get a variable.
 *
 * Get the value of the variable with name @a name within the
 * environment @a env. The variable has to be registered to the
 * environment before using the env_set() function.
 *
 * @param env The environment to look at.
 *
 * @param name The name of the variable to lookup. It must not contain
 * the character '='.
 *
 * @param index The integer will receive the index in the environment.
 *
 * @return On success, i.e. if the variable could be found, a pointer
 * to the value is returned. Or NULL if an error occured.
 */
char *env_getIdx(env_fields_t *env, const char *name, int *index);

/**
 * @brief Remove a variable.
 *
 * Remove the variable with name @a name from the environment @a
 * env. The variable has to be registered to the environment before
 * using the env_set() function.
 *
 * @param env The environment to remove from.
 *
 * @param name The name of the variable to remove. It must not contain
 * the character '='.
 *
 * @return On success, i.e. if the variable could be found, 0 is
 * returned. Or -1 if an error occured.
 */
int env_unset(env_fields_t *env, const char *name);

/**
 * @brief Dump an environment.
 *
 * Dump the variable with index @a idx from the environment @a env.
 * This function is mainly used for putting whole environments into a
 * processes real environment via successiv calls of putenv().
 *
 * @param env The environment to dump from.
 *
 * @param idx The index of the variable to dump.
 *
 * @return If an environment variable is stored under the index @a
 * idx, a string of the format 'name=value' is returned. Otherwise
 * NULL is returned.
 */
char *env_dump(env_fields_t *env, int idx);

#endif  /* _ENV_H_ */
