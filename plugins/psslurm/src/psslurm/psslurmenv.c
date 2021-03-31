/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#include "psidnodes.h"
#include "psipartition.h"

#include "pluginmalloc.h"

#include "slurmcommon.h"
#include "psslurmconfig.h"
#include "psslurmlog.h"
#include "psslurmproto.h"
#include "psslurmpin.h"
#include "psslurmalloc.h"

#include "psslurmenv.h"

char **envFilter = NULL;

#define GPU_VARIABLE_MAXLEN 20
static char * gpu_variables[] = {
    "CUDA_VISIBLE_DEVICES", /* Nvidia GPUs */
    "GPU_DEVICE_ORDINAL",   /* AMD GPUs */
    NULL
};

bool initEnvFilter(void)
{
    char *conf, *dup, *next, *saveptr;
    const char delimiters[] =",\n";
    uint32_t count = 0, index = 0;

    if (!(conf = getConfValueC(&Config, "PELOGUE_ENV_FILTER"))) {
	mlog("%s: invalid PELOGUE_ENV_FILTER config option", __func__);
	return false;
    }

    dup = ustrdup(conf);
    next = strtok_r(dup, delimiters, &saveptr);
    while (next) {
	count++;
	next = strtok_r(NULL, delimiters, &saveptr);
    }

    envFilter = (char **) umalloc(sizeof(char *) * count+1);

    strcpy(dup, conf);
    next = strtok_r(dup, delimiters, &saveptr);
    while (next) {
	envFilter[index++] = ustrdup(next);
	next = strtok_r(NULL, delimiters, &saveptr);
    }
    envFilter[index] = NULL;
    ufree(dup);

    return true;
}

void freeEnvFilter(void)
{
    char *ptr;
    uint32_t index = 0;

    while ((ptr = envFilter[index])) {
	ufree(envFilter[index++]);
    }
    ufree(envFilter);
}

/**
 * @brief Calculate the number of CPUs per node
 *
 * @param job The job to calculate the number of CPUs
 *
 * @return Returns a comma separated list representing the
 * number of CPUs per node
 */
static char *getCPUsPerNode(Job_t *job)
{
    char *buffer = NULL;
    size_t bufSize = 0;
    uint32_t i;
    char tmp[256];

    for (i=0; i<job->cpuGroupCount; i++) {
	if (i>0) str2Buf(",", &buffer, &bufSize);

	if (job->cpuCountReps[i] > 1) {
	    snprintf(tmp, sizeof(tmp), "%u(x%u)", job->cpusPerNode[i],
			job->cpuCountReps[i]);
	} else {
	    snprintf(tmp, sizeof(tmp), "%u", job->cpusPerNode[i]);
	}
	str2Buf(tmp, &buffer, &bufSize);
    }
    return buffer;
}

/**
 * @brief calculates the tasks per node
 *
 * We need this to set SLURM_TASKS_PER_NODE in sbatch environment
 * since we do not have this information provided by slurm.
 *
 * The returned array has to be freed using ufree()
 *
 * @param job The job to calculate the tasks per node
 *
 * @return In case of an error, NULL is returned
 */
static uint16_t *calcTasksPerNode(Job_t *job)
{
    uint32_t N, n, i;
    uint16_t *tasksPerNode;

    N = job->nrOfNodes;
    n = job->np;

    if (N == 0 || n == 0) {
	return NULL;
    }

    tasksPerNode = umalloc(N * sizeof(tasksPerNode));

    for (i = 0; i < N; i++) {
	tasksPerNode[i] = n / N + ((i < (n % N)) ? 1 : 0);
    }
    return tasksPerNode;
}

/**
 * @brief create string for SLURM_TASKS_PER_NODE
 *
 * @param tasksPerNode array with the number of tasks for each node
 *
 * @param nrOfNodes    number of nodes, length of @a tasksPerNode
 *
 * @param str          pointer to an allocated string
 *
 * @param strsize      length of @a str
 *
 * @return Returns the requested tasks per node as comma separated string
 * or NULL on error
 */
