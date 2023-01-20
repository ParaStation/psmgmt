/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmenv.h"

#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <limits.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pscommon.h"
#include "psipartition.h"
#include "pluginconfig.h"
#include "pluginmalloc.h"
#include "psidnodes.h"

#include "slurmcommon.h"

#include "psslurmalloc.h"
#include "psslurmcomm.h"
#include "psslurmconfig.h"
#include "psslurmgres.h"
#include "psslurmtopo.h"
#include "psslurmlog.h"
#include "psslurmpin.h"
#include "psslurmproto.h"

char **envFilter = NULL;

extern char **environ;

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
    uint32_t index = 0;
    if (envFilter) while (envFilter[index]) ufree(envFilter[index++]);
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

static void getCompactThreadList(StrBuffer_t *strBuf,
	const PSCPU_set_t threads)
{
    short numThreads = PSIDnodes_getNumThrds(PSC_getMyID());

    bool mapped[numThreads];
    memset(mapped, 0, sizeof(mapped));
    for (short t = 0; t < numThreads; t++) {
	short m = PSIDnodes_mapCPU(PSC_getMyID(), t);
	if (m < 0 || m > numThreads) continue;
	mapped[m] = PSCPU_isSet(threads, t);
    }

    if (psslurmlogger->mask & PSSLURM_LOG_ENV) {
	flog("handling threads set %s, mapped ",
	    PSCPU_print_part(threads, PSCPU_bytesForCPUs(numThreads)));
	for (short m = 0; m < numThreads; m++) mlog("%d", mapped[m]);
	mlog("\n");
    }

    strBuf->buf = NULL;
    strBuf->strLen = 0;
    char tmp[32];
    int last = -1;
    bool range = false;
    for (short m = 0; m < numThreads; m++) {
	if (!mapped[m]) continue;

	if (last < 0) {
	    /* found first CPU */
	    last = m;
	    continue;
	}

	if (!range) {
	    /* if we are not in a range, last is solo or started a range */
	    snprintf(tmp, sizeof(tmp), "%s%i", strBuf->strLen ? "," : "", last);
	    addStrBuf(tmp, strBuf);
	}

	/* check if m continues a range */
	if (last == m - 1) {
	    range = true;
	    last = m;
	    continue;
	}

	/* last is solo or finalized a range */
	if (range) {
	    /* last finalized a range */
	    snprintf(tmp, sizeof(tmp), "-%i", last);
	    addStrBuf(tmp, strBuf);
	    range = false;
	}

	last = m;
    }

    /* write last assigned thread */
    if (range) {
	snprintf(tmp, sizeof(tmp), "-%d", last);
    } else {
	snprintf(tmp, sizeof(tmp), "%s%i", strBuf->strLen ? "," : "", last);
    }
    addStrBuf(tmp, strBuf);
}

static void setThreadsBitmapsEnv(const PSCPU_set_t *stepcpus,
	const PSCPU_set_t *jobcpus)
{
    StrBuffer_t strBuf;
    if (stepcpus) {
	getCompactThreadList(&strBuf, *stepcpus);
	setenv("PSSLURM_STEP_CPUS", strBuf.buf, 1);
	freeStrBuf(&strBuf);
    }

    if (jobcpus) {
	getCompactThreadList(&strBuf, *jobcpus);
	setenv("PSSLURM_JOB_CPUS", strBuf.buf, 1);
	freeStrBuf(&strBuf);
    }
}

