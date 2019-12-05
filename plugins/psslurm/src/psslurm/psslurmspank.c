/*
 * ParaStation
 *
 * Copyright (C) 2019 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <sys/stat.h>

#include "psslurmalloc.h"
#include "psslurmjob.h"
#include "psslurmstep.h"
#include "psslurmlog.h"
#include "psslurmspank.h"
#include "psslurmproto.h"

#include "pluginmalloc.h"

/** symbols every spank plugin has to implement */
#define PLUGIN_NAME     "plugin_name"
#define PLUGIN_TYPE     "plugin_type"
#define PLUGIN_VERSION  "plugin_version"

/** magic value to detect corrupted spank structures */
#define SPANK_MAGIC 0x043a933c

/** library holding the global symbols of the spank API */
#define GLOBAL_SYMBOLS SLURMLIBDIR "/spank_api.so"

/** maximum number of seconds a spank hook may take to execute before
 * a warning is generated */
#define HOOK_MAX_RUNTIME 1

static const struct {
    bool sup;
    int hook;
    char *strName;
} Spank_Hook_Table[] = {
    { false, SPANK_INIT,                   "slurm_spank_init"		      },
    { false, SPANK_SLURMD_INIT,            "slurm_spank_slurmd_init"	      },
    { true,  SPANK_JOB_PROLOG,             "slurm_spank_job_prolog"	      },
    { false, SPANK_INIT_POST_OPT,          "slurm_spank_init_post_opt"	      },
    { true,  SPANK_LOCAL_USER_INIT,        "slurm_spank_local_user_init"      },
    { true,  SPANK_USER_INIT,              "slurm_spank_user_init"	      },
    { true,  SPANK_TASK_INIT_PRIVILEGED,   "slurm_spank_task_init_privileged" },
    { true,  SPANK_TASK_INIT,              "slurm_spank_task_init"	      },
    { true,  SPANK_TASK_POST_FORK,         "slurm_spank_task_post_fork"	      },
    { true,  SPANK_TASK_EXIT,              "slurm_spank_task_exit"	      },
    { true,  SPANK_JOB_EPILOG,             "slurm_spank_job_epilog"	      },
    { false, SPANK_SLURMD_EXIT,            "slurm_spank_slurmd_exit"	      },
    { false, SPANK_EXIT,                   "slurm_spank_exit"		      },
    { false, SPANK_END,                    NULL				      }
};

/** List of all spank plugins */
static LIST_HEAD(SpankList);

/** handle to symbols of the spank API */
static void *globalSym = NULL;

void SpankSavePlugin(Spank_Plugin_t *def)
{
    def->handle = NULL;
    def->name = NULL;
    list_add_tail(&def->next, &SpankList);
}

bool SpankInitPlugins(void)
{
    list_t *s;
    struct stat sbuf;
    int count = 0;

    list_for_each(s, &SpankList) {
	Spank_Plugin_t *sp = list_entry(s, Spank_Plugin_t, next);

	if (stat(sp->path, &sbuf) == -1) {
	    flog("plugin %s not found", sp->path);
	    return false;
	}

	sp->handle = dlopen(sp->path, RTLD_LAZY);
	if (!sp->handle) {
	    flog("dlopen(%s) failed: %s\n", sp->path, dlerror());
	    return false;
	}

	char *type = dlsym(sp->handle, PLUGIN_TYPE);
	char *name = dlsym(sp->handle, PLUGIN_NAME);
	uint32_t *version = (uint32_t *) dlsym(sp->handle, PLUGIN_VERSION);

	if (!type || !name || !version) {
	    flog("missing symbols in plugin %s, type %s name %s "
		    "version %u\n", sp->path, type, name, *version);
	    return false;
	}
	sp->name = name;

	fdbg(PSSLURM_LOG_SPANK, "plugin=%s type=%s version=%u\n", name,
	     type, *version);
	count++;
    }
    if (count) flog("successfully loaded %i spank plugins\n", count);

    return true;
}

void SpankFinalize(void)
{
    list_t *s, *tmp;

    /* unload all spank plugins */
    list_for_each_safe(s, tmp, &SpankList) {
	Spank_Plugin_t *sp = list_entry(s, Spank_Plugin_t, next);

	if (sp->handle) {
	    dlclose(sp->handle);
	    sp->handle = NULL;
	}
	ufree(sp->path);
	strvDestroy(&sp->argV);
	list_del(&sp->next);
	ufree(sp);
    }

    /* unload global spank symbols */
    if (globalSym) dlclose(globalSym);
}

