/*
 * ParaStation
 *
 * Copyright (C) 2019-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmspank.h"

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include "pscommon.h"
#include "psenv.h"
#include "pluginmalloc.h"

#include "slurmcommon.h"
#include "slurmerrno.h"

#include "psslurmstep.h"
#include "psslurmlog.h"
#include "psslurmproto.h"

#include "spank_api.h"

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
    { true,  SPANK_INIT,                   "slurm_spank_init"		      },
    { true,  SPANK_SLURMD_INIT,            "slurm_spank_init"		      },
    { true,  SPANK_JOB_PROLOG,             "slurm_spank_job_prolog"	      },
    { true,  SPANK_INIT_POST_OPT,          "slurm_spank_init_post_opt"	      },
    { true,  SPANK_LOCAL_USER_INIT,        "slurm_spank_local_user_init"      },
    { true,  SPANK_USER_INIT,              "slurm_spank_user_init"	      },
    { true,  SPANK_TASK_INIT_PRIVILEGED,   "slurm_spank_task_init_privileged" },
    { true,  SPANK_TASK_INIT,              "slurm_spank_task_init"	      },
    { true,  SPANK_TASK_POST_FORK,         "slurm_spank_task_post_fork"	      },
    { true,  SPANK_TASK_EXIT,              "slurm_spank_task_exit"	      },
    { true,  SPANK_JOB_EPILOG,             "slurm_spank_job_epilog"	      },
    { true,  SPANK_SLURMD_EXIT,            "slurm_spank_slurmd_exit"	      },
    { true,  SPANK_EXIT,                   "slurm_spank_exit"		      },
    { false, SPANK_END,                    NULL				      }
};

/** List of all spank plugins */
static LIST_HEAD(SpankList);

/** handle to symbols of the spank API */
static void *globalSym = NULL;

/** pointer to the spank structure of the executing callback */
static spank_t current_spank = NULL;

/** flag psid to be tainted by external spank plugins */
bool tainted = false;

void SpankSavePlugin(Spank_Plugin_t *def)
{
    list_add_tail(&def->next, &SpankList);
}

static void delSpankPlug(Spank_Plugin_t *sp)
{
    if (sp->handle) {
	dlclose(sp->handle);
	sp->handle = NULL;
    }
    ufree(sp->path);
    strvDestroy(&sp->argV);

    list_del(&sp->next);
    ufree(sp);
}

bool SpankInitPlugins(void)
{
    int count = 0;
    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &SpankList) {
	Spank_Plugin_t *sp = list_entry(s, Spank_Plugin_t, next);

	int ret = SpankLoadPlugin(sp, false);
	switch (ret) {
	case -1:
	    return false;
	case 0:
	    break;
	case 1:
	    /* remove optional/non spank plugins */
	    delSpankPlug(sp);
	    continue;
	default:
	    flog("unexpected return %d\n", ret);
	    return false;
	}
	count++;
    }
    if (count) flog("successfully loaded %i spank plugin(s)\n", count);

    return true;
}

void SpankFinalize(void)
{
    /* unload all spank plugins */
    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &SpankList) {
	Spank_Plugin_t *sp = list_entry(s, Spank_Plugin_t, next);
	delSpankPlug(sp);
    }

    /* unload global spank symbols */
    if (globalSym) dlclose(globalSym);
    globalSym = NULL;
}

bool SpankInitGlobalSym(void)
{
    if (SpankIsInitialized()) return true;

    globalSym = dlopen(GLOBAL_SYMBOLS, RTLD_NOW | RTLD_GLOBAL);
    if (!globalSym) {
	flog("dlopen(%s) failed: %s\n", GLOBAL_SYMBOLS, dlerror());
	return false;
    }
    fdbg(PSSLURM_LOG_SPANK, "dlopen(%s) success\n", GLOBAL_SYMBOLS);

    /* register global spank symbols */
    psSpank_Init = dlsym(globalSym, "psSpank_Init");
    if (!psSpank_Init) {
	dlclose(globalSym);
	globalSym = NULL;
	flog("dlsym(%s) failed to get psSpank_Init handle\n", GLOBAL_SYMBOLS);
	return false;
    }

    return psSpank_Init(psslurmlogger->mask & PSSLURM_LOG_SPANK);
}

bool SpankIsInitialized(void)
{
    return globalSym ? true : false;
}

