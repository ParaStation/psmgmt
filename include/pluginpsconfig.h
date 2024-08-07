/*
 * ParaStation
 *
 * Copyright (C) 2020-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021,2023-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PLUGIN_LIB_PSCONFIG
#define __PLUGIN_LIB_PSCONFIG

#include <stdbool.h>
#include <stddef.h>

#include "psstrbuf.h"

/**
 * @brief Configuration context
 *
 * To be initialized with @ref pluginConfig_new() and filled with @ref
 * pluginConfig_parse().
 */
typedef struct pluginConfig * pluginConfig_t;

/**
 * @brief Allocate configuration context
 *
 * Allocate and initialize the configuration context @a conf.
 *
 * @param conf Pointer the newly allocated configuration context is
 * assigned to
 *
 * @return true on success, false on error
 */
bool pluginConfig_new(pluginConfig_t *conf);

/**
 * @brief Terminate and free configuration context
 *
 * Terminate and free the complete configuration @a conf, i.e. release
 * all dynamic memory associated to this configuration.
 *
 * @param conf The configuration to be released
 *
 * @return No return value
 */
void pluginConfig_destroy(pluginConfig_t conf);

/** Types of data available for configuration values */
typedef enum {
    PLUGINCONFIG_VALUE_NONE,  /**< No type specified (unused) */
    PLUGINCONFIG_VALUE_NUM,   /**< Value is number */
    PLUGINCONFIG_VALUE_STR,   /**< Value is character string */
    PLUGINCONFIG_VALUE_LST,   /**< Value is NULL-terminated array of strings */
} pluginConfigValType_t;

/** Configuration's value data type */
typedef struct {
    pluginConfigValType_t type;  /**< Flag the union member to use */
    union {
	long num;             /**< Value as number */
	char *str;            /**< Value as character string */
	char **lst;           /**< Value as NULL-terminated array of strings */
    } val;
} pluginConfigVal_t;

/** Definition of a single configuration parameter */
typedef struct {
    const char *name;                 /**< name of the config key */
    const pluginConfigValType_t type; /**< type of the config value */
    const char *desc;                 /**< short help description */
} pluginConfigDef_t;

/**
 * @brief Set configuration definition
 *
 * Add the configuration definition @a def to the configuration
 * context @a conf. @a def will be used in order to validate all
 * values within the configuration loaded from psconfig or set via
 * @ref pluginConfig_add() et al. Furthermore, @a def contains all
 * information required to describe the configuration entries to the
 * outside world.
 *
 * @a def must be NULL terminated, i.e. the last entry must be of the
 * form `{ NULL, PLUGINCONFIG_VALUE_NONE, NULL }`.
 *
 * @remark @a def must be static, i.e. conf will refer to the passed
 * argument without creating a copy.
 *
 * @param conf Configuration context to be described
 *
 * @param def Definition describing the content of the handled
 * configuration context
 *
 * @return Return true if @a def is valid and was added; or false
 * otherwise
 */
bool pluginConfig_setDef(pluginConfig_t conf, const pluginConfigDef_t def[]);

/**
 * @brief Load configuration from psconfig
 *
 * Load the configuration from the branch of psconfig identified by @a
 * configKey and store it to the configuration context @a conf. All
 * configuration is fetched from Psid.PluginCfg.@a configKey of the
 * local host object.
 *
 * If a definition of the configuration is available, i.e. was
 * registered before via @ref pluginConfig_setDef(), the configuration
 * will be verified immediately via @ref pluginConfig_verify().
 *
 * @param conf Configuration ready for further use
 *
 * @param configKey Name of the psconfig branch to be used
 *
 * @return If the configuration was successfully loaded, true is
 * returned; or false if an error occurred, i.e. either no
 * configuration was found within the local host object or not all
 * configuration elements were validated successfully.
 */
bool pluginConfig_load(pluginConfig_t conf, const char *configKey);

