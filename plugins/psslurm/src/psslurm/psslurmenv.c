/*
 * ParaStation
 *
 * Copyright (C) 2014 - 2015 ParTec Cluster Competence Center GmbH, Munich
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

#include "psidnodes.h"
#include "pluginenv.h"

#include "pluginmalloc.h"
#include "pluginhostlist.h"

#include "slurmcommon.h"
#include "psslurmconfig.h"
#include "psslurmjob.h"
#include "psslurmlog.h"
#include "psslurmproto.h"

#include "psslurmenv.h"

char **envFilter = NULL;

int initEnvFilter()
{
    char *conf, *dup, *next, *saveptr;
    const char delimiters[] =",\n";
    uint32_t count = 0, index = 0;

    if (!(conf = getConfValueC(&Config, "PELOGUE_ENV_FILTER"))) {
	mlog("%s: invalid PELOGUE_ENV_FILTER config option", __func__);
	return 0;
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

    return 1;
}

void freeEnvFilter()
{
    char *ptr;
    uint32_t index = 0;

    while ((ptr = envFilter[index])) {
	ufree(envFilter[index++]);
    }
    ufree(envFilter);
}

/*
static void addInt2StringList(uint32_t val, char **list, size_t *listSize,
				int finish)
{
    static uint32_t last, repeat = 1;
    char tmp[256];

    if (!*list) {
	if (finish) {
	    snprintf(tmp, sizeof(tmp), "%u", val);
	    str2Buf(tmp, list, listSize);
	} else {
	    str2Buf("", list, listSize);
	}
	repeat = 1;
    } else {
	if (val == last) {
	    repeat++;
	    if (finish) {
		if (strlen(*list) >0) str2Buf(",", list, listSize);
		if (repeat >1) {
		    snprintf(tmp, sizeof(tmp), "%u(x%i)", val, repeat);
		} else {
		    snprintf(tmp, sizeof(tmp), "%u", val);
		}
		str2Buf(tmp, list, listSize);
	    }
	} else {
	    if (strlen(*list) >0) str2Buf(",", list, listSize);
	    if (repeat >1) {
		snprintf(tmp, sizeof(tmp), "%u(x%i)", val, repeat);
	    } else {
		snprintf(tmp, sizeof(tmp), "%u", val);
	    }
	    str2Buf(tmp, list, listSize);
	    repeat = 1;
	}
    }

    last = val;
}
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
 * In case of an error, NULL is returned
 */