static void doCallHook(Spank_Plugin_t *plugin, spank_t spank, char *hook)
{
    struct timeval time_start, time_now, time_diff;

    spank_f *hSym = dlsym(plugin->handle, hook);
    if (!hSym) {
	fdbg(PSSLURM_LOG_SPANK, "no symbol %s in plugin %s\n", hook,
	     plugin->name);
	return;
    }

    fdbg(PSSLURM_LOG_SPANK, "calling hook %s for %s\n", hook, plugin->name);
    spank->magic = SPANK_MAGIC;
    spank->plugin = plugin;
    if (spank->hook == SPANK_SLURMD_INIT ||
	spank->hook == SPANK_SLURMD_EXIT) {
	spank->context = S_CTX_SLURMD;
    } else if (spank->hook == SPANK_JOB_PROLOG ||
	       spank->hook == SPANK_JOB_EPILOG) {
	spank->context = S_CTX_JOB_SCRIPT;
    } else {
	spank->context = S_CTX_REMOTE;
    }
    current_spank = spank;

    gettimeofday(&time_start, NULL);
    int res = (*hSym)(spank, plugin->argV.count, plugin->argV.strings);
    gettimeofday(&time_now, NULL);

    timersub(&time_now, &time_start, &time_diff);
    if (time_diff.tv_sec > HOOK_MAX_RUNTIME) {
	flog("warning: hook %s from spank plugin %s took %.4f seconds\n", hook,
	     plugin->name, time_diff.tv_sec + 1e-6 * time_diff.tv_usec);
    }

    if (res != SLURM_SUCCESS) {
	flog("warning: hook %s from spank plugin %s returned %i\n", hook,
	     plugin->name, res);
    }
}

static Spank_Plugin_t *findPlugin(const char *name)
{
    if (!name) return NULL;

    list_t *s;
    list_for_each(s, &SpankList) {
	Spank_Plugin_t *plugin = list_entry(s, Spank_Plugin_t, next);
	if (plugin->name && !strcmp(plugin->name, name)) return plugin;
    }
    return NULL;
}

int SpankLoadPlugin(Spank_Plugin_t *sp, bool initialize)
{
    struct stat sbuf;
    if (stat(sp->path, &sbuf) == -1) {
	flog("%s plugin %s not found\n",
	     sp->optional ? "optional" : "required", sp->path);
	return sp->optional ? 1 : -1;
    }

    sp->handle = dlopen(sp->path, RTLD_LAZY);
    if (!sp->handle) {
	flog("%s plugin dlopen(%s) failed: %s\n",
	     sp->optional ? "optional" : "required", sp->path, dlerror());
	return sp->optional ? 1 : -1;
    }

    sp->type = dlsym(sp->handle, PLUGIN_TYPE);
    char *name = dlsym(sp->handle, PLUGIN_NAME);
    uint32_t *version = (uint32_t *) dlsym(sp->handle, PLUGIN_VERSION);

    if (!sp->type || !name || !version) {
	flog("missing symbols in plugin %s, type %s name %s\n",
	     sp->path, sp->type, name);
	return sp->optional ? 1 : -1;
    }

    Spank_Plugin_t *loadedSpank = findPlugin(name);
    if (loadedSpank) {
	flog("spank plugin %s already loaded\n", name);
	return -1;
    }
    sp->name = name;
    sp->version = *version;

    fdbg(PSSLURM_LOG_SPANK, "plugin=%s type=%s version=%u\n", sp->name,
	 sp->type, sp->version);

    if (strcmp(sp->type, "spank")) {
	/* drop non spank plugins */
	fdbg(PSSLURM_LOG_SPANK, "Dropping plugin=%s type=%s\n",
	     sp->name, sp->type);
	return sp->optional ? 1 : -1;
    }

    char *initHook = Spank_Hook_Table[SPANK_SLURMD_INIT].strName;
    char *exitHook = Spank_Hook_Table[SPANK_SLURMD_EXIT].strName;
    if (!dlsym(sp->handle, "psid_plugin") &&
	(dlsym(sp->handle, initHook) || dlsym(sp->handle, exitHook))) {
	fdbg(tainted ? PSSLURM_LOG_SPANK : -1,
	     "spank plugin %s taints the psid\n", sp->name);
	tainted = true;
    }

    if (initialize) {
	struct spank_handle spank = {
	    .task = NULL,
	    .alloc = NULL,
	    .job = NULL,
	    .step = NULL,
	    .hook = SPANK_SLURMD_INIT,
	    .envSet = NULL,
	    .envUnset = NULL
	};
	fdbg(PSSLURM_LOG_SPANK,
	     "call SPANK_SLURMD_INIT to init %s\n", sp->name);
	doCallHook(sp, &spank, Spank_Hook_Table[spank.hook].strName);
    }

    return 0;
}