/**
 * @brief Init function for the global spank API
 */
typedef void(fpInitSpank)(bool);

bool SpankInitGlobalSym(void)
{
    globalSym = dlopen(GLOBAL_SYMBOLS, RTLD_NOW | RTLD_GLOBAL);
    if (!globalSym) {
	flog("dlopen(%s) failed: %s\n", GLOBAL_SYMBOLS, dlerror());
	return false;
    }
    fdbg(PSSLURM_LOG_SPANK, "dlopen(%s) success\n", GLOBAL_SYMBOLS);

    /* register global spank symbols */
    fpInitSpank *pSpankInit = dlsym(globalSym, "psSpank_Init");
    if (!pSpankInit) {
	dlclose(globalSym);
	flog("dlsym(%s) failed to get psSpank_Init handle\n", GLOBAL_SYMBOLS);
	return false;
    }
    pSpankInit(psslurmlogger->mask & PSSLURM_LOG_SPANK);

    return true;
}

static void doCallHook(Spank_Plugin_t *plugin, spank_t spank, char *hook)
{
    struct timeval time_start, time_now, time_diff;
    spank_f *hSym;

    if (!(hSym = dlsym(plugin->handle, hook))) {
	fdbg(PSSLURM_LOG_SPANK, "no symbol %s in plugin %s\n", hook,
	     plugin->name);
	return;
    }

    fdbg(PSSLURM_LOG_SPANK, "calling hook %s for %s\n", hook, plugin->name);
    spank->magic = SPANK_MAGIC;
    spank->plugin = plugin;

    gettimeofday(&time_start, NULL);
    (*hSym)(spank, plugin->argV.count, plugin->argV.strings);
    gettimeofday(&time_now, NULL);

    timersub(&time_now, &time_start, &time_diff);
    if (time_diff.tv_sec > HOOK_MAX_RUNTIME) {
	flog("warning: hook %s from spank plugin %s took %.4f seconds\n", hook,
		plugin->name, time_diff.tv_sec + 1e-6 * time_diff.tv_usec);
    }
}

void __SpankCallHook(spank_t spank, const char *func, const int line)
{
    if (!globalSym) {
	flog("global spank symbols are unavailable\n");
	return;
    }

    if (spank->hook >= SPANK_END) {
	flog("invalid hook %i from %s:%i\n", spank->hook, func, line);
	return;
    }

    char *strHook = Spank_Hook_Table[spank->hook].strName;
    if (!strHook) {
	flog("hook %i from caller %s:%i not found\n", spank->hook, func, line);
	return;
    }

    fdbg(PSSLURM_LOG_SPANK, "hooking %s for %s:%i\n", strHook, func, line);

    list_t *s;
    list_for_each(s, &SpankList) {
	Spank_Plugin_t *plugin = list_entry(s, Spank_Plugin_t, next);
	if (!plugin->handle) {
	    flog("no handle for plugin %s\n", plugin->name);
	    continue;
	}
	doCallHook(plugin, spank, strHook);
    }
}

static bool testMagic(spank_t spank, const char *func)
{
    if (!spank || spank->magic != SPANK_MAGIC) {
	flog("error: invalid spank magic in %s\n", func);
	return false;
    }
    return true;
}

/**
 * The following functions will be called by Spank plugins
 *
 * Also see src/spank/spank_api.c holding the wrapper functions and
 * further documentation.
 **/

spank_err_t psSpankUnsetenv(spank_t spank, const char *var)
{
    if (!testMagic(spank, __func__)) return ESPANK_BAD_ARG;
    fdbg(PSSLURM_LOG_SPANK, "unset %s from %s\n", var, spank->plugin->name);

    if (spank->step) envUnset(&spank->step->env, var);
    if (spank->job) envUnset(&spank->job->env, var);
    if (spank->alloc) envUnset(&spank->alloc->env, var);

    switch (spank->hook) {
	case SPANK_JOB_PROLOG:
	case SPANK_LOCAL_USER_INIT:
	case SPANK_USER_INIT:
	case SPANK_TASK_INIT_PRIVILEGED:
	case SPANK_TASK_INIT:
	case SPANK_JOB_EPILOG:
	    unsetenv(var);
    }

    return ESPANK_SUCCESS;
}

