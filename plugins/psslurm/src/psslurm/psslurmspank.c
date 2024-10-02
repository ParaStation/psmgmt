/*
 * ParaStation
 *
 * Copyright (C) 2019-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmspank.h"

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <ctype.h>

#include <slurm/slurm_errno.h>

#include "pscommon.h"
#include "psenv.h"
#include "pluginconfig.h"
#include "pluginmalloc.h"
#include "psidforwarder.h"

#include "slurmcommon.h"

#include "psslurmstep.h"
#include "psslurmlog.h"
#include "psslurmproto.h"
#include "psslurmconfig.h"
#include "psslurmfwcomm.h"

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

/* magic prefix for SPANK options transferred in environment */
#define SPANK_ENV_OPT "_SLURM_SPANK_OPTION_"

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

/** SPANK option cache */
static LIST_HEAD(OptCacheList);

/** handle to symbols of the spank API */
static void *globalSym = NULL;

/** pointer to the spank structure of the executing callback */
static spank_t current_spank = NULL;

/** flag psid to be tainted by external spank plugins */
bool tainted = false;

typedef struct {
    list_t next;                /**< used to put into some cache-list */
    Spank_Plugin_t *plugin;
    struct spank_option *spOpt;
    char *value;
} Opt_Cache_Entry_t;

static Opt_Cache_Entry_t *optCacheFind(Spank_Plugin_t *plugin,
				       const char *optName)
{
    list_t *o;
    list_for_each(o, &OptCacheList) {
	Opt_Cache_Entry_t *optCache = list_entry(o, Opt_Cache_Entry_t, next);
	if (optCache->plugin != plugin ||
	    strcmp(optCache->spOpt->name, optName)) continue;
	return optCache;
    }

    return NULL;
}

static void optCacheSave(Spank_Plugin_t *plugin, struct spank_option *spOpt,
			 const char *value)
{
    Opt_Cache_Entry_t *optCache = optCacheFind(plugin, spOpt->name);
    if (optCache) {
	fdbg(PSSLURM_LOG_SPANK, "update plugin %s option %s=%s\n", plugin->name,
	     spOpt->name, value);

	ufree(optCache->value);
	optCache->value = ustrdup(value);
	return;
    }

    fdbg(PSSLURM_LOG_SPANK, "add plugin %s option %s=%s\n", plugin->name,
	 spOpt->name, value);

    optCache = umalloc(sizeof(*optCache));
    optCache->plugin = plugin;
    optCache->spOpt = spOpt;
    optCache->value = ustrdup(value);

    list_add_tail(&optCache->next, &OptCacheList);
}

static void optCacheClear(Spank_Plugin_t *plugin)
{
    list_t *o;
    list_for_each(o, &OptCacheList) {
	Opt_Cache_Entry_t *optCache = list_entry(o, Opt_Cache_Entry_t, next);
	if (optCache->plugin != plugin) continue;
	ufree(optCache->value);

	list_del(&optCache->next);
	ufree(optCache);
    }
}

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
    optCacheClear(sp);
    ufree(sp->path);
    strvDestroy(sp->argV);

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

    char *logLevel = getConfValueC(SlurmConfig, "SlurmdDebug");

    return psSpank_Init(mset(PSSLURM_LOG_SPANK), logLevel);
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
    int res = (*hSym)(spank, strvSize(plugin->argV), strvGetArray(plugin->argV));
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

/**
 * @brief Convert a name to SPANK environment encoding
 *
 * @param name The name to convert
 *
 * @return Returns the converted name
 */
static char *getSpankEnvName(const char *name)
{
    char *dup = ustrdup(name);
    for (char *ptr = dup; *ptr; ptr++) {
	if (!isalnum((int)*ptr)) *ptr = '_';
    }
    return dup;
}

static bool testSpankEnvName(const char *regName, const char *envName,
			     const char sep)
{
    char *envName2 = getSpankEnvName(regName);
    if (!envName2) return false;

    bool res = false;
    size_t len2 = strlen(envName2);
    if (!strncmp(envName2, envName, len2) && (envName[len2] == sep)) {
	res = true;
    }

    ufree(envName2);
    return res;
}

/**
 * @brief Find a SPANK plugin by name
 *
 * @param name The name of the plugin to find
 *
 * @return Returns the found plugin on success otherwise false is returned
 */
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

#define DEFAULT_PLUG_DIR "/usr/lib64/slurm"