bool SpankUnloadPlugin(const char *name, bool finalize)
{
    Spank_Plugin_t *sp = findPlugin(name);
    if (!sp) return false;

    flog("unloading spank plugin %s\n", name);
    if (finalize && sp->handle) {
	struct spank_handle spank = {
	    .task = NULL,
	    .alloc = NULL,
	    .job = NULL,
	    .step = NULL,
	    .hook = SPANK_SLURMD_EXIT,
	    .envSet = NULL,
	    .envUnset = NULL
	};
	fdbg(PSSLURM_LOG_SPANK,
	     "call SPANK_SLURMD_EXIT to finalize %s\n", sp->name);
	doCallHook(sp, &spank, Spank_Hook_Table[spank.hook].strName);
    }
    delSpankPlug(sp);
    return true;
}

static struct spank_option *findPluginOpt(Spank_Plugin_t *plugin, char *name)
{
    /* find option in spank_options table */
    struct spank_option *opt = dlsym(plugin->handle, "spank_options");
    for (int i = 0; opt && opt[i].name; i++) {
	if (!strcmp(opt[i].name, name)) return &opt[i];
    }

    /* find option in plugin table */
    opt = plugin->opt;
    for (uint32_t i = 0; i < plugin->optCount; i++) {
	if (!strcmp(opt[i].name, name)) return &opt[i];
    }

    return NULL;
}