static void doSetJailMemEnv(const uint64_t ram, const char *scope)
{
    char name[256], val[128];
    char *prefix = "__PSJAIL_";

    /* total node memory in Byte */
    uint64_t nodeMem = getNodeMem()*1024*1024;

    /* RAM limits */
    uint64_t softRamLimit = ram, hardRamLimit = ram;

    /* allowed RAM in percent */
    float f = getConfValueF(&SlurmCgroupConfig, "AllowedRAMSpace");
    uint64_t allowedRam = (f != -1) ? (f/100.0) * ram : ram;
    hardRamLimit = allowedRam;
    if (hardRamLimit < softRamLimit) softRamLimit = hardRamLimit;

    /* max RAM in percent */
    f = getConfValueF(&SlurmCgroupConfig, "MaxRAMPercent");
    uint64_t maxRam = ram;
    if (f != -1) {
	maxRam = (f/100.0) * (nodeMem);
	if (hardRamLimit > maxRam) hardRamLimit = maxRam;
	if (softRamLimit > maxRam) softRamLimit = maxRam;
    }

    /* lower RAM limit in MByte */
    long minRam = getConfValueL(&SlurmCgroupConfig, "MinRAMSpace");
    if (minRam != -1) {
	uint64_t minRamLimit = minRam*1024*1024;
	if (softRamLimit < minRamLimit) softRamLimit = minRamLimit;
	if (hardRamLimit < minRamLimit) hardRamLimit = minRamLimit;
    }

    snprintf(name, sizeof(name), "%s%s_RAM_SOFT", prefix, scope);
    snprintf(val, sizeof(val), "%zu", softRamLimit);
    setenv(name, val, 1);

    snprintf(name, sizeof(name), "%s%s_RAM_HARD", prefix, scope);
    snprintf(val, sizeof(val), "%zu", hardRamLimit);
    setenv(name, val, 1);

    /* KMEM constrains */
    uint64_t kmemLimit = hardRamLimit;
    /* allowed KMEM in *bytes* */
    long aKmem = getConfValueL(&SlurmCgroupConfig, "AllowedKmemSpace");
    if (aKmem != -1) kmemLimit = aKmem;

    /* upper KMEM limit in percent */
    f = getConfValueF(&SlurmCgroupConfig, "MaxKmemPercent");
    if (f != -1) {
	uint64_t maxKmem = (f/100.0) * hardRamLimit;
	if (kmemLimit > maxKmem) kmemLimit = maxKmem;
    }

    /* lower KMEM limit in MByte */
    long minKmem = getConfValueL(&SlurmCgroupConfig, "MinKmemSpace");
    if (minKmem != -1) {
	uint64_t minKmemLimit = minKmem*1024*1024;
	if (kmemLimit < minKmemLimit) kmemLimit = minKmemLimit;
    }

    snprintf(name, sizeof(name), "%s%s_KMEM", prefix, scope);
    snprintf(val, sizeof(val), "%zu", kmemLimit);
    setenv(name, val, 1);

    /* swap constrain */
    uint64_t swapLimit = ram;
    /* allowed swap in percent */
    f = getConfValueF(&SlurmCgroupConfig, "AllowedSwapSpace");
    if (f != -1) {
	if (!swapLimit) swapLimit = nodeMem;
	swapLimit = ((f/100.0) * swapLimit) + allowedRam;
    }

    /* upper swap limit in percent */
    f = getConfValueF(&SlurmCgroupConfig, "MaxSwapPercent");
    if (f != -1) {
	uint64_t maxSwap = ((f/100.0) * nodeMem) + maxRam;
	if (swapLimit > maxSwap) swapLimit = maxSwap;
    }

    /* lower limit for swap equals lower limit for RAM */
    if (minRam != -1) {
	uint64_t minRamLimit = minRam*1024*1024;
	if (swapLimit < minRamLimit) swapLimit = minRamLimit;
    }

    snprintf(name, sizeof(name), "%s%s_SWAP", prefix, scope);
    snprintf(val, sizeof(val), "%zu", swapLimit);
    setenv(name, val, 1);

    /* memory swappiness */
    long swappiness = getConfValueL(&SlurmCgroupConfig, "MemorySwappiness");
    if (swappiness != -1) {
	if (swappiness > 100) swappiness = 100;
	snprintf(name, sizeof(name), "%sSWAPPINESS", prefix);
	snprintf(val, sizeof(val), "%lu", swappiness);
	setenv(name, val, 1);
    }

    fdbg(PSSLURM_LOG_JAIL, "%s requested ram %zu mem soft: %zu mem hard: %zu "
	 "kmem %zu swap %zu swappiness %li\n", scope, ram, softRamLimit,
	 hardRamLimit, kmemLimit, swapLimit, swappiness);
}

