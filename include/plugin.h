/*
 * ParaStation
 *
 * Copyright (C) 2009-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file ParaStation plugin template. Each plugin should implement the
 * symbols defined as 'extern' below. The symbols @ref name and @ref
 * version are required. Plugins not defining these symbols will be
 * unloaded immediately. All other symbols are optional.
 *
 * In addition each plugin can provide one function with
 * __attribute__((constructor)) registering the plugin and another
 * function with __attribute__((destructor)) unregistering it. These
 * functions are not required. For several reasons discussed in the
 * Wiki in detail it makes more sense to use the functionality
 * provided by the functions @ref initialize(), @ref finalize() and
 * @ref cleanup().
 */
#ifndef _PLUGIN_H_
#define _PLUGIN_H_

#include <stdio.h>

/** Name of the plugin */
extern char name[];

/** Current version of the plugin */
extern int version;

/**
 * Minimum version of the ParaStation daemon's plugin API required. If
 * this symbol is not defined, any version is accepted.
 */
extern int requiredAPI;

/** This describes plugin dependencies */
typedef struct {
    char *name;   /**< Name of the plugin we depend on */
    int version;  /**< Minimum version of the plugin we depend on */
} plugin_dep_t;

/**
 * List of dependent plugins the be loaded by the loader-mechanism
 *
 * For each dependant plugin a minimum version-number might be
 * defined. If this number is set to 0, any version of this plugin is
 * accepted.
 *
 * This array is terminated by an entry with name set to NULL and
 * version set to 0.
 */
extern plugin_dep_t dependencies[];

/**
 * @brief Initialize plugin
 *
 * This function will be called after all plugins defined within @ref
 * dependencies are successfully loaded and initialized. Thus, all
 * functions provided by the plugins marked to be dependents are
 * accessible when this function is called.
 *
 * The @a logfile parameter defines the logging destiation. Plugins
 * are expected to use this file for logging or to use syslog when
 * NULL. Thus, iIt is save to pass this argument directly to @ref
 * logger_init() or @ref initPluginLogger().
 *
 * If initialization of the plugin fails, this shall be signaled to
 * the ParaStation daemon by a return value different from 0. In this
 * case resolving dependencies and initialization of triggering
 * plugins is stopped immediately.
 *
 * @attention Uninitialized plugins will not be finalized. Thus, if
 * an plugin was not (or not successfully) initialized, its
 * cleanup-method might be called without calling the finalize-method
 * beforehand.
 *
 * @param logfile Logging destination to be used by the plugin
 *
 * @return If initialization was successful, 0 shall be
 * returned. Thus, a value different from 0 flags some problem during
 * initialization.
 */
int initialize(FILE *logfile);

/**
 * @brief Stop plugin
 *
 * This function will be called in order to signal the plugin to
 * prepare getting unloaded soon. The plugin is expected to call @ref
 * PSIDplugin_unload() as soon as all cleanup and unregistration
 * actions are finished.
 *
 * Even if the plugin does not call @ref PSIDplugin_unload() it should
 * expect to get unloaded soon. Before actually unloading the plugin
 * @ref cleanup() will be called, if the plugin exposes such symbol.
 *
 * If initialization of the plugin failed (as signaled by a
 * return-value different from 0), this function will not be called.
 *
 * If a plugin does not expose this symbol, calling @ref
 * PSIDplugin_finalize() referring this plugin behaves exactly like
 * calling @ref PSIDplugin_unload(). Thus, the plugin will be marked
 * to be unloaded immediately if it is no longer required by other
 * plugins depending on it.
 *
 * @return No return value.
 */
void finalize(void);

/**
 * @brief Cleanup plugin
 *
 * This function will be called immediately before actually unloading
 * the plugin using dlclose(). Thus, the plugin shall release all
 * resources currently allocated including memory, selectors, timers,
 * etc. immediately.
 *
 * This function will be called for each plugin independently from the
 * result of the initialization of the plugin.
 *
 * @return No return value.
 */
void cleanup(void);

/**
 * @brief Get plugin's help-text
 *
 * This function is called in order to receive the plugin's
 * help-text. The text will be sent to the requesting psiadmin in
 * order to get displayed to the user.
 *
 * The plugin is expected to create the text in dynamic memory
 * allocated by malloc() or strdup() as a null-terminated
 * character-string. After the text was sent to the requester in one
 * or more messages the memory is given back by calling free().
 *
 * @return Pointer to dynamic memory holding the help-text or NULL, if
 * no such text exists.
 */
char * help(void);

/**
 * @brief Set plugin's key-value pair
 *
 * This function is called in order to set the plugin's key-value pair
 * indexed by @a key to @a val.
 *
 * As a result, the plugin might create some answer-text to be sent to
 * the requester. This text is used to signal success or failure of
 * the modification of the plugin's key-value space. If no such
 * message shall be sent, NULL is returned in order signal success.
 *
 * The text to be returned by the plugin is created in dynamic memory
 * allocated by malloc() or strdup() as a null-terminated
 * character-string. After this text was sent to the requester in one
 * or more messages the memory is given back by calling free().
 *
 * @return Pointer to dynamic memory holding a message-text or NULL, if
 * no such text exists.
 */
char * set(char *key, char *val);

/**
 * @brief Unset plugin's key-value pair
 *
 * This function is called in order to unset the plugin's key-value
 * pair indexed by @a key.
 *
 * As a result, the plugin might create some answer-text to be sent to
 * the requester. This text is used to signal success or failure of
 * the modification of the plugin's key-value space. If no such
 * message shall be sent, NULL is returned in order signal success.
 *
 * The text to be returned by the plugin is created in dynamic memory
 * allocated by malloc() or strdup() as a null-terminated
 * character-string. After this text was sent to the requester in one
 * or more message the memory is given back by calling free().
 *
 * @return Pointer to dynamic memory holding a message-text or NULL, if
 * no such text exists.
 */
char * unset(char *key);

/**
 * @brief Show plugin's key-value pair
 *
 * This function is called in order to show the value of the plugin's
 * key-value pair indexed by @a key.
 *
 * @a key might be NULL in order to request all known key-value pairs.
 *
 * As a result, the plugin shall create some answer-text holding the
 * value to be sent to the requester. This text is used to signal
 * success or failure of the search within the plugin's key-value
 * space. If no such message shall be sent, NULL is returned.
 *
 * The text to be returned by the plugin is created in dynamic memory
 * allocated by malloc() or strdup() as a null-terminated
 * character-string. After this text was sent to the requester in one
 * or more message the memory is given back by calling free().
 *
 * @return Pointer to dynamic memory holding a message-text or NULL, if
 * no such text exists.
 */
char * show(char *key);

#endif /* _PLUGIN_H_ */
