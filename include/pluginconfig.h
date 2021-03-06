/*
 * ParaStation
 *
 * Copyright (C) 2014-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PLUGIN_LIB_CONFIG
#define __PLUGIN_LIB_CONFIG

#include <stdbool.h>
#include <stdint.h>

#include "list.h"

/** Configuration parsed from a file */
typedef list_t Config_t;

/**
 * @brief Init configuration
 *
 * Initialize the configuration @a conf. @a conf is assumed to be
 * empty. If @a conf already holds valid configuration, some memory
 * might get leaked. Therefore, previous configurations shall be
 * destructed via @ref freeConfig().
 *
 * @param conf Configuration ready for further use
 *
 * @return No return value
 */
void initConfig(Config_t *conf);

/**
 * @brief Fetch configuration from a file
 *
 * Parse the configuration from the file named by @a filename and
 * store it into @a conf. @a conf is assumed to be empty. If @a conf
 * already holds valid configuration, some memory might get
 * leaked. Therefore, previous configurations shall be destructed via
 * @ref freeConfig(). If the flag @a trimQuotes is true, each
 * configuration value gets unquoted before being put into the
 * configuration.
 *
 * As a side-effect hash accumulation of the configuration might be
 * conducted if a hash accumulator was registered before via @ref
 * registerConfigHashAccumulator().
 *
 * @param filename Name of the configuration file to be handled
 *
 * @param conf Configuration ready for further use
 *
 * @param trimQuotes Flag to unquote each configuration value before
 * putting it into @a conf
 *
 * @return Upon success the number of configuration entries found in
 * the file is returned. Or -1 if an error occurred.
 */
int parseConfigFile(char *filename, Config_t *conf, bool trimQuotes);

/**
 * @brief Register a hash accumulator
 *
 * Register a hash accumulator @a hashAcc ready to collect the hash of
 * a configuration file during the call of @ref parseConfigFile().
 *
 * Since subsequent calls of @ref parseConfigFile() would do further
 * changes to the hash accumulator it shall be unregistered by
 * registering a new hash accumulator or by disabling hash
 * accumulation via calling this function with a NULL argument.
 *
 * @param hashAcc Ready prepared hash accumulator to register or NULL
 * to disable hash accumulation.
 *
 * @return No return value
 */
void registerConfigHashAccumulator(uint32_t *hashAcc);

/**
 * @brief Free complete configuration
 *
 * Free the complete configuration @a conf, i.e. release all dynamic
 * memory associated to the configuration. For this, the whole
 * configuration is traversed and for each key, value and
 * helper-structure free() is called.
 *
 * @param conf The configuration to be released
 *
 * @return No return value
 */
void freeConfig(Config_t *conf);

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
typedef bool configVisitor_t(char *key, char *value, const void *info);

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
bool traverseConfig(Config_t *conf, configVisitor_t visitor, const void *info);

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
 * @return No return value
 */
void addConfigEntry(Config_t *conf, char *key, char *value);

/**
 * @brief Get value as character array
 *
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
char *getConfValueC(Config_t *conf, char *key);

/**
 * @brief Get value as unsigned integer
 *
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
unsigned int getConfValueU(Config_t *conf, char *key);

/**
 * @brief Get value as integer
 *
 * Get the value of the entry identified by the key @a key from the
 * configuration @a conf. The value is returned as an integer. If no
 * entry was found or conversion into an integer failed -1 is
 * returned.
 *
 * @param conf Configuration to be searched
 *
 * @param key Key identifying the entry
 *
 * @return If a corresponding entry is found and its value can be
 * converted to an integer, this value is returned. Otherwise -1 is
 * returned.
 */
int getConfValueI(Config_t *conf, char *key);

/**
 * @brief Get value as long
 *
 * Get the value of the entry identified by the key @a key from the
 * configuration @a conf. The value is returned as a long integer. If
 * no entry was found or conversion into a long failed -1 is returned.
 *
 * @param conf Configuration to be searched
 *
 * @param key Key identifying the entry
 *
 * @return If a corresponding entry is found and its value can be
 * converted to a long integer, this value is returned. Otherwise -1
 * is returned.
 */
long getConfValueL(Config_t *conf, char *key);

/** Definition of a single configuration parameter */
typedef struct {
    char *name;	    /**< name of the config key */
    bool isNum;	    /**< flag if value is numeric */
    char *type;	    /**< type of the config value e.g. <string> */
    char *def;	    /**< default value; NULL if there is no default */
    char *desc;	    /**< short help description */
} ConfDef_t;

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
 * @param confDef Definition of a valid configuration to be ensured
 *
 * @param key Key to be checked
 *
 * @param value Value to be checkd
 *
 * @return 0, 1 or 2 according to discussion above.
 */
int verifyConfigEntry(const ConfDef_t confDef[], char *key, char *value);

/**
 * @brief Verify a configuration
 *
 * Verify the correctness of the configuration @a conf according to
 * the definition within @a confDef.
 *
 * For this @ref verifyConfigEntry() is called for each key-value pair
 * found within @a conf.
 *
 * @param conf Configuration to be verified
 *
 * @param confDef Definition of a valid configuration to be ensured
 *
 * @return If @a conf conforms to @a confDef, 0 is returned. Or 1 or 2
 * depending on the results of @ref verifyConfigEntry() for the first
 * non-conforming pair.
 */
int verifyConfig(Config_t *conf, const ConfDef_t confDef[]);

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
const ConfDef_t *getConfigDef(char *name, const ConfDef_t confDef[]);

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
bool unsetConfigEntry(Config_t *conf, const ConfDef_t confDef[], char *key);

/**
 * @brief Extend configuration by defaults
 *
 * Extend the configuration @a conf by entries given by the defaults
 * within the definition @a confDef. No key-value pair already
 * existing within @a conf is replaced. For each key described within
 * @a confDef together with a default and not yet found within @a conf
 * will be added to the configration.
 *
 * @param conf Configuration to be extended
 *
 * @param confDef Definition of the configuration holding default values
 *
 * @return No return value
 */
void setConfigDefaults(Config_t *conf, const ConfDef_t confDef[]);

/**
 * @brief Get length of longest key-name
 *
 * Get the length of the longest key-name withing the definition @a confDef.
 *
 * @param confDef Configuration definition to be analyzed
 *
 * @return The length of the longest key-name
 */
size_t getMaxKeyLen(const ConfDef_t confDef[]);

#endif  /* __PLUGIN_LIB_CONFIG */
