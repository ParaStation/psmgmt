/*
 * ParaStation
 *
 * Copyright (C) 2015-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmspawn.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pscommon.h"
#include "psenv.h"
#include "psstrv.h"

#include "pluginconfig.h"

#include "slurmcommon.h"
#include "psslurmconfig.h"
#include "psslurmlog.h"
#include "psslurmenv.h"

static char buffer[1024];

static Step_t *step = NULL;

void initSpawnFacility(Step_t *jobstep)
{
    step = jobstep;
}

/**
 * @brief Prepare preput keys and values to pass by environment.
 *
 * Generates an array of strings representing the preput key-value-pairs in the
 * format "__PMI_<KEY>=<VALUE>" and one variable "__PMI_preput_num=<COUNT>"
 *
 * @param preputc  Number of key value pairs.
 *
 * @param preputv  Array of key value pairs.
 *
 * @param env      Plugin environment struct to extend.
 */
static void addSpawnPreputToEnv(int preputc, KVP_t *preputv, env_t env)
{
    snprintf(buffer, sizeof(buffer), "__PMI_preput_num=%i", preputc);
    envAdd(env, buffer);

    for (int i = 0; i < preputc; i++) {
	snprintf(buffer, sizeof(buffer), "__PMI_preput_key_%i", i);
	envSet(env, buffer, preputv[i].key);

	snprintf(buffer, sizeof(buffer), "__PMI_preput_val_%i", i);
	envSet(env, buffer, preputv[i].value);
    }
}

static size_t fillWithSrun(SpawnRequest_t *req, PStask_t *task)
{
    const char *srun = getConfValueC(Config, "SRUN_BINARY");
    if (!srun) {
	flog("no SRUN_BINARY provided\n");
	return 0;
    }

    strv_t argV = strvNew(NULL);
    strvAdd(argV, srun);

    /* ensure srun will not hang and wait for resources */
    strvAdd(argV, "--immediate=5");

    /* always use exact the resources requested */
    strvAdd(argV, "--exact");

    /* this is stupid but needed for best slurm compatibility
       actually this removes our default rank binding from the spawned
       processes which is needed, since slurm does none cpu binding by
       default and inherits that to the spawned processes */
    if (!(step->cpuBindType & ( CPU_BIND_NONE | CPU_BIND_TO_BOARDS
				| CPU_BIND_MAP | CPU_BIND_MASK | CPU_BIND_LDMAP
				| CPU_BIND_LDMASK | CPU_BIND_TO_SOCKETS
				| CPU_BIND_TO_LDOMS | CPU_BIND_LDRANK
				| CPU_BIND_RANK | CPU_BIND_TO_THREADS ))) {
	strvAdd(argV, "--cpu-bind=none");
    }

    for (int i = 0; i < req->infoc; i++) {
	KVP_t *info = &(req->infov[i]);

	/* "srunopts" info field is extremely dangerous and only meant for
	 * easier testing purposes during development. It is officially
	 * undocumented and can be removed at any time in the future.
	 * Every option that is found useful using this mechanism should
	 * get it's own info key for in production use. */
	if (strcmp(info->key, "srunopts") == 0) {
	    flog("WARNING: Undocumented feature 'srunopts' used (job): '%s'\n",
		 info->value);
	    /* simply split at blanks */
	    char *srunopts = strdup(info->value);
	    char *ptr = strtok(srunopts, " ");
	    while (ptr) {
		strvAdd(argV, ptr);
		ptr = strtok(NULL, " ");
	    }
	    free(srunopts);
	}
    }

    size_t nTasks = 0;
    for (int s = 0; s < req->num; s++) {
	SingleSpawn_t *spawn = &(req->spawns[s]);

	if (s) {
	    /* separate the next het group */
	    strvAdd(argV, ":");
	    /* reset environment propagation */
	    strvAdd(argV, "--export=NONE");
	}

	/* set the number of processes to spawn */
	strvAdd(argV, "-n");                  // --ntasks=
	snprintf(buffer, sizeof(buffer), "%d", spawn->np);
	strvAdd(argV, buffer);

	/* extract info values and keys
	 *
	 * These info variables are implementation dependend and can
	 * be used for e.g. process placement. All unsupported values
	 * will be silently ignored.
	 *
	 * ParaStation will support:
	 *
	 *  - wdir: The working directory of the spawned processes
	 *  - parricide: The type of nodes to be used
	 *  - path: The directory were to search for the executable
	 *  - tpp: Threads per process
	 *
	 */
	char *srunopts = NULL;
	for (int i = 0; i < spawn->infoc; i++) {
	    KVP_t *info = &(spawn->infov[i]);

	    if (!strcmp(info->key, "wdir")) {
		if (!s) {
		    strvAdd(argV, "-D");          // --chdir=
		    strvAdd(argV, info->value);
		}
	    } else if (!strcmp(info->key, "host")) {
		strvAdd(argV, "-w");          // --nodelist=
		strvAdd(argV, info->value);
	    } else if (!strcmp(info->key, "srunconstraint")) {
		strvAdd(argV, "-C");          // --constraint=
		strvAdd(argV, info->value);
	    } else if (!strcmp(info->key, "parricide")) {
		if (!strcmp(info->value, "disabled")) {
		    task->noParricide = true;
		}
	    /* "srunopts" info field is extremely dangerous (see above) */
	    } else if (!strcmp(info->key, "srunopts")) {
		flog("WARNING: Undocumented feature 'srunopts' used (app %d):"
		     " '%s'\n", s, info->value);
		srunopts = strdup(info->value);
	    } else {
		flog("info key '%s' not supported\n", info->key);
	    }
	}

	char *envArg = strdup("--export=ALL");
	for (char **env = envGetArray(spawn->env); env && *env; env++) {
	    char *eq = strchr(*env, '=');
	    if (!eq || eq == *env) continue;

	    /* append environment to argument */
	    char *tmpArg = PSC_concat(envArg, ",", *env);
	    if (!tmpArg) continue;
	    free(envArg);
	    envArg = tmpArg;

	    /* unset the current environment since it would have precedence */
	    *eq = '\0';
	    unsetenv(*env);
	    *eq = '=';
	}
	strvLink(argV, envArg);

	/* append developer options if passed as last options */
	if (srunopts) {
	    /* simply split at blanks */
	    char *ptr = strtok(srunopts, " ");
	    while (ptr) {
		strvAdd(argV, ptr);
		ptr = strtok(NULL, " ");
	    }
	    free(srunopts);
	}

	strvAppend(argV, spawn->argV);

	nTasks += spawn->np;
    }

    task->argV = argV;

    return nTasks;
}