spank_err_t psSpankSetenv(spank_t spank, const char *var, const char *val,
			  int overwrite)
{
    if (!testMagic(spank, __func__)) return ESPANK_BAD_ARG;
    fdbg(PSSLURM_LOG_SPANK, "set %s=%s overwrite %i from %s\n", var, val,
	 overwrite, spank->plugin->name);

    switch (spank->hook) {
	case SPANK_JOB_PROLOG:
	case SPANK_LOCAL_USER_INIT:
	case SPANK_USER_INIT:
	case SPANK_TASK_INIT_PRIVILEGED:
	case SPANK_TASK_INIT:
	case SPANK_JOB_EPILOG:
	    if (setenv(var, var, overwrite) == -1) return ESPANK_BAD_ARG;
    }


    /* TODO: should we set it also in the management structures? */
    if (spank->step) {

    }

    if (spank->job) {

    }

    if (spank->alloc) {

    }

    return ESPANK_SUCCESS;
}

spank_err_t psSpankGetenv(spank_t spank, const char *var, char *buf, int len)
{
    char *res = NULL;

    if (!testMagic(spank, __func__)) return ESPANK_BAD_ARG;
    fdbg(PSSLURM_LOG_SPANK, "get %s from %s\n", var, spank->plugin->name);

    if (!res && spank->step) res = envGet(&spank->step->env, var);
    if (!res && spank->job) res = envGet(&spank->job->env, var);
    if (!res && spank->alloc) res = envGet(&spank->alloc->env, var);
    if (!res) res = getenv(var);

    if (res) {
	size_t envLen = strlen(res);
	if (envLen+1 > (size_t)len) return ESPANK_NOSPACE;

	strncpy(buf, res, len);
	buf[envLen] = '\0';

	return ESPANK_SUCCESS;
    }

    return ESPANK_ENV_NOEXIST;
}

static spank_err_t getJobItem(spank_t spank, spank_item_t item, va_list ap)
{
    uid_t *pUID;
    gid_t *pGID;
    gid_t **pGIDs;
    uint32_t *pUint32;
    int *pInt;
    char ***pChar;

    switch(item) {
	case S_JOB_UID:
	    pUID = va_arg(ap, uid_t *);
	    if (spank->step) {
		*pUID = spank->step->uid;
	    } else if (spank->job) {
		*pUID = spank->job->uid;
	    } else if (spank->alloc) {
		*pUID = spank->alloc->uid;
	    } else {
		return ESPANK_BAD_ARG;
	    }
	    break;
	case S_JOB_GID:
	    pGID = va_arg(ap, gid_t *);
	    if (spank->step) {
		*pGID = spank->step->gid;
	    } else if (spank->job) {
		*pGID = spank->job->gid;
	    } else if (spank->alloc) {
		*pGID = spank->alloc->gid;
	    } else {
		return ESPANK_BAD_ARG;
	    }
	    break;
	case S_JOB_ID:
	    pUint32 = va_arg(ap, uint32_t *);
	    if (spank->step) {
		*pUint32 = spank->step->jobid;
	    } else if (spank->job) {
		*pUint32 = spank->job->jobid;
	    } else if (spank->alloc) {
		*pUint32 = spank->alloc->id;
	    } else {
		return ESPANK_BAD_ARG;
	    }
	    break;
	case S_JOB_STEPID:
	    pUint32 = va_arg(ap, uint32_t *);
	    if (spank->step) {
		*pUint32 = spank->step->stepid;
	    } else if (spank->job || spank->alloc) {
		*pUint32 = SLURM_BATCH_SCRIPT;
	    } else {
		return ESPANK_BAD_ARG;
	    }
	    break;
	case S_JOB_NNODES:
	    pUint32 = va_arg(ap, uint32_t *);
	    if (spank->step) {
		*pUint32 = spank->step->nrOfNodes;
	    } else if (spank->job) {
		*pUint32 = spank->job->nrOfNodes;
	    } else if (spank->alloc) {
		*pUint32 = spank->alloc->nrOfNodes;
	    } else {
		return ESPANK_BAD_ARG;
	    }
	    break;
	case S_JOB_NODEID:
	    pUint32 = va_arg(ap, uint32_t *);
	    if (spank->step) {
		*pUint32 = spank->step->localNodeId;
	    } else if (spank->job) {
		*pUint32 = spank->job->localNodeId;
	    } else if (spank->alloc) {
		*pUint32 = spank->alloc->localNodeId;
	    } else {
		return ESPANK_BAD_ARG;
	    }
	    break;
	case S_JOB_ARGV:
	    pInt = va_arg(ap, int *);
	    pChar = va_arg(ap, char ***);
	    if (spank->step) {
		*pInt = spank->step->argc;
		*pChar = spank->step->argv;
	    } else if (spank->job) {
		*pInt = spank->job->argc;
		*pChar = spank->job->argv;
	    } else {
		*pInt = 0;
		*pChar = NULL;
	    }
	    break;
	case S_JOB_ENV:
	    pChar = va_arg(ap, char ***);
	    if (spank->step) {
		*pChar = spank->step->env.vars;
	    } else if (spank->job) {
		*pChar = spank->job->env.vars;
	    } else if (spank->alloc) {
		*pChar = spank->alloc->env.vars;
	    } else {
		*pChar = NULL;
	    }
	    break;
	case S_SLURM_RESTART_COUNT:
	    pUint32 = va_arg(ap, uint32_t *);
	    if (spank->job) {
		*pUint32 = spank->job->restartCnt;
	    } else {
		return ESPANK_BAD_ARG;
	    }
	    break;
	case S_JOB_SUPPLEMENTARY_GIDS:
	    pGIDs = va_arg(ap, gid_t **);
	    pInt = va_arg(ap, int *);
	    if (spank->step) {
		*pGIDs = spank->step->gids;
		*pInt = spank->step->gidsLen;
	    } else if (spank->job) {
		*pGIDs = spank->job->gids;
		*pInt = spank->job->gidsLen;
	    } else {
		*pInt = 0;
		return ESPANK_BAD_ARG;
	    }
	    break;
	/* TODO */
	case S_JOB_NCPUS:
	    /* Number of CPUs used by this job (uint16_t *) */
	case S_JOB_ALLOC_CORES:
	    /* Job allocated cores in list format (char **) */
	case S_JOB_ALLOC_MEM:
	    /* Job allocated memory in MB (uint64_t *)      */
	case S_STEP_ALLOC_CORES:
	    /* Step alloc'd cores in list format  (char **) */
	case S_STEP_ALLOC_MEM:
	    /* Step alloc'd memory in MB (uint64_t *)       */
	default:
	    return ESPANK_BAD_ARG;
    }

    return ESPANK_SUCCESS;
}

