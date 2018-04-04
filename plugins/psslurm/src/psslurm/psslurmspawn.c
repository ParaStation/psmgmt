/*
 * ParaStation
 *
 * Copyright (C) 2015-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>

#include "psenv.h"
#include "psserial.h"

#include "pluginmalloc.h"
#include "pluginhelper.h"

#include "slurmcommon.h"
#include "psslurmlog.h"
#include "psslurmauth.h"
#include "psslurmcomm.h"
#include "psslurmspawn.h"
#include "psslurmlog.h"

#define SRUN_BINARY "/usr/bin/srun"

static char buffer[1024];

static Step_t *step = NULL;


void initSpawnFacility(Step_t *jobstep) {
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
    int i;

    snprintf(buffer, sizeof(buffer), "__PMI_preput_num=%i", preputc);
    envPut(env, buffer);

    for (i = 0; i < preputc; i++) {
	snprintf(buffer, sizeof(buffer), "__PMI_preput_key_%i", i);
	envSet(env, buffer, preputv[i].key);

	snprintf(buffer, sizeof(buffer), "__PMI_preput_val_%i", i);
	envSet(env, buffer, preputv[i].value);
    }
}

static int fillCmdForSingleSpawn(SpawnRequest_t *req, int usize,
				 PStask_t *task) {

    int i, argc, maxargc;

    SingleSpawn_t *spawn;
    KVP_t *info;

    if (req->num != 1) {
	return 0;
    }

    /* calc max number of arguments to be passed to srun */
    maxargc = 6; /* "srun --cpu_bind=none --ntasks=<NP> --chdir=<WDIR>
		    --hosts=<HOSTLIST> <BINARY> ... */
    maxargc += req->spawns[0].argc;

    argc = 0;
    task->argv = umalloc(maxargc * sizeof(char *));

    task->argv[argc++] = ustrdup(SRUN_BINARY);

    /* this is stupid but needed for best slurm compatibility
       actually this removes our default rank binding from the spawned
       processes which is needed, since slurm does none cpu binding by
       default and inherits that to the spawned processes */
    if (!(step->cpuBindType & ( CPU_BIND_NONE | CPU_BIND_TO_BOARDS
				| CPU_BIND_MAP | CPU_BIND_MASK | CPU_BIND_LDMAP
				| CPU_BIND_LDMASK | CPU_BIND_TO_SOCKETS
				| CPU_BIND_TO_LDOMS | CPU_BIND_LDRANK
				| CPU_BIND_LDRANK | CPU_BIND_RANK
				| CPU_BIND_TO_THREADS ))) {
        task->argv[argc++] = ustrdup("--cpu_bind=none");
    }

    spawn = &(req->spawns[0]);

    /* set the number of processes to spawn */
    snprintf(buffer, sizeof(buffer), "--ntasks=%d", spawn->np);
    task->argv[argc++] = ustrdup(buffer);

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
    for (i = 0; i < spawn->infoc; i++) {
	info = &(spawn->infov[i]);

	if (strcmp(info->key, "wdir") == 0) {
	    snprintf(buffer, sizeof(buffer), "--chdir=%s", info->value);
	    task->argv[argc++] = ustrdup(buffer);
	}
	else if (strcmp(info->key, "host") == 0) {
	    snprintf(buffer, sizeof(buffer), "--nodelist=%s", info->value);
	    task->argv[argc++] = ustrdup(buffer);
	}
	else {
	    mlog("%s: info key '%s' not supported\n", __func__, info->key);
	}
    }

    for (i = 0; i < spawn->argc; i++) {
	task->argv[argc++] = ustrdup(spawn->argv[i]);
    }

    task->argv[argc] = NULL;
    task->argc = argc;

    return 1;
}