static char *getTasksPerNode(uint16_t tasksPerNode[], uint32_t nrOfNodes)
{
    char *buffer = NULL;
    size_t bufSize = 0;
    uint32_t i;
    uint16_t current, last, count;
    char tmp[21];

    if (nrOfNodes == 0) return NULL;

    count = 0;
    current = 0;
    last = tasksPerNode[0]; /* for loop initialization */
    for (i = 0; i <= nrOfNodes; i++) {
	if (i != nrOfNodes) { /* don't do this in the last iteration */
	    current = tasksPerNode[i];
	    if (current == last) {
		count++;
		continue;
	    }
	}

	if (count == 1) {
	    snprintf(tmp, sizeof(tmp), "%u,", last);
	} else {
	    snprintf(tmp, sizeof(tmp), "%u(x%d),", last, count);
	}
	str2Buf(tmp, &buffer, &bufSize);

	last = current;
	count = 1;
    }
    buffer[strlen(buffer)-1] = '\0'; //override last comma
    return buffer;
}

void initJobEnv(Job_t *job)
{
    char tmp[1024], *cpus = NULL, *list = NULL;
    Gres_Cred_t *gres;
    size_t listSize = 0;
    uint16_t *tasksPerNode;

    /* MISSING BATCH VARS:
     *
     * from topology plugin
     *
     * SLURM_TOPOLOGY_ADDR=j3c053
     * SLURM_TOPOLOGY_ADDR_PATTERN=node
     * */

    if (job->partition) {
	envSet(&job->env, "SLURM_JOB_PARTITION", job->partition);
    }

    envSet(&job->env, "SLURMD_NODENAME",
		getConfValueC(&Config, "SLURM_HOSTNAME"));

    envSet(&job->env, "SLURM_JOBID", strJobID(job->jobid));
    envSet(&job->env, "SLURM_JOB_ID", strJobID(job->jobid));

    snprintf(tmp, sizeof(tmp), "%u", job->nrOfNodes);
    envSet(&job->env, "SLURM_JOB_NUM_NODES", tmp);
    envSet(&job->env, "SLURM_NNODES", tmp);
    envSet(&job->env, "SLURM_GTIDS", "0");
    envSet(&job->env, "SLURM_JOB_USER", job->username);
    snprintf(tmp, sizeof(tmp), "%u", job->uid);
    envSet(&job->env, "SLURM_JOB_UID", tmp);
    envSet(&job->env, "SLURM_CPUS_ON_NODE",
		getConfValueC(&Config, "SLURM_CPUS"));

    cpus = getCPUsPerNode(job);
    envSet(&job->env, "SLURM_JOB_CPUS_PER_NODE", cpus);
    ufree(cpus);

    /* set SLURM_TASKS_PER_NODE for intel mpi */
    tasksPerNode = calcTasksPerNode(job);
    if (tasksPerNode) {
	cpus = getTasksPerNode(tasksPerNode, job->nrOfNodes);
	ufree(tasksPerNode);
    } else {
	cpus = getCPUsPerNode(job);
    }
    envSet(&job->env, "SLURM_TASKS_PER_NODE", cpus);
    ufree(cpus);

    if (job->arrayTaskId != NO_VAL) {
	snprintf(tmp, sizeof(tmp), "%u", job->arrayJobId);
	envSet(&job->env, "SLURM_ARRAY_JOB_ID", tmp);

	snprintf(tmp, sizeof(tmp), "%u", job->arrayTaskId);
	envSet(&job->env, "SLURM_ARRAY_TASK_ID", tmp);
    }

    envSet(&job->env, "SLURM_NODELIST", job->slurmHosts);
    envSet(&job->env, "SLURM_JOB_NODELIST", job->slurmHosts);
    envSet(&job->env, "SLURM_CHECKPOINT_IMAGE_DIR", job->checkpoint);

    if (!job->nodeAlias || !strlen(job->nodeAlias)) {
	envSet(&job->env, "SLURM_NODE_ALIASES", "(null)");
    } else {
	envSet(&job->env, "SLURM_NODE_ALIASES", job->nodeAlias);
    }

    if (job->hostname) {
	envSet(&job->env, "HOSTNAME", job->hostname);
    }

    /* GRes "gpu" plugin */
    gres = findGresCred(&job->gresList, GRES_PLUGIN_GPU, GRES_CRED_JOB);
    if (gres && gres->bitAlloc) {
	if (gres->bitAlloc[0]) {
	    hexBitstr2List(gres->bitAlloc[0], &list, &listSize);

	    /* always set informational variable */
	    envSet(&job->env, "SLURM_JOB_GPUS", list);

	    /* tell doClamps() which gpus to use */
	    envSet(&job->env, "__PSID_USE_GPUS", list);

	    char *prefix = "__AUTO_";
	    char name[GPU_VARIABLE_MAXLEN+strlen(prefix)+1];
	    for (size_t i = 0; gpu_variables[i]; i++) {
		/* set variable if not already set by the user */
		if (!envGet(&job->env, gpu_variables[i])) {
		    snprintf(name, sizeof(name), "%s%s", prefix,
			    gpu_variables[i]);
		    /* append some spaces to help step code to detect whether
		     * the user has changed the variable in his job script */
		    char *val = umalloc(strlen(list) + 6);
		    sprintf(val, "%s     ", list);
		    envSet(&job->env, gpu_variables[i], val);
		    envSet(&job->env, name, val);
		    ufree(val);
		}
	    }

	    ufree(list);
	    list = NULL;
	    listSize = 0;
	} else {
	    flog("invalid gpu gres bitAlloc for local nodeID 0\n");
	}
    }

    /* GRes "mic" plugin */
    gres = findGresCred(&job->gresList, GRES_PLUGIN_MIC, GRES_CRED_JOB);
    if (gres && gres->bitAlloc) {
	if (gres->bitAlloc[0]) {
	    hexBitstr2List(gres->bitAlloc[0], &list, &listSize);
	    envSet(&job->env, "OFFLOAD_DEVICES", list);
	    ufree(list);
	    list = NULL;
	    listSize = 0;
	} else {
	    flog("invalid mic gres bitAlloc for local nodeID 0\n");
	}
    }

    /* set JOB_GRES */
    gres = findGresCred(&job->gresList, NO_VAL, GRES_CRED_JOB);
    if (gres && gres->bitAlloc) {
	if (gres->bitAlloc[0]) {
	    hexBitstr2List(gres->bitAlloc[0], &list, &listSize);
	    envSet(&job->env, "SLURM_JOB_GRES", list);
	    ufree(list);
	    list = NULL;
	    listSize = 0;
	} else {
	    flog("invalid job gres bitAlloc for local nodeID 0\n");
	}
    }
}