static bool findSpankAbsPath(char *relPath, char *absPath, size_t lenPath)
{
    const char delimiters[] =": ";
    char *toksave;
    char *plugDir = getConfValueC(SlurmConfig, "PluginDir");
    if (!plugDir) plugDir = DEFAULT_PLUG_DIR;

    char *dirDup = ustrdup(plugDir);
    char *dirNext = strtok_r(dirDup, delimiters, &toksave);

    while(dirNext) {
	struct dirent *dent;
	DIR *dir = opendir(dirNext);

	if (!dir) {
	    mwarn(errno, "%s: open directory %s failed :", __func__, dirNext);
	    ufree(dirDup);
	    return false;
	}
	rewinddir(dir);

	while ((dent = readdir(dir))) {
	    if (!strcmp(relPath, dent->d_name)) {
		size_t len = strlen(dirNext);
		if (dirNext[len-1] == '/') dirNext[len-1] = '\0';
		snprintf(absPath, lenPath, "%s/%s", dirNext, relPath);
		ufree(dirDup);
		return true;
	    }
	}
	closedir(dir);

	dirNext = strtok_r(NULL, delimiters, &toksave);
    }
    ufree(dirDup);

    flog("spank plugin %s in PluginDir %s not found\n", relPath, plugDir);
    return false;
}

Spank_Plugin_t *SpankNewPlug(char *spankDef)
{
    const char delimiters[] =" \t\n";
    char *toksave;

    if (!spankDef) {
	flog("error: invalid spank definition\n");
	return NULL;
    }

    /* path to plugin */
    char *path = strtok_r(spankDef, delimiters, &toksave);
    if (!path) {
	flog("invalid path to spank plugin '%s'\n", spankDef);
	return NULL;
    }

    Spank_Plugin_t *def = ucalloc(sizeof(*def));

    /* find absolute path to plugin */
    if (path[0] != '/') {
	char absPath[1024];
	if (!findSpankAbsPath(path, absPath, sizeof(absPath))) {
	    flog("path for plugin '%s' not found\n", path);
	    ufree(def);
	    return NULL;
	}
	def->path = ustrdup(absPath);
    } else {
	def->path = ustrdup(path);
    }
    fdbg(PSSLURM_LOG_SPANK, "path '%s'", def->path);

    /* additional arguments */
    def->argV = strvNew(NULL);
    char *args = strtok_r(NULL, delimiters, &toksave);
    while (args) {
	strvAdd(def->argV, args);
	mdbg(PSSLURM_LOG_SPANK, " args: '%s'", args);
	args = strtok_r(NULL, delimiters, &toksave);
    }

    mdbg(PSSLURM_LOG_SPANK, "\n");

    return def;
}