static bool spawnEnvFilter(const char *envStr)
{
    static bool first = true;
    static char *display = NULL;
    if (first) display = getenv("DISPLAY");
    first = false;

    if (!strncmp(envStr, "SLURM_RLIMIT_", 13)
	|| !strncmp(envStr, "SLURM_UMASK=", 12)
	|| !strncmp(envStr, "PWD=", 4)
	|| (display && !strncmp(envStr, "DISPLAY=", 8))) return false;

    return true;
}

int fillSpawnTaskWithSrun(SpawnRequest_t *req, int usize, PStask_t *task)
{
    if (!step) {
	flog("There is no slurm step. Assuming this is not a slurm job.\n");
	return -1;
    }

    /* *** build environment *** */
    /* add (filtered) step environment */
    envAppend(task->env, step->env, spawnEnvFilter);

    setSlurmConfEnvVar(task->env);

    /* propagate parent TID, logger TID and rank through Slurm */
    char nStr[32];
    snprintf(nStr, sizeof(nStr), "%ld", task->ptid);
    envSet(task->env, "__PSSLURM_SPAWN_PTID", nStr);
    snprintf(nStr, sizeof(nStr), "%ld", task->loggertid);
    envSet(task->env, "__PSSLURM_SPAWN_LTID", nStr);
    snprintf(nStr, sizeof(nStr), "%d", task->rank);
    envSet(task->env, "__PSSLURM_STEP_RANK", nStr);

    /* make sure to use the same PMI */
    char *pmitype = getenv("PSSLURM_PMI_TYPE");
    envSet(task->env, "PSSLURM_PMI_TYPE", pmitype);

    /* put preput key-value-pairs into environment
     *
     * We will transport this kv-pairs using the spawn environment. When
     * our children are starting the pmi_init() call will add them to their
     * local KVS.
     *
     * Only the values of the first single spawn are used. */
    SingleSpawn_t *spawn = &(req->spawns[0]);
    addSpawnPreputToEnv(spawn->preputc, spawn->preputv, task->env);

    size_t jobSize = fillWithSrun(req, task);

    /* update environment */
    pmitype = envGet(task->env, "PSSLURM_PMI_TYPE");
    flog("pmitype is '%s'\n", pmitype);
    if (jobSize && !strcmp(pmitype, "pmix")) {
	char tmp[32];
	snprintf(tmp, sizeof(tmp), "%zu", jobSize);
	envSet(task->env, "PMIX_JOB_SIZE", tmp);
    }

    return jobSize ? 1 : 0;
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet:*/
