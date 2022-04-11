/*
 * ParaStation
 *
 * Copyright (C) 2015-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmspawn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "psenv.h"

#include "pluginmalloc.h"
#include "pluginstrv.h"

#include "slurmcommon.h"
#include "psslurmconfig.h"
#include "psslurmlog.h"

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
static void addSpawnPreputToEnv(int preputc, KVP_t *preputv, env_t *env)
{
    snprintf(buffer, sizeof(buffer), "__PMI_preput_num=%i", preputc);
    envPut(env, buffer);

    for (int i = 0; i < preputc; i++) {
	snprintf(buffer, sizeof(buffer), "__PMI_preput_key_%i", i);
	envSet(env, buffer, preputv[i].key);

	snprintf(buffer, sizeof(buffer), "__PMI_preput_val_%i", i);
	envSet(env, buffer, preputv[i].value);
    }
}

static int fillCmdForSingleSpawn(SpawnRequest_t *req, int usize,
				 PStask_t *task)
{
    if (req->num != 1) return 0; // ensure to only handle single spawns

    strv_t argV;
    strvInit(&argV, NULL, 0);

    const char *srun = getConfValueC(&Config, "SRUN_BINARY");
    if (!srun) {
	flog("no SRUN_BINARY provided\n");
	return 0;
    }
    strvAdd(&argV, ustrdup(srun));

    /* ensure srun will not hang and wait for resources */
    strvAdd(&argV, ustrdup("--immediate=5"));

    /* this is stupid but needed for best slurm compatibility
       actually this removes our default rank binding from the spawned
       processes which is needed, since slurm does none cpu binding by
       default and inherits that to the spawned processes */
    if (!(step->cpuBindType & ( CPU_BIND_NONE | CPU_BIND_TO_BOARDS
				| CPU_BIND_MAP | CPU_BIND_MASK | CPU_BIND_LDMAP
				| CPU_BIND_LDMASK | CPU_BIND_TO_SOCKETS
				| CPU_BIND_TO_LDOMS | CPU_BIND_LDRANK
				| CPU_BIND_RANK | CPU_BIND_TO_THREADS ))) {
	strvAdd(&argV, ustrdup("--cpu_bind"));
	strvAdd(&argV, ustrdup("none"));
    }

    SingleSpawn_t *spawn = &(req->spawns[0]);

    /* set the number of processes to spawn */
    strvAdd(&argV, ustrdup("--ntasks"));
    snprintf(buffer, sizeof(buffer), "%d", spawn->np);
    strvAdd(&argV, ustrdup(buffer));

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
	    strvAdd(&argV, ustrdup("--chdir"));
	    strvAdd(&argV, ustrdup(info->value));
	} else if (strcmp(info->key, "host") == 0) {
	    strvAdd(&argV, ustrdup("--nodelist"));
	    strvAdd(&argV, ustrdup(info->value));
	} else {
	    flog("info key '%s' not supported\n", info->key);
	}
    }

    for (int i = 0; i<spawn->argc; i++) strvAdd(&argV, ustrdup(spawn->argv[i]));

    task->argc = argV.count;
    task->argv = argV.strings;

    return 1;
}