void __SpankInitOpt(spank_t spank, const char *func, const int line)
{
    Step_t *step = spank->step;
    if (!step) {
	flog("invalid step from %s:%i\n", func, line);
	return;
    }

    for (uint32_t i=0; i<step->spankOptCount; i++) {
	Spank_Opt_t stepOpt = step->spankOpt[i];

	if (stepOpt.type != OPT_TYPE_SPANK) continue;

	Spank_Plugin_t *plugin = findPlugin(stepOpt.pluginName);
	if (!plugin) {
	    flog("no plugin %s for option %s found\n", stepOpt.pluginName,
		 stepOpt.optName);
	    continue;
	}

	struct spank_option *opt = findPluginOpt(plugin, stepOpt.optName);
	if (!opt) {
	    /* a spank plugin may choose to register options only in
	     * local context */
	    fdbg(PSSLURM_LOG_SPANK, "no option %s for plugin %s found\n",
		 stepOpt.optName, stepOpt.pluginName);
	} else {
	    /* execute option callback */
	    fdbg(PSSLURM_LOG_SPANK, "exec callback for name %s val %s"
		 " callback %p\n", stepOpt.optName, stepOpt.val, opt->cb);
	    if (opt->cb) opt->cb(opt->val, stepOpt.val, 1);
	}
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
	    flog("no handle for plugin %s (%s)\n", plugin->name, plugin->path);
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

bool SpankTraversePlugins(SpankVisitor_t visitor, const void *info)
{
    list_t *s, *tmp;
    list_for_each_safe(s, tmp, &SpankList) {
	Spank_Plugin_t *sp = list_entry(s, Spank_Plugin_t, next);

	if (visitor(sp, info)) return true;
    }

    return false;
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

    /* let the psslurmforwarder send change to mother */
    if (spank->envUnset) spank->envUnset(spank->step, var);

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
	    if (setenv(var, val, overwrite) == -1) return ESPANK_BAD_ARG;
    }

    if (spank->step) envSet(&spank->step->env, var, val);
    if (spank->job) envSet(&spank->job->env, var, val);
    if (spank->alloc) envSet(&spank->alloc->env, var, val);

    /* let the psslurmforwarder send change to mother */
    if (spank->envSet) spank->envSet(spank->step, var, val);

    return ESPANK_SUCCESS;
}

spank_err_t psSpankGetenv(spank_t spank, const char *var, char *buf, int len)
{
    char *res = NULL;

    if (!testMagic(spank, __func__)) return ESPANK_BAD_ARG;
    fdbg(PSSLURM_LOG_SPANK, "get %s from %s\n", var, spank->plugin->name);

    if (spank->step) res = envGet(&spank->step->env, var);
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
    uint16_t *pUint16;
    uint32_t *pUint32;
    uint64_t *pUint64;
    int *pInt;
    char ***pChar3;
    char **pChar2;

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
		*pUID = 0;
		return ESPANK_NOT_AVAIL;
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
		*pGID = 0;
		return ESPANK_NOT_AVAIL;
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
		*pUint32 = 0;
		return ESPANK_NOT_AVAIL;
	    }
	    break;
	case S_JOB_STEPID:
	    pUint32 = va_arg(ap, uint32_t *);
	    if (spank->step) {
		*pUint32 = spank->step->stepid;
	    } else if (spank->job || spank->alloc) {
		*pUint32 = SLURM_BATCH_SCRIPT;
	    } else {
		*pUint32 = 0;
		return ESPANK_NOT_AVAIL;
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
		*pUint32 = 0;
		return ESPANK_NOT_AVAIL;
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
		*pUint32 = 0;
		return ESPANK_NOT_AVAIL;
	    }
	    break;
	case S_JOB_ARGV:
	    pInt = va_arg(ap, int *);
	    pChar3 = va_arg(ap, char ***);
	    if (spank->step) {
		*pInt = spank->step->argc;
		*pChar3 = spank->step->argv;
	    } else if (spank->job) {
		*pInt = spank->job->argc;
		*pChar3 = spank->job->argv;
	    } else {
		*pInt = 0;
		*pChar3 = NULL;
	    }
	    break;
	case S_JOB_ENV:
	    pChar3 = va_arg(ap, char ***);
	    if (spank->step) {
		*pChar3 = spank->step->env.vars;
	    } else if (spank->job) {
		*pChar3 = spank->job->env.vars;
	    } else if (spank->alloc) {
		*pChar3 = spank->alloc->env.vars;
	    } else {
		*pChar3 = NULL;
	    }
	    break;
	case S_SLURM_RESTART_COUNT:
	    pUint32 = va_arg(ap, uint32_t *);
	    if (spank->job) {
		*pUint32 = spank->job->restartCnt;
	    } else {
		*pUint32 = 0;
		return ESPANK_NOT_AVAIL;
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
		return ESPANK_NOT_AVAIL;
	    }
	    break;
	case S_JOB_ALLOC_MEM:
	    pUint64 = va_arg(ap, uint64_t *);
	    if (spank->job) {
		*pUint64 = spank->job->memLimit;
	    } else if (spank->step) {
		*pUint64 = spank->step->jobMemLimit;
	    } else {
		*pUint64 = 0;
		return ESPANK_NOT_AVAIL;
	    }
	    break;
	case S_STEP_ALLOC_MEM:
	    pUint64 = va_arg(ap, uint64_t *);
	    if (spank->step) {
		*pUint64 = spank->step->stepMemLimit;
	    } else {
		*pUint64 = 0;
		return ESPANK_NOT_AVAIL;
	    }
	    break;
	case S_JOB_NCPUS:
	    pUint16 = va_arg(ap, uint16_t *);
	    if (spank->job) {
		*pUint16 = spank->job->tpp;
	    } else {
		*pUint16 = 0;
		return ESPANK_NOT_AVAIL;
	    }
	    break;
	/* TODO */
	case S_JOB_ALLOC_CORES:
	    /* Job allocated cores in list format (char **) */
	    pChar2 = va_arg(ap, char **);
	    *pChar2 = NULL;
	    return ESPANK_NOT_AVAIL;
	case S_STEP_ALLOC_CORES:
	    /* Step alloc'd cores in list format  (char **) */
	    pChar2 = va_arg(ap, char **);
	    *pChar2 = NULL;
	    return ESPANK_NOT_AVAIL;
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
#ifdef HAVE_S_TASK_ARGV
    uint32_t **p2UInt32;
    char ****p4Char;
#endif
    Step_t *step = spank->step;

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
	    if (step && spank->task) {
		*pInt = getLocalRankID(spank->task->rank, step);
		if (*pInt == (int32_t) NO_VAL) return -1;
	    } else {
		*pInt = -1;
		return ESPANK_NOT_TASK;
	    }
	    break;
	case S_STEP_CPUS_PER_TASK:
	    pUint32 = va_arg(ap, uint32_t *);
	    if (step) {
		*pUint32 = step->tpp;
	    } else {
		*pUint32 = 0;
		return ESPANK_NOT_TASK;
	    }
	    break;
	case S_JOB_TOTAL_TASK_COUNT:
	    pUint32 = va_arg(ap, uint32_t *);
	    if (step) {
		*pUint32 = step->np;
	    } else if (spank->job) {
		*pUint32 = spank->job->np;
	    } else {
		*pUint32 = 0;
		return ESPANK_NOT_AVAIL;
	    }
	    break;
	case S_JOB_LOCAL_TASK_COUNT:
	    pUint32 = va_arg(ap, uint32_t *);
	    if (step) {
		*pUint32 = step->tasksToLaunch[step->localNodeId];
	    } else {
		*pUint32 = 0;
		return ESPANK_NOT_AVAIL;
	    }
	    break;
	case S_TASK_EXIT_STATUS:
	    pInt = va_arg(ap, int *);
	    if (spank->hook == SPANK_TASK_EXIT) {
		*pInt = step->exitCode;
	    } else {
		*pInt = 0;
		return ESPANK_NOT_AVAIL;
	    }
	    break;
#ifdef HAVE_S_TASK_ARGV
	case S_TASK_ARGV:
	    p2UInt32 = va_arg(ap, uint32_t **);
	    p4Char = va_arg(ap, char ****);
	    if ((spank->hook == SPANK_TASK_INIT
		 || spank->hook == SPANK_TASK_INIT_PRIVILEGED)
		&& spank->task) {
		*p2UInt32 = &spank->task->argc;
		*p4Char = &spank->task->argv;
	    } else {
		*p2UInt32 = NULL;
		return ESPANK_NOT_AVAIL;
	    }
	    break;
#endif
	default:
	    return ESPANK_BAD_ARG;
    }

    return ESPANK_SUCCESS;
}