/**
 * @brief Convert my global task IDs to a list
 *
 * @param step The step holding the global task IDs
 *
 * @return Returns a comma separated list of my global task IDs
 */
static char *GTIDsToList(Step_t *step)
{
    char *buf = NULL;
    size_t bufSize;
    uint32_t i;
    char tmp[128];

    uint32_t offset = step->packTaskOffset != NO_VAL ? step->packTaskOffset : 0;
    for (i=0; i<step->globalTaskIdsLen[step->localNodeId]; i++) {
	if (i > 0) str2Buf(",", &buf, &bufSize);
	snprintf(tmp, sizeof(tmp), "%u",
		 step->globalTaskIds[step->localNodeId][i] + offset);
	str2Buf(tmp, &buf, &bufSize);
    }
    return buf;
}

/**
 * @brief Get jobs localNodeId if job exists, get alloc's else
 *
 * The jobNodeId is used in step environments to access GRES credentials.
 *
 * @return job node id
 */
static uint32_t getJobNodeId(Step_t *step)
{
    Alloc_t *alloc = findAlloc(step->jobid);
    Job_t *job = findJobById(step->jobid);

    uint32_t jobNodeId = NO_VAL;
    if (job) {
	jobNodeId = job->localNodeId;
    } else if (alloc) {
	/* if there is no job, the allocation node counting is the only one */
	jobNodeId = alloc->localNodeId;
    }
    return jobNodeId;
}

