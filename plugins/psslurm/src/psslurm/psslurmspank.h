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
    strv_t argV;                /**< argument vector from plugstack config */
    void *handle;               /**< handle returned by dlopen() */
} Spank_Plugin_t;

typedef enum {
    SPANK_INIT = 0,
    SPANK_SLURMD_INIT,
    SPANK_JOB_PROLOG,
    SPANK_INIT_POST_OPT,
    SPANK_LOCAL_USER_INIT,
    SPANK_USER_INIT,
    SPANK_TASK_INIT_PRIVILEGED,
    SPANK_TASK_INIT,
    SPANK_TASK_POST_FORK,
    SPANK_TASK_EXIT,
    SPANK_JOB_EPILOG,
    SPANK_SLURMD_EXIT,
    SPANK_EXIT,
    SPANK_END
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
