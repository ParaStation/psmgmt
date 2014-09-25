/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#include "env.h"
#include "slurmcommon.h"
#include "pluginmalloc.h"
#include "psslurmconfig.h"
#include "psslurmjob.h"
#include "psslurmlog.h"

#include "psslurmenv.h"

/**
 * @brief
 *
 * env set by slurmctld:
 *
 * SLURM_JOB_NAME=batchjob
 * SLURM_PRIO_PROCESS=0
 * SLURM_SUBMIT_DIR=/direct/home-fs/rauh
 * SLURM_SUBMIT_HOST=prosciutto
 *
 */
void setSlurmEnv(Job_t *job)
{
    env_fields_t env;
    char tmp[1024];

    env.vars = job->env;
    env.cnt = env.size = job->envc;

    /* MISSING BATCH VARS:
     *
     * maybe from topo plugin??
     *
     * SLURM_TOPOLOGY_ADDR=j3c053
     * SLURM_TOPOLOGY_ADDR_PATTERN=node
     *
     * SLURM_TASKS_PER_NODE=32
     * SLURM_CPUS_ON_NODE=32
     * SLURM_JOB_CPUS_PER_NODE=32
     * SLURM_GTIDS=0
     * */

    if (job->np) {
	snprintf(tmp, sizeof(tmp), "%u", job->np);
	env_set(&env, "SLURM_NTASKS", tmp);
	env_set(&env, "SLURM_NPROCS", tmp);
    }

    if (job->partition) env_set(&env, "SLURM_JOB_PARTITION", job->partition);

    env_set(&env, "SLURMD_NODENAME", getConfValueC(&Config, "SLURM_HOSTNAME"));

    env_set(&env, "SLURM_JOBID", job->id);
    env_set(&env, "SLURM_JOB_ID", job->id);

    snprintf(tmp, sizeof(tmp), "%u", job->nrOfNodes);
    env_set(&env, "SLURM_JOB_NUM_NODES", tmp);
    env_set(&env, "SLURM_NNODES", tmp);

    if (job->arrayTaskId != NO_VAL) {
	snprintf(tmp, sizeof(tmp), "%u", job->arrayJobId);
	env_set(&env, "SLURM_ARRAY_JOB_ID", tmp);

	snprintf(tmp, sizeof(tmp), "%u", job->arrayTaskId);
	env_set(&env, "SLURM_ARRAY_TASK_ID", tmp);
    }

    env_set(&env, "SLURM_NODELIST", job->slurmNodes);
    env_set(&env, "SLURM_JOB_NODELIST", job->slurmNodes);
    env_set(&env, "SLURM_CHECKPOINT_IMAGE_DIR", job->checkpoint);

    if (!job->nodeAlias || !strlen(job->nodeAlias)) {
	env_set(&env, "SLURM_NODE_ALIASES", "(null)");
    } else {
	env_set(&env, "SLURM_NODE_ALIASES", job->nodeAlias);
    }

    if (job->hostname) {
	mlog("%s: set hostname:%s\n", __func__, job->hostname);
	env_set(&env, "HOSTNAME", job->hostname);
    }

    job->env = env.vars;
    job->envc = env.cnt;
}

void setRankEnv(Step_t *step)
{
    char tmp[128];

    setenv("SLURMD_NODENAME", getConfValueC(&Config, "SLURM_HOSTNAME"), 1);
    snprintf(tmp, sizeof(tmp), "%u", getpid());
    setenv("SLURM_TASK_PID", tmp, 1);
    setenv("SLURM_CPUS_ON_NODE", getConfValueC(&Config, "SLURM_CPUS"), 1);
    // TODO: "SLURM_NODEID"
}

