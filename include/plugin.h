/*
 *               ParaStation
 *
 * Copyright (C) 2009-2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file ParaStation plugin template. Each plugin should implement the
 * symbols defined es 'extern' below. The symbols @ref name and @ref
 * version are required. Plugins not defining these symbols will be
 * unloaded immediately. All other symbols are optional.
 *
 * In addition each plugin can provide one function with
 * __attribute__((constructor)) registering the plugin and another
 * function with __attribute__((destructor)) de-registering it. These
 * functions are not required. For several reasons discussed in the
 * Wiki it makes more sense to use the functionality provided by the
 * functions @ref initialize(), @ref finalize() and @ref cleanup().
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef _PLUGIN_H_
#define _PLUGIN_H_

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

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
 * functions provided by the plugins marked to be dependants are
 * accessible when this function is called.
 *
 * If initialization of the plugin fails, the plugin shall call
 * PSIDplugin_unload() in order to unload itself as soon as possible.
 * At the same time all plugins just loaded to resolve dependencies of
 * the original plugin will be marked for unloading, too.
 *
 * @return No return value.
 */
void initialize(void);

/**
 * @brief Stop plugin
 *
 * This function will be called in order to signal the plugin to
 * prepare getting unloaded soon. The plugin is expected to call @ref
 * PSIDplugin_unload() as soon as all cleanup and de-registration
 * actions are finished.
 *
 * Even if the plugin does not call @ref PSIDplugin_unload() it should
 * expect to get unloaded soon. Before actually unloading the plugin
 * @ref cleanup() will be called, if the plugin exposes such symbol.
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
 * @return No return value.
 */
void cleanup(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* _PLUGIN_H_ */