static int fillCmdForMultiSpawn(SpawnRequest_t *req, int usize,
				PStask_t *task)
{
    /* create multi-prog file */
    char filebuf[64];
    sprintf(filebuf, "/tmp/psslurm-spawn.%d.XXXXXX", getpid());
    int fd = mkstemp(filebuf);
    if (fd < 0) {
	mlog("%s: failed to create temporary multi-prog file %s: %s\n",
		__func__, filebuf, strerror(errno));
	return 0;
    }
    FILE *fs = fdopen(fd, "w");
    if (!fs) {
	mlog("%s: failed to open stream for temporary multi-prog file %s: %s\n",
		__func__, filebuf, strerror(errno));
	return 0;
    }

    int totalSpawns = req->num;
    mlog("Writing %d spawn requests to multi-prog file '%s'\n", totalSpawns,
	    filebuf);

    int ntasks = 0;
    for (int i = 0; i < totalSpawns; i++) {
	SingleSpawn_t *spawn = &(req->spawns[i]);

	/* TODO: handle info (slurm ignores too) */
	if (spawn->infoc > 0) {
	    mlog("Spawn info not supported for spawn multiple binaries.\n");
	}

	if (spawn->np == 1) {
	    fprintf(fs, "%d ", ntasks);
	} else {
	    fprintf(fs, "%d-%d ", ntasks, ntasks + spawn->np - 1);
	}

	for (int j = 0; j < spawn->argc; j++) {
	    fprintf(fs, " %s", spawn->argv[j]);
	}
	fprintf(fs, "\n");

	ntasks += spawn->np;
    }
    fclose(fs);

    strv_t argV;
    strvInit(&argV, NULL, 0);

    const char *srun = getConfValueC(&Config, "SRUN_BINARY");
    if (!srun) {
	flog("no SRUN_BINARY provided\n");
	return 0;
    }
    strvAdd(&argV, ustrdup(srun));

    /* ensure srun will not hang and wait for resources */
    strvAdd(&argV, ustrdup("--immediate=5"));

    /* this is stupid but needed for best slurm compatibility
       actually this removes our default rank binding from the spawned
       processes which is needed, since slurm does none cpu binding by
       default and inherits that to the spawned processes */
    if (!(step->cpuBindType & ( CPU_BIND_NONE | CPU_BIND_TO_BOARDS
				| CPU_BIND_MAP | CPU_BIND_MASK | CPU_BIND_LDMAP
				| CPU_BIND_LDMASK | CPU_BIND_TO_SOCKETS
				| CPU_BIND_TO_LDOMS | CPU_BIND_LDRANK
				| CPU_BIND_RANK | CPU_BIND_TO_THREADS ))) {
	strvAdd(&argV, ustrdup("--cpu_bind"));
	strvAdd(&argV, ustrdup("none"));
    }

    /* set the number of processes to spawn */
    strvAdd(&argV, ustrdup("--ntasks"));
    snprintf(buffer, sizeof(buffer), "%d", ntasks);
    strvAdd(&argV, ustrdup(buffer));

    strvAdd(&argV, ustrdup("--multi-prog"));
    strvAdd(&argV, ustrdup(filebuf));

    task->argc = argV.count;
    task->argv = argV.strings;

    return 1;
}

int fillSpawnTaskWithSrun(SpawnRequest_t *req, int usize, PStask_t *task)
{
    if (!step) {
	mlog("%s: There is no slurm step. Assuming this is not a slurm job.\n",
		__func__);
	return -1;
    }

    /* *** build environment *** */
    env_t newenv;
    envInit(&newenv);

    /* start with existing task environment */
    for (size_t i=0; task->environ[i] != NULL; i++) {
	envPut(&newenv, task->environ[i]);
    }

    /* add step environment */

    /* we need the DISPLAY variable set by psslurm */
    char *display = getenv("DISPLAY");

    for (size_t i=0; i < step->env.cnt; i++) {
	if (!(strncmp(step->env.vars[i], "SLURM_RLIMIT_", 13))) continue;
	if (!(strncmp(step->env.vars[i], "SLURM_UMASK=", 12))) continue;
	if (!(strncmp(step->env.vars[i], "PWD=", 4))) continue;
	if (display &&
	    !(strncmp(step->env.vars[i], "DISPLAY=", 8))) continue;
	envPut(&newenv, step->env.vars[i]);
    }

    char *confServer = getConfValueC(&Config, "SLURM_CONF_SERVER");
    if (confServer && strcmp(confServer, "none")) {
	/* ensure the configuration cache is used */
	envSet(&newenv, "SLURM_CONF", getConfValueC(&Config, "SLURM_CONF"));
    }

    /* XXX: Do we need to set further variables as in setRankEnv()
     *      in psslurmforwarder.c? */

    /* put preput key-value-pairs into environment
     *
     * We will transport this kv-pairs using the spawn environment. When
     * our children are starting the pmi_init() call will add them to their
     * local KVS.
     *
     * Only the values of the first single spawn are used. */
    SingleSpawn_t *spawn = &(req->spawns[0]);
    addSpawnPreputToEnv(spawn->preputc, spawn->preputv, &newenv);

    /* replace task environment */
    for (size_t i = 0; task->environ[i] != NULL; i++) ufree(task->environ[i]);
    ufree(task->environ);
    task->environ = newenv.vars;
    task->envSize = newenv.cnt;

    if (req->num == 1) {
	return fillCmdForSingleSpawn(req, usize, task);
    } else {
	return fillCmdForMultiSpawn(req, usize, task);
    }
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet:*/
