/*
 * ParaStation
 *
 * Copyright (C) 2014-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PLUGIN_LIB_CONFIG
#define __PLUGIN_LIB_CONFIG

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "list.h"

/** Configuration parsed from a file */
typedef struct pluginConfig * Config_t;

/**
 * @brief Init configuration
 *
 * Initialize the configuration @a conf. @a conf is assumed to be
 * empty. If @a conf already holds valid configuration, this will be
 * destroyed with all memory used getting free()ed.
 *
 * @param conf Configuration ready for further use
 *
 * @return true on success, false on error
 */
bool initConfig(Config_t *conf);

/**
 * @ brief Set configuration's quote trimming of values
 *
 * Adjust quote trimming of all values within the configuration @a
 * conf. If @a trimQuotes is true, all quotes are trimmed from a value
 * before it is stored to the configuration. The default is to not
 * trim quotes from values.
 *
 * In order to make quote trimming consistent, this must be adjusted
 * before any entries are added or the actual configration is fetched
 * from a file. This function will check for an empty configuration
 * and fail in case of existing entries.
 *
 * @param trimQuotes Flag the configuration's quote trimming strategy
 *
 * @return true on success, false on error
 */
bool setConfigTrimQuotes(Config_t conf, bool trimQuotes);

/**
 * @brief Get configuration's quote trimming
 *
 * Determine if quotes are trimmed from values within the
 * configuration @a conf.
 *
 * @return Returns true if quotes are trimmed from values in @a conf;
 * otherwise false is returned
 */
bool getConfigTrimQuotes(Config_t conf);

/**
 * @brief Set configuration's case sensitivity
 *
 * Adjust case sensitivity of all key matching within the
 * configuration @a conf. If @a sensitivity is true, all key matching
 * in the context of @a conf is case sensitive. This is the
 * default. Otherwise key matching will be case insensitive.
 *
 * In order to avoid double or ghost entries the configuration's case
 * sensitivity must be adjusted before any entries are added or the
 * actual configration is fetched from a file. This function will
 * check for an empty configuration and fail in case of existing
 * entries.
 *
 * @param sensitivity Flag the configuration's case sensitivity
 *
 * @return true on success, false on error
 */
bool setConfigCaseSensitivity(Config_t conf, bool sensitivity);

/**
 * @brief Get configuration's case sensitivity
 *
 * Determine if the key handling within the configuration @a conf is
 * case sensitive.
 *
 * @return Returns true if key handling in @a conf is case sensitive;
 * otherwise false is returned
 */
bool getConfigCaseSensitivity(Config_t conf);

/**
 * @brief Set configuration's double entry avoidance
 *
 * Adjust double entry avoidance of the configuration @a conf. If @a
 * flag is true, no double entries will be stored in @a conf. In this
 * case if an entry is added twice, the second addition will replace
 * the first one. This is the default. Otherwise multiple entries
 * belonging to a single key might be added.
 *
 * Double or ghost entries in the configuration are only accessible
 * through @ref traverseConfig() and might not be found by @ref
 * getConfValue*(). Furthermore, such entries might have to be removed
 * multiple times via @ref unsetConfigEntry() before all ghost entries
 * are gone. In order to avoid unexpected behavior this function will
 * check for an empty configuration and fail in case of existing
 * entries.
 *
 * @param flag Flag the avoidance of double entries
 *
 * @return true on success, false on error
 */
bool setConfigAvoidDoubleEntry(Config_t conf, bool flag);

/**
 * @brief Get configuration's double entry avoidance
 *
 * Determine if the key handling within the configuration @a conf
 * avoids double entries only accessible through @ref traverseConfig().
 *
 * @return Returns true if key handling in @a conf avoids double
 * entries; otherwise false is returned
 */
bool getConfigAvoidDoubleEntry(Config_t conf);

/**
 * @brief Line-handler function
 *
 * Handler function used by @ref parseConfigFile() in order to handle
 * a line immediately. This might be used to include
 * sub-configurations straight away which might be required to
 * guarantee compatibility in the hash computation.
 *
 * The parameters are as follows: @a line is the line that was just
 * read after trailing comments were removed. @a conf is the
 * configuration to read that might be extended by the line-handler
 * function itself or subsequent functionality. This is especially
 * used for including sub-configurations when the original
 * configuration to read shall be extended. @a info points to the
 * additional information that might be passed to @ref
 * parseConfigFile() et al in order to extend the configuration @a
 * conf.
 *
 * If the handler function returns true, further handling of the line
 * will be skipped and the next line is read. This means a key-value
 * pair resulting from splitting the line at '=' is not added to the
 * configuration. Otherwise line handling is continued in the same way
 * as if no line-handler is defined.
 */
typedef bool configLineHandler_t(char *line, Config_t conf, const void *info);

/**
 * @brief Fetch configuration from a file
 *
 * Parse the configuration from the file named by @a filename and
 * store it into @a conf. @a conf is assumed to be initialized via
 * initConfig(). If @a conf already holds a valid configuration, this
 * will be destroyed with all used memory free()ed before fetching the
 * new configuration unless the flag @a keepObjects is true.
 *
 * If @a handleImmediate is given, each line will be passed to this
 * function before adding its content to the configuration. This
 * handler might veto adding the line's content to the configuration
 * by returning true. Additional information might be passed to the
 * handler function via the @a info pointer.
 *
 * As a side-effect hash accumulation of the configuration might be
 * conducted if a hash accumulator was registered before via @ref
 * registerConfigHashAccumulator().
 *
 * @param filename Name of the configuration file to be handled
 *
 * @param conf Configuration ready for further use
 *
 * @param keepObjects Flag to keep existing objects in @a conf
 *
 * @param handleImmediate Line-handler function to handle and filter
 * each line before adding its content to the configuration
 *
 * @param info Pointer to additional information to be passed to @a
 * handleImmediate()
 *
 * @return Upon success the number of configuration entries found in
 * the file is returned. Or -1 if an error occurred.
 */
int parseConfigFileExt(char *filename, Config_t conf, bool keepObjects,
		       configLineHandler_t handleImmediate, const void *info);

#define parseConfigFile(filename, conf)				\
    parseConfigFileExt(filename, conf, false, NULL, NULL)

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
 * helper-structure free() is called. Furthermore the configuration
 * context itself will be invalidated and free()ed.
 *
 * @param conf The configuration to be released
 *
 * @return No return value
 */
void freeConfig(Config_t conf);

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
bool traverseConfig(Config_t conf, configVisitor_t visitor, const void *info);

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
void addConfigEntry(Config_t conf, char *key, char *value);

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
char *getConfValueC(Config_t conf, char *key);

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
unsigned int getConfValueU(Config_t conf, char *key);

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
int getConfValueI(Config_t conf, char *key);

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
long getConfValueL(Config_t conf, char *key);

/**
 * @brief Get value as float
 *
 * Get the value of the entry identified by the key @a key from the
 * configuration @a conf. The value is returned as a floating point variable. If
 * no entry was found or conversion into a float failed -1 is returned.
 *
 * @param conf Configuration to be searched
 *
 * @param key Key identifying the entry
 *
 * @return If a corresponding entry is found and its value can be
 * converted to a float, this value is returned. Otherwise -1
 * is returned.
 */
float getConfValueF(Config_t conf, char *key);

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
 * non-conforming pair. -1 will be returned if @a conf is not
 * initialized.
 */
int verifyConfig(Config_t conf, const ConfDef_t confDef[]);

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
bool unsetConfigEntry(Config_t conf, const ConfDef_t confDef[], char *key);

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
void setConfigDefaults(Config_t conf, const ConfDef_t confDef[]);

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