static uint16_t * calcTasksPerNode(Job_t *job) {

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
 * @param nrOfNodes    number of nodes, length of @a tasksPerNode
 * @param str          pointer to an allocated string
 * @param strsize      length of @a str
 */
static char * getTasksPerNode(uint16_t tasksPerNode[], uint32_t nrOfNodes) {

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

void setSlurmEnv(Job_t *job)
{
    char tmp[1024], *cpus = NULL, *list = NULL;
    Gres_Cred_t *gres;
    size_t listSize = 0;
    uint32_t count = 0;
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

    envSet(&job->env, "SLURM_JOBID", job->id);
    envSet(&job->env, "SLURM_JOB_ID", job->id);

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

    envSet(&job->env, "SLURM_NODELIST", job->slurmNodes);
    envSet(&job->env, "SLURM_JOB_NODELIST", job->slurmNodes);
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
    if ((gres = findGresCred(&job->gres, GRES_PLUGIN_GPU, 1))) {
	range2List(NULL, gres->bitAlloc[0], &list, &listSize, &count);
	envSet(&job->env, "CUDA_VISIBLE_DEVICES", list);
	envSet(&job->env, "GPU_DEVICE_ORDINAL", list);
	ufree(list);
    }

    /* gres "mic" plugin */
    if ((gres = findGresCred(&job->gres, GRES_PLUGIN_MIC, 1))) {
	range2List(NULL, gres->bitAlloc[0], &list, &listSize, &count);
	envSet(&job->env, "OFFLOAD_DEVICES", list);
	ufree(list);
    }
}

static char *getMyGTIDsForNode(uint32_t **globalTaskIds,
				uint32_t *globalTaskIdsLen, PSnodes_ID_t nodeId)
{
    char *buf = NULL;
    size_t bufSize;
    uint32_t i;
    char tmp[128];

    for (i=0; i<globalTaskIdsLen[nodeId]; i++) {
	if (i > 0) str2Buf(",", &buf, &bufSize);
	snprintf(tmp, sizeof(tmp), "%u", globalTaskIds[nodeId][i]);
	str2Buf(tmp, &buf, &bufSize);
    }
    return buf;
}

void setRankEnv(int32_t rank, Step_t *step)
{
    char tmp[128], *myGTIDs, *list = NULL;
    size_t listSize = 0;
    uint32_t myNodeId = step->myNodeIndex, myLocalId, count = 0;
    Gres_Cred_t *gres;

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
	myGTIDs = getMyGTIDsForNode(step->globalTaskIds,
					step->globalTaskIdsLen, myNodeId);
	setenv("SLURM_GTIDS", myGTIDs, 1);
	ufree(myGTIDs);
    }

    myLocalId = getLocalRankID(rank, step, myNodeId);
    snprintf(tmp, sizeof(tmp), "%u", myLocalId);
    setenv("SLURM_LOCALID", tmp, 1);

    /* gres "gpu" plugin */
    if ((gres = findGresCred(&step->gres, GRES_PLUGIN_GPU, 0))) {
	range2List(NULL, gres->bitAlloc[myNodeId], &list, &listSize, &count);
	setenv("CUDA_VISIBLE_DEVICES", list, 1);
	setenv("GPU_DEVICE_ORDINAL", list, 1);
	ufree(list);
    }

    /* gres "mic" plugin */
    if ((gres = findGresCred(&step->gres, GRES_PLUGIN_MIC, 0))) {
	range2List(NULL, gres->bitAlloc[myNodeId], &list, &listSize, &count);
	setenv("OFFLOAD_DEVICES", list, 1);
	ufree(list);
    }
}

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

static char * genCPUbindString(Step_t *step) {

    char *string;
    int len;

    string = (char *) umalloc(sizeof(char) * (25 + strlen(step->cpuBind) + 1));

    *string = '\0';
    len = 0;

    if (step->cpuBindType & CPU_BIND_VERBOSE) {
	strcpy(string, "verbose");
	len += 7;
    } else {
	strcpy(string, "quiet");
	len += 5;
    }

    if (step->cpuBindType & CPU_BIND_TO_THREADS) {
	strcpy(string+len, ",threads");
	len += 8;
    } else if (step->cpuBindType & CPU_BIND_TO_CORES) {
	strcpy(string+len, ",cores");
	len += 6;
    } else if (step->cpuBindType & CPU_BIND_TO_SOCKETS) {
	strcpy(string+len, ",sockets");
	len += 8;
    } else if (step->cpuBindType & CPU_BIND_TO_LDOMS) {
	strcpy(string+len, ",ldoms");
	len += 6;
    } else if (step->cpuBindType & CPU_BIND_TO_BOARDS) {
	strcpy(string+len, ",boards");
	len += 7;
    }

    if (step->cpuBindType & CPU_BIND_NONE) {
	strcpy(string+len, ",none");
	len += 5;
    } else if (step->cpuBindType & CPU_BIND_RANK) {
	strcpy(string+len, ",rank");
	len += 5;
    } else if (step->cpuBindType & CPU_BIND_MAP) {
	strcpy(string+len, ",map_cpu:");
	len += 9;
    } else if (step->cpuBindType & CPU_BIND_MASK) {
	strcpy(string+len, ",mask_cpu:");
	len += 10;
    } else if (step->cpuBindType & CPU_BIND_LDRANK) {
	strcpy(string+len, ",rank_ldom");
	len += 10;
    } else if (step->cpuBindType & CPU_BIND_LDMAP) {
	strcpy(string+len, ",map_ldom");
	len += 9;
    } else if (step->cpuBindType & CPU_BIND_LDMASK) {
	strcpy(string+len, ",mask_ldom");
	len += 10;
    }

    if (step->cpuBindType & (CPU_BIND_MAP | CPU_BIND_MASK)) {
	strcpy(string+len, step->cpuBind);
    }

    return string;
}

static char * genMemBindString(Step_t *step) {

    char *string;
    int len;

    string = (char *) umalloc(sizeof(char) * (25 + strlen(step->memBind) + 1));

    *string = '\0';
    len = 0;

    if (step->memBindType & MEM_BIND_VERBOSE) {
	strcpy(string, "verbose");
	len += 7;
    } else {
	strcpy(string, "quiet");
	len += 5;
    }

    if (step->memBindType & MEM_BIND_NONE) {
	strcpy(string+len, ",none");
	len += 5;
    } else if (step->memBindType & MEM_BIND_RANK) {
	strcpy(string+len, ",rank");
	len += 5;
    } else if (step->memBindType & MEM_BIND_MAP) {
	strcpy(string+len, ",map_mem:");
	len += 9;
    } else if (step->memBindType & MEM_BIND_MASK) {
	strcpy(string+len, ",mask_mem:");
	len += 10;
    } else if (step->memBindType & MEM_BIND_LOCAL) {
	strcpy(string+len, ",local");
	len += 6;
    }

    if (step->memBindType & (MEM_BIND_MAP | MEM_BIND_MASK)) {
	strcpy(string+len, step->memBind);
    }

    return string;
}

void setTaskEnv(Step_t *step)
{
    char *val, tmp[1024];
    mode_t slurmUmask;

    snprintf(tmp, sizeof(tmp), "%u", step->tpp);
    envSet(&step->env, "SLURM_CPUS_PER_TASK", tmp);

    snprintf(tmp, sizeof(tmp), "%u", step->numHwThreads / step->np);
    envSet(&step->env, "PSI_TPP", tmp);

    /* cpu bind variables */
    val = genCPUbindString(step);
    envSet(&step->env, "SLURM_CPU_BIND", val);
    envSet(&step->env, "SBATCH_CPU_BIND", val);
    ufree(val);

    if (step->cpuBindType & CPU_BIND_VERBOSE) {
	envSet(&step->env, "SLURM_CPU_BIND_VERBOSE", "verbose");
	envSet(&step->env, "SBATCH_CPU_BIND_VERBOSE", "verbose");
    } else {
	envSet(&step->env, "SLURM_CPU_BIND_VERBOSE", "quiet");
	envSet(&step->env, "SBATCH_CPU_BIND_VERBOSE", "quiet");
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
    envSet(&step->env, "SLURM_CPU_BIND_TYPE", val);
    envSet(&step->env, "SBATCH_CPU_BIND_TYPE", val);

    if (step->cpuBindType & (CPU_BIND_MAP | CPU_BIND_MASK)) {
	envSet(&step->env, "SLURM_CPU_BIND_LIST", step->cpuBind);
	envSet(&step->env, "SBATCH_CPU_BIND_LIST", step->cpuBind);
    } else {
	envSet(&step->env, "SLURM_CPU_BIND_LIST", "");
	envSet(&step->env, "SBATCH_CPU_BIND_LIST", "");
    }

    /* mem bind variables */
    val = genMemBindString(step);
    envSet(&step->env, "SLURM_MEM_BIND", val);
    envSet(&step->env, "SBATCH_MEM_BIND", val);
    ufree(val);

    if (step->memBindType & MEM_BIND_VERBOSE) {
	envSet(&step->env, "SLURM_MEM_BIND_VERBOSE", "verbose");
    } else {
	envSet(&step->env, "SLURM_MEM_BIND_VERBOSE", "quiet");
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
    envSet(&step->env, "SLURM_MEM_BIND_TYPE", val);

    envSet(&step->env, "SLURM_CHECKPOINT_IMAGE_DIR", step->checkpoint);
    envSet(&step->env, "SLURM_LAUNCH_NODE_IPADDR",
	    inet_ntoa(step->srun.sin_addr));
    envSet(&step->env, "SLURM_SRUN_COMM_HOST", inet_ntoa(step->srun.sin_addr));
    //envSet(&step->env, "PSI_LOGGERDEBUG", "1");
    //envSet(&step->env, "PSI_FORWARDERDEBUG", "1");

    /* forward overbook mode */
    if ((val = envGet(&step->env, "SLURM_OVERCOMMIT"))) {
	if (!strcmp(val, "1")) {
	    envSet(&step->env, "PSI_OVERBOOK", "1");
	}
    }

    /* unbuffered (raw I/O) mode */
    if (!step->bufferedIO) {
	envSet(&step->env, "__PSI_RAW_IO", "1");
	envSet(&step->env, "PSI_LOGGER_UNBUFFERED", "1");
    }

    if (!step->pty) envSet(&step->env, "PSI_INPUTDEST", "all");

    /* set slurm umask */
    if ((val = envGet(&step->env, "SLURM_UMASK"))) {
	slurmUmask = strtol(val, NULL, 8);
	umask(slurmUmask);
	envSet(&step->env, "__PSI_UMASK", val);
	envUnset(&step->env, "SLURM_UMASK");
    }

    envUnset(&step->env, "SLURM_MPI_TYPE");
    envSet(&step->env, "SLURM_JOB_USER", step->username);
    snprintf(tmp, sizeof(tmp), "%u", step->uid);
    envSet(&step->env, "SLURM_JOB_UID", tmp);

    /* handle memory mapping */
    if (step->memBindType & MEM_BIND_NONE) {
	envSet(&step->env, "__PSI_NO_MEMBIND", "1");
    }

    /* set SLURM_TASKS_PER_NODE */
    val = getTasksPerNode(step->tasksToLaunch, step->nrOfNodes);
    envSet(&step->env, "SLURM_TASKS_PER_NODE", val);

    removeSpankOptions(&step->env);
}

void setBatchEnv(Job_t *job)
{
    char tmp[1024], *val = NULL;
    mode_t slurmUmask;

    envSet(&job->env, "ENVIRONMENT", "BATCH");
    envSet(&job->env, "SLURM_NODEID", "0");
    envSet(&job->env, "SLURM_PROCID", "0");
    envSet(&job->env, "SLURM_LOCALID", "0");

    snprintf(tmp, sizeof(tmp), "%u", job->nodeMinMemory);
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