/**
 * @brief Set jail environment to constrain memory
 *
 * The slurmctld might not give a memory limit even though constraining
 * memory is activated. In this case the total memory of the node is
 * used as limit.
 *
 * @param cred The job credential holding memory information
 *
 * @param localNodeId Local node ID for this jobs/step
 */
static void setJailMemEnv(JobCred_t *cred, uint32_t localNodeId)
{
    /* total node memory in Byte */
    uint64_t nodeMem = getNodeMem()*1024*1024;

    /* set job env */
    if (cred->jobMemAllocSize) {
	for (uint32_t i=0, idx=0; i<cred->jobMemAllocSize; i++) {
	    for (uint32_t j=0; j<cred->jobMemAllocRepCount[i]; j++) {
		if (idx++ == localNodeId) {
		    uint64_t ramSpace = cred->jobMemAlloc[i]*1024*1024;
		    doSetJailMemEnv(ramSpace ? ramSpace : nodeMem, "JOB");
		    break;
		}
	    }
	    if (idx > localNodeId) break;
	}
    }

    /* set step env */
    if (cred->stepMemAllocSize) {
	for (uint32_t i=0, idx=0; i<cred->stepMemAllocSize; i++) {
	    for (uint32_t j=0; j<cred->stepMemAllocRepCount[i]; j++) {
		if (idx++ == localNodeId) {
		    uint64_t ramSpace = cred->stepMemAlloc[i]*1024*1024;
		    doSetJailMemEnv(ramSpace ? ramSpace : nodeMem, "STEP");
		    break;
		}
	    }
	    if (idx > localNodeId) break;
	}
    }
}

/**
 * @brief Set jail environment for GRes devices
 *
 * @param gresList List of generic resources for this job/step
 *
 * @param localNodeId Local node ID for this jobs/step
 */
static void setJailDevEnv(list_t *gresList, uint32_t localNodeId)
{
    list_t *g;
    uint16_t numDevAllow = 0, numDevDeny = 0;
    list_for_each(g, gresList) {
	Gres_Cred_t *gres = list_entry(g, Gres_Cred_t, next);

	PSCPU_set_t gresSet;
	if (gres->bitAlloc[localNodeId]) {
	    if (!hexBitstr2Set(gres->bitAlloc[localNodeId], gresSet)) {
		flog("unable to get gres node allocation for nodeId %u\n",
		     localNodeId);
		return;
	    }
	} else {
	    PSCPU_clrAll(gresSet);
	}

	char name[128], val[64];
	for (uint16_t i = 0; i < sizeof(PSCPU_set_t)/sizeof(PSCPU_mask_t);
	     i++) {
	    GRes_Dev_t *dev = GRes_findDevice(gres->id, i);
	    if (!dev) continue;
	    snprintf(val, sizeof(val), "%s %u:%u rwm",
		     dev->isBlock ? "b" : "c", dev->major, dev->minor);
	    if (PSCPU_isSet(gresSet, i)) {
		fdbg(PSSLURM_LOG_JAIL, "Allow GRes ID %u path %s num %u "
		     "major %u minor %u\n", gres->id, dev->path, i, dev->major,
		     dev->minor);
		snprintf(name, sizeof(name), "__PSJAIL_DEV_ALLOW_%u",
			 numDevAllow++);
		setenv(name, val, 1);
	    } else {
		fdbg(PSSLURM_LOG_JAIL, "Deny GRes ID %u path %s num %u "
		     "major %u minor %u\n", gres->id, dev->path, i, dev->major,
		     dev->minor);
		snprintf(name, sizeof(name), "__PSJAIL_DEV_DENY_%u",
			 numDevDeny++);
		setenv(name, val, 1);
	    }
	}
    }
}