/**
 * @brief Set binding environment variables
 *
 * @param step The step to set the variables for
 */
static void setBindingEnvVars(Step_t *step)
{
    char *val;

    /* cpu bind variables */
    val = genCPUbindString(step);
    setenv("SLURM_CPU_BIND", val, 1);
    setenv("SBATCH_CPU_BIND", val, 1);
    ufree(val);

    if (step->cpuBindType & CPU_BIND_VERBOSE) {
	setenv("SLURM_CPU_BIND_VERBOSE", "verbose", 1);
	setenv("SBATCH_CPU_BIND_VERBOSE", "verbose", 1);
    } else {
	setenv("SLURM_CPU_BIND_VERBOSE", "quiet", 1);
	setenv("SBATCH_CPU_BIND_VERBOSE", "quiet", 1);
    }

    val = genCPUbindTypeString(step->cpuBindType);
    setenv("SLURM_CPU_BIND_TYPE", val, 1);
    setenv("SBATCH_CPU_BIND_TYPE", val, 1);

    if (step->cpuBindType & (CPU_BIND_MAP | CPU_BIND_MASK
		| CPU_BIND_LDMAP | CPU_BIND_LDMASK)) {
	setenv("SLURM_CPU_BIND_LIST", step->cpuBind, 1);
	setenv("SBATCH_CPU_BIND_LIST", step->cpuBind, 1);
    } else {
	setenv("SLURM_CPU_BIND_LIST", "", 1);
	setenv("SBATCH_CPU_BIND_LIST", "", 1);
    }

    /* mem bind variables */
    val = genMemBindString(step);
    setenv("SLURM_MEM_BIND", val, 1);
    setenv("SBATCH_MEM_BIND", val, 1);
    ufree(val);

    if (step->memBindType & MEM_BIND_VERBOSE) {
	setenv("SLURM_MEM_BIND_VERBOSE", "verbose", 1);
    } else {
	setenv("SLURM_MEM_BIND_VERBOSE", "quiet", 1);
    }

    if (step->memBindType & MEM_BIND_NONE) {
	val = "none";
    } else if (step->memBindType & (MEM_BIND_RANK | MEM_BIND_MAP
	     | MEM_BIND_MASK)) {
	val = "unsupported";
    } else if (step->memBindType & MEM_BIND_LOCAL) {
	val = "local";
    } else {
	/* this is our default */
	val = "local";
    }
    setenv("SLURM_MEM_BIND_TYPE", val, 1);
}

void setPsslurmEnv(env_t *env)
{
    uint32_t i;

    for (i=0; i<env->cnt; i++) {
	if (env->vars[i] && !(strncmp("_PSSLURM_ENV_", env->vars[i], 13))) {
	    char *ptr = env->vars[i] + 13;
	    fdbg(PSSLURM_LOG_ENV, "set %s\n", ptr);
	    if (ptr) putenv(ptr);
	}
    }
}

