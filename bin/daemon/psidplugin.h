/*
 * ParaStation
 *
 * Copyright (C) 2009-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * Helper functions for plugin handling.
 */
#ifndef __PSIDPLUGIN_H
#define __PSIDPLUGIN_H

#include <stdio.h>

#include "pstask.h"

/** Handle to uniquely identifying a loaded plugin */
typedef struct PSIDplugin * PSIDplugin_t;

/**
 * @brief Initialize plugin stuff
 *
 * Initialize the plugin handling framework. This also registers the
 * necessary message handlers and loads the plugins as defined in the
 * configuration. Once the plugins are loaded the structures
 * describing the plugins to load are cleared.
 *
 * The @a logfile parameter will be passed to all plugins in order to
 * define the logging destiation. Plugins are expected to use this
 * file for logging or to use syslog when NULL.
 *
 * @param logfile Logging destination to be used by plugins
 *
 * @return No return value.
 */
void initPlugins(FILE *logfile);

/**
 * @brief Get unload-timeout for plugins
 *
 * Get the timeout before forcefully unloading a plugin in
 * seconds.
 *
 * Forcefully unloading a plugin has to be triggered
 * explicitly. Currently this requires to send a corresponding
 * message (PSP_PLUGIN_FORCEREMOVE) to the daemon.
 *
 * @return The timeout in seconds
 */
int PSIDplugin_getUnloadTmout(void);

/**
 * @brief Set unload-timeout for plugins
 *
 * Set the timeout before forcefully unloading a plugin to @a tmout
 * seconds.
 *
 * Forcefully unloading a plugin has to be triggered
 * explicitly. Currently this requires to send a corresponding
 * message (PSP_PLUGIN_FORCEREMOVE) to the daemon.
 *
 * @param tmout The timeout to set in seconds
 *
 * @return No return value
 */
void PSIDplugin_setUnloadTmout(int tmout);

/**
 * @brief Get number of plugin
 *
 * Get the number of plugins currently loaded. This does not include
 * plugins that have already called PSIDplugin_unload(), i.e. plugins
 * that have stopped all operations.
 *
 * @return The number of currently loaded plugins is returned.
 */
int PSIDplugin_getNum(void);

/**
 * @brief Send list of plugins.
 *
 * Send a list of information on the plugins currently loaded in the
 * local daemon. All information about a single plugin (i.e. name,
 * version, and triggering plugins) is given back in a character
 * string.
 *
 * @param dest Task ID of process waiting for answer.
 *
 * @return No return value.
 */
void PSIDplugin_sendList(PStask_ID_t dest);

/**
 * @brief Get plugin's handle
 *
 * Get the identifying handle of the plugin loaded via @a pName. It
 * might be used as a trigger for further plugins to be loaded.
 *
 * @param pName Uniquely identifying name used to load the plugin
 *
 * @return If the plugin is found, the identifying handle is returned;
 * otherwise NULL is returned
 */
PSIDplugin_t PSIDplugin_find(char *pName);

/**
 * @brief Get handle on plugin
 *
 * Get a handle on the plugin loaded via @a name. The handle was
 * returned by dlopen() while the plugin was loaded. It might be used
 * in order to resolve additional symbols exposed by the plugin.
 *
 * @param name The name used to load the plugin. Each plugin can be
 * uniquely identified by its name.
 *
 * @return If the plugin is found, the handle as returned by dlopen()
 * while loading the plugin is returned. Otherwise NULL is returned.
 */
void *PSIDplugin_getHandle(char *name);

/**
 * @brief Finalize a plugin
 *
 * Trigger plugin @a name to get finalized. This is the standard way
 * to safely unload a plugin in a graceful way. The plugin will not be
 * affected, if the plugin is still triggered by another plugin
 * depending on it. Basically, this function just removes the
 * self-trigger of the plugin, i.e. a trigger of the plugin pointing
 * to itself, if the plugin was loaded explicitly. If this was the
 * plugin's last trigger, further measures will be taken in order to
 * actually unload the plugin @a name.
 *
 * If the plugin exposes the function-symbol @a finalize, this method
 * will be called. It is expected that the @a finalize method will do
 * all necessary cleanup that has to be done in an asynchronous way
 * (detaching from a service, etc.) before the plugin itself triggers
 * the actual unload by calling @ref PSIDplugin_unload(). This gives a
 * plugin the chance to cleanup properly before it is evicted from the
 * address-space via dlclose().
 *
 * If no @a finalize method is exposed by the plugin @a name, calling
 * this function behaves exactly like calling @ref
 * PSIDplugin_unload(). Thus, the plugin will be marked to be unloaded
 * immediately if it is no longer required by other plugins depending
 * on it.
 *
 * @param name The name of the plugin to be finalized.
 *
 * @return If the plugin is not found, -1 is returned. Otherwise the
 * return-value flags, if the finalize method would have been called,
 * i.e if there are no other plugins still depending on the plugin to
 * finalize. If there are still pending triggers, 0 is
 * returned. Otherwise 1 is returned.
 */
int PSIDplugin_finalize(char *name);

/**
 * @brief Unload a plugin
 *
 * Trigger the plugin @a name to get actually unloaded.
 *
 * Usually this function is called by the plugin itself from its @a
 * finalize() methods once all the cleanup necessary to prepare the
 * plugin for unload is completed. In order to trigger the plugin's
 * method @a finalize(), @ref PSIDplugin_finalize() shall be called.
 *
 * If the plugin exposes the function-symbol @a cleanup, this function
 * will be called. Within this method all cleanup necessary before
 * actually evicting the plugin from address-space via dlclose() shall
 * be done. This includes free()ing memory segments allocated by the
 * plugin via malloc(), unregistering of timer, message-handler and
 * selectors, etc. Afterwards the plugin is marked to get evicted from
 * address-space via dlclose(). The actual action will be performed
 * from within the main-loop.
 *
 * @param name The name of the plugin to be unloaded.
 *
 * @return If the plugin is not found, -1 is returned. Otherwise 0 is
 * returned.
 */
int PSIDplugin_unload(char *name);

/**
 * @brief Unload all plugins
 *
 * Get all plugins forcefully unloaded.
 *
 * This function triggers to all plugins to get unloaded. Even if the
 * plugin ignores this demand, it will get unloaded forcefully once
 * the unload-timeout is elapsed.
 *
 * @return No return value.
 */
void PSIDplugin_forceUnloadAll(void);

/**
 * @brief Get API version
 *
 * Get the current version number of the daemon's plugin interface
 * API.
 *
 * @return The version number of the plugin API.
 */
int PSIDplugin_getAPIversion(void);

#endif  /* __PSIDPLUGIN_H */