void setJailEnv(const env_t *env, const char *user, const PSCPU_set_t *stepcpus,
		const PSCPU_set_t *jobcpus, list_t *gresList, JobCred_t *cred,
		uint32_t localNodeId)
{
    setThreadsBitmapsEnv(stepcpus, jobcpus);

    if (env) {
	char *id = envGet(env, "SLURM_JOBID");
	if (id) setenv("SLURM_JOBID", id, 1);
	id = envGet(env, "SLURM_STEPID");
	if (id) setenv("SLURM_STEPID", id, 1);
    }

    if (user) setenv("SLURM_USER", user, 1);

    char *c = getConfValueC(&SlurmCgroupConfig, "ConstrainCores");
    if (c) setenv("__PSJAIL_CONSTRAIN_CORES", c, 1);

    c = getConfValueC(&SlurmCgroupConfig, "ConstrainDevices");
    if (c) setenv("__PSJAIL_CONSTRAIN_DEVICES", c, 1);

    c = getConfValueC(&SlurmCgroupConfig, "ConstrainKmemSpace");
    if (c) setenv("__PSJAIL_CONSTRAIN_KMEM", c, 1);

    c = getConfValueC(&SlurmCgroupConfig, "ConstrainRAMSpace");
    if (c) setenv("__PSJAIL_CONSTRAIN_RAM", c, 1);

    c = getConfValueC(&SlurmCgroupConfig, "ConstrainSwapSpace");
    if (c) setenv("__PSJAIL_CONSTRAIN_SWAP", c, 1);

    if (gresList) setJailDevEnv(gresList, localNodeId);

    if (cred) setJailMemEnv(cred, localNodeId);
}

/**
 * @brief Set GRes job environment
 *
 * @param gresList The GRes list of the job
 *
 * @param env The environment of the job to change
 */
static void setGResJobEnv(list_t *gresList, env_t *env)
{
    /* GRes "gpu" plugin */
    Gres_Cred_t *gres = findGresCred(gresList, GRES_PLUGIN_GPU, GRES_CRED_JOB);
    if (gres && gres->bitAlloc) {
	if (gres->bitAlloc[0]) {
	    StrBuffer_t strList = { .buf = NULL };
	    hexBitstr2List(gres->bitAlloc[0], &strList, false);

	    /* always set informational variable */
	    envSet(env, "SLURM_JOB_GPUS", strList.buf);

	    /* tell doClamps() which gpus to use */
	    envSet(env, "__PSID_USE_GPUS", strList.buf);

	    char *prefix = "__AUTO_";
	    char name[GPU_VARIABLE_MAXLEN+strlen(prefix)+1];
	    for (size_t i = 0; gpu_variables[i]; i++) {
		/* set variable if not already set by the user */
		if (!envGet(env, gpu_variables[i])) {
		    snprintf(name, sizeof(name), "%s%s", prefix,
			    gpu_variables[i]);
		    /* append some spaces to help step code to detect whether
		     * the user has changed the variable in his job script */
		    char *val = umalloc(strList.strLen + 6);
		    sprintf(val, "%s     ", strList.buf);
		    envSet(env, gpu_variables[i], val);
		    envSet(env, name, val);
		    ufree(val);
		}
	    }

	    freeStrBuf(&strList);
	} else {
	    flog("invalid gpu gres bitAlloc for local nodeID 0\n");
	}
    }

    /* GRes "mic" plugin */
    gres = findGresCred(gresList, GRES_PLUGIN_MIC, GRES_CRED_JOB);
    if (gres && gres->bitAlloc) {
	if (gres->bitAlloc[0]) {
	    StrBuffer_t strList = { .buf = NULL };
	    hexBitstr2List(gres->bitAlloc[0], &strList, false);
	    envSet(env, "OFFLOAD_DEVICES", strList.buf);
	    freeStrBuf(&strList);
	} else {
	    flog("invalid mic gres bitAlloc for local nodeID 0\n");
	}
    }

    /* set JOB_GRES */
    gres = findGresCred(gresList, NO_VAL, GRES_CRED_JOB);
    if (gres && gres->bitAlloc) {
	if (gres->bitAlloc[0]) {
	    StrBuffer_t strList = { .buf = NULL };
	    hexBitstr2List(gres->bitAlloc[0], &strList, false);
	    envSet(env, "SLURM_JOB_GRES", strList.buf);
	    freeStrBuf(&strList);
	} else {
	    flog("invalid job gres bitAlloc for local nodeID 0\n");
	}
    }
}