static void setGPUEnv(Gres_Cred_t *gres, uint32_t jobNodeId, Step_t *step,
	uint32_t localRankId)
{
    uint32_t stepNodeId = step->localNodeId;

    if (!gres->bitAlloc[jobNodeId]) {
	flog("invalid gpu gres bitAlloc for job node id %u\n", jobNodeId);
	return;
    }

    /* get assigned GPUs */
    int *gpusAssigned;
    size_t numGPUsAssigned = 0;
    hexBitstr2Array(gres->bitAlloc[jobNodeId], &gpusAssigned, &numGPUsAssigned);

    uint16_t gpus[step->globalTaskIdsLen[stepNodeId]];

    bool success = getNodeGPUPinning(gpus, step, stepNodeId, gpusAssigned,
		numGPUsAssigned);
    ufree(gpusAssigned);

    if (!success) return;

    char tmp[10];
    snprintf(tmp, sizeof(tmp), "%i", gpus[localRankId]);

    /* always set our own variable */
    setenv("PSSLURM_BIND_GPU", tmp, 1);

    char *prefix = "__AUTO_";
    char name[GPU_VARIABLE_MAXLEN+strlen(prefix)+1];
    for (size_t i = 0; gpu_variables[i]; i++) {
	snprintf(name, sizeof(name), "%s%s", prefix, gpu_variables[i]);
	if (!getenv(gpu_variables[i])
		|| (getenv(name)
		    && !strcmp(getenv(name), getenv(gpu_variables[i])))) {
	    /* variable is not set at all
	     * or it had been set automatically and not changed in the meantime,
	     * so set it */
	    setenv(gpu_variables[i], tmp, 1);
	}

	/* automation detection is no longer needed */
	unsetenv(name);
    }
}

static void setGresEnv(uint32_t localRankId, Step_t *step)
{
    uint32_t jobNodeId = getJobNodeId(step);

    if (jobNodeId != NO_VAL) {
	Gres_Cred_t *gres;
	char *list = NULL;
	size_t listSize = 0;

	/* gres "gpu" plugin */
	gres = findGresCred(&step->gresList, GRES_PLUGIN_GPU, GRES_CRED_STEP);
	if (gres && gres->bitAlloc) {
	    setGPUEnv(gres, jobNodeId, step, localRankId);
	}

	/* gres "mic" plugin */
	gres = findGresCred(&step->gresList, GRES_PLUGIN_MIC, GRES_CRED_STEP);
	if (gres && gres->bitAlloc) {
	    if (gres->bitAlloc[jobNodeId]) {
		hexBitstr2List(gres->bitAlloc[jobNodeId], &list, &listSize);
		setenv("OFFLOAD_DEVICES", list, 1);
		ufree(list);
		list = NULL;
		listSize = 0;
	    } else {
		flog("invalid mic gres bitAlloc for job node ID %u\n",
		     jobNodeId);
	    }
	}

	/* set STEP_GRES */
	gres = findGresCred(&step->gresList, NO_VAL, GRES_CRED_STEP);
	if (gres && gres->bitAlloc) {
	    if (gres->bitAlloc[jobNodeId]) {
		hexBitstr2List(gres->bitAlloc[jobNodeId], &list, &listSize);
		setenv("SLURM_STEP_GRES", list, 1);
		ufree(list);
		list = NULL;
		listSize = 0;
	    } else {
		flog("invalid step gres bitAlloc for job node ID %u\n",
		     jobNodeId);
	    }
	}
    } else {
	flog("unable to set gres: invalid job node ID for %s\n",
	     strStepID(step));
    }
}

