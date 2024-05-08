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
#include <errno.h>
#include <unistd.h>

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

static size_t fillCmdForSingleSpawn(SpawnRequest_t *req, PStask_t *task)
{
    if (req->num != 1) return 0; // ensure to only handle single spawns

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

    SingleSpawn_t *spawn = &(req->spawns[0]);

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
     *  - arch/nodetype: The type of nodes to be used
     *  - path: The directory were to search for the executable
     *  - tpp: Threads per process
     *
     * TODO:
     *
     *  - soft:
     *	    - if not all processes could be started the spawn is still
     *		considered successful
     *	    - ignore negative values and >maxproc
     *	    - format = list of triplets
     *		+ list 1,3,4,5,8
     *		+ triplets a:b:c where a:b range c= multiplicator
     *		+ 0:8:2 means 0,2,4,6,8
     *		+ 0:8:2,7 means 0,2,4,6,7,8
     *		+ 0:2 means 0,1,2
     */
    for (int i = 0; i < spawn->infoc; i++) {
	KVP_t *info = &(spawn->infov[i]);

	if (strcmp(info->key, "wdir") == 0) {
	    strvAdd(argV, "-D");          // --chdir=
	    strvAdd(argV, info->value);
	} else if (strcmp(info->key, "host") == 0) {
	    strvAdd(argV, "-w");          // --nodelist=
	    strvAdd(argV, info->value);
	} else if (strcmp(info->key, "parricide") == 0) {
	    if (!strcmp(info->value, "disabled")) {
		task->noParricide = true;
	    }
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

    for (int a = 0; a < spawn->argc; a++) strvAdd(argV, spawn->argv[a]);

    task->argV = argV;

    return spawn->np;
}

static int fillCmdForMultiSpawn(SpawnRequest_t *req, PStask_t *task)
{
    const char *srun = getConfValueC(Config, "SRUN_BINARY");
    if (!srun) {
	flog("no SRUN_BINARY provided\n");
	return 0;
    }

    /* create multi-prog file */
    char filebuf[64];
    sprintf(filebuf, "/tmp/psslurm-spawn.%d.XXXXXX", getpid());
    int fd = mkstemp(filebuf);
    if (fd < 0) {
	fwarn(errno, "failed to create temporary multi-prog file %s", filebuf);
	return 0;
    }
    FILE *fs = fdopen(fd, "w");
    if (!fs) {
	fwarn(errno, "no stream for multi-prog file %s", filebuf);
	return 0;
    }

    int numApps = req->num;
    flog("writing %d applications to multi-prog file '%s'\n", numApps, filebuf);

    size_t nTasks = 0;
    for (int i = 0; i < numApps; i++) {
	SingleSpawn_t *spawn = &(req->spawns[i]);

	/* TODO: handle info (slurm ignores too) */
	if (spawn->infoc > 0) {
	    flog("spawn info not supported for spawn multiple binaries.\n");
	}

	if (spawn->np == 1) {
	    fprintf(fs, "%zd", nTasks);
	} else {
	    fprintf(fs, "%zd-%zd", nTasks, nTasks + spawn->np - 1);
	}

	if (envSize(spawn->env)) fprintf(fs, " env");
	for (char **env = envGetArray(spawn->env); env && *env; env++) {
	    char *eq = strchr(*env, '=');
	    if (!eq || eq == *env) continue;

	    /* prepend environment to app */
	    fprintf(fs, " %s", *env);
	}

	for (int j = 0; j < spawn->argc; j++) fprintf(fs, " %s", spawn->argv[j]);
	fprintf(fs, "\n");

	nTasks += spawn->np;
    }
    fclose(fs);

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

    /* set the number of processes to spawn */
    strvAdd(argV, "-n");                  // --ntasks=
    snprintf(buffer, sizeof(buffer), "%zd", nTasks);
    strvAdd(argV, buffer);

    strvAdd(argV, "--multi-prog");
    strvAdd(argV, filebuf);

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
    snprintf(nStr, sizeof(nStr), "%d", task->ptid);
    envSet(task->env, "__PSSLURM_SPAWN_PTID", nStr);
    snprintf(nStr, sizeof(nStr), "%d", task->loggertid);
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

    size_t jobSize;
    if (req->num == 1) {
	jobSize = fillCmdForSingleSpawn(req, task);
    } else {
	jobSize = fillCmdForMultiSpawn(req, task);
    }

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