void initJobEnv(Job_t *job)
{
    if (job->partition) {
	envSet(&job->env, "SLURM_JOB_PARTITION", job->partition);
    }

    envSet(&job->env, "SLURMD_NODENAME",
	   getConfValueC(&Config, "SLURM_HOSTNAME"));

    envSet(&job->env, "SLURM_JOBID", Job_strID(job->jobid));
    envSet(&job->env, "SLURM_JOB_ID", Job_strID(job->jobid));

    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%u", job->nrOfNodes);
    envSet(&job->env, "SLURM_JOB_NUM_NODES", tmp);
    envSet(&job->env, "SLURM_NNODES", tmp);
    envSet(&job->env, "SLURM_GTIDS", "0");
    envSet(&job->env, "SLURM_JOB_USER", job->username);
    snprintf(tmp, sizeof(tmp), "%u", job->uid);
    envSet(&job->env, "SLURM_JOB_UID", tmp);
    envSet(&job->env, "SLURM_CPUS_ON_NODE",
	   getConfValueC(&Config, "SLURM_CPUS"));

    char *cpus = getCPUsPerNode(job);
    envSet(&job->env, "SLURM_JOB_CPUS_PER_NODE", cpus);
    ufree(cpus);

    /* set SLURM_TASKS_PER_NODE for intel mpi */
    uint16_t *tasksPerNode = calcTasksPerNode(job);
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

    if (job->checkpoint) {
	/* removed in 21.08 */
	envSet(&job->env, "SLURM_CHECKPOINT_IMAGE_DIR", job->checkpoint);
    }

    if (!job->nodeAlias || !strlen(job->nodeAlias)) {
	envSet(&job->env, "SLURM_NODE_ALIASES", "(null)");
    } else {
	envSet(&job->env, "SLURM_NODE_ALIASES", job->nodeAlias);
    }

    if (job->hostname) envSet(&job->env, "HOSTNAME", job->hostname);

    /* set GRes environment */
    setGResJobEnv(&job->gresList, &job->env);
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
    Alloc_t *alloc = Alloc_find(step->jobid);
    Job_t *job = Job_findById(step->jobid);

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

static void setPsslurmEnv(env_t *alloc_env, env_t *dest_env)
{
    for (uint32_t i=0; i<alloc_env->cnt; i++) {
	if (alloc_env->vars[i] &&
	    !(strncmp("_PSSLURM_ENV_", alloc_env->vars[i], 13))) {
	    char *ptr = alloc_env->vars[i] + 13;
	    fdbg(PSSLURM_LOG_ENV, "set %s\n", ptr);
	    if (ptr) {
		if (dest_env) {
		    envPut(dest_env, ptr);
		} else {
		    putenv(ptr);
		}
	    }
	}
    }
}

static void setGPUEnv(Gres_Cred_t *gres, uint32_t jobNodeId, Step_t *step,
		      uint32_t localRankId)
{
    uint32_t stepNId = step->localNodeId;

    if (!gres->bitAlloc[jobNodeId]) {
	flog("invalid gpu gres bitAlloc for job node id %u\n", jobNodeId);
	return;
    }

    uint32_t ltnum = step->globalTaskIdsLen[stepNId];

    /* if there is only one local rank, bind all assigned GPUs to it */
    if (ltnum == 1) {
	/* nothing to do, gpu_variables[*] should already be set that way */
	flog("Only one task on this node, bind all assigned GPUs to it.\n");
	/* always set our own variable */
	char *value = getenv("SLURM_STEP_GPUS");
	setenv("PSSLURM_BIND_GPUS", value ? value : "", 1);
    /* if this is an interactive step, bind all assigned GPUs to it, too */
    } else if (step->stepid == SLURM_INTERACTIVE_STEP) {
	/* nothing to do, gpu_variables[*] should already be set that way */
	flog("interactive step detected, bind to all GPUs assigned"
		" (usually none)\n");
	/* always set our own variable */
	char *value = getenv("SLURM_STEP_GPUS");
	setenv("PSSLURM_BIND_GPUS", value ? value : "", 1);
    } else {
	/* get assigned GPUs from GRES info */
	PSCPU_set_t assGPUs;
	if (!hexBitstr2Set(gres->bitAlloc[jobNodeId], assGPUs)) {
	    flog("failed to get assigned GPUs from bitstring\n");
	    return;
	}

	int16_t gpu = getRankGpuPinning(localRankId, step, stepNId, &assGPUs);
	if (gpu < 0) return;

	char tmp[10];
	snprintf(tmp, sizeof(tmp), "%hd", gpu);

	/* always set our own variable */
	setenv("PSSLURM_BIND_GPUS", tmp, 1);
    }

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
	    setenv(gpu_variables[i], getenv("PSSLURM_BIND_GPUS"), 1);
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
	StrBuffer_t strList = { .buf = NULL };

	/* gres "gpu" plugin */
	gres = findGresCred(&step->gresList, GRES_PLUGIN_GPU, GRES_CRED_STEP);
	if (gres && gres->bitAlloc) {
	    setGPUEnv(gres, jobNodeId, step, localRankId);
	}

	/* gres "mic" plugin */
	gres = findGresCred(&step->gresList, GRES_PLUGIN_MIC, GRES_CRED_STEP);
	if (gres && gres->bitAlloc) {
	    if (gres->bitAlloc[jobNodeId]) {
		hexBitstr2List(gres->bitAlloc[jobNodeId], &strList, false);
		setenv("OFFLOAD_DEVICES", strList.buf, 1);
		freeStrBuf(&strList);
	    } else {
		flog("invalid mic gres bitAlloc for job node ID %u\n",
		     jobNodeId);
	    }
	}

	/* set STEP_GRES */
	gres = findGresCred(&step->gresList, NO_VAL, GRES_CRED_STEP);
	if (gres && gres->bitAlloc) {
	    if (gres->bitAlloc[jobNodeId]) {
		hexBitstr2List(gres->bitAlloc[jobNodeId], &strList, false);
		setenv("SLURM_STEP_GRES", strList.buf, 1);
		freeStrBuf(&strList);
	    } else {
		flog("invalid step gres bitAlloc for job node ID %u\n",
		     jobNodeId);
	    }
	}
    } else {
	flog("unable to set gres: invalid job node ID for %s\n",
	     Step_strID(step));
    }
}