void setRankEnv(int32_t rank, Step_t *step)
{
    char tmp[128], *myGTIDs, *val, *display;
    uint32_t count = 0;

    /* remove unwanted variables */
    unsetenv("PSI_INPUTDEST");
    unsetenv(ENV_PSID_BATCH);

    /* we need the DISPLAY variable set by psslurm */
    display = getenv("DISPLAY");

    /* set environment variables from user */
    for (count=0; count<step->env.cnt; count++) {
	/* protect selected variables from changes */
	if (!(strncmp(step->env.vars[count], "SLURM_RLIMIT_", 13))) continue;
	if (!(strncmp(step->env.vars[count], "SLURM_UMASK=", 12))) continue;
	if (!(strncmp(step->env.vars[count], "PWD=", 4))) continue;
	if (display &&
	    !(strncmp(step->env.vars[count], "DISPLAY=", 8))) continue;
	if (!(strncmp(step->env.vars[count], "PMI_FD=", 7))) continue;
	if (!(strncmp(step->env.vars[count], "PMI_PORT=", 9))) continue;
	if (!(strncmp(step->env.vars[count], "PMI_RANK=", 9))) continue;
	if (!(strncmp(step->env.vars[count], "PMI_SIZE=", 9))) continue;
	if (!(strncmp(step->env.vars[count], "PMI_UNIVERSE_SIZE=", 18))) {
	    continue;
	}
	if (!(strncmp(step->env.vars[count], "PMI_ID=", 7))) continue;
	if (!(strncmp(step->env.vars[count], "PMI_APPNUM=", 11))) continue;
	if (!(strncmp(step->env.vars[count], "PMI_ENABLE_SOCKP=", 17))) {
	    continue;
	}
	if (!(strncmp(step->env.vars[count], "PMI_SUBVERSION=", 15))) continue;
	if (!(strncmp(step->env.vars[count], "PMI_VERSION=", 12))) continue;
	if (!(strncmp(step->env.vars[count], "PMI_BARRIER_ROUNDS=", 19))) {
	    continue;
	}
	if (!(strncmp(step->env.vars[count], "PMIX_", 5))) continue;

	putenv(step->env.vars[count]);
    }

    setenv("SLURMD_NODENAME", getConfValueC(&Config, "SLURM_HOSTNAME"), 1);
    gethostname(tmp, sizeof(tmp));
    setenv("HOSTNAME", tmp, 1);
    snprintf(tmp, sizeof(tmp), "%u", getpid());
    setenv("SLURM_TASK_PID", tmp, 1);
    setenv("SLURM_CPUS_ON_NODE", getConfValueC(&Config, "SLURM_CPUS"), 1);

    sprintf(tmp, "%d", rank);
    setenv("SLURM_PROCID", tmp, 1);

    if (step->localNodeId < step->nrOfNodes) {
	snprintf(tmp, sizeof(tmp), "%u", step->localNodeId);
	setenv("SLURM_NODEID", tmp, 1);
	myGTIDs = GTIDsToList(step);
	setenv("SLURM_GTIDS", myGTIDs, 1);
	ufree(myGTIDs);
    }

    uint32_t myLocalId = getLocalRankID(rank, step);
    if (myLocalId == NO_VAL) {
	flog("failed to find local ID for rank %u\n", rank);
    } else {
	snprintf(tmp, sizeof(tmp), "%u", myLocalId);
	setenv("SLURM_LOCALID", tmp, 1);

	/* set gres environment */
	setGresEnv(myLocalId, step);
    }

    /* set cpu/memory bind env vars */
    setBindingEnvVars(step);

    snprintf(tmp, sizeof(tmp), "%u", step->tpp);
    setenv("SLURM_CPUS_PER_TASK", tmp, 1);

    setenv("SLURM_CHECKPOINT_IMAGE_DIR", step->checkpoint, 1);
    setenv("SLURM_LAUNCH_NODE_IPADDR", inet_ntoa(step->srun.sin_addr), 1);
    setenv("SLURM_SRUN_COMM_HOST", inet_ntoa(step->srun.sin_addr), 1);

    setenv("SLURM_JOB_USER", step->username, 1);
    snprintf(tmp, sizeof(tmp), "%u", step->uid);
    setenv("SLURM_JOB_UID", tmp, 1);

    /* set SLURM_TASKS_PER_NODE */
    val = getTasksPerNode(step->tasksToLaunch, step->nrOfNodes);
    setenv("SLURM_TASKS_PER_NODE", val, 1);

    Alloc_t *alloc = findAlloc(step->jobid);
    if (alloc) setPsslurmEnv(&alloc->env);
}

