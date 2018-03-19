/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
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
#include "pluginenv.h"

#include "pluginmalloc.h"
#include "pluginhostlist.h"

#include "slurmcommon.h"
#include "psslurmconfig.h"
#include "psslurmjob.h"
#include "psslurmlog.h"
#include "psslurmproto.h"
#include "psslurmpin.h"

#include "psslurmenv.h"

char **envFilter = NULL;

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
	    snprintf(tmp, sizeof(tmp), "%u(x%i)", job->cpusPerNode[i],
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

    if (job->np) {
	snprintf(tmp, sizeof(tmp), "%u", job->np);
	envSet(&job->env, "SLURM_NTASKS", tmp);
	envSet(&job->env, "SLURM_NPROCS", tmp);
    }

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

    /* gres "gpu" plugin */
    gres = findGresCred(&job->gresList, GRES_PLUGIN_GPU, 1);
    if (gres) {
	hexBitstr2List(gres->bitAlloc[0], &list, &listSize);
	envSet(&job->env, "CUDA_VISIBLE_DEVICES", list);
	envSet(&job->env, "GPU_DEVICE_ORDINAL", list);
	ufree(list);
	list = NULL;
	listSize = 0;
    }

    /* gres "mic" plugin */
    gres = findGresCred(&job->gresList, GRES_PLUGIN_MIC, 1);
    if (gres) {
	hexBitstr2List(gres->bitAlloc[0], &list, &listSize);
	envSet(&job->env, "OFFLOAD_DEVICES", list);
	ufree(list);
	list = NULL;
	listSize = 0;
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

    for (i=0; i<step->globalTaskIdsLen[step->myNodeIndex]; i++) {
	if (i > 0) str2Buf(",", &buf, &bufSize);
	snprintf(tmp, sizeof(tmp), "%u",
		 step->globalTaskIds[step->myNodeIndex][i]);
	str2Buf(tmp, &buf, &bufSize);
    }
    return buf;
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

    if (step->cpuBindType & CPU_BIND_NONE) {
	val = "none";
    } else if (step->cpuBindType & CPU_BIND_RANK) {
	val = "rank";
    } else if (step->cpuBindType & CPU_BIND_TO_SOCKETS) {
	val = "sockets";
    } else if (step->cpuBindType & CPU_BIND_TO_LDOMS) {
	val = "ldoms";
    } else if (step->cpuBindType & CPU_BIND_MAP) {
	val = "map_cpu:";
    } else if (step->cpuBindType & CPU_BIND_MASK) {
	val = "mask_cpu:";
    } else {
	val = "unsupported";
    }
    setenv("SLURM_CPU_BIND_TYPE", val, 1);
    setenv("SBATCH_CPU_BIND_TYPE", val, 1);

    if (step->cpuBindType & (CPU_BIND_MAP | CPU_BIND_MASK)) {
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

void setRankEnv(int32_t rank, Step_t *step)
{
    char tmp[128], *myGTIDs, *list = NULL, *val, *display;
    size_t listSize = 0;
    uint32_t myNodeId = step->myNodeIndex, myLocalId, count = 0, localNodeId;
    Alloc_t *alloc;
    Job_t *job;

    /* remove unwanted variables */
    unsetenv("PSI_INPUTDEST");

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

    if (myNodeId < step->nrOfNodes) {
	snprintf(tmp, sizeof(tmp), "%u", myNodeId);
	setenv("SLURM_NODEID", tmp, 1);
	myGTIDs = GTIDsToList(step);
	setenv("SLURM_GTIDS", myGTIDs, 1);
	ufree(myGTIDs);
    }

    myLocalId = getLocalRankID(rank, step, myNodeId);
    snprintf(tmp, sizeof(tmp), "%u", myLocalId);
    setenv("SLURM_LOCALID", tmp, 1);

    if ((job = findJobById(step->jobid))) {
	localNodeId = job->localNodeId;
    } else if ((alloc = findAlloc(step->jobid))) {
	localNodeId = alloc->localNodeId;
    } else {
	localNodeId = -1;
    }

    if ((int32_t) localNodeId != -1) {
	Gres_Cred_t *gres;
	/* gres "gpu" plugin */
	gres = findGresCred(&step->gresList, GRES_PLUGIN_GPU, 0);
	if (gres) {
	    if (gres->bitAlloc[localNodeId]) {
		hexBitstr2List(gres->bitAlloc[localNodeId], &list, &listSize);
		setenv("CUDA_VISIBLE_DEVICES", list, 1);
		setenv("GPU_DEVICE_ORDINAL", list, 1);
		ufree(list);
		list = NULL;
		listSize = 0;
	    } else {
		mlog("%s: invalid gpu gres bitAlloc for local nodeID '%u'\n",
			__func__, localNodeId);
	    }
	}

	/* gres "mic" plugin */
	gres = findGresCred(&step->gresList, GRES_PLUGIN_MIC, 0);
	if (gres) {
	    if (gres->bitAlloc[localNodeId]) {
		hexBitstr2List(gres->bitAlloc[localNodeId], &list, &listSize);
		setenv("OFFLOAD_DEVICES", list, 1);
		ufree(list);
		list = NULL;
		listSize = 0;
	    } else {
		mlog("%s: invalid mic gres bitAlloc for local nodeID '%u'\n",
			__func__, localNodeId);
	    }
	}
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

void removeUserVars(env_t *env)
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

	if (!strncmp(env->vars[i], "PMI_", 4)) continue;
	if (!strncmp(env->vars[i], "__PMI_", 6)) continue;
	if (!strncmp(env->vars[i], "MEASURE_KVS_PROVIDER", 20)) continue;

	envUnsetIndex(env, i);
	i--;
    }
}

void setStepEnv(Step_t *step)
{
    char *val, tmp[1024];
    mode_t slurmUmask;
    int dist;

    /* overwrite with pack jobid/stepid, will be reset
     * in the rank env */
    snprintf(tmp, sizeof(tmp), "%u", step->jobid);
    envSet(&step->env, "SLURM_JOBID", tmp);
    snprintf(tmp, sizeof(tmp), "%u", step->stepid);
    envSet(&step->env, "SLURM_STEPID", tmp);

    snprintf(tmp, sizeof(tmp), "%u", step->numHwThreads / step->np);
    envSet(&step->env, "PSI_TPP", tmp);

    //envSet(&step->env, "PSI_LOGGERDEBUG", "1");
    //envSet(&step->env, "PSI_FORWARDERDEBUG", "1");

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

    /* cleanup env */
    removeSpankOptions(&step->env);
}

void setJobEnv(Job_t *job)
{
    char tmp[1024], *val = NULL;
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
    val = envGet(&job->env, "SLURM_OVERCOMMIT");

    if (job->overcommit || (val && !strcmp(val, "1"))) {
	envSet(&job->env, "PSI_OVERBOOK", "1");
    }

    /* set slurm umask */
    if ((val = envGet(&job->env, "SLURM_UMASK"))) {
	slurmUmask = strtol(val, NULL, 8);
	umask(slurmUmask);
	envUnset(&job->env, "SLURM_UMASK");
    }

    removeSpankOptions(&job->env);
}