static void doUnset(char *env)
{
    char *name = strchr(env, '=');
    if (!name) return;
    name[0] = '\0';
    unsetenv(env);
}

/**
 * @brief Set additional environment for an interactive rank
 *
 * @param step The step to set the environment for
 */
static void setInteractiveRankEnv(Step_t *step)
{
    char **env = environ;
    while (*env) {
	if (!strncmp(*env, "MPI_", 4)) doUnset(*env);
	if (!strncmp(*env, "OLDPWD", 6)) doUnset(*env);
	env++;
    }

    /* we need to set the GRes job environment variables
     * for the interactive step */
    env_t gresEnv;
    envInit(&gresEnv);
    setGResJobEnv(&step->gresList, &gresEnv);
    for (uint32_t i = 0; i < gresEnv.cnt; i++) {
	char *val = strchr(gresEnv.vars[i], '=');
	if (val) {
	    val[0]='\0';
	    setenv(gresEnv.vars[i], ++val, 1);
	}
    }
    envDestroy(&gresEnv);
}

/**
 * @brief Set additional environment for an ordinary rank
 *
 * @param rank The rank to set the environment for
 *
 * @param step The step to set the environment for
 */
static void setOrdinaryRankEnv(uint32_t rank, Step_t *step)
{
    /* set cpu/memory bind env vars */
    setBindingEnvVars(step);

    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%u", step->tpp);
    setenv("SLURM_CPUS_PER_TASK", tmp, 1);

    uint32_t myLocalId = getLocalRankID(rank, step);
    if (myLocalId == NO_VAL) {
	flog("failed to find local ID for rank %u\n", rank);
    } else {
	snprintf(tmp, sizeof(tmp), "%u", myLocalId);
	setenv("SLURM_LOCALID", tmp, 1);

	/* set GRes environment */
	setGresEnv(myLocalId, step);
    }
}