void setTaskEnv(Step_t *step)
{
    char *val, tmp[128];
    mode_t slurmUmask;

    env_fields_t env;
    //char tmp[1024];

    env.vars = step->env;
    env.cnt = env.size = step->envc;

    snprintf(tmp, sizeof(tmp), "%u", step->tpp);
    env_set(&env, "SLURM_CPUS_PER_TASK", tmp);

    env_set(&env, "SLURM_LAUNCH_NODE_IPADDR", inet_ntoa(step->srun.sin_addr));
    env_set(&env, "SLURM_SRUN_COMM_HOST", inet_ntoa(step->srun.sin_addr));
    //env_set(&env, "PSI_LOGGERDEBUG", "1");
    //env_set(&env, "PSI_FORWARDERDEBUG", "1");
    env_set(&env, "__SLURM_INFORM_TIDS", "1");

    /* forward overbook mode */
    if ((val = env_get(&env, "SLURM_OVERCOMMIT"))) {
	if (!strcmp(val, "1")) {
	    env_set(&env, "PSI_OVERBOOK", "1");
	}
    }

    /* unbuffered (raw I/O) mode */
    if (!step->bufferedIO) {
	env_set(&env, "__PSI_RAW_IO", "1");
	env_set(&env, "PSI_LOGGER_UNBUFFER", "1");
    }

    /*
    snprintf(tmp, sizeof(tmp), "%u", step->nodes[0]);
    env_set(&env, "PSI_NODES", tmp);
    */

    if (!step->pty) env_set(&env, "PSI_INPUTDEST", "all");

    /* set slurm umask */
    if ((val = env_get(&env, "SLURM_UMASK"))) {
	slurmUmask = strtol(val, NULL, 8);
	umask(slurmUmask);
	env_unset(&env, "SLURM_UMASK");
	env_set(&env, "__PSI_UMASK", val);
    }

    env_unset(&env, "SLURM_MPI_TYPE");
    env_set(&env, "SLURM_JOB_USER", step->username);
    snprintf(tmp, sizeof(tmp), "%u", step->uid);
    env_set(&env, "SLURM_JOB_UID", tmp);

    step->env = env.vars;
    step->envc = env.cnt;
}

/*
     * depending on the option we need to call "su - username /usr/bin/env" and
     * parse the output. This will be set first and can be overwritten by env
     * which comes from slurmd, or is slurmd doing that for us? */
void setBatchEnv(Job_t *job)
{
    env_fields_t env;
    char tmp[1024], *val = NULL;
    mode_t slurmUmask;

    env.vars = job->env;
    env.cnt = env.size = job->envc;

    env_set(&env, "ENVIRONMENT", "BATCH");
    env_set(&env, "SLURM_NODEID", "0");
    env_set(&env, "SLURM_PROCID", "0");
    env_set(&env, "SLURM_LOCALID", "0");

    /* CORRECT ME */
    /*
    snprintf(tmp, sizeof(tmp), "%u", job->nodeMinMemory);
    env_set(&env, "SLURM_MEM_PER_NODE", tmp);
    */

    snprintf(tmp, sizeof(tmp), "%u", getpid());
    env_set(&env, "SLURM_TASK_PID", tmp);

    /* forward overbook mode */
    val = env_get(&env, "SLURM_OVERCOMMIT");

    if (job->overcommit || (val && !strcmp(val, "1"))) {
	env_set(&env, "PSI_OVERBOOK", "1");
    }

    /* set slurm umask */
    if ((val = env_get(&env, "SLURM_UMASK"))) {
	slurmUmask = strtol(val, NULL, 8);
	umask(slurmUmask);
	env_unset(&env, "SLURM_UMASK");
    }

    job->env = env.vars;
    job->envc = env.cnt;
}

char *getValueFromEnv(char **origEnv, uint32_t envc, char *name)
{
    env_fields_t env;
    char *val;

    env.vars = origEnv;
    env.cnt = env.size = envc;

    if ((val = env_get(&env, name))) return val;
    return NULL;
}

int getUint32FromEnv(char **origEnv, uint32_t envc, char *name, uint32_t *val)
{
    env_fields_t env;
    char *valc;

    env.vars = origEnv;
    env.cnt = env.size = envc;

    if ((valc = env_get(&env, name))) {
	if ((sscanf(valc, "%u", val)) == 1) return 1;
	return 0;
    }
    return 0;
}