/**
 * @brief Visitor function
 *
 * Visitor function used by @ref traverseConfig() in order to visit
 * each object in a given configuration context.
 *
 * The parameters are as follows: @a key and @a value are the
 * corresponding parts of the key-value pair forming the configuration
 * object. @a info points to the additional information passed to @ref
 * traverseConfig() in order to be forwarded to each object.
 *
 * If the visitor function returns false, the traversal will be
 * interrupted and @ref traverseConfig() will return to its calling
 * function.
 */
typedef bool pluginConfigVisitor_t(const char *key,
				   const pluginConfigVal_t *val,
				   const void *info);

/**
 * @brief Visitor to implement plugin's show function
 *
 * Special visitor to implement a plugin's show functionality. It will
 * add all key-value pairs of a given configuration context passed to
 * @ref pluginConfig_traverse() to the string buffer of type
 * strbuf_t that was passed as the info parameter.
 */
pluginConfigVisitor_t pluginConfig_showVisitor;

/**
 * @brief Traverse configuration
 *
 * Traverse the configuration context @a conf by calling @a visitor for each
 * of the embodied objects. In addition to the object's key and value
 * @a info is passed as additional information.
 *
 * If @a visitor returns false, the traversal will be stopped
 * immediately and false is returned to the calling function.
 *
 * @param conf Configuration context to be traversed
 *
 * @param visitor Visitor function to be called for each object
 *
 * @param info Additional information to be passed to @a visitor while
 * visiting all objects within @a conf
 *
 * @return If @a visitor returns false, traversal will be stopped and
 * false is returned; or true if no visitor returned false during the
 * traversal
 */
bool pluginConfig_traverse(pluginConfig_t conf, pluginConfigVisitor_t visitor,
			   const void *info);

/**
 * @brief Add entry to configuration
 *
 * Add the key-value pair given by @a key and @a value to the existing
 * configuration context @a conf. If an entry with key @a key is already
 * existing in the configuration, the corresponding value is
 * replaced. Otherwise a new key-value pair is added to the
 * configuration context.
 *
 * If a definition of the configuration is available, i.e. was
 * registered before via @ref pluginConfig_setDef(), the entry will be
 * verified immediately via @ref pluginConfig_verifyEntry().
 *
 * If @a key is defined to be PLUGINCONFIG_VALUE_NUM and @a value can
 * be converted to a number the key-value pair will be add accordingly.
 *
 * @param conf Configuration context to be expanded
 *
 * @param key Key-part of the key-value pair to be added
 *
 * @param value Value-part of the key-value pair to be added
 *
  * @return If the configuration was successfully expanded, i.e. the
 * key-value pair could be added, true is returned; or false if an
 * error occurred; the latter might hint to the fact that @a value
 * violates the definition
 */
bool pluginConfig_addStr(pluginConfig_t conf, const char *key, char *value);

/**
 * @brief Add item to list-entry of configuration
 *
 * Add a single list-item @a item to the list identified by @a key in
 * the configuration context @a conf. If no entry with key @a key is
 * existing yet, a new key-value pair is added to the configuration
 * context with a single-entry-ed list as value.
 *
 * If a definition of the configuration is available, i.e. was
 * registered before via @ref pluginConfig_setDef(), the entry will be
 * verified immediately via @ref pluginConfig_verifyEntry().
 *
 * @param conf Configuration context to be expanded
 *
 * @param key Key-part of the key-value pair to be added
 *
 * @param value Value-part of the key-value pair to be added
 *
 * @return If the key-value pair was successfully expanded or created,
 * true is returned; or false if an error occurred; the latter might
 * hint to the fact that it violates the definition
 */
bool pluginConfig_addToLst(pluginConfig_t conf, const char *key, char *item);

