/*
 *               ParaStation
 *
 * Copyright (C) 2009 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file ParaStation plugin template. Each plugin should implement the
 * symbols defined es 'extern' below.
 *
 * In addition each plugin must provide one function with
 * __attribute__((constructor)) registering the plugin and another
 * function with __attribute__((destructor)) de-registering it.
 *
 * If a plugin has no dependencies, it might miss to implement the
 * @ref dependencies symbol below.
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

/** This describes plugin dependencies */
typedef struct {
    char *name;   /**< Name of the plugin we depend on */
    int version;  /**< Minimum version of the plugin we depend on */
} plugin_dep_t;

/** Name of the plugin */
extern char name[];

/** Current version of the plugin */
extern int version;

/**
 * Minimum version of the ParaStation daemon's plugin API required. If
 * set to 0, any version is accepted.
 */
extern int requiredAPI;

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

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* _PLUGIN_H_ */
