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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "psenv.h"

#include "pluginconfig.h"
#include "pluginstrv.h"

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

    const char *srun = getConfValueC(Config, "SRUN_BINARY");
    if (!srun) {
	flog("no SRUN_BINARY provided\n");
	return 0;
    }
    strvAdd(&argV, srun);

    /* ensure srun will not hang and wait for resources */
    strvAdd(&argV, "--immediate=5");

    /* always use exact the resources requested */
    strvAdd(&argV, "--exact");

    /* this is stupid but needed for best slurm compatibility
       actually this removes our default rank binding from the spawned
       processes which is needed, since slurm does none cpu binding by
       default and inherits that to the spawned processes */
    if (!(step->cpuBindType & ( CPU_BIND_NONE | CPU_BIND_TO_BOARDS
				| CPU_BIND_MAP | CPU_BIND_MASK | CPU_BIND_LDMAP
				| CPU_BIND_LDMASK | CPU_BIND_TO_SOCKETS
				| CPU_BIND_TO_LDOMS | CPU_BIND_LDRANK
				| CPU_BIND_RANK | CPU_BIND_TO_THREADS ))) {
	strvAdd(&argV, "--cpu-bind=none");
    }

    SingleSpawn_t *spawn = &(req->spawns[0]);

    /* set the number of processes to spawn */
    strvAdd(&argV, "-n");                  // --ntasks=
    snprintf(buffer, sizeof(buffer), "%d", spawn->np);
    strvAdd(&argV, buffer);

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
	    strvAdd(&argV, "-D");          // --chdir=
	    strvAdd(&argV, info->value);
	} else if (strcmp(info->key, "host") == 0) {
	    strvAdd(&argV, "-w");          // --nodelist=
	    strvAdd(&argV, info->value);
	} else {
	    flog("info key '%s' not supported\n", info->key);
	}
    }

    for (int i = 0; i<spawn->argc; i++) strvAdd(&argV, spawn->argv[i]);

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

    const char *srun = getConfValueC(Config, "SRUN_BINARY");
    if (!srun) {
	flog("no SRUN_BINARY provided\n");
	return 0;
    }
    strvAdd(&argV, srun);

    /* ensure srun will not hang and wait for resources */
    strvAdd(&argV, "--immediate=5");

    /* always use exact the resources requested */
    strvAdd(&argV, "--exact");

    /* this is stupid but needed for best slurm compatibility
       actually this removes our default rank binding from the spawned
       processes which is needed, since slurm does none cpu binding by
       default and inherits that to the spawned processes */
    if (!(step->cpuBindType & ( CPU_BIND_NONE | CPU_BIND_TO_BOARDS
				| CPU_BIND_MAP | CPU_BIND_MASK | CPU_BIND_LDMAP
				| CPU_BIND_LDMASK | CPU_BIND_TO_SOCKETS
				| CPU_BIND_TO_LDOMS | CPU_BIND_LDRANK
				| CPU_BIND_RANK | CPU_BIND_TO_THREADS ))) {
	strvAdd(&argV, "--cpu-bind=none");
    }

    /* set the number of processes to spawn */
    strvAdd(&argV, "-n");                  // --ntasks=
    snprintf(buffer, sizeof(buffer), "%d", ntasks);
    strvAdd(&argV, buffer);

    strvAdd(&argV, "--multi-prog");
    strvAdd(&argV, filebuf);

    task->argc = argV.count;
    task->argv = argV.strings;

    return 1;
}

int fillSpawnTaskWithSrun(SpawnRequest_t *req, int usize, PStask_t *task)
{
    if (!step) {
	flog("There is no slurm step. Assuming this is not a slurm job.\n");
	return -1;
    }

    /* *** build environment *** */
    // can use task->environ since it will be replaced later anyhow
    env_t env = envNew(task->environ);

    /* add (filtered) step environment */

    /* we need the DISPLAY variable set by psslurm */
    char *display = getenv("DISPLAY");

    for (size_t i = 0; i < envSize(step->env); i++) {
	char *thisEnv = envDumpIndex(step->env, i);
	if (!strncmp(thisEnv, "SLURM_RLIMIT_", 13)) continue;
	if (!strncmp(thisEnv, "SLURM_UMASK=", 12)) continue;
	if (!strncmp(thisEnv, "PWD=", 4)) continue;
	if (display && !strncmp(thisEnv, "DISPLAY=", 8)) continue;
	envPut(env, thisEnv);
    }

    setSlurmConfEnvVar(env);

    /* propagate parent TID, logger TID and rank through Slurm */
    char nStr[32];
    snprintf(nStr, sizeof(nStr), "%d", task->ptid);
    envSet(env, "__PSSLURM_SPAWN_PTID", nStr);
    snprintf(nStr, sizeof(nStr), "%d", task->loggertid);
    envSet(env, "__PSSLURM_SPAWN_LTID", nStr);
    snprintf(nStr, sizeof(nStr), "%d", task->rank - 1);
    envSet(env, "__PSSLURM_SPAWN_RANK", nStr);

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
    addSpawnPreputToEnv(spawn->preputc, spawn->preputv, env);

    /* replace task environment */
    task->envSize = envSize(env);
    task->environ = envStealArray(env);

    if (req->num == 1) {
	return fillCmdForSingleSpawn(req, usize, task);
    } else {
	return fillCmdForMultiSpawn(req, usize, task);
    }
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet:*/
