/*
 * ParaStation
 *
 * Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PLUGIN_LIB_PSCONFIG
#define __PLUGIN_LIB_PSCONFIG

#include <stdbool.h>
#include <stddef.h>

/** Opaque structure holing the configuration */
struct pluginConfig;

/**
 * @brief Configuration context
 *
 * To be initialzied with @ref pluginConfig_new() and filled with @ref
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

/**
 * @brief Load configuration from psconfig
 *
 * Load the configuration from the branch of psconfig identified by
 * @a configKey and store it to @a conf. All configuration is fetched
 * from Psid.PluginCfg.@a configKey of the local host object.
 *
 * If a definition of the configuration is available, i.e. was
 * registered via @ref pluginConfig_setDef(), the configuration will
 * be verified immediately via @ref pluginConfig_verify().
 *
 * @param conf Configuration ready for further use
 *
 * @param configKey Name of the psconfig branch to be used
 *
 * @param setDefaults @doctodo
 *
 * @return Upon success the number of matching configuration entries
 * found in psconfig is returned. Or -1 if an error occurred. @todo make it bool
 */
bool pluginConfig_load(pluginConfig_t conf, char *configKey, bool setDefaults);

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
    const char *deflt[];              /**< default value; NULL for no
				       default; NULL-terminated  @todo */
} pluginConfigDef_t;

/**
 * @brief Set configuraton definition
 *
 * @doctodo
 *
 * @doctodo def must be NULL terminated!!
 *
 * @remark def must be static, i.e. conf will reffer to the argument
 * passed without creating a copy.
 */
bool pluginConfig_setDef(pluginConfig_t conf, pluginConfigDef_t def[]);

/**
 * @brief Visitor function
 *
 * Visitor function used by @ref traverseConfig() in order to visit
 * each object in a given configuration.
 *
 * The parameters are as follows: @a key and @a value are the
 * corresponding parts of the key-value pair forming the configuration
 * object. @a info points to the additional information passed to @ref
 * traverseConfig() in order to be forwarded to each object.
 *
 * If the visitor function returns true the traversal will be
 * interrupted and @ref traverseConfig() will return to its calling
 * function.
 */
typedef bool pluginConfigVisitor_t(char *key, pluginConfigVal_t *val,
				   const void *info);

/**
 * @brief Traverse configuration
 *
 * Traverse the configuration @a conf by calling @a visitor for each
 * of the embodied objects. In addition to the object's key and value
 * @a info is passed as additional information.
 *
 * If @a visitor returns true, the traversal will be stopped
 * immediately and true is returned to the calling function.
 *
 * @param conf Configuration to be traversed
 *
 * @param visitor Visitor function to be called for each object
 *
 * @param info Additional information to be passed to @a visitor while
 * visiting the objects within @a conf
 *
 * @return If the visitor returns true, traversal will be stopped and
 * true is returned. If no visitor returned true during the traversal
 * false is returned.
 */
bool pluginConfig_traverse(pluginConfig_t conf, pluginConfigVisitor_t visitor,
			   const void *info);

/**
 * @brief Add entry to configuration
 *
 * Add the key-value pair given by @a key and @a value to the existing
 * configuration @a conf. If an entry with key @a key is already
 * existing in the configuration, the corresponding value is
 * replaced. Otherwise a new key-value pair is added to the
 * configuration.
 *
 * @param conf Configuration to be modified
 *
 * @param key Key-part of the key-value pair to be added
 *
 * @param value Value-part of the key-value pair to be added
 *
 * @return @docotodo
 */
bool pluginConfig_add(pluginConfig_t conf, char *key, pluginConfigVal_t *value);

/**
 * @brief Get value
 *
 * @doctodo
 * Get the value of the entry identified by the key @a key from the
 * configuration @a conf. The value is returned as the original
 * character array.
 *
 * @param conf Configuration to be searched
 *
 * @param key Key identifying the entry
 *
 * @return If a corresponding entry is found, a pointer to the value's
 * character array is returned. Otherwise NULL is returned.
 */
pluginConfigVal_t * pluginConfig_get(pluginConfig_t conf, const char *key);

/**
 * @brief Get value as number
 *
 * @doctodo
 * Get the value of the entry identified by the key @a key from the
 * configuration @a conf. The value is returned as an unsigned
 * integer. If no entry was found or conversion into an unsigned
 * integer failed -1 is returned.
 *
 * @param conf Configuration to be searched
 *
 * @param key Key identifying the entry
 *
 * @return If a corresponding entry is found and its value can be
 * converted to an unsigned integer, this value is returned. Otherwise
 * -1 is returned.
 */
