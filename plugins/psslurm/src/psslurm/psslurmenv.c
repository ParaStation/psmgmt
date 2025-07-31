/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
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
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "pscommon.h"
#include "pscpu.h"
#include "psstrbuf.h"
#include "psstrv.h"
#include "psipartition.h"
#include "pluginconfig.h"
#include "pluginmalloc.h"
#include "psidnodes.h"
#include "psidpin.h"

#include "slurmcommon.h"

#include "psslurmalloc.h"
#include "psslurmcomm.h"
#include "psslurmconfig.h"
#include "psslurmgres.h"
#include "psslurmtopo.h"
#include "psslurmlog.h"
#include "psslurmpin.h"
#include "psslurmproto.h"

extern char **environ;

#define IS_SET(s) (s && (s)[0])

static strv_t envFilterData;

bool initEnvFilter(void)
{
    char *conf = getConfValueC(Config, "PELOGUE_ENV_FILTER");
    if (!conf) {
	flog("invalid PELOGUE_ENV_FILTER config option");
	return false;
    }

    if (strvInitialized(envFilterData)) return true;
    envFilterData = strvNew(NULL);

    char *dup = ustrdup(conf);
    const char delimiters[] =",\n";
    char *saveptr, *next = strtok_r(dup, delimiters, &saveptr);
    while (next) {
	strvAdd(envFilterData, next);
	next = strtok_r(NULL, delimiters, &saveptr);
    }

    ufree(dup);

    return true;
}

void freeEnvFilter(void)
{
    strvDestroy(envFilterData);
    envFilterData = NULL;
}

bool envFilterFunc(const char *envStr)
{
    for (char **cur = strvGetArray(envFilterData); cur && *cur; cur++) {
	size_t len = strlen(*cur);
	size_t cmpLen = ((*cur)[len-1] == '*') ? (len-1) : len;
	if (!strncmp(*cur, envStr, cmpLen)
	    && (envStr[len] == '=' || (*cur)[len-1] == '*')) return true;
    }
    return false;
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
    strbuf_t buf = strbufNew(NULL);

    for (uint32_t i = 0; i < job->cpuGroupCount; i++) {
	if (i) strbufAdd(buf, ",");

	char tmp[256];
	if (job->cpuCountReps[i] > 1) {
	    snprintf(tmp, sizeof(tmp), "%u(x%u)", job->cpusPerNode[i],
		     job->cpuCountReps[i]);
	} else {
	    snprintf(tmp, sizeof(tmp), "%u", job->cpusPerNode[i]);
	}
	strbufAdd(buf, tmp);
    }
    return strbufSteal(buf);
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
    uint32_t N = job->nrOfNodes;
    uint32_t n = job->np;

    if (!N || !n) return NULL;

    uint16_t *tasksPerNode = umalloc(N * sizeof(*tasksPerNode));

    for (uint32_t i = 0; i < N; i++) {
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
    if (nrOfNodes == 0) return NULL;

    strbuf_t buf = strbufNew(NULL);

    uint16_t count = 0, current = 0;
    uint16_t last = tasksPerNode[0]; /* for loop initialization */
    for (uint32_t i = 0; i <= nrOfNodes; i++) {
	if (i != nrOfNodes) { /* don't do this in the last iteration */
	    current = tasksPerNode[i];
	    if (current == last) {
		count++;
		continue;
	    }
	}

	char tmp[32];
	if (count == 1) {
	    snprintf(tmp, sizeof(tmp), "%u,", last);
	} else {
	    snprintf(tmp, sizeof(tmp), "%u(x%d),", last, count);
	}
	if (strbufLen(buf)) strbufAdd(buf, ",");
	strbufAdd(buf, tmp);

	last = current;
	count = 1;
    }
    return strbufSteal(buf);
}

static char *getCompactThreadList(const PSCPU_set_t threads)
{
    short numThreads = PSIDnodes_getNumThrds(PSC_getMyID());

    bool mapped[numThreads];
    memset(mapped, 0, sizeof(mapped));
    for (short t = 0; t < numThreads; t++) {
	short m = PSIDnodes_mapCPU(PSC_getMyID(), t);
	if (m < 0 || m > numThreads) continue;
	mapped[m] = PSCPU_isSet(threads, t);
    }

    if (mset(PSSLURM_LOG_ENV)) {
	flog("handling threads set %s, mapped ",
	    PSCPU_print_part(threads, PSCPU_bytesForCPUs(numThreads)));
	for (short m = 0; m < numThreads; m++) mlog("%d", mapped[m]);
	mlog("\n");
    }

    strbuf_t buf = strbufNew(NULL);

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
	    snprintf(tmp, sizeof(tmp), "%s%i", strbufLen(buf) ? "," : "", last);
	    strbufAdd(buf, tmp);
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
	    strbufAdd(buf, tmp);
	    range = false;
	}

	last = m;
    }

    /* write last assigned thread */
    if (range) {
	snprintf(tmp, sizeof(tmp), "-%d", last);
    } else {
	snprintf(tmp, sizeof(tmp), "%s%i", strbufLen(buf) ? "," : "", last);
    }
    strbufAdd(buf, tmp);

    return strbufSteal(buf);
}

