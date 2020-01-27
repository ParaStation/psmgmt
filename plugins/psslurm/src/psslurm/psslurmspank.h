/*
 * ParaStation
 *
 * Copyright (C) 2019-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSSLURM_SPANK
#define __PSSLURM_SPANK

#include <stdbool.h>

#include "slurm/spank.h"

#include "pluginstrv.h"
#include "psslurmstep.h"
#include "psslurmjob.h"
#include "psslurmalloc.h"

typedef struct {
    list_t next;                /**< used to put into some plugin-lists */
    bool optional;              /**< is the plugin optional or required */
    char *path;                 /**< absolute path of the plugin */
    char *name;                 /**< plugin name */
    char *type;                 /**< plugin type */
    uint32_t version;           /**< plugin version */
    strv_t argV;                /**< argument vector from plugstack config */
    void *handle;               /**< handle returned by dlopen() */
} Spank_Plugin_t;

typedef enum {
    SPANK_INIT = 0,             /**< called in forwarder initialize */
    SPANK_SLURMD_INIT,          /**< unsupported, prevent changes to main psid*/
    SPANK_JOB_PROLOG,           /**< called before prologue is started */
    SPANK_INIT_POST_OPT,        /**< unsupported, because of missing options */
    SPANK_LOCAL_USER_INIT,      /**< called in local (srun) context only */
    SPANK_USER_INIT,            /**< called after privileges temp drop */
    SPANK_TASK_INIT_PRIVILEGED, /**< called for every task as root user */
    SPANK_TASK_INIT,            /**< called for every task before execve() */
    SPANK_TASK_POST_FORK,       /**< called in parent after fork */
    SPANK_TASK_EXIT,            /**< exec when task exit status is available */
    SPANK_JOB_EPILOG,           /**< called before epilogue is started */
    SPANK_SLURMD_EXIT,          /**< unsupported, prevent changes to main psid*/
    SPANK_EXIT,                 /**< called before step forwarder exits */
    SPANK_END                   /**< mark end of hook table */
} Spank_Hook_Calls_t;

struct spank_handle {
    int magic;               /**< magic to detect corrupted spank structures */
    Alloc_t *alloc;          /**< allocation of the current context or NULL */
    Job_t *job;              /**< job of the current context or NULL */
    Step_t *step;            /**< step of the current context or NULL */
    unsigned int hook;       /**< hook which is currently called */
    PStask_t *task;          /**< child task structure which called the hook */
    Spank_Plugin_t *plugin;  /**< spank plugin currently executed */
};

/**
 * @brief Initialize the spank facility
 *
 * Load library holding the global symbols of the spank API.
 *
 * @return Returns true on success or false otherwise
 * */
bool SpankInitGlobalSym(void);

/**
 * @brief Initialize all configured spank plugins
 *
 * @return Returns true on success or false otherwise
 * */
bool SpankInitPlugins(void);

/**
 * @brief Save a spank plugin definition
 *
 * @param def The plugin definition to save
 */
void SpankSavePlugin(Spank_Plugin_t *def);

/**
 * @brief Finalize the spank facility
 *
 * Unload all spank plugins and global spank symbols.
 */
void SpankFinalize(void);

void __SpankCallHook(spank_t spank, const char *func, const int line);
#define SpankCallHook(spank) __SpankCallHook(spank, __func__, __LINE__)

/**
 * @brief Visitor function
 *
 * Visitor function used by @ref SpankTraversePlugins() in order to visit
 * each spank plugin currently registered.
 *
 * The parameters are as follows: @a sp points to the spank plugin to
 * visit. @a info points to the additional information passed to @ref
 * SpankTraversePlugins() in order to be forwarded to each plugin.
 *
 * If the visitor function returns true the traversal will be
 * interrupted and @ref SpankTraversePlugins() will return to its calling
 * function.
 */
typedef bool SpankVisitor_t(Spank_Plugin_t *sp, const void *info);

/**
 * @brief Traverse all spank plugins
 *
 * Traverse all spank plugins by calling @a visitor for each of the registered
 * plugins. In addition to a pointer to the current spank plugin @a info
 * is passed as additional information to @a visitor.
 *
 * If @a visitor returns true, the traversal will be stopped
 * immediately and true is returned to the calling function.
 *
 * @param visitor Visitor function to be called for each plugin
 *
 * @param info Additional information to be passed to @a visitor while
 * visiting the spank plugins
 *
 * @return If the visitor returns true, traversal will be stopped and
 * true is returned. If no visitor returned true during the traversal
 * false is returned.
 */
bool SpankTraversePlugins(SpankVisitor_t visitor, const void *info);

/**
 * The following functions will be called by Spank plugins
 *
 * Also see src/spank/spank_api.c holding the wrapper functions and
 * further documentation.
 **/

spank_err_t psSpankSetenv(spank_t spank, const char *var, const char *val,
                          int overwrite);

spank_err_t psSpankGetenv(spank_t spank, const char *var, char *buf, int len);

spank_err_t psSpankUnsetenv(spank_t spank, const char *var);

spank_err_t psSpankGetItem(spank_t spank, spank_item_t item, va_list ap);

int psSpankSymbolSup(const char *symbol);

#endif /* __PSSLURM_SPANK */