/**
 * @brief Remove spank options from environment
 *
 * @param env The environment to alter
 */
static void removeSpankOptions(env_t *env)
{
    uint32_t i;

    /* remove srun/spank options */
    for (i=0; i<env->cnt; i++) {
	while (env->vars[i] &&
		(!(strncmp("_SLURM_SPANK_OPTION", env->vars[i], 19)))) {
	    envUnsetIndex(env, i);
	}
    }
}

/**
 * @brief Stripping down environment for mpiexec. The intention is to remove
 *        all environment variables that are not evaluated by mpiexec.
 *        User variables will are transfered by srun and later merged back
 *        into the environment in @a setRankEnv()
 *
 * @param env       The environment to alter
 * @param pmi_type  The PMI type of the job
 */
void removeUserVars(env_t *env, pmi_type_t pmi_type)
{
    uint32_t i = 0;

    /* get rid of all environment variables which are not needed
     * for spawning of processes via mpiexec */
    for (i=0; i<env->cnt; i++) {
	if (!strncmp(env->vars[i], "USER=", 5)) continue;
	if (!strncmp(env->vars[i], "HOSTNAME=", 9)) continue;
	if (!strncmp(env->vars[i], "PATH=", 5)) continue;
	if (!strncmp(env->vars[i], "HOME=", 5)) continue;
	if (!strncmp(env->vars[i], "PWD=", 4)) continue;
	if (!strncmp(env->vars[i], "DISPLAY=", 8)) continue;

	if (!(strncmp(env->vars[i], "SLURM_STEPID=", 13))) continue;
	if (!(strncmp(env->vars[i], "SLURM_JOBID=", 12))) continue;

	if (!(strncmp(env->vars[i], "__MPIEXEC_DIST_START=", 21))) continue;
	if (!(strncmp(env->vars[i], "MPIEXEC_", 8))) continue;

	if (!strncmp(env->vars[i], "PSI_", 4)) continue;
	if (!strncmp(env->vars[i], "__PSI_", 6)) continue;
	if (pmi_type == PMI_TYPE_DEFAULT) {
	    if (!strncmp(env->vars[i], "PMI_", 4)) continue;
	    if (!strncmp(env->vars[i], "__PMI_", 6)) continue;
	    if (!strncmp(env->vars[i], "MEASURE_KVS_PROVIDER", 20)) continue;
	}
	if (pmi_type == PMI_TYPE_PMIX) {
	    if (!strncmp(env->vars[i], "PMIX_DEBUG", 10)) continue;
	    if (!strncmp(env->vars[i], "PMIX_SPAWNED", 12)) continue;
	}
	if (!strncmp(env->vars[i], "__PSID_", 7)) continue;
	if (!(strncmp(env->vars[i], "SLURM_STEP_GPUS=", 16))) continue;

	envUnsetIndex(env, i);
	i--;
    }
}