static spank_err_t getTaskItem(spank_t spank, spank_item_t item, va_list ap)
{
    pid_t *pPID;
    uint32_t *pUint32;
    int *pInt;

    switch(item) {
	case S_TASK_GLOBAL_ID:
	    pUint32 = va_arg(ap, uint32_t *);
	    if (spank->task) {
		*pUint32 = spank->task->rank;
	    } else {
		*pUint32 = -1;
		return ESPANK_NOT_TASK;
	    }
	    break;
	case S_TASK_PID:
	    pPID = va_arg(ap, pid_t *);
	    if (spank->task) {
		*pPID = PSC_getPID(spank->task->tid);
	    } else {
		*pPID = -1;
		return ESPANK_NOT_TASK;
	    }
	    break;
	case S_TASK_ID:
	    pInt = va_arg(ap, int *);
	    if (spank->step) {
		*pInt = getLocalRankID(spank->task->rank, spank->step,
				       spank->step->localNodeId);
	    } else {
		*pInt = -1;
		return ESPANK_NOT_TASK;
	    }
	    break;
	case S_STEP_CPUS_PER_TASK:
	    pUint32 = va_arg(ap, uint32_t *);
	    if (spank->step) {
		*pUint32 = spank->step->tpp;
	    } else {
		*pUint32 = 0;
		return ESPANK_NOT_TASK;
	    }
	    break;
	/* TODO */
	case S_JOB_LOCAL_TASK_COUNT:
	    /* Number of local tasks (uint32_t *)           */
	case S_JOB_TOTAL_TASK_COUNT:
	    /* Total number of tasks in job (uint32_t *)    */
	case S_TASK_EXIT_STATUS:
	    /* Exit status of task if exited (int *)        */
	default:
	    return ESPANK_BAD_ARG;
    }

    return ESPANK_SUCCESS;
}