/**
 * @brief Add entry to configuration
 *
 * Add the key-value pair given by @a key and @a value to the existing
 * configuration context @a conf. If an entry with key @a key is
 * already existing in the configuration, the corresponding value is
 * replaced. Otherwise a new key-value pair is added to the
 * configuration.
 *
 * Filling the entry with @a value will reuse all dynamic data @a
 * value is referring to (e.g. the character array @a str, or the list
 * @a lst and its content). Thus, the mentioned data of @a value must
 * be dynamically allocated and must not be free()ed by the calling
 * process.
 *
 * If a definition of the configuration is available, i.e. was
 * registered before via @ref pluginConfig_setDef(), the entry will be
 * verified immediately via @ref pluginConfig_verifyEntry().
 *
 * @param conf Configuration context to be expanded
 *
 * @param key Key-part of the key-value pair to be added
 *
 * @param value Value-part of the key-value pair to be added
 *
  * @return If the configuration was successfully expanded, i.e. the
 * key-value pair could be added, true is returned; or false if an
 * error occurred; the latter might hint to the fact that @a value
 * violates the definition
 */
bool pluginConfig_add(pluginConfig_t conf,
		      const char *key, pluginConfigVal_t *value);

/**
 * @brief Get value
 *
 * Get a pointer to the value of the entry identified by the key @a
 * key from the configuration context @a conf. The pointer must not be
 * de-referenced for manipulation.
 *
 * @param conf Configuration context to be searched
 *
 * @param key Key identifying the entry
 *
 * @return If a corresponding entry is found a pointer to its value is
 * returned; otherwise NULL is returned
 */
const pluginConfigVal_t *pluginConfig_get(pluginConfig_t conf, const char *key);

/**
 * @brief Get value as number
 *
 * Get the value of the entry identified by the key @a key from the
 * configuration context @a conf. The value is returned as a long
 * if it is of type PLUGINCONFIG_VALUE_NUM.
 *
 * @param conf Configuration context to be searched
 *
 * @param key Key identifying the entry
 *
 * @return If a corresponding entry is found and is of type
 * PLUGINCONFIG_VALUE_NUM, its value is returned; otherwise -1 is
 * returned
 */
long pluginConfig_getNum(pluginConfig_t conf, const char *key);

/**
 * @brief Get value as character array
 *
 * Get the value of the entry identified by the key @a key from the
 * configuration context @a conf. The value is returned as a pointer
 * to a character array if it is of type PLUGINCONFIG_VALUE_STR.
 *
 * @param conf Configuration context to be searched
 *
 * @param key Key identifying the entry
 *
 * @return If a corresponding entry is found and is of type
 * PLUGINCONFIG_VALUE_STR, a pointer to the value's character array is
 * returned; otherwise NULL is returned
 */
char * pluginConfig_getStr(pluginConfig_t conf, const char *key);

/**
 * @brief Get value as string list
 *
 * Get the value of the entry identified by the key @a key from the
 * configuration context @a conf. The value is returned as a pointer
 * to an array of pointers to character arrays if it is of type
 * PLUGINCONFIG_VALUE_LST.
 *
 * @param conf Configuration context to be searched
 *
 * @param key Key identifying the entry
 *
 * @return If a corresponding entry is found and is of type
 * PLUGINCONFIG_VALUE_LST, a pointer to the value's list is returned;
 * otherwise NULL is returned
 */
char ** pluginConfig_getLst(pluginConfig_t conf, const char *key);

/**
 * @brief Get length of value's string list
 *
 * Get the length of the string list representing the value of the
 * entry identified by the key @a key from the configuration context
 * @a conf. The length is returned if the value is of type
 * PLUGINCONFIG_VALUE_LST.
 *
 * @param conf Configuration context to be searched
 *
 * @param key Key identifying the entry
 *
 * @return If a corresponding entry is found and is of type
 * PLUGINCONFIG_VALUE_LST, the length of the list is returned;
 * otherwise 0 is returned
 */
size_t pluginConfig_getLstLen(pluginConfig_t conf, const char *key);

/**
 * @brief Verify a key-value pair
 *
 * Verify the correctness of the key-value pair @a key and @a value
 * being part of a configuration context @a conf according to the
 * definition within. The definition has to be registered via @ref
 * pluginConfig_setDef() before.
 *
 * If @a key is not found within this definition or no definition
 * exists at all, 1 is returned. If @a val corresponds to the expected
 * type as given by the definition, 0 is returned. Or 2 if @a value
 * turned out to be not matching.
 *
 * @param conf Configuration context to be used
 *
 * @param key Key to be checked
 *
 * @param value Value to be checked
 *
 * @return 0, 1 or 2 according to discussion above
 */