static spank_err_t getVersionItem(spank_t spank, spank_item_t item, va_list ap)
{
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
	init = true;
    }

    switch(item) {
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
	default:
	    return ESPANK_BAD_ARG;
    }

    return ESPANK_SUCCESS;
}

static spank_err_t getOtherItem(spank_t spank, spank_item_t item, va_list ap)
{
    uint32_t *pUint32;
    uint32_t Uint32Val, len;
    Step_t *step = spank->step;
    pid_t cPID;
    PStask_t *task = spank->task;

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
	case S_JOB_GLOBAL_TO_LOCAL_ID:
	    Uint32Val = va_arg(ap, uint32_t);
	    pUint32 = va_arg(ap, uint32_t *);
	    if (step) {
		*pUint32 = getLocalRankID(Uint32Val, step);
		if (*pUint32 == NO_VAL) return ESPANK_NOEXIST;
	    } else {
		*pUint32 = 0;
		return ESPANK_NOT_AVAIL;
	    }
	    break;
	case S_JOB_LOCAL_TO_GLOBAL_ID:
	    Uint32Val = va_arg(ap, uint32_t);
	    pUint32 = va_arg(ap, uint32_t *);
	    if (step) {
		len = step->globalTaskIdsLen[step->localNodeId];
		if (Uint32Val < len) {
		    *pUint32 =
			step->globalTaskIds[step->localNodeId][Uint32Val];
		} else {
		    *pUint32 = 0;
		    return ESPANK_BAD_ARG;
		}
	    } else {
		*pUint32 = 0;
		return ESPANK_NOT_AVAIL;
	    }
	    break;
	case S_JOB_PID_TO_GLOBAL_ID:
	    cPID = va_arg(ap, pid_t);
	    pUint32 = va_arg(ap, uint32_t *);
	    if (task) {
		if (PSC_getPID(task->tid) == cPID) {
		    *pUint32 = task->rank;
		}
	    } else {
		*pUint32 = 0;
		return ESPANK_NOT_AVAIL;
	    }
	    break;
	case S_JOB_PID_TO_LOCAL_ID:
	    cPID = va_arg(ap, pid_t);
	    pUint32 = va_arg(ap, uint32_t *);
	    if (task && step) {
		if (PSC_getPID(task->tid) == cPID) {
		    *pUint32 = getLocalRankID(task->rank, step);
		    if (*pUint32 == NO_VAL) return ESPANK_NOEXIST;
		}
	    } else {
		*pUint32 = 0;
		return ESPANK_NOT_AVAIL;
	    }
	    break;
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
#ifdef HAVE_S_TASK_ARGV
	case S_TASK_ARGV:
#endif
	    return getTaskItem(spank, item, ap);
	case S_SLURM_VERSION:
	case S_SLURM_VERSION_MAJOR:
	case S_SLURM_VERSION_MINOR:
	case S_SLURM_VERSION_MICRO:
	    return getVersionItem(spank, item, ap);
	case S_JOB_ARRAY_ID:
	case S_JOB_ARRAY_TASK_ID:
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

spank_err_t psSpankPrependArgv(spank_t spank, int argc, const char *argv[])
{
    if (!testMagic(spank, __func__)) return ESPANK_BAD_ARG;

    if (!spank->task || !spank->task->argv
	|| (spank->hook != SPANK_TASK_INIT_PRIVILEGED
	    && spank->hook != SPANK_TASK_INIT))
	return ESPANK_NOT_TASK;

    uint32_t new_argc = argc + spank->task->argc;
    char **new_argv = umalloc((new_argc+1) * sizeof(char *));

    uint32_t j = 0;
    for (int i = 0; i < argc && argv[i]; i++) {
	new_argv[j++] = ustrdup(argv[i]);
    }
    for (uint32_t i = 0; i < spank->task->argc && spank->task->argv[i]; i++) {
	new_argv[j++] = spank->task->argv[i];
    }
    new_argv[j] = NULL;

    free(spank->task->argv);

    spank->task->argc = new_argc;
    spank->task->argv = new_argv;

    return ESPANK_SUCCESS;
}

int psSpankSymbolSup(const char *symbol)
{
    if (!symbol) return -1;

    for (int i=0; Spank_Hook_Table[i].strName; i++) {
	if (!strcmp(symbol, Spank_Hook_Table[i].strName)) {
	    return Spank_Hook_Table[i].sup;
	}
    }
    return 0;
}

int psSpankGetContext(spank_t spank)
{
    if (spank) return spank->context;
    if (current_spank) return current_spank->context;
    return S_CTX_ERROR;
}

int psSpankOptRegister(spank_t spank, struct spank_option *opt)
{
    if (spank->hook == SPANK_SLURMD_INIT) return ESPANK_SUCCESS;
    if (spank->hook != SPANK_INIT) {
	fdbg(PSSLURM_LOG_SPANK, "invalid call in hook %s from %s\n",
	     Spank_Hook_Table[spank->hook].strName, spank->plugin->name);
	return ESPANK_BAD_ARG;
    }

    Spank_Plugin_t *plugin = spank->plugin;
    if (plugin->optCount+1 > plugin->optSize) {
	plugin->optSize += 20;
	plugin->opt = urealloc(plugin->opt,
			       sizeof(*plugin->opt) * plugin->optSize);
    }

    struct spank_option *new = &plugin->opt[plugin->optCount];
    new->name = ustrdup(opt->name);
    new->arginfo = ustrdup(opt->arginfo);
    new->usage = ustrdup(opt->usage);
    new->has_arg = opt->has_arg;
    new->val = opt->val;
    new->cb = opt->cb;

    plugin->optCount++;

    fdbg(PSSLURM_LOG_SPANK, "save option %s val %i count %u cb %p\n", new->name,
	 new->val, plugin->optCount, new->cb);

    return ESPANK_SUCCESS;
}

int psSpankOptGet(spank_t spank, struct spank_option *opt, char **retval)
{
    *retval = NULL;

    switch (spank->hook) {
	case SPANK_INIT:
	case SPANK_SLURMD_INIT:
	case SPANK_INIT_POST_OPT:
	case SPANK_TASK_POST_FORK:
	case SPANK_SLURMD_EXIT:
	case SPANK_EXIT:
	    fdbg(PSSLURM_LOG_SPANK, "invalid call in hook %s from %s\n",
		Spank_Hook_Table[spank->hook].strName, spank->plugin->name);
	    return ESPANK_NOT_AVAIL;
    }

    Step_t *step = spank->step;
    if (!step) {
	flog("invalid step from for opt %s\n", opt->name);
	return ESPANK_NOEXIST;
    }

    for (uint32_t i=0; i<step->spankOptCount; i++) {
	Spank_Opt_t stepOpt = step->spankOpt[i];

	if (stepOpt.type != OPT_TYPE_SPANK) continue;

	if (!strcmp(stepOpt.optName, opt->name)) {
	    *retval = stepOpt.val;
	    fdbg(PSSLURM_LOG_SPANK, "get option %s val %s\n",
		 stepOpt.optName, stepOpt.val);
	    return ESPANK_SUCCESS;
	}
    }

    return ESPANK_NOEXIST;
}

/**
 * The functions above will be called by Spank plugins
 *
 * Also see src/spank/spank_api.c holding the wrapper functions and
 * further documentation.
 **/