int SpankLoadPlugin(Spank_Plugin_t *sp, bool initialize)
{
    struct stat sbuf;
    if (!sp->path || stat(sp->path, &sbuf) == -1) {
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

/**
 * @brief Find a SPANK plugin option
 *
 * The SPANK option may be defined in the spank_options table or
 * added by a call to spank_option_register().
 *
 * @param plugin The plugin which registered the option
 *
 * @param name The name of the option to find
 *
 * @param isEnvName True if the name is in environment format
 * (see @ref getSpankEnvName())
 *
 * @return Returns the found SPANK option on success or NULL otherwise
 */
static struct spank_option *findPluginOpt(Spank_Plugin_t *plugin,
					  const char *name, bool isEnvName)
{
    if (!plugin || !name) return NULL;

    /* find option in spank_options table */
    struct spank_option *opt = dlsym(plugin->handle, "spank_options");
    for (int i = 0; opt && opt[i].name; i++) {
	if (isEnvName) {
	    if (testSpankEnvName(opt[i].name, name, '=')) return &opt[i];
	} else {
	    if (!strcmp(opt[i].name, name)) return &opt[i];
	}
    }

    /* find option in plugin table (see psSpankOptRegister()) */
    opt = plugin->opt;
    for (uint32_t i = 0; i < plugin->optCount; i++) {
	if (isEnvName) {
	    if (testSpankEnvName(opt[i].name, name, '=')) return &opt[i];
	} else {
	    if (!strcmp(opt[i].name, name)) return &opt[i];
	}
    }

    return NULL;
}

/**
 * @brief Initialize SPANK option from environment
 *
 * Search the given environment for SPANK plugin options. An option string
 * starts with a defined prefix followed by the plugin and option names.
 * If a option is successfully identified an optional callback is invoked so
 * the SPANK plugin may initialize itself.
 *
 * @param env Environment to use
 */
static void initSpankOptByEnv(env_t env)
{
    if (!env) return;
    size_t len = strlen(SPANK_ENV_OPT);

    for (char **e = envGetArray(env); e && *e; e++) {
	/* remove optional SPANK prefix */
	char *ptr = !strncmp("SPANK_", *e, 6) ? *e + 6 : *e;
	if (strncmp(SPANK_ENV_OPT, ptr, len)) continue;

	/* remove SPANK prefix */
	char *optEnv = ptr + len;
	if (!optEnv) {
	    flog("erro: empty option environment string %s\n", ptr);
	    continue;
	}

	/* find SPANK plugin */
	bool found = false;
	list_t *s;
	list_for_each(s, &SpankList) {
	    Spank_Plugin_t *plugin = list_entry(s, Spank_Plugin_t, next);
	    if (!plugin->name) continue;
	    if (!testSpankEnvName(plugin->name, optEnv, '_')) continue;

	    /* find SPANK option in plugin */
	    char *optName = optEnv + strlen(plugin->name) + 1;
	    struct spank_option *opt = findPluginOpt(plugin, optName, true);
	    if (!opt) {
		fdbg(PSSLURM_LOG_SPANK, "unable to find option '%s' in"
		     " plugin %s\n", optName, plugin->name);
		continue;
	    }

	    /* extract option value */
	    char *value = optName + strlen(opt->name) + 1;
	    if (value) {
		fdbg(PSSLURM_LOG_SPANK, "set option %s=%s for plugin %s \n",
		     opt->name, value, plugin->name);
		optCacheSave(plugin, opt, value);
		found = true;
	    }
	}
	if (!found) {
	    fdbg(PSSLURM_LOG_SPANK, "error: could not match option %s\n", *e);
	}
    }
}

/**
 * @brief Initialize SPANK option from step
 *
 * @param step step holding options to handle
 */
void initSpankOptByStep(Step_t *step)
{
    for (uint32_t i=0; step && i < step->spankOptCount; i++) {
	Spank_Opt_t stepOpt = step->spankOpt[i];

	if (stepOpt.type != OPT_TYPE_SPANK) continue;

	Spank_Plugin_t *plugin = findPlugin(stepOpt.pluginName);
	if (!plugin) {
	    flog("no plugin %s for option %s found\n", stepOpt.pluginName,
		 stepOpt.optName);
	    continue;
	}

	struct spank_option *opt = findPluginOpt(plugin, stepOpt.optName, false);
	if (!opt) {
	    /* a spank plugin may choose to register options only in
	     * local context */
	    fdbg(PSSLURM_LOG_SPANK, "no option %s for plugin %s found\n",
		 stepOpt.optName, stepOpt.pluginName);
	} else {
	    /* save option in cache */
	    fdbg(PSSLURM_LOG_SPANK, "exec callback for name %s val %s"
		 " callback %p\n", stepOpt.optName, stepOpt.val, opt->cb);
	    optCacheSave(plugin, opt, stepOpt.val);
	}
    }
}

void __SpankInitOpt(spank_t spank, const char *func, const int line)
{
    /* parse SPANK options included in allocation/job/step environment */
    initSpankOptByEnv(spank->spankEnv);

    /* parse SPANK options transferred by launch-step RPC */
    initSpankOptByStep(spank->step);

    /* execute callbacks */
    list_t *o;
    list_for_each(o, &OptCacheList) {
	Opt_Cache_Entry_t *optCache = list_entry(o, Opt_Cache_Entry_t, next);
	spank_opt_cb_f cb = optCache->spOpt->cb;
	if (cb) {
	    fdbg(PSSLURM_LOG_SPANK, "option callback %s=%s\n",
		 optCache->spOpt->name, optCache->value);
	    cb(optCache->spOpt->val, optCache->value, 1);
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

    if (spank->step) envUnset(spank->step->env, var);
    if (spank->job) envUnset(spank->job->env, var);
    if (spank->alloc) envUnset(spank->alloc->env, var);

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

    if (spank->step) envSet(spank->step->env, var, val);
    if (spank->job) envSet(spank->job->env, var, val);
    if (spank->alloc) envSet(spank->alloc->env, var, val);

    /* let the psslurmforwarder send change to mother */
    if (spank->envSet) spank->envSet(spank->step, var, val);

    return ESPANK_SUCCESS;
}

spank_err_t psSpankGetenv(spank_t spank, const char *var, char *buf, int len)
{
    char *res = NULL;

    if (!testMagic(spank, __func__)) return ESPANK_BAD_ARG;
    fdbg(PSSLURM_LOG_SPANK, "get %s from %s\n", var, spank->plugin->name);

    if (spank->step) res = envGet(spank->step->env, var);
    if (!res && spank->job) res = envGet(spank->job->env, var);
    if (!res && spank->alloc) res = envGet(spank->alloc->env, var);
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
		*pInt = strvSize(spank->step->argV);
		*pChar3 = strvGetArray(spank->step->argV);
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
		*pChar3 = envGetArray(spank->step->env);
	    } else if (spank->job) {
		*pChar3 = envGetArray(spank->job->env);
	    } else if (spank->alloc) {
		    *pChar3 = envGetArray(spank->alloc->env);
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
	    } else if (spank->job) {
		*pUint64 = spank->job->memLimit;
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
	    } else if (spank->job) {
		/* jobscript task */
		*pUint32 = 1;
	    } else {
		*pUint32 = 0;
		return ESPANK_NOT_AVAIL;
	    }
	    break;
	case S_TASK_EXIT_STATUS:
	    pInt = va_arg(ap, int *);
	    if (spank->hook == SPANK_TASK_EXIT) {
		if (step) {
		    *pInt = step->exitCode;
		} else if (spank->job) {
		    *pInt = spank->job->exitCode;
		} else {
		    *pInt = 0;
		    return ESPANK_NOT_AVAIL;
		}
	    } else {
		*pInt = 0;
		return ESPANK_NOT_AVAIL;
	    }
	    break;
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

    if (!spank->task || !strvInitialized(spank->task->argV)
	|| (spank->hook != SPANK_TASK_INIT_PRIVILEGED
	    && spank->hook != SPANK_TASK_INIT))
	return ESPANK_NOT_TASK;

    strv_t argV = strvNew(NULL);
    for (int i = 0; i < argc && argv[i]; i++) strvAdd(argV, argv[i]);
    for (char **a = strvGetArray(spank->task->argV); a && *a; a++) {
	strvLink(argV, *a);
    }
    strvSteal(spank->task->argV);

    spank->task->argV = argV;

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

void psSpankPrint(char *prefix, char *buf)
{
    spank_t sp = current_spank;
    if (!sp) {
	flog("invalid SPANK handle\n");
	return;
    }

    if (sp->context == S_CTX_JOB_SCRIPT) {
	/* no connection to user (pelogue), print to syslog */

	mlog("%s: %s%s\n", __func__, (prefix ? prefix : ""), buf);
    } else if (sp->hook == SPANK_TASK_POST_FORK ||
	    sp->hook == SPANK_TASK_EXIT) {
	/* psidforwarder context */

	if (prefix && PSIDfwd_printMsg(STDERR, prefix) == -1) {
	    fwarn(errno, "PSIDfwd_printMsg(%s) failed", prefix);
	}
	if (PSIDfwd_printMsg(STDERR, buf) == -1) {
	    fwarn(errno, "PSIDfwd_printMsg(%s) failed", buf);
	}
	if (PSIDfwd_printMsg(STDERR, "\n") == -1) {
	    fwarn(errno, "PSIDfwd_printMsg(\\n) failed");
	}
    } else if (sp->hook == SPANK_USER_INIT) {
	/* psslurmforwarder context */

	if (!sp->step) {
	    flog("missing step in spank context\n");
	    return;
	}

	if (prefix) {
	    queueFwMsg(&sp->step->fwMsgQueue, prefix, strlen(prefix), STDERR, 0);
	}
	queueFwMsg(&sp->step->fwMsgQueue, buf, strlen(buf), STDERR, 0);
	queueFwMsg(&sp->step->fwMsgQueue, "\n", 1, STDERR, 0);
    } else {
	/* child context */
	if (fprintf(stderr, "%s%s\n", (prefix ? prefix : ""), buf) < 1) {
	    flog("fprintf(%s%s) failed\n", (prefix ? prefix : ""), buf);
	}
    }
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

    Opt_Cache_Entry_t *optCache = optCacheFind(spank->plugin, opt->name);
    if (!optCache) {
	flog("SPANK plugin %s option %s not found\n", spank->plugin->name,
	     opt->name);
	return ESPANK_ERROR;
    }

    *retval = optCache->value;
    fdbg(PSSLURM_LOG_SPANK, "get option %s val %s\n", opt->name,
	 optCache->value);
    return ESPANK_SUCCESS;
}

/**
 * The functions above will be called by Spank plugins
 *
 * Also see src/spank/spank_api.c holding the wrapper functions and
 * further documentation.
 **/