void setSlurmConfEnvVar(env_t *env)
{
    char *confServer = getConfValueC(&Config, "SLURM_CONF_SERVER");
    if (confServer && strcmp(confServer, "none")) {
	/* ensure the configuration cache is used */
	char *confDir = getConfValueC(&Config, "SLURM_CONFIG_DIR");
	if (!confDir) {
	    flog("warning: invalid SLURM_CONFIG_DIR");
	    return;
	}

	char *confFile = getConfValueC(&Config, "SLURM_CONF");
	if (!confFile) {
	    flog("warning: invalid SLURM_CONF");
	    return;
	}

	char cPath[PATH_MAX];
	snprintf(cPath, sizeof(cPath), "%s/%s", confDir, confFile);
	if (env) {
	    envSet(env, "SLURM_CONF", cPath);
	} else {
	    setenv("SLURM_CONF", cPath, 1);
	}
    }
}

static void setTopoEnv(env_t *env)
{
    Topology_t *topo = getTopology(getConfValueC(&Config, "SLURM_HOSTNAME"));
    if (env) {
	envSet(env, "SLURM_TOPOLOGY_ADDR", topo->address);
	envSet(env, "SLURM_TOPOLOGY_ADDR_PATTERN", topo->pattern);
    } else {
	setenv("SLURM_TOPOLOGY_ADDR", topo->address, 1);
	setenv("SLURM_TOPOLOGY_ADDR_PATTERN", topo->pattern, 1);
    }

    clearTopology(topo);
}

/**
 * @brief Set common environment for a rank
 *
 * @param rank The rank to set the environment for
 *
 * @param step The step to set the environment for
 */
static void setCommonRankEnv(int32_t rank, Step_t *step)
{
    /* remove unwanted variables */
    unsetenv("PSI_INPUTDEST");
    unsetenv(ENV_PSID_BATCH);

    /* we need the DISPLAY variable set by psslurm */
    char *display = getenv("DISPLAY");

    /* set environment variables from user */
    for (uint32_t i = 0; i < step->env.cnt; i++) {
	/* protect selected variables from changes */
	if (!(strncmp(step->env.vars[i], "SLURM_RLIMIT_", 13))) continue;
	if (!(strncmp(step->env.vars[i], "SLURM_UMASK=", 12))) continue;
	if (!(strncmp(step->env.vars[i], "PWD=", 4))) continue;
	if (display && !(strncmp(step->env.vars[i], "DISPLAY=", 8))) continue;
	if (!(strncmp(step->env.vars[i], "PMI_FD=", 7))) continue;
	if (!(strncmp(step->env.vars[i], "PMI_PORT=", 9))) continue;
	if (!(strncmp(step->env.vars[i], "PMI_RANK=", 9))) continue;
	if (!(strncmp(step->env.vars[i], "PMI_SIZE=", 9))) continue;
	if (!(strncmp(step->env.vars[i], "PMI_UNIVERSE_SIZE=", 18))) continue;
	if (!(strncmp(step->env.vars[i], "PMI_ID=", 7))) continue;
	if (!(strncmp(step->env.vars[i], "PMI_APPNUM=", 11))) continue;
	if (!(strncmp(step->env.vars[i], "PMI_ENABLE_SOCKP=", 17))) continue;
	if (!(strncmp(step->env.vars[i], "PMI_SUBVERSION=", 15))) continue;
	if (!(strncmp(step->env.vars[i], "PMI_VERSION=", 12))) continue;
	if (!(strncmp(step->env.vars[i], "PMI_BARRIER_ROUNDS=", 19))) continue;
	if (!strncmp(step->env.vars[i], "PMIX_", 5)
		&& strncmp(step->env.vars[i], "PMIX_MCA_", 9)) continue;
	if (!(strncmp(step->env.vars[i], "PSP_SMP_NODE_ID=", 16))) continue;

	putenv(step->env.vars[i]);
    }

    /* use pwd over cwd if realpath is identical */
    char *pwd = envGet(&step->env, "PWD");
    if (pwd) {
	char *rpath = realpath(pwd, NULL);
	if (rpath && !strcmp(rpath, step->cwd)) {
	    setenv("PWD", pwd, 1);
	}
	free(rpath);
    }

    setSlurmConfEnvVar(NULL);

    setenv("SLURMD_NODENAME", getConfValueC(&Config, "SLURM_HOSTNAME"), 1);
    char tmp[128];
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
	char *myGTIDs = GTIDsToList(step);
	setenv("SLURM_GTIDS", myGTIDs, 1);
	ufree(myGTIDs);
    }

    setenv("SLURM_LAUNCH_NODE_IPADDR", inet_ntoa(step->srun.sin_addr), 1);
    setenv("SLURM_SRUN_COMM_HOST", inet_ntoa(step->srun.sin_addr), 1);

    setenv("SLURM_JOB_USER", step->username, 1);
    snprintf(tmp, sizeof(tmp), "%u", step->uid);
    setenv("SLURM_JOB_UID", tmp, 1);
    snprintf(tmp, sizeof(tmp), "%u", step->gid);
    setenv("SLURM_JOB_GID", tmp, 1);

    /* set SLURM_TASKS_PER_NODE */
    char *val = getTasksPerNode(step->tasksToLaunch, step->nrOfNodes);
    setenv("SLURM_TASKS_PER_NODE", val, 1);

    /* set topology environment */
    setTopoEnv(NULL);
}