void setStepEnv(Step_t *step)
{
    char *val;
    mode_t slurmUmask;
    int dist;

    /* distribute mpiexec service processes */
    dist = getConfValueI(&Config, "DIST_START");
    if (dist) envSet(&step->env, "__MPIEXEC_DIST_START", "1");

    /* forward overbook mode */
    if ((val = envGet(&step->env, "SLURM_OVERCOMMIT"))) {
	if (!strcmp(val, "1")) {
	    envSet(&step->env, "PSI_OVERBOOK", "1");
	}
    }

    /* unbuffered (raw I/O) mode */
    if (!(step->taskFlags & LAUNCH_LABEL_IO) &&
	!(step->taskFlags & LAUNCH_BUFFERED_IO)) {
	envSet(&step->env, "__PSI_RAW_IO", "1");
	envSet(&step->env, "__PSI_LOGGER_UNBUFFERED", "1");
    }

    if (!(step->taskFlags & LAUNCH_PTY)) {
	envSet(&step->env, "PSI_INPUTDEST", "all");
    }

    /* set slurm umask */
    if ((val = envGet(&step->env, "SLURM_UMASK"))) {
	slurmUmask = strtol(val, NULL, 8);
	umask(slurmUmask);
	envSet(&step->env, "__PSI_UMASK", val);
	envUnset(&step->env, "SLURM_UMASK");
    }

    /* handle memory mapping */
    val = getConfValueC(&Config, "MEMBIND_DEFAULT");
    if (step->memBindType & MEM_BIND_NONE ||
	    (!(step->memBindType & (MEM_BIND_RANK | MEM_BIND_MAP |
				    MEM_BIND_MASK | MEM_BIND_LOCAL)) &&
		    (strcmp(val, "none") == 0))) {
	envSet(&step->env, "__PSI_NO_MEMBIND", "1");
    }

    /* prevent mpiexec from resolving the nodelist */
    envSet(&step->env, ENV_PSID_BATCH, "1");

    if (step->tresBind) envSet(&step->env, "SLURMD_TRES_BIND", step->tresBind);
    if (step->tresFreq) envSet(&step->env, "SLURMD_TRES_FREQ", step->tresFreq);


    /* if GPUs are assigned */
    Gres_Cred_t *gres;
    gres = findGresCred(&step->gresList, GRES_PLUGIN_GPU, GRES_CRED_STEP);
    if (gres && gres->bitAlloc) {
	uint32_t jobNodeId = getJobNodeId(step);
	if (jobNodeId != NO_VAL) {
	    if (gres->bitAlloc[jobNodeId]) {
		char *list = NULL;
		size_t listSize = 0;
		hexBitstr2List(gres->bitAlloc[jobNodeId], &list, &listSize);

		/* always set informational variable */
		envSet(&step->env, "SLURM_STEP_GPUS", list);

		/* tell doClamps() which gpus to use */
		envSet(&step->env, "__PSID_USE_GPUS", list);

		ufree(list);
	    }
	}
	else {
	    flog("Cannot find job node id for getting GPU credentials.\n");
	}
    }
    else {
	/* tell psid to bind no GPUs */
	envSet(&step->env, "__PSID_USE_GPUS", "");
    }


    /* cleanup env */
    removeSpankOptions(&step->env);
}

void setJobEnv(Job_t *job)
{
    char tmp[1024];
    mode_t slurmUmask;

    envSet(&job->env, "ENVIRONMENT", "BATCH");
    envSet(&job->env, "SLURM_NODEID", "0");
    envSet(&job->env, "SLURM_PROCID", "0");
    envSet(&job->env, "SLURM_LOCALID", "0");
    snprintf(tmp, sizeof(tmp), "%lu", job->nodeMinMemory);
    envSet(&job->env, "SLURM_MEM_PER_NODE", tmp);

    snprintf(tmp, sizeof(tmp), "%u", getpid());
    envSet(&job->env, "SLURM_TASK_PID", tmp);

    /* forward overbook mode */
    char *val = envGet(&job->env, "SLURM_OVERCOMMIT");

    if (job->overcommit || (val && !strcmp(val, "1"))) {
	envSet(&job->env, "PSI_OVERBOOK", "1");
    }

    /* set slurm umask */
    if ((val = envGet(&job->env, "SLURM_UMASK"))) {
	slurmUmask = strtol(val, NULL, 8);
	umask(slurmUmask);
	envUnset(&job->env, "SLURM_UMASK");
    }

    if (job->tresBind) envSet(&job->env, "SLURMD_TRES_BIND", job->tresBind);
    if (job->tresFreq) envSet(&job->env, "SLURMD_TRES_FREQ", job->tresFreq);

    removeSpankOptions(&job->env);

    Alloc_t *alloc = findAlloc(job->jobid);
    if (alloc) setPsslurmEnv(&alloc->env);
}