static void setThreadsBitmapsEnv(const PSCPU_set_t stepcpus,
				 const PSCPU_set_t jobcpus)
{
    if (stepcpus) {
	char *threadListStr = getCompactThreadList(stepcpus);
	setenv("__PSJAIL_STEP_CPUS", threadListStr, 1);
	fdbg(PSSLURM_LOG_JAIL, "step cpus: %s\n",threadListStr);
	free(threadListStr);
    }

    if (jobcpus) {
	char *threadListStr = getCompactThreadList(jobcpus);
	setenv("__PSJAIL_JOB_CPUS", threadListStr, 1);
	fdbg(PSSLURM_LOG_JAIL, "job cpus: %s\n", threadListStr);
	free(threadListStr);
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
    float f = getConfValueF(SlurmCgroupConfig, "AllowedRAMSpace");
    uint64_t allowedRam = (f >= 0) ? (f/100.0) * ram : ram;
    hardRamLimit = allowedRam;
    if (hardRamLimit < softRamLimit) softRamLimit = hardRamLimit;

    /* max RAM in percent */
    f = getConfValueF(SlurmCgroupConfig, "MaxRAMPercent");
    uint64_t maxRam = ram;
    if (f >= 0) {
	maxRam = (f/100.0) * (nodeMem);
	if (hardRamLimit > maxRam) hardRamLimit = maxRam;
	if (softRamLimit > maxRam) softRamLimit = maxRam;
    }

    /* lower RAM limit in MByte */
    long minRam = getConfValueL(SlurmCgroupConfig, "MinRAMSpace");
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

    /* swap constrain */
    uint64_t swapLimit = ram;
    /* allowed swap in percent */
    f = getConfValueF(SlurmCgroupConfig, "AllowedSwapSpace");
    if (f >= 0) {
	if (!swapLimit) swapLimit = nodeMem;
	swapLimit = ((f/100.0) * swapLimit) + allowedRam;
    }

    /* upper swap limit in percent */
    f = getConfValueF(SlurmCgroupConfig, "MaxSwapPercent");
    if (f >= 0) {
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
    long swappiness = getConfValueL(SlurmCgroupConfig, "MemorySwappiness");
    if (swappiness != -1) {
	if (swappiness > 100) swappiness = 100;
	snprintf(name, sizeof(name), "%sSWAPPINESS", prefix);
	snprintf(val, sizeof(val), "%lu", swappiness);
	setenv(name, val, 1);
    }

    fdbg(PSSLURM_LOG_JAIL, "%s requested ram %zu mem soft: %zu mem hard: %zu"
	 " swap %zu swappiness %li\n", scope, ram, softRamLimit,
	 hardRamLimit, swapLimit, swappiness);
}

/**
 * @brief Set jail environment to constrain memory
 *
 * The slurmctld might not give a memory limit even though constraining
 * memory is activated. In this case the total memory of the node is
 * used as limit.
 *
 * @param cred Job credential holding memory information
 *
 * @param credID Credentail ID for this jobs/step
 */
static void setJailMemEnv(JobCred_t *cred, uint32_t credID)
{
    /* total node memory in Byte */
    uint64_t nodeMem = getNodeMem()*1024*1024;

    /* set job env */
    if (cred->jobMemAllocSize) {
	uint32_t i = 0, idx = 0;
	while (i < cred->jobMemAllocSize
	       && idx + cred->jobMemAllocRepCount[i] < credID)
	    idx += cred->jobMemAllocRepCount[i++];
	if (i < cred->jobMemAllocSize) {
	    uint64_t ramSpace = cred->jobMemAlloc[i]*1024*1024;
	    doSetJailMemEnv(ramSpace ? ramSpace : nodeMem, "JOB");
	}
    }

    /* set step env */
    if (cred->stepMemAllocSize) {
	uint32_t i = 0, idx = 0;
	while (i < cred->stepMemAllocSize
	       && idx + cred->stepMemAllocRepCount[i] < credID)
	    idx += cred->stepMemAllocRepCount[i++];
	if (i < cred->stepMemAllocSize) {
	    uint64_t ramSpace = cred->stepMemAlloc[i]*1024*1024;
	    doSetJailMemEnv(ramSpace ? ramSpace : nodeMem, "STEP");
	}
    }
}

static bool devEnvVisitor(GRes_Dev_t *dev, uint32_t id, void *info)
{
    PSCPU_set_t *set = info;

    bool isSet = PSCPU_isSet(*set, dev->slurmIdx);
    if (!isSet) {
	fdbg(PSSLURM_LOG_JAIL, "Skipping GRes ID %u path %s num %u major %u "
	     "minor %u\n", id, dev->path, dev->slurmIdx, dev->major,
	     dev->minor);
	return false;
    }

    fdbg(PSSLURM_LOG_JAIL, "Allow GRes ID %u path %s num %u major %u "
	 "minor %u\n", id, dev->path, dev->slurmIdx, dev->major, dev->minor);

    char val[64];
    snprintf(val, sizeof(val), "%s %u:%u rwm",
	     dev->isBlock ? "b" : "c", dev->major, dev->minor);

    char name[128];
    static int count = 0;
    snprintf(name, sizeof(name), "__PSJAIL_DEV_ALLOW_%u", count++);
    setenv(name, val, 1);

    return false;
}

/**
 * @brief Set jail environment for GRes devices
 *
 * @param gresList List of generic resources for this job/step
 *
 * @param credType GRes credential type to use
 *
 * @param credID Credentail ID for this jobs/step
 */
static void setJailDevEnv(list_t *gresList, GRes_Cred_type_t credType,
			  uint32_t credID)
{
    fdbg(PSSLURM_LOG_JAIL, "type %s credID %u\n", GRes_strType(credType),
	 credID);

    list_t *g;
    list_for_each(g, gresList) {
	Gres_Cred_t *gres = list_entry(g, Gres_Cred_t, next);
	if (gres->credType != credType) {
	    fdbg(PSSLURM_LOG_JAIL, "skip bitAlloc of gres %i name %s type %s "
		 "credID %u\n", gres->hash, GRes_getNamebyHash(gres->hash),
		 GRes_strType(gres->credType), credID);
	    continue;
	}

	fdbg(PSSLURM_LOG_JAIL, "test bitAlloc of gres %i name %s type %s "
	     "credID %u\n", gres->hash, GRes_getNamebyHash(gres->hash),
	     GRes_strType(gres->credType), credID);

	PSCPU_set_t set;
	PSCPU_clrAll(set);

	if (gres->bitAlloc && gres->bitAlloc[credID]
	    && !hexBitstr2Set(gres->bitAlloc[credID], set)) {
	    flog("unable to get gres node allocation for credId %u\n", credID);
	    return;
	}

	traverseGResDevs(gres->hash, devEnvVisitor, &set);
    }
}

/**
 * @brief Deny all configured devices
 *
 * The allowed devices will overwrite this setting later.
 *
 * @param conf gres configuration possible holding devices
 *
 * @param info unused
 *
 * @return Always returns false to continue
 */
static bool denyAllDevs(Gres_Conf_t *conf, void *info)
{
    static int n = 0;
    list_t *d;
    list_for_each(d, &conf->devices) {
	GRes_Dev_t *dev = list_entry(d, GRes_Dev_t, next);

	char val[64];
	snprintf(val, sizeof(val), "%s %u:%u rwm",
		 dev->isBlock ? "b" : "c", dev->major, dev->minor);

	char name[128];
	snprintf(name, sizeof(name), "__PSJAIL_DEV_DENY_%u", n++);
	setenv(name, val, 1);
    }

    return false;
}

void setJailEnv(const env_t env, const char *user, const PSCPU_set_t stepcpus,
		const PSCPU_set_t jobcpus, list_t *gresList,
		GRes_Cred_type_t credType, JobCred_t *cred,
		uint32_t credID)
{
    static bool isInit = false;
    if (isInit || PSC_isDaemon()) {
	flog("do not call within main psid or twice per process\n");
	return;
    }

    setThreadsBitmapsEnv(stepcpus, jobcpus);

    if (envInitialized(env)) {
	char *id = envGet(env, "SLURM_JOBID");
	if (id) setenv("__PSJAIL_JOBID", id, 1);
	id = envGet(env, "SLURM_STEPID");
	if (id) setenv("__PSJAIL_STEPID", id, 1);
    } else {
	char *id = getenv("SLURM_JOBID");
	if (id) setenv("__PSJAIL_JOBID", id, 1);
	id = getenv("SLURM_STEPID");
	if (id) setenv("__PSJAIL_STEPID", id, 1);
    }

    if (user) setenv("__PSJAIL_USER", user, 1);

    const char *c = getConfValueC(SlurmCgroupConfig, "ConstrainDevices");
    if (c && !strcasecmp(c, "yes") && gresList) {
	traverseGresConf(&denyAllDevs, NULL);
	setJailDevEnv(gresList, credType, credID);
    }

    if (cred) setJailMemEnv(cred, credID);

    isInit = true;
}

void setGlobalJailEnvironment(void)
{
    char *c = getConfValueC(SlurmCgroupConfig, "ConstrainCores");
    if (c) setenv("__PSJAIL_CONSTRAIN_CORES", c, 1);

    c = getConfValueC(SlurmCgroupConfig, "ConstrainDevices");
    if (c) setenv("__PSJAIL_CONSTRAIN_DEVICES", c, 1);

    c = getConfValueC(SlurmCgroupConfig, "ConstrainRAMSpace");
    if (c) setenv("__PSJAIL_CONSTRAIN_RAM", c, 1);

    c = getConfValueC(SlurmCgroupConfig, "ConstrainSwapSpace");
    if (c) setenv("__PSJAIL_CONSTRAIN_SWAP", c, 1);

    c = getConfValueC(SlurmCgroupConfig, "CgroupPlugin");
    if (c) {
	if (!strcmp(c, "cgroup/v2")) {
	    setenv("__PSJAIL_CGROUP_VERSION", "v2", 1);
	} else {
	    setenv("__PSJAIL_CGROUP_VERSION", "autodetect", 1);
	}
    }
}

/**
 * @brief Set GRes job environment
 *
 * @param gresList The GRes list of the job
 *
 * @param env The environment of the job to change
 */
static void setGResJobEnv(list_t *gresList, env_t env)
{
    /* GRes "gpu" plugin */
    Gres_Cred_t *gres = findGresCred(gresList, GRES_PLUGIN_GPU, GRES_CRED_JOB);
    if (gres && gres->bitAlloc) {
	if (gres->bitAlloc[0]) {
	    strbuf_t strList = strbufNew(NULL);
	    hexBitstr2List(gres->bitAlloc[0], strList, false);

	    /* always set informational variable */
	    envSet(env, "SLURM_JOB_GPUS", strbufStr(strList));

	    /* tell doClamps() which gpus to use
	     * this is for the case of using pure mpiexec in the job script
	     * or from the interactive step */
	    envSet(env, "__PSID_USE_GPUS", strbufStr(strList));

	    /* deactivate automatic GPU pinning in next PSIDpin_doClamps() */
	    envSet(env, "__PSID_SKIP_PIN_GPUS", "1");

	    /* append some spaces to help step code to detect whether
	     * the user has changed the variable in his job script */
	    strbufAdd(strList, "     ");

	    for (size_t i = 0; PSIDpin_GPUvars[i]; i++) {
		/* set variable if not already set by the user */
		if (envGet(env, PSIDpin_GPUvars[i])) continue;

		envSet(env, PSIDpin_GPUvars[i], strbufStr(strList));
		char *autoName = PSIDpin_getAutoName(PSIDpin_GPUvars[i]);
		envSet(env, autoName, strbufStr(strList));
		free(autoName);
	    }
	    strbufDestroy(strList);
	} else {
	    flog("invalid gpu gres bitAlloc for local nodeID 0\n");
	}
    }

    /* GRes "mic" plugin */
    gres = findGresCred(gresList, GRES_PLUGIN_MIC, GRES_CRED_JOB);
    if (gres && gres->bitAlloc) {
	if (gres->bitAlloc[0]) {
	    strbuf_t strList = strbufNew(NULL);
	    hexBitstr2List(gres->bitAlloc[0], strList, false);
	    envSet(env, "OFFLOAD_DEVICES", strbufStr(strList));
	    strbufDestroy(strList);
	} else {
	    flog("invalid mic gres bitAlloc for local nodeID 0\n");
	}
    }

    /* set JOB_GRES */
    gres = findGresCred(gresList, NO_VAL, GRES_CRED_JOB);
    if (gres && gres->bitAlloc) {
	if (gres->bitAlloc[0]) {
	    strbuf_t strList = strbufNew(NULL);
	    hexBitstr2List(gres->bitAlloc[0], strList, false);
	    envSet(env, "SLURM_JOB_GRES", strbufStr(strList));
	    strbufDestroy(strList);
	} else {
	    flog("invalid job gres bitAlloc for local nodeID 0\n");
	}
    }
}

static void doSetEnv(env_t env, char *key, char *val)
{
    if (val[0] == '\0') return;

    if (envInitialized(env)) {
	envSet(env, key, val);
    } else {
	setenv(key, val, 1);
    }
}

/**
 * @brief Set various environment variables from job credential
 *
 * Already set by srun/slurmctld: SLURM_JOB_ACCOUNT, SLURM_JOB_PARTITION
 */
static void setCredEnv(JobCred_t *cred, env_t env)
{
    char tmp[1024];

    if (!cred) return;

    if (cred->jobStartTime) {
	snprintf(tmp, sizeof(tmp), "%lu", cred->jobStartTime);
	doSetEnv(env, "SLURM_JOB_START_TIME", tmp);
    }

    if (cred->jobEndTime) {
	snprintf(tmp, sizeof(tmp), "%lu", cred->jobEndTime);
	doSetEnv(env, "SLURM_JOB_END_TIME", tmp);
    }

    if (cred->jobExtra) doSetEnv(env, "SLURM_JOB_EXTRA", cred->jobExtra);

    if (cred->jobLicenses) {
	doSetEnv(env, "SLURM_JOB_LICENSES", cred->jobLicenses);
    }

    if (cred->jobStderr) doSetEnv(env, "SLURM_JOB_STDERR", cred->jobStderr);

    if (cred->jobStdin) doSetEnv(env, "SLURM_JOB_STDIN", cred->jobStdin);

    if (cred->jobStdout) doSetEnv(env, "SLURM_JOB_STDOUT", cred->jobStdout);

    if (cred->jobRestartCount != INFINITE16) {
	snprintf(tmp, sizeof(tmp), "%u", cred->jobRestartCount);
	doSetEnv(env, "SLURM_JOB_RESTART_COUNT", tmp);
    }

    if (cred->jobReservation) {
	doSetEnv(env, "SLURM_JOB_RESERVATION", cred->jobReservation);
    }

    if (cred->jobComment) doSetEnv(env, "SLURM_JOB_COMMENT", cred->jobComment);

    if (cred->jobConstraints) {
	doSetEnv(env, "SLURM_JOB_CONSTRAINTS", cred->jobConstraints);
    }
}

void initJobEnv(Job_t *job)
{
    if (job->partition) envSet(job->env, "SLURM_JOB_PARTITION", job->partition);

    envSet(job->env, "SLURMD_NODENAME", getConfValueC(Config, "SLURM_HOSTNAME"));

    envSet(job->env, "SLURM_JOBID", Job_strID(job->jobid));
    envSet(job->env, "SLURM_JOB_ID", Job_strID(job->jobid));

    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%u", job->nrOfNodes);
    envSet(job->env, "SLURM_JOB_NUM_NODES", tmp);
    envSet(job->env, "SLURM_NNODES", tmp);
    envSet(job->env, "SLURM_GTIDS", "0");
    envSet(job->env, "SLURM_JOB_USER", job->username);
    snprintf(tmp, sizeof(tmp), "%u", job->uid);
    envSet(job->env, "SLURM_JOB_UID", tmp);
    envSet(job->env, "SLURM_CPUS_ON_NODE", getConfValueC(Config, "SLURM_CPUS"));

    /* ensure USER is set (even if sbatch --export=NONE is used) */
    if (!envGet(job->env, "USER")) {
	envSet(job->env, "USER", job->username);
    }

    char *cpus = getCPUsPerNode(job);
    envSet(job->env, "SLURM_JOB_CPUS_PER_NODE", cpus);
    ufree(cpus);

    /* number of process might not always be set and has to be calculated */
    if (!job->np) {
	char *numTasksPerNode = envGet(job->env, "SLURM_NTASKS_PER_NODE");
	if (numTasksPerNode) job->np = atoi(numTasksPerNode) * job->nrOfNodes;
    }

    /* SLURM_NTASKS and SLURM_NPROCS has to be set by psslurm */
    if (job->np) {
	snprintf(tmp, sizeof(tmp), "%u", job->np);
	envSet(job->env, "SLURM_NTASKS", tmp);
	envSet(job->env, "SLURM_NPROCS", tmp);
    }

    /* set SLURM_TASKS_PER_NODE for intel mpi */
    uint16_t *tasksPerNode = calcTasksPerNode(job);
    if (tasksPerNode) {
	cpus = getTasksPerNode(tasksPerNode, job->nrOfNodes);
	ufree(tasksPerNode);
    } else {
	cpus = getCPUsPerNode(job);
    }
    envSet(job->env, "SLURM_TASKS_PER_NODE", cpus);
    ufree(cpus);

    if (job->arrayTaskId != NO_VAL) {
	snprintf(tmp, sizeof(tmp), "%u", job->arrayJobId);
	envSet(job->env, "SLURM_ARRAY_JOB_ID", tmp);

	snprintf(tmp, sizeof(tmp), "%u", job->arrayTaskId);
	envSet(job->env, "SLURM_ARRAY_TASK_ID", tmp);
    }

    envSet(job->env, "SLURM_NODELIST", job->slurmHosts);
    envSet(job->env, "SLURM_JOB_NODELIST", job->slurmHosts);

    /* node alias was removed in 23.11 */
    if (!job->nodeAlias || !strlen(job->nodeAlias)) {
	if (slurmProto <= SLURM_23_02_PROTO_VERSION) {
	    envSet(job->env, "SLURM_NODE_ALIASES", "(null)");
	}
    } else {
	envSet(job->env, "SLURM_NODE_ALIASES", job->nodeAlias);
    }

    if (job->hostname) envSet(job->env, "HOSTNAME", job->hostname);

    /* set GRes environment */
    setGResJobEnv(&job->gresList, job->env);

    /* set job credential environment */
    setCredEnv(job->cred, job->env);
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
    strbuf_t buf = strbufNew(NULL);

    uint32_t offset = step->packTaskOffset != NO_VAL ? step->packTaskOffset : 0;
    for (uint32_t i = 0; i < step->globalTaskIdsLen[step->localNodeId]; i++) {
	if (i) strbufAdd(buf, ",");

	char tmp[128];
	snprintf(tmp, sizeof(tmp), "%u",
		 step->globalTaskIds[step->localNodeId][i] + offset);
	strbufAdd(buf, tmp);
    }
    return strbufSteal(buf);
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

static void setPsslurmEnv(env_t alloc_env, env_t dest_env)
{
    for (char **e = envGetArray(alloc_env); e && *e; e++) {
	if (strncmp("_PSSLURM_ENV_", *e, 13)) continue;
	char *ptr = *e + 13;
	fdbg(PSSLURM_LOG_ENV, "set %s\n", ptr);
	if (!*ptr) continue;
	if (envInitialized(dest_env)) {
	    envAdd(dest_env, ptr);
	} else {
	    putenv(ptr);
	}
    }
}

static void setGPUEnv(Step_t *step, uint32_t jobNodeId, uint32_t localRankId)
{
    Gres_Cred_t *gres;
    gres = findGresCred(&step->gresList, GRES_PLUGIN_GPU, GRES_CRED_STEP);

    if (!gres || !gres->bitAlloc || !gres->bitAlloc[jobNodeId]) {
	/* this step does not have GPUs assigned on this node */
	unsetenv("SLURM_STEP_GPUS");
	unsetenv("PSSLURM_BIND_GPUS");

	for (size_t i = 0; PSIDpin_GPUvars[i]; i++) {
	    char *gpuVar = getenv(PSIDpin_GPUvars[i]);
	    if (gpuVar
		&& PSIDpin_checkAutoVar(PSIDpin_GPUvars[i], gpuVar, NULL)) {
		/* variable not changed by the user */
		unsetenv(PSIDpin_GPUvars[i]);
	    }
	}

	/* prevent doClamps() from doing automatic GPU pinning */
	setenv("__PSID_USE_GPUS", "", 1);

	return;
    }

    /* decode step GPUs */
    strbuf_t strList = strbufNew(NULL);
    hexBitstr2List(gres->bitAlloc[jobNodeId], strList, false);

    /* always set informational variable */
    setenv("SLURM_STEP_GPUS", strbufStr(strList), 1);

    /* tell doClamps() which gpus to use to correctly set PSID_CLOSE_GPUS */
    setenv("__PSID_USE_GPUS", strbufStr(strList), 1);

    strbufDestroy(strList);

    uint32_t stepNId = step->localNodeId;
    uint32_t ltnum = step->globalTaskIdsLen[stepNId];
    char tmpbuf[21]; /* max uin64_t */
    char *bindgpus;

    /* if there is only one local rank, bind all assigned GPUs to it */
    if (ltnum == 1) {
	flog("step has only one local task, bind all assigned GPUs to it\n");
	/* always set our own variable */
	char *value = getenv("SLURM_STEP_GPUS");
	bindgpus = value ? value : "";
    } else {
	/* get assigned GPUs from GRES info */
	PSCPU_set_t assGPUs;
	if (!hexBitstr2Set(gres->bitAlloc[jobNodeId], assGPUs)) {
	    flog("failed to get assigned GPUs from bitstring\n");
	    return;
	}

	int16_t gpu = getRankGpuPinning(localRankId, step, stepNId, assGPUs);
	if (gpu < 0) return; /* error message already printed */

	snprintf(tmpbuf, sizeof(tmpbuf), "%hd", gpu);
	bindgpus = tmpbuf;
    }

    /* always set our own variable */
    setenv("PSSLURM_BIND_GPUS", bindgpus, 1);

    char *gpulibVar;
    char *c = getConfValueC(SlurmCgroupConfig, "ConstrainDevices");
    if (c && !strcasecmp(c, "yes")) {
	/* cgroups enabled, only SLURM_STEP_GPUS are visible, adjust IDs as
	 * done at least by libcuda (@todo check with AMD) */

	strbuf_t cgroupsList = strbufNew("");
	if (!cgroupsList) {
	    flog("Unable to allocate memory");
	    abort();
	}
	for (uint64_t gpu = 0; gpu < gres->totalGres; gpu++) {
	    snprintf(tmpbuf, sizeof(tmpbuf), "%zd", gpu);
	    if (gpu) strbufAdd(cgroupsList, ",");
	    strbufAdd(cgroupsList, tmpbuf);
	}
	gpulibVar = strbufSteal(cgroupsList);
    } else {
	gpulibVar = strdup(bindgpus);
    }

    for (size_t i = 0; PSIDpin_GPUvars[i]; i++) {
	char *gpuVar = getenv(PSIDpin_GPUvars[i]);
	if (!gpuVar || PSIDpin_checkAutoVar(PSIDpin_GPUvars[i], gpuVar, NULL)) {
	    /* variable not set at all or set automatically and
	     * unchanged in the meantime => set it */
	    setenv(PSIDpin_GPUvars[i], gpulibVar, 1);
	}
    }

    free(gpulibVar);
}

static void setGresEnv(uint32_t localRankId, Step_t *step)
{
    uint32_t jobNodeId = getJobNodeId(step);

    if (jobNodeId != NO_VAL) {
	Gres_Cred_t *gres;

	/* gres "gpu" plugin */
	setGPUEnv(step, jobNodeId, localRankId);

	/* gres "mic" plugin */
	gres = findGresCred(&step->gresList, GRES_PLUGIN_MIC, GRES_CRED_STEP);
	if (gres && gres->bitAlloc) {
	    if (gres->bitAlloc[jobNodeId]) {
		strbuf_t strList = strbufNew(NULL);
		hexBitstr2List(gres->bitAlloc[jobNodeId], strList, false);
		setenv("OFFLOAD_DEVICES", strbufStr(strList), 1);
		strbufDestroy(strList);
	    } else {
		flog("invalid mic gres bitAlloc for job node ID %u\n",
		     jobNodeId);
	    }
	}

	/* set STEP_GRES */
	gres = findGresCred(&step->gresList, NO_VAL, GRES_CRED_STEP);
	if (gres && gres->bitAlloc) {
	    if (gres->bitAlloc[jobNodeId]) {
		strbuf_t strList = strbufNew(NULL);
		hexBitstr2List(gres->bitAlloc[jobNodeId], strList, false);
		setenv("SLURM_STEP_GRES", strbufStr(strList), 1);
		strbufDestroy(strList);
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
    env_t gresEnv = envNew(NULL);
    setGResJobEnv(&step->gresList, gresEnv);
    for (char **e = envGetArray(gresEnv); e && *e; e++) {
	if (!strchr(*e, '=')) continue;
	putenv(*e);
    }
    envSteal(gresEnv);
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

    char *val = envGet(step->env, "SLURM_UMASK");
    if (IS_SET(val)) setenv(val, "SLURM_UMASK", 1);
}

void setSlurmConfEnvVar(env_t env)
{
    char *confServer = getConfValueC(Config, "SLURM_CONF_SERVER");
    if (confServer && strcmp(confServer, "none")) {
	/* ensure the configuration cache is used */
	char *confDir = getConfValueC(Config, "SLURM_CONFIG_DIR");
	if (!confDir) {
	    flog("warning: invalid SLURM_CONFIG_DIR");
	    return;
	}

	char *confFile = getConfValueC(Config, "SLURM_CONF");
	if (!confFile) {
	    flog("warning: invalid SLURM_CONF");
	    return;
	}

	char cPath[PATH_MAX];
	snprintf(cPath, sizeof(cPath), "%s/%s", confDir, confFile);
	doSetEnv(env, "SLURM_CONF", cPath);
    }
}

static void setTopoEnv(env_t env)
{
    Topology_t *topo = getTopology(getConfValueC(Config, "SLURM_HOSTNAME"));

    doSetEnv(env, "SLURM_TOPOLOGY_ADDR", topo->address);
    doSetEnv(env, "SLURM_TOPOLOGY_ADDR_PATTERN", topo->pattern);

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
    for (char **e = envGetArray(step->env); e && *e; e++) {
	/* protect selected variables from changes */
	if (!strncmp(*e, "SLURM_RLIMIT_", 13)) continue;
	if (!strncmp(*e, "SLURM_UMASK=", 12)) continue;
	if (!strncmp(*e, "PWD=", 4)) continue;
	if (display && !strncmp(*e, "DISPLAY=", 8)) continue;
	if (!strncmp(*e, "PMI_FD=", 7)) continue;
	if (!strncmp(*e, "PMI_PORT=", 9)) continue;
	if (!strncmp(*e, "PMI_RANK=", 9)) continue;
	if (!strncmp(*e, "PMI_SIZE=", 9)) continue;
	if (!strncmp(*e, "PMI_UNIVERSE_SIZE=", 18)) continue;
	if (!strncmp(*e, "PMI_ID=", 7)) continue;
	if (!strncmp(*e, "PMI_APPNUM=", 11)) continue;
	if (!strncmp(*e, "PMI_ENABLE_SOCKP=", 17)) continue;
	if (!strncmp(*e, "PMI_SUBVERSION=", 15)) continue;
	if (!strncmp(*e, "PMI_VERSION=", 12)) continue;
	if (!strncmp(*e, "PMI_BARRIER_ROUNDS=", 19)) continue;
	if (!strncmp(*e, "PMIX_JOB_SIZE=", 14)) continue;
	if (!strncmp(*e, "PMIX_SPAWNID=", 13)) continue;
	if (!strncmp(*e, "PMIX_APP_WDIR_", 14)) continue;
	if (!strncmp(*e, "PMIX_APP_ARGV_", 14)) continue;
	if (!strncmp(*e, "PMIX_APP_NAME_", 14)) continue;
	if (!strncmp(*e, "PMIX_DEBUG=", 11)) continue;
	if (!strncmp(*e, "PMIX_JOB_NUM_APPS=", 18)) continue;
	if (!strncmp(*e, "PMIX_UNIV_SIZE=", 15)) continue;
	if (!strncmp(*e, "PSPMIX_ENV_TMOUT=", 17)) continue;
	if (!strncmp(*e, "PSP_SMP_NODE_ID=", 16)) continue;

	putenv(*e);
    }

    /* use pwd over cwd if realpath is identical */
    char *pwd = envGet(step->env, "PWD");
    if (pwd) {
	char *rpath = realpath(pwd, NULL);
	if (rpath && !strcmp(rpath, step->cwd)) {
	    setenv("PWD", pwd, 1);
	}
	free(rpath);
    }

    setSlurmConfEnvVar(NULL);

    if (IS_SET(step->tresBind)) setenv("SLURMD_TRES_BIND", step->tresBind, 1);
    if (IS_SET(step->tresFreq)) setenv("SLURMD_TRES_FREQ", step->tresFreq, 1);

    setenv("SLURMD_NODENAME", getConfValueC(Config, "SLURM_HOSTNAME"), 1);
    char tmp[128];
    gethostname(tmp, sizeof(tmp));
    setenv("HOSTNAME", tmp, 1);
    snprintf(tmp, sizeof(tmp), "%u", getpid());
    setenv("SLURM_TASK_PID", tmp, 1);
    setenv("SLURM_CPUS_ON_NODE", getConfValueC(Config, "SLURM_CPUS"), 1);

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

    /* set job credential environment */
    setCredEnv(step->cred, NULL);
}

void setRankEnv(int32_t rank, Step_t *step)
{
    setCommonRankEnv(rank, step);

    Alloc_t *alloc = Alloc_find(step->jobid);
    if (alloc) setPsslurmEnv(alloc->env, NULL);

    if (step->stepid == SLURM_INTERACTIVE_STEP ||
	step->taskFlags & LAUNCH_EXT_LAUNCHER) {
	return setInteractiveRankEnv(step);
    } else {
	return setOrdinaryRankEnv(rank, step);
    }
}

/**
 * @brief Filter spank option variables
 *
 * @param envStr Environment string to test
 *
 * @param info Extra info (ignored)
 *
 * @return Return true if @a envStr represents a SPANK option or false
 * otherwise
 */
static bool spankVarFilter(const char *envStr, void *info)
{
    if (!strncmp("_SLURM_SPANK_OPTION", envStr, 19)) return true;

    return false;
}

pmi_type_t getPMIType(Step_t *step)
{
    /* for interactive steps, ignore pmi type and use none */
    if (step->stepid == SLURM_INTERACTIVE_STEP) {
	flog("interactive step detected, using PMI type 'none'\n");
	return PMI_TYPE_NONE;
    }

    /* PSSLURM_PMI_TYPE can be used to choose PMI environment to be set up */
    char *pmi = envGet(step->env, "PSSLURM_PMI_TYPE");
    if (!pmi) {
	/* if PSSLURM_PMI_TYPE is not set and srun is called with --mpi=none,
	 *  do not setup any pmi environment */
	pmi = envGet(step->env, "SLURM_MPI_TYPE");
	if (pmi && !strcmp(pmi, "none")) {
	    flog("%s SLURM_MPI_TYPE set to 'none'\n", Step_strID(step));
	    return PMI_TYPE_NONE;
	}
	return PMI_TYPE_DEFAULT;
    }

    flog("%s PSSLURM_PMI_TYPE set to '%s'\n", Step_strID(step), pmi);
    if (!strcmp(pmi, "none")) return PMI_TYPE_NONE;
    if (!strcmp(pmi, "pmix")) return PMI_TYPE_PMIX;

    /* if PSSLURM_PMI_TYPE is set to anything else, use default */
    return PMI_TYPE_DEFAULT;
}

void setPMITypeEnv(pmi_type_t pmi_type)
{
    char *type = "pmi";  /* still the default */
    switch(pmi_type) {
	case PMI_TYPE_NONE:
	    type = "none";
	    break;
	case PMI_TYPE_PMIX:
	    type = "pmix";
	    break;
	case PMI_TYPE_DEFAULT:
	    break;
    }

    setenv("PSSLURM_PMI_TYPE", type, 1);
}

/**
 * @brief Filter variable not foreseen to be user-visible
 *
 * @param envStr Environment string to test
 *
 * @param info Extra info expected to present the PMI type
 *
 * @return Return true if @a envStr shall be evicted or false
 * otherwise
 */
static bool userVarFilter(const char *envStr, void *info)
{
    pmi_type_t pmi_type = *(pmi_type_t *)info;

    /* get rid of all environment variables which are not needed
     * for spawning of processes via mpiexec */
    if (!strncmp(envStr, "USER=", 5)
	|| !strncmp(envStr, "HOSTNAME=", 9)
	|| !strncmp(envStr, "PATH=", 5)
	|| !strncmp(envStr, "HOME=", 5)
	|| !strncmp(envStr, "PWD=", 4)
	|| !strncmp(envStr, "DISPLAY=", 8)
	|| !strncmp(envStr, "SLURM_STEPID=", 13)
	|| !strncmp(envStr, "SLURM_JOBID=", 12)
	|| !strncmp(envStr, "__MPIEXEC_DIST_START=", 21)
	|| !strncmp(envStr, "MPIEXEC_", 8)
	|| !strncmp(envStr, "PSI_", 4)
	|| !strncmp(envStr, "__PSI_", 6)
	|| !strncmp(envStr, "PSSLURM_", 8)
	|| !strncmp(envStr, "__PSSLURM_", 10)) return false;

    if (pmi_type == PMI_TYPE_DEFAULT && (
	    !strncmp(envStr, "PMI_", 4)
	    || !strncmp(envStr, "__PMI_", 6)
	    || !strncmp(envStr, "__SPAWNER_SERVICE_RANK=", 23)
	    || !strncmp(envStr, "MEASURE_KVS_PROVIDER", 20))) return false;

    if (pmi_type == PMI_TYPE_PMIX && (
	    !strncmp(envStr, "PMIX_JOB_SIZE=", 14)
	    || !strncmp(envStr, "PMIX_SPAWNID=", 13)
	    || !strncmp(envStr, "__SPAWNER_SERVICE_RANK=", 23)
	    || !strncmp(envStr, "PSPMIX_ENV_TMOUT=", 17)
	    || !strncmp(envStr, "__PMIX_", 7))) return false;

    return true;
}

void removeUserVars(env_t env, pmi_type_t pmi_type)
{
    envEvict(env, userVarFilter, &pmi_type);
}

void setStepEnv(Step_t *step)
{
    char *val;
    mode_t slurmUmask;
    int dist;

    /* distribute mpiexec service processes */
    dist = getConfValueI(Config, "DIST_START");
    /* spawn does not provide standard partition handling! */
    if (dist && !step->spawned) envSet(step->env, "__MPIEXEC_DIST_START", "1");

    /* forward overbook mode */
    if ((val = envGet(step->env, "SLURM_OVERCOMMIT"))) {
	if (!strcmp(val, "1")) {
	    envSet(step->env, ENV_PART_OVERBOOK, "1");
	}
    }

    /* unbuffered (raw I/O) mode */
    if (!(step->taskFlags & LAUNCH_LABEL_IO) &&
	!(step->taskFlags & LAUNCH_BUFFERED_IO)) {
	envSet(step->env, "__PSI_RAW_IO", "1");
	envSet(step->env, "__PSI_LOGGER_UNBUFFERED", "1");
    }

    if (!(step->taskFlags & LAUNCH_PTY)) {
	envSet(step->env, "PSI_INPUTDEST", "all");
    }

    /* set slurm umask */
    if ((val = envGet(step->env, "SLURM_UMASK"))) {
	slurmUmask = strtol(val, NULL, 8);
	umask(slurmUmask);
	envSet(step->env, "__PSI_UMASK", val);
	envUnset(step->env, "SLURM_UMASK");
    }

    /* handle memory mapping */
    val = getConfValueC(Config, "MEMBIND_DEFAULT");
    if (step->memBindType & MEM_BIND_NONE ||
	    (!(step->memBindType & (MEM_BIND_RANK | MEM_BIND_MAP |
				    MEM_BIND_MASK | MEM_BIND_LOCAL)) &&
		    (strcmp(val, "none") == 0))) {
	envSet(step->env, "__PSI_NO_MEMBIND", "1");
    }

    /* prevent mpiexec from resolving the nodelist */
    envSet(step->env, ENV_PSID_BATCH, "1");

    /* cleanup env */
    envEvict(step->env, spankVarFilter, NULL);

    /* ensure USER is set (even if srun --export=NONE is used) */
    if (!envGet(step->env, "USER")) {
	envSet(step->env, "USER", step->username);
    }
}

void setJobEnv(Job_t *job)
{
    char tmp[1024];
    mode_t slurmUmask;

    envSet(job->env, "ENVIRONMENT", "BATCH");
    envSet(job->env, "SLURM_NODEID", "0");
    envSet(job->env, "SLURM_PROCID", "0");
    envSet(job->env, "SLURM_LOCALID", "0");

    if (job->nodeMinMemory & MEM_PER_CPU) {
	snprintf(tmp, sizeof(tmp), "%lu", job->nodeMinMemory & (~MEM_PER_CPU));
	envSet(job->env, "SLURM_MEM_PER_CPU", tmp);
    } else if (job->nodeMinMemory) {
	snprintf(tmp, sizeof(tmp), "%lu", job->nodeMinMemory);
	envSet(job->env, "SLURM_MEM_PER_NODE", tmp);
    }

    snprintf(tmp, sizeof(tmp), "%u", getpid());
    envSet(job->env, "SLURM_TASK_PID", tmp);

    snprintf(tmp, sizeof(tmp), "%u", job->gid);
    envSet(job->env, "SLURM_JOB_GID", tmp);

    /* forward overbook mode */
    char *val = envGet(job->env, "SLURM_OVERCOMMIT");

    if (job->overcommit || (val && !strcmp(val, "1"))) {
	envSet(job->env, ENV_PART_OVERBOOK, "1");
    }

    /* set slurm umask */
    if ((val = envGet(job->env, "SLURM_UMASK"))) {
	slurmUmask = strtol(val, NULL, 8);
	umask(slurmUmask);
	envUnset(job->env, "SLURM_UMASK");
    }

    if (IS_SET(job->tresBind)) {
	envSet(job->env, "SLURMD_TRES_BIND", job->tresBind);
    }
    if (IS_SET(job->tresFreq)) {
	envSet(job->env, "SLURMD_TRES_FREQ", job->tresFreq);
    }

    envEvict(job->env, spankVarFilter, NULL);

    setSlurmConfEnvVar(job->env);

    char *cname = getConfValueC(SlurmConfig, "ClusterName");
    if (cname) envSet(job->env, "SLURM_CLUSTER_NAME", cname);

    if (job->account) envSet(job->env, "SLURM_JOB_ACCOUNT", job->account);
    if (job->qos) envSet(job->env, "SLURM_JOB_QOS", job->qos);

    uint32_t numGPUs = GRes_countDevices(GRES_PLUGIN_GPU);
    if (numGPUs) {
	snprintf(tmp, sizeof(tmp), "%u", numGPUs);
	envSet(job->env, "SLURM_GPUS_ON_NODE", tmp);
    }

    /* set topology environment */
    setTopoEnv(job->env);

    Alloc_t *alloc = Alloc_find(job->jobid);
    if (alloc) setPsslurmEnv(alloc->env, job->env);
}