void setRankEnv(int32_t rank, Step_t *step)
{
    setCommonRankEnv(rank, step);

    Alloc_t *alloc = Alloc_find(step->jobid);
    if (alloc) setPsslurmEnv(&alloc->env, NULL);

    if (step->stepid == SLURM_INTERACTIVE_STEP) {
	return setInteractiveRankEnv(step);
    } else {
	return setOrdinaryRankEnv(rank, step);
    }
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
	    envSet(&step->env, ENV_PART_OVERBOOK, "1");
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
		StrBuffer_t strList = { .buf = NULL };
		hexBitstr2List(gres->bitAlloc[jobNodeId], &strList, false);

		/* always set informational variable */
		envSet(&step->env, "SLURM_STEP_GPUS", strList.buf);

		/* tell doClamps() which gpus to use */
		envSet(&step->env, "__PSID_USE_GPUS", strList.buf);

		freeStrBuf(&strList);
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

    if (job->nodeMinMemory & MEM_PER_CPU) {
	snprintf(tmp, sizeof(tmp), "%lu", job->nodeMinMemory & (~MEM_PER_CPU));
	envSet(&job->env, "SLURM_MEM_PER_CPU", tmp);
    } else if (job->nodeMinMemory) {
	snprintf(tmp, sizeof(tmp), "%lu", job->nodeMinMemory);
	envSet(&job->env, "SLURM_MEM_PER_NODE", tmp);
    }

    snprintf(tmp, sizeof(tmp), "%u", getpid());
    envSet(&job->env, "SLURM_TASK_PID", tmp);

    snprintf(tmp, sizeof(tmp), "%u", job->gid);
    envSet(&job->env, "SLURM_JOB_GID", tmp);

    /* forward overbook mode */
    char *val = envGet(&job->env, "SLURM_OVERCOMMIT");

    if (job->overcommit || (val && !strcmp(val, "1"))) {
	envSet(&job->env, ENV_PART_OVERBOOK, "1");
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

    setSlurmConfEnvVar(&job->env);

    char *cname = getConfValueC(&SlurmConfig, "ClusterName");
    if (cname) envSet(&job->env, "SLURM_CLUSTER_NAME", cname);

    if (job->account) envSet(&job->env, "SLURM_JOB_ACCOUNT", job->account);
    if (job->qos) envSet(&job->env, "SLURM_JOB_QOS", job->qos);

    uint32_t numGPUs = GRes_countDevices(GRES_PLUGIN_GPU);
    if (numGPUs) {
	snprintf(tmp, sizeof(tmp), "%u", numGPUs);
	envSet(&job->env, "SLURM_GPUS_ON_NODE", tmp);
    }

    /* set topology environment */
    setTopoEnv(&job->env);

    Alloc_t *alloc = Alloc_find(job->jobid);
    if (alloc) setPsslurmEnv(&alloc->env, &job->env);
}