long pluginConfig_getNum(pluginConfig_t conf, char *key);

/**
 * @brief Get value as character array
 *
 * @doctodo
 * Get the value of the entry identified by the key @a key from the
 * configuration @a conf. The value is returned as the original
 * character array.
 *
 * @param conf Configuration to be searched
 *
 * @param key Key identifying the entry
 *
 * @return If a corresponding entry is found, a pointer to the value's
 * character array is returned. Otherwise NULL is returned.
 */
char * pluginConfig_getStr(pluginConfig_t conf, char *key);

/**
 * @brief Get value as string list
 *
 * @doctodo
 */
char ** pluginConfig_getLst(pluginConfig_t conf, char *key);

/**
 * @brief Verify a configuration entry
 *
 * Verify the correctness of the key-value pair @a key and @a value
 * being part of a configuration according to the definition within @a
 * confDef.
 *
 * If @a key is not found within @a confDef, 1 is returned. If @a key
 * is marked to expect a numerical value, this is checked, too. Upon
 * success 0 is returned. Or 2 if @a value turned out to be no number.
 *
 * @doctodo check againt type
 *
 *
 * @remark according to definition registered via @ref pluginConfig_setDef()
 *
 *
 * @param confDef Definition of a valid configuration to be ensured
 *
 * @param key Key to be checked
 *
 * @param value Value to be checkd
 *
 * @return 0, 1 or 2 according to discussion above.
 */
int pluginConfig_verifyEntry(pluginConfig_t conf,
			     char *key, pluginConfigVal_t *val);

/**
 * @brief Verify a configuration
 *
 * Verify the correctness of the configuration @a conf according to
 * the definition within @a confDef.
 *
 * For this @ref verifyPSConfigEntry() is called for each key-value pair
 * found within @a conf.
 *
 *
 * @remark according to definition registered via @ref pluginConfig_setDef()
 *
 *
 * @param conf Configuration to be verified
 *
 * @param confDef Definition of a valid configuration to be ensured
 *
 * @return If @a conf conforms to @a confDef, 0 is returned. Or 1 or 2
 * depending on the results of @ref verifyConfigEntry() for the first
 * non-conforming pair.
 */
int pluginConfig_verify(pluginConfig_t conf);

/**
 * @brief Get definition for a name
 *
 * Search for the definition of the key @a name within the configuration
 * definition given in @a confDef.
 *
 * @param name Name to be searched for
 *
 * @param confDef Definition of a valid configuration to be scanned
 *
 * @return If a definition for @a name is found a corresponding
 * pointer is retured. Or NULL otherwise.
 */
const pluginConfigDef_t *pluginConfig_getDef(pluginConfig_t conf, char *key);

/**
 * @brief Reset configuration entry
 *
 * Reset the configuration entry within the configuration @a conf
 * marked by @a key with its default value given within the
 * configuration definition @a confDef. If no default value is given
 * in @a confDef the corresponding entry is removed from @a conf.
 *
 * @param conf Configuration to be modified
 *
 * @param confDef Definition of the configuration
 *
 * @param key Key identifying the entry to be modified.
 *
 * @return If a corresponding entry is found and modified true is
 * returned. Or false otherwise.
 */
bool pluginConfig_unset(pluginConfig_t conf, char *key);

/**
 * @brief Extend configuration by defaults
 *
 * Extend the configuration @a conf by entries given by the defaults
 * within the definition @a confDef. No key-value pair already
 * existing within @a conf is replaced. For each key described within
 * @a confDef together with a default and not yet found within @a conf
 * will be added to the configration.
 *
 * @remark according to definition registered via @ref pluginConfig_setDef()
 *
 * @param conf Configuration to be extended
 *
 * @param confDef Definition of the configuration holding default values
 *
 * @return No return value
 */
void pluginConfig_setDefaults(pluginConfig_t conf);

/**
 * @brief Get length of longest key-name
 *
 * Get the length of the longest key-name withing the definition @a confDef.
 *
 * @param confDef Configuration definition to be analyzed
 *
 * @return The length of the longest key-name
 */
size_t pluginConfig_maxKeyLen(pluginConfig_t conf);

#endif  /* __PLUGIN_LIB_PSCONFIG */