static int fillCmdForMultiSpawn(SpawnRequest_t *req, int usize,
				PStask_t *task) {

    int totalSpawns, i, j, argc, maxargc, ntasks, fd;

    FILE *fs;

    char filebuf[64];

    SingleSpawn_t *spawn;

    totalSpawns = req->num;

    /* create multi-prog file */
    sprintf(filebuf, "/tmp/psslurm-spawn.%d.XXXXXX", getpid());
    fd = mkstemp(filebuf);
    if (fd < 0) {
	mlog("%s: failed to create temporary multi-prog file %s: %s\n",
		__func__, filebuf, strerror(errno));
	return 0;
    }
    fs = fdopen(fd, "w");
    if (!fs) {
	mlog("%s: failed to open stream for temporary multi-prog file %s: %s\n",
		__func__, filebuf, strerror(errno));
	return 0;
    }

    mlog("Writing %d spawn requests to multi-prog file '%s'\n", totalSpawns,
	    filebuf);

    ntasks = 0;
    for (i = 0; i < totalSpawns; i++) {
	spawn = &(req->spawns[i]);

	/* TODO: handle info (slurm ignores too) */
	if (spawn->infoc > 0) {
	    mlog("Spawn info not supported for spawn multiple binaries.\n");
	}

	if (spawn->np == 1) {
		fprintf(fs, "%d ", ntasks);
	} else {
		fprintf(fs, "%d-%d ", ntasks, ntasks + spawn->np - 1);
	}

	for (j = 0; j < spawn->argc; j++) {
		fprintf(fs, " %s", spawn->argv[j]);
	}
	fprintf(fs, "\n");

	ntasks += spawn->np;
    }
    fclose(fs);

    /* calc max number of arguments to be passed to srun */
    maxargc = 5; /* "srun --cpu_bind=none --ntasks=<NP> --multi-prog <FILE> */

    argc = 0;
    task->argv = umalloc(maxargc * sizeof(char *));

    task->argv[argc++] = ustrdup(SRUN_BINARY);

    /* this is stupid but needed for best slurm compatibility
       actually this removes our default rank binding from the spawned
       processes which is needed, since slurm does none cpu binding by
       default and inherits that to the spawned processes */
    if (!(step->cpuBindType & ( CPU_BIND_NONE | CPU_BIND_TO_BOARDS
				| CPU_BIND_MAP | CPU_BIND_MASK | CPU_BIND_LDMAP
				| CPU_BIND_LDMASK | CPU_BIND_TO_SOCKETS
				| CPU_BIND_TO_LDOMS | CPU_BIND_LDRANK
				| CPU_BIND_LDRANK | CPU_BIND_RANK
				| CPU_BIND_TO_THREADS ))) {
        task->argv[argc++] = ustrdup("--cpu_bind=none");
    }

    /* set the number of processes to spawn */
    snprintf(buffer, sizeof(buffer), "--ntasks=%d", ntasks);
    task->argv[argc++] = ustrdup(buffer);

    task->argv[argc++] = ustrdup("--multi-prog");
    task->argv[argc++] = ustrdup(filebuf);
    task->argv[argc] = NULL;
    task->argc = argc;

    return 1;
}
/*
 *  fills the passed task structure to spawn processes using srun
 *
 *  @param req    spawn request
 *
 *  @param usize  universe size
 *
 *  @param task   task structure to adjust
 *
 *  @return 1 on success, 0 on error, -1 on not responsible
 */
int fillSpawnTaskWithSrun(SpawnRequest_t *req, int usize, PStask_t *task) {

    size_t i, totalSpawns;
    char *display;

    env_t newenv;

    SingleSpawn_t *spawn;

    if (step == NULL) {
	mlog("%s: There is no slurm step. Assuming this is not a slurm job.\n",
		__func__);
	return -1;
    }

    totalSpawns = req->num;

    /* *** build environment *** */
    envInit(&newenv);

    /* start with existing task environment */
    for (i=0; task->environ[i] != NULL; i++) {
	envPut(&newenv, task->environ[i]);
    }

    /* add step environment */

    /* we need the DISPLAY variable set by psslurm */
    display = getenv("DISPLAY");

    for (i=0; i < step->env.cnt; i++) {
	if (!(strncmp(step->env.vars[i], "SLURM_RLIMIT_", 13))) continue;
	if (!(strncmp(step->env.vars[i], "SLURM_UMASK=", 12))) continue;
	if (!(strncmp(step->env.vars[i], "PWD=", 4))) continue;
	if (display &&
	    !(strncmp(step->env.vars[i], "DISPLAY=", 8))) continue;
	envPut(&newenv, step->env.vars[i]);
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
    spawn = &(req->spawns[0]);
    addSpawnPreputToEnv(spawn->preputc, spawn->preputv, &newenv);

    /* replace task environment */
    for (i=0; task->environ[i] != NULL; i++) {
	ufree(task->environ[i]);
    }
    ufree(task->environ);
    task->environ = newenv.vars;
    task->envSize = newenv.cnt;
    task->environ = urealloc(task->environ,
				(task->envSize + 1) * sizeof(char *));
    task->environ[task->envSize] = NULL;

    if (totalSpawns == 1) {
	return fillCmdForSingleSpawn(req, usize, task);
    }
    else {
	return fillCmdForMultiSpawn(req, usize, task);
    }
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet:*/