spank_err_t getOtherItem(spank_t spank, spank_item_t item, va_list ap)
{
    uint32_t *pUint32;
    char **pChar;
    static char verMajor[8], verMinor[8], verMicro[8];
    static bool init = false;

    if (!init) {
	int major = 0, minor = 0, micro = 0;
	if (sscanf(slurmVerStr, "%i.%i.%i-", &major, &minor, &micro) != 3) {
	    flog("Slurm version string '%s' parsing failed\n", slurmVerStr);
	}
	snprintf(verMajor, sizeof(verMajor), "%i", major);
	snprintf(verMinor, sizeof(verMinor), "%i", minor);
	snprintf(verMicro, sizeof(verMicro), "%i", micro);
    }

    switch(item) {
	case S_JOB_ARRAY_ID:
	    pUint32 = va_arg(ap, uint32_t *);
	    if (spank->job) {
		*pUint32 = spank->job->arrayJobId;
	    } else {
		*pUint32 = 0;
	    }
	    break;
	case S_JOB_ARRAY_TASK_ID:
	    pUint32 = va_arg(ap, uint32_t *);
	    if (spank->job) {
		*pUint32 = spank->job->arrayTaskId;
	    } else {
		*pUint32 = 0;
	    }
	    break;
	case S_SLURM_VERSION:
	    pChar = va_arg(ap, char **);
	    *pChar = slurmVerStr;
	    break;
	case S_SLURM_VERSION_MAJOR:
	    pChar = va_arg(ap, char **);
	    *pChar = verMajor;
	    break;
	case S_SLURM_VERSION_MINOR:
	    pChar = va_arg(ap, char **);
	    *pChar = verMinor;
	    break;
	case S_SLURM_VERSION_MICRO:
	    pChar = va_arg(ap, char **);
	    *pChar = verMicro;
	    break;
	case S_JOB_PID_TO_GLOBAL_ID:
	    /* global task id from pid (pid_t, uint32_t *)  */
	case S_JOB_PID_TO_LOCAL_ID:
	    /* local task id from pid (pid_t, uint32_t *)   */
	case S_JOB_LOCAL_TO_GLOBAL_ID:
	    /* local id to global id (uint32_t, uint32_t *) */
	case S_JOB_GLOBAL_TO_LOCAL_ID:
	    /* global id to local id (uint32_t, uint32_t *) */
	default:
	    return ESPANK_BAD_ARG;
    }

    return ESPANK_SUCCESS;
}

spank_err_t psSpankGetItem(spank_t spank, spank_item_t item, va_list ap)
{
    if (!testMagic(spank, __func__)) return ESPANK_BAD_ARG;
    fdbg(PSSLURM_LOG_SPANK, "get item %i from %s\n", item, spank->plugin->name);

    switch(item) {
	case S_JOB_UID:
	case S_JOB_GID:
	case S_JOB_ID:
	case S_JOB_STEPID:
	case S_JOB_NNODES:
	case S_JOB_NODEID:
	case S_JOB_NCPUS:
	case S_JOB_ARGV:
	case S_JOB_ENV:
	case S_SLURM_RESTART_COUNT:
	case S_JOB_SUPPLEMENTARY_GIDS:
	case S_JOB_ALLOC_CORES:
	case S_JOB_ALLOC_MEM:
	case S_STEP_ALLOC_CORES:
	case S_STEP_ALLOC_MEM:
	    return getJobItem(spank, item, ap);
	case S_JOB_LOCAL_TASK_COUNT:
	case S_JOB_TOTAL_TASK_COUNT:
	case S_TASK_ID:
	case S_TASK_GLOBAL_ID:
	case S_TASK_EXIT_STATUS:
	case S_TASK_PID:
	case S_STEP_CPUS_PER_TASK:
	    return getTaskItem(spank, item, ap);
	case S_JOB_ARRAY_ID:
	case S_JOB_ARRAY_TASK_ID:
	case S_SLURM_VERSION:
	case S_SLURM_VERSION_MAJOR:
	case S_SLURM_VERSION_MINOR:
	case S_SLURM_VERSION_MICRO:
	case S_JOB_PID_TO_GLOBAL_ID:
	case S_JOB_PID_TO_LOCAL_ID:
	case S_JOB_LOCAL_TO_GLOBAL_ID:
	case S_JOB_GLOBAL_TO_LOCAL_ID:
	    return getOtherItem(spank, item, ap);
	default:
	    return ESPANK_BAD_ARG;
    }

    return ESPANK_SUCCESS;
}

int psSpankSymbolSup(const char *symbol)
{
    int i;
    if (!symbol) return -1;

    for (i=0; Spank_Hook_Table[i].strName; i++) {
	if (!strcmp(symbol, Spank_Hook_Table[i].strName)) {
	    return Spank_Hook_Table[i].sup;
	}
    }
    return 0;
}