int pluginConfig_verifyEntry(pluginConfig_t conf,
			     const char *key, pluginConfigVal_t *val);

/**
 * @brief Verify a configuration
 *
 * Verify the correctness of the whole configuration being part of a
 * configuration context @a conf according to the definition
 * within. The definition has to be registered via @ref
 * pluginConfig_setDef() before.
 *
 * For this @ref pluginConfig_verifyEntry() is called for each entry
 * found within @a conf.
 *
 * @param conf Configuration context to be verified
 *
 * @return If all key-value entries of @a conf conform to the
 * definition contained, 0 is returned; or 1 or 2 depending on the
 * results of @ref pluginConfig_verifyEntry() for the first
 * non-conforming pair
 */
int pluginConfig_verify(pluginConfig_t conf);

/**
 * @brief Get definition for key
 *
 * Search for the definition of the key @a key within the definition
 * of the configuration context @a conf.
 *
 * @param conf Configuration context to be searched
 *
 * @param key Name to be searched for
 *
 * @return If a definition for @a key is found, a pointer to the
 * corresponding definition is returned; or NULL otherwise
 */
const pluginConfigDef_t *pluginConfig_getDef(pluginConfig_t conf,
					     const char *key);

/**
 * @brief Unset configuration entry
 *
 * Unset the configuration entry identified by @a key within the
 * configuration context @a conf. For this the value's type will be
 * set to PLUGINCONFIG_VALUE_NONE and all associated dynamic memory
 * will be free()ed.
 *
 * @param conf Configuration context to be modified
 *
 * @param key Key identifying the entry to be unset
 *
 * @return If a corresponding entry is found and unset, true is
 * returned; or false otherwise
 */
bool pluginConfig_unset(pluginConfig_t conf, const char *key);

/**
 * @brief Remove configuration entry
 *
 * Remove the configuration entry identified by @a key from the
 * configuration context @a conf.
 *
 * @param conf Configuration context to be modified
 *
 * @param key Key identifying the entry to be removed
 *
 * @return If a corresponding entry is found and removed, true is
 * returned; or false otherwise
 */
bool pluginConfig_remove(pluginConfig_t conf, const char *key);

/**
 * @brief Provide string describing the value type
 *
 * Provide a constant string describing the value type @a type.
 *
 * @param type Value type that shall be described
 *
 * @return Return a pointer to the constant string describing the type
 */
const char *pluginConfig_typeStr(pluginConfigValType_t type);

/**
 * @brief Get length of longest key-name
 *
 * Get the length of the longest key-name within the definition and
 * key-value pairs of the configuration context @a conf.
 *
 * @param conf Configuration context to be analyzed
 *
 * @return The length of the longest key-name; or 0 if @a conf neither
 * contains a definition nor a key-value pair
 */
size_t pluginConfig_maxKeyLen(pluginConfig_t conf);

/**
 * @brief Provide key-value pair description
 *
 * Provide a description of the key-value pair indexed by @a key
 * within the configuration context @a conf and append it to the
 * string-buffer @a buf.
 *
 * @param conf Configuration context to be used
 *
 * @param key Key indexing the requested key-value pair
 *
 * @param buf String buffer the description is appended to
 *
 * @return If the key-value pair was found and the description was
 * successfully appended to @a buf, true is returned; or false in case
 * of failure
 */
bool pluginConfig_showKeyVal(pluginConfig_t conf, const char *key, strbuf_t buf);

/**
 * @brief Provide description for plugin's help function
 *
 * Provide a description of the expected parameters within the
 * configuration context @a conf and append it to the string-buffer @a
 * buf. The description will be based on the configuration definition
 * that has to be added via @ref pluginConfig_setDef() before.
 *
 * @param conf Configuration context to be described
 *
 * @param buf String buffer the description is appended to
 *
 * @return No return value
 */
void pluginConfig_helpDesc(pluginConfig_t conf, strbuf_t buf);

#endif  /* __PLUGIN_LIB_PSCONFIG */
