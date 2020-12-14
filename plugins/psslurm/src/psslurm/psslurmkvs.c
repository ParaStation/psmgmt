/*
 * ParaStation
 *
 * Copyright (C) 2016-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pluginmalloc.h"
#include "pluginhelper.h"
#include "pshostlist.h"

#include "psidnodes.h"

#include "psslurmconfig.h"
#include "psslurmlog.h"
#include "psslurmjob.h"
#include "psslurmalloc.h"
#include "psslurmpin.h"
#include "psslurmproto.h"
#include "psslurmpscomm.h"
#include "psslurmtasks.h"
#ifdef HAVE_SPANK
#include "psslurmspank.h"
#endif

#include "psidtask.h"
#include "plugin.h"
#include "psmungehandles.h"

typedef struct {
    StrBuffer_t strBuf;
    bool all;
} StepInfo_t;

static char line[256];

/**
 * @brief Visitor to add information about a job to a buffer
 *
 * @param job The job to use
 *
 * @param info A StrBuffer structure to save the information
 *
 * @return Always returns false to loop throw all jobs
 */
static bool addJobInfo(Job_t *job, const void *info)
{
    char start[50];
    struct tm *ts;
    StrBuffer_t *strBuf = (StrBuffer_t *) info;

    snprintf(line, sizeof(line), "- jobid %u -\n", job->jobid);
    addStrBuf(line, strBuf);

    snprintf(line, sizeof(line), "user '%s'\n", job->username);
    addStrBuf(line, strBuf);

    snprintf(line, sizeof(line), "# nodes %u\n", job->nrOfNodes);
    addStrBuf(line, strBuf);

    snprintf(line, sizeof(line), "hosts '%s'\n", job->slurmHosts);
    addStrBuf(line, strBuf);

    snprintf(line, sizeof(line), "jobscript '%s'\n", job->jobscript);
    addStrBuf(line, strBuf);

    snprintf(line, sizeof(line), "job state '%s'\n",
	    strJobState(job->state));
    addStrBuf(line, strBuf);

    if (job->fwdata) {
	snprintf(line, sizeof(line), "job pid %u\n", job->fwdata->cPid);
	addStrBuf(line, strBuf);
    }

    /* format start time */
    ts = localtime(&job->startTime);
    strftime(start, sizeof(start), "%Y-%m-%d %H:%M:%S", ts);

    snprintf(line, sizeof(line), "start time '%s'\n", start);
    addStrBuf(line, strBuf);

    if (job->packSize) {
	snprintf(line, sizeof(line), "pack size %u\n", job->packSize);
	addStrBuf(line, strBuf);

	snprintf(line, sizeof(line), "pack host list %s\n", job->packHostlist);
	addStrBuf(line, strBuf);
    }

    addStrBuf("-\n\n", strBuf);

    return false;
}

/**
 * @brief Show current jobs.
 *
 * @return Returns the buffer with the updated job information.
 */
static char *showJobs(void)
{
    StrBuffer_t strBuf = {
	.buf = NULL,
	.bufSize = 0 };

    if (!countJobs()) {
	return addStrBuf("\nNo current jobs.\n", &strBuf);
    }

    addStrBuf("\njobs:\n\n", &strBuf);
    traverseJobs(addJobInfo, &strBuf);

    return strBuf.buf;
}

/**
 * @brief Visitor to add information about a allocation to a buffer
 *
 * @param alloc The allocation to use
 *
 * @param info A StrBuffer structure to save the information
 *
 * @return Always returns false to loop throw all allocations
 */
static bool addAllocInfo(Alloc_t *alloc, const void *info)
{
    struct tm *ts;
    char start[50];
    StrBuffer_t *strBuf = (StrBuffer_t *) info;

    snprintf(line, sizeof(line), "- jobid %u -\n", alloc->id);
    addStrBuf(line, strBuf);

    if (alloc->packID != NO_VAL) {
	snprintf(line, sizeof(line), "packid %u \n", alloc->packID);
	addStrBuf(line, strBuf);
    }

    snprintf(line, sizeof(line), "user '%s'\n", alloc->username);
    addStrBuf(line, strBuf);

    snprintf(line, sizeof(line), "# nodes %u\n", alloc->nrOfNodes);
    addStrBuf(line, strBuf);

    snprintf(line, sizeof(line), "hosts '%s'\n", alloc->slurmHosts);
    addStrBuf(line, strBuf);

    snprintf(line, sizeof(line), "alloc state '%s'\n",
	    strAllocState(alloc->state));
    addStrBuf(line, strBuf);

    /* format start time */
    ts = localtime(&alloc->startTime);
    strftime(start, sizeof(start), "%Y-%m-%d %H:%M:%S", ts);

    snprintf(line, sizeof(line), "start time '%s'\n", start);
    addStrBuf(line, strBuf);

    snprintf(line, sizeof(line), "local node id %u -\n", alloc->localNodeId);
    addStrBuf(line, strBuf);

    addStrBuf("-\n\n", strBuf);

    return false;
}

/**
 * @brief Visitor to add information about tasks of a step
 *
 * @param step The step to use
 *
 * @param info A StrBuffer_t structure to save the information
 *
 * @return Always returns false to loop throw all steps
 */
static bool addTaskInfo(Step_t *step, const void *info)
{
    StrBuffer_t *strBuf = (StrBuffer_t *) info;
    list_t *tasks = &step->tasks;

    if (step->state == JOB_COMPLETE) return false;

    if (!countTasks(tasks)) {
	snprintf(line, sizeof(line), "\nno tasks for step %u:%u\n",
		 step->jobid, step->stepid);
	addStrBuf(line, strBuf);
	return false;
    }

    snprintf(line, sizeof(line), "\n%u tasks for step %u:%u\n",
	     countTasks(tasks), step->jobid, step->stepid);
    addStrBuf(line, strBuf);

    list_t *t;
    list_for_each(t, tasks) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);

	snprintf(line, sizeof(line), "child %s ", PSC_printTID(task->childTID));
	addStrBuf(line, strBuf);

	snprintf(line, sizeof(line), "forwarder %s rank %i exit %i sent "
		 "exit %u\n", PSC_printTID(task->forwarderTID), task->childRank,
		 task->exitCode, task->sentExit);
	addStrBuf(line, strBuf);
    }

    addStrBuf("-\n\n", strBuf);

    return false;
}

/**
 * @brief Show tasks of all local steps
 *
 * @return Returns the buffer with the updated task information
 */
static char *showTasks(void)
{
    StrBuffer_t strBuf = {
	.buf = NULL,
	.bufSize = 0 };

    if (!countSteps()) {
	return addStrBuf("\nNo current tasks.\n", &strBuf);
    }

    addStrBuf("\ntasks for all steps:\n", &strBuf);
    traverseSteps(addTaskInfo, &strBuf);

    return strBuf.buf;
}

#ifdef HAVE_SPANK

/**
 * @brief Visitor to add information about spank plugins
 *
 * @param sp The spank plugin to use
 *
 * @param info A StrBuffer_t structure to save the information
 *
 * @return Always returns false to loop throw all steps
 */
static bool addSpankInfo(Spank_Plugin_t *sp, const void *info)
{
    StrBuffer_t *strBuf = (StrBuffer_t *) info;

    snprintf(line, sizeof(line), "plugin %s: type=%s ver=%u optional=%s "
	     "path=%s\n", sp->name, sp->type, sp->version,
	     (sp->optional ? "true" : false), sp->path);
    addStrBuf(line, strBuf);

    return false;
}

#endif

/**
 * @brief Show registered spank plugins
 *
 * @return Returns the buffer with the updated spank information
 */
static char *showSpank(void)
{
    StrBuffer_t strBuf = {
	.buf = NULL,
	.bufSize = 0 };

#ifdef HAVE_SPANK
    addStrBuf("\nactive spank plugins:\n\n", &strBuf);
    SpankTraversePlugins(addSpankInfo, &strBuf);
#else
    addStrBuf("\npsmgmt was compiled without spank support\n\n", &strBuf);
#endif

    return strBuf.buf;
}

/**
 * @brief Show current allocations.
 *
 * @return Returns the buffer with the updated allocation information.
 */
static char *showAllocations(void)
{
    StrBuffer_t strBuf = {
	.buf = NULL,
	.bufSize = 0 };

    if (!countAllocs()) {
	return addStrBuf("\nNo current allocations.\n", &strBuf);
    }

    addStrBuf("\nallocations:\n\n", &strBuf);
    traverseAllocs(addAllocInfo, &strBuf);

    return strBuf.buf;
}

/**
 * @brief Resolve a given host-list
 *
 * Resolve a compressed host-list and show the single hosts including
 * the corresponding ParaStation node IDs and the local node ID.
 *
 * @return Returns the buffer with the nodeID and host pairs.
 */
static char *resolveIDs(char *hosts)
{
    PSnodes_ID_t *nodes;
    uint32_t i, nrOfNodes;
    char *buf = NULL;
    size_t bufSize = 0;

    if (!hosts) {
	return str2Buf("\nSpecify hosts to resolve\n", &buf, &bufSize);
    }

    if (!convHLtoPSnodes(hosts, getNodeIDbySlurmHost, &nodes, &nrOfNodes)) {
	return str2Buf("\nResolving PS nodeIDs failed\n", &buf, &bufSize);
    }

    uint32_t localNodeId = getLocalID(nodes, nrOfNodes);
    if (localNodeId == NO_VAL) {
	str2Buf("\nCould not find my local ID\n", &buf, &bufSize);
    } else {
	snprintf(line, sizeof(line), "\nLocal node ID is %u\n", localNodeId);
	str2Buf(line, &buf, &bufSize);
    }

    for (i=0; i<nrOfNodes; i++) {
	snprintf(line, sizeof(line), "%u nodeID %i hostname %s\n", i, nodes[i],
		 getSlurmHostbyNodeID(nodes[i]));
	str2Buf(line, &buf, &bufSize);
    }

    return buf;
}

/**
 * @brief Visitor to add information about a step to a buffer
 *
 * @param step The step to use
 *
 * @param info A StepInfo structure to save the information
 *
 * @return Always returns false to loop throw all steps
 */
static bool addHwthreadsInfo(Step_t *step, const void *info)
{
    StepInfo_t *stepInfo = (StepInfo_t *) info;
    StrBuffer_t *strBuf = &stepInfo->strBuf;
    PSnodes_ID_t lastNode = -1;
    uint32_t i;
    uint16_t cpu;

    if (step->state == JOB_COMPLETE && !stepInfo->all) return false;

    snprintf(line, sizeof(line), "- stepid %u:%u threads %u core "
	    "map '%s'-\n", step->jobid, step->stepid, step->numHwThreads,
	    step->cred->stepCoreBitmap);
    addStrBuf(line, strBuf);

    if (!step->slots) {
	addStrBuf("\nno HW threads\n-\n\n", strBuf);
	return false;
    }

    addStrBuf("\npsslurm threads:", strBuf);
    for (i=0; i<step->np; i++) {
	if (lastNode != step->slots[i].node) {
	    snprintf(line, sizeof(line), "\nnode %i: ",
		    step->slots[i].node);
	    addStrBuf(line, strBuf);
	}
	lastNode = step->slots[i].node;

	for (cpu = 0; cpu < PSIDnodes_getNumThrds(step->slots[i].node); cpu++) {
	    if (PSCPU_isSet(step->slots[i].CPUset, cpu)) {
		snprintf(line, sizeof(line), "%hu ", cpu);
		addStrBuf(line, strBuf);
	    }
	}
    }

    if (step->fwdata && step->fwdata->cPid != 0) {
	PStask_ID_t cTID = PSC_getTID(-1, step->fwdata->cPid);
	PStask_t *task = PStasklist_find(&managedTasks, cTID);
	if (task) {
	    snprintf(line, sizeof(line), "\n\npsid threads for logger %s:",
		    PSC_printTID(cTID));
	    addStrBuf(line, strBuf);

	    for (i=0; i<task->totalThreads; i++) {
		if (lastNode != task->partThrds[i].node) {
		    snprintf(line, sizeof(line), "\nnode %i: ",
			    task->partThrds[i].node);
		    addStrBuf(line, strBuf);
		}
		lastNode = task->partThrds[i].node;

		snprintf(line, sizeof(line), "%i ", task->partThrds[i].id);
		addStrBuf(line, strBuf);
	    }
	} else {
	    snprintf(line, sizeof(line), "\n\nno psid threads for "
		    "logger %s:", PSC_printTID(cTID));
	    addStrBuf(line, strBuf);
	}
    }
    addStrBuf("\n-\n\n", strBuf);

    return false;
}

/**
 * @brief Show HW threads
 *
 * @return Returns the buffer with the updated information.
 */
static char *showHWthreads(bool all)
{
    StepInfo_t stepInfo = {
	.all = all,
	.strBuf.buf = NULL,
	.strBuf.bufSize = 0 };

    if (!countSteps()) {
	return addStrBuf("\nNo current HW threads.\n", &stepInfo.strBuf);
    }

    addStrBuf("\nHW threads:\n\n", &stepInfo.strBuf);
    traverseSteps(addHwthreadsInfo, &stepInfo);

    return stepInfo.strBuf.buf;
}

/**
 * @brief Visitor to add information about a step to a buffer
 *
 * @param step The step to use
 *
 * @param info A StepInfo structure to save the information
 *
 * @return Always returns false to loop throw all steps
 */
static bool addStepInfo(Step_t *step, const void *info)
{
    StepInfo_t *stepInfo = (StepInfo_t *) info;
    StrBuffer_t *strBuf = &stepInfo->strBuf;
    struct tm *ts;
    char start[50], *ptr;

    if (step->state == JOB_COMPLETE && !stepInfo->all) return false;

    snprintf(line, sizeof(line), "- stepid %u:%u -\n", step->jobid,
	    step->stepid);
    addStrBuf(line, strBuf);

    if (step->packJobid != NO_VAL) {
	snprintf(line, sizeof(line), "pack ID: %u\n", step->packJobid);
	addStrBuf(line, strBuf);

	snprintf(line, sizeof(line), "pack hosts: %s\n", step->packHostlist);
	addStrBuf(line, strBuf);
    }

    snprintf(line, sizeof(line), "user: '%s' uid: %u gid: %u\n",
	    step->username, step->uid, step->gid);
    addStrBuf(line, strBuf);

    snprintf(line, sizeof(line), "%u hosts: '%s'\n", step->nrOfNodes,
	    step->slurmHosts);
    addStrBuf(line, strBuf);

    snprintf(line, sizeof(line), "step state: '%s'\n",
	    strJobState(step->state));
    addStrBuf(line, strBuf);

    snprintf(line, sizeof(line), "tpp: %u numHwThreads: %u\n",
	    step->tpp, step->numHwThreads);
    addStrBuf(line, strBuf);

    if (step->fwdata) {
	snprintf(line, sizeof(line), "step PID: %u\n", step->fwdata->cPid);
	addStrBuf(line, strBuf);
    }

    /* format start time */
    ts = localtime(&step->startTime);
    strftime(start, sizeof(start), "%Y-%m-%d %H:%M:%S", ts);

    snprintf(line, sizeof(line), "start time: '%s'\n", start);
    addStrBuf(line, strBuf);

    ptr = genCPUbindString(step);
    snprintf(line, sizeof(line), "cpuBind: '%s'\n", ptr);
    ufree(ptr);
    addStrBuf(line, strBuf);

    ptr = genMemBindString(step);
    snprintf(line, sizeof(line), "memBind: '%s'\n", ptr);
    ufree(ptr);
    addStrBuf(line, strBuf);

    snprintf(line, sizeof(line), "logger TID: %s\n",
	    PSC_printTID(step->loggerTID));
    addStrBuf(line, strBuf);

    snprintf(line, sizeof(line), "local node ID: %u\n", step->localNodeId);
    addStrBuf(line, strBuf);

    addStrBuf("-\n\n", strBuf);

    return false;
}

/**
 * @brief Show current steps.
 *
 * @return Returns the buffer with the updated step information.
 */
static char *showSteps(bool all)
{
    StepInfo_t stepInfo = {
	.all = all,
	.strBuf.buf = NULL,
	.strBuf.bufSize = 0 };

    if (!countSteps()) {
	return addStrBuf("\nNo current steps.\n", &stepInfo.strBuf);
    }

    addStrBuf("\nsteps:\n\n", &stepInfo.strBuf);
    traverseSteps(addStepInfo, &stepInfo);

    return stepInfo.strBuf.buf;
}

/**
 * @brief Show current configuration.
 *
 * @return Returns the buffer with the updated configuration information.
 */
static char *showConfig(void)
{
    char *buf = NULL;
    size_t bufSize = 0;
    int i = 0;

    str2Buf("\n", &buf, &bufSize);

    while (confDef[i].name != NULL) {
	char *cName = confDef[i].name;
	char *cVal = getConfValueC(&Config, cName);
	snprintf(line, sizeof(line), "%21s = %s\n",
		 cName, cVal ? cVal : "<empty>");
	str2Buf(line, &buf, &bufSize);
	i++;
    }

    return buf;
}

/**
 * @brief Show all supported virtual keys.
 *
 * @param buf The buffer to write the information to.
 *
 * @param bufSize The size of the buffer.
 *
 * @return Returns the buffer with the updated forwarder information.
 */
static char *showVirtualKeys(char *buf, size_t *bufSize, bool example)
{
    str2Buf("\n# available keys #\n\n", &buf, bufSize);
    str2Buf("     config\tshow current configuration\n", &buf, bufSize);
    str2Buf("       jobs\tshow all jobs\n", &buf, bufSize);
    str2Buf("allocations\tshow all allocations\n", &buf, bufSize);
    str2Buf("      steps\tshow running steps\n", &buf, bufSize);
    str2Buf("     asteps\tshow all steps\n", &buf, bufSize);
    str2Buf(" ahwThreads\tshow all hwThreads\n", &buf, bufSize);
    str2Buf("      tasks\tshow all tasks\n", &buf, bufSize);
    str2Buf(" resolveIDs\tresolve a Slurm host-list\n", &buf, bufSize);
    str2Buf("      spank\tshow active spank plugins\n", &buf, bufSize);
    str2Buf("    tainted\tshow if a spank plugin taints psid\n", &buf, bufSize);

    if (example) {
	str2Buf("\nExamples:\n * Use 'plugin show psslurm key jobs' "
		"to display jobs\n", &buf, bufSize);
	str2Buf(" * Use 'plugin set psslurm CLEAR_CONF_CACHE 1' to clear "
		"config cache\n", &buf, bufSize);
    }
    return buf;
}

char *set(char *key, char *value)
{
    char *buf = NULL;
    size_t bufSize = 0;

    /* search in config for given key */
    if (getConfigDef(key, confDef)) {
	int ret = verifyConfigEntry(confDef, key, value);
	if (ret) {
	    switch (ret) {
	    case 1:
		str2Buf("\nInvalid key '", &buf, &bufSize);
		str2Buf(key, &buf, &bufSize);
		str2Buf("' for cmd set : use 'plugin help psslurm' for help.\n",
			&buf, &bufSize);
		break;
	    case 2:
		str2Buf("\nThe key '", &buf, &bufSize);
		str2Buf(key, &buf, &bufSize);
		str2Buf("' for cmd set has to be numeric.\n", &buf, &bufSize);
	    }
	    return buf;
	}

	if (!strcmp(key, "DEBUG_MASK")) {
	    int32_t mask;

	    if (sscanf(value, "%i", &mask) != 1) {
		return str2Buf("\nInvalid debug mask: NAN\n", &buf, &bufSize);
	    }
	    maskLogger(mask);
	}

	if (!strcmp(key, "MEASURE_MUNGE")) {
	    int32_t active;

	    if (sscanf(value, "%i", &active) != 1) {
		return str2Buf("\nInvalid flag: NAN\n", &buf, &bufSize);
	    }
	    psMungeMeasure(active);
	}

	if (!strcmp(key, "MEASURE_RPC")) {
	    int32_t active;

	    if (sscanf(value, "%i", &active) != 1) {
		return str2Buf("\nInvalid flag: NAN\n", &buf, &bufSize);
	    }
	    measureRPC = active;
	}

	/* save new config value */
	addConfigEntry(&Config, key, value);

	snprintf(line, sizeof(line), "\nsaved '%s = %s'\n", key, value);
	return str2Buf(line, &buf, &bufSize);
    }

    if (!strcmp(key, "CLEAR_CONF_CACHE")) {
	char *confDir = getConfValueC(&Config, "SLURM_CONF_DIR");
	removeDir(confDir, 0);
	str2Buf("Clear Slurm configuration cache ", &buf, &bufSize);
	str2Buf(confDir, &buf, &bufSize);
	return str2Buf("\n", &buf, &bufSize);
    }

    str2Buf("\nInvalid key '", &buf, &bufSize);
    str2Buf(key, &buf, &bufSize);
    return str2Buf("' for cmd set : use 'plugin help psslurm' for help.\n",
		   &buf, &bufSize);
}

char *unset(char *key)
{
    char *buf = NULL;
    size_t bufSize = 0;

    if (unsetConfigEntry(&Config, confDef, key)) return buf;

    str2Buf("\nInvalid key '", &buf, &bufSize);
    str2Buf(key, &buf, &bufSize);
    str2Buf("' for cmd unset : use 'plugin help psslurm' for help.\n",
	    &buf, &bufSize);

    return buf;
}

char *help(void)
{
    char *buf = NULL;
    size_t bufSize = 0;
    int i = 0;
    char type[10];

    str2Buf("\n# configuration options #\n\n", &buf, &bufSize);

    while (confDef[i].name != NULL) {
	snprintf(type, sizeof(type), "<%s>", confDef[i].type);
	snprintf(line, sizeof(line), "%21s\t%8s    %s\n", confDef[i].name,
		type, confDef[i].desc);
	str2Buf(line, &buf, &bufSize);
	i++;
    }

    return showVirtualKeys(buf, &bufSize, true);
}

char *show(char *key)
{
    char *buf = NULL, *tmp;
    size_t bufSize = 0;

    if (!key) return showVirtualKeys(buf, &bufSize, true);

    /* search in config for given key */
    tmp = getConfValueC(&Config, key);
    if (tmp) {
	str2Buf(key, &buf, &bufSize);
	str2Buf(" = ", &buf, &bufSize);
	str2Buf(tmp, &buf, &bufSize);
	str2Buf("\n", &buf, &bufSize);

	return buf;
    }

    /* show current config */
    if (!strcmp(key, "config")) return showConfig();

    /* show current jobs */
    if (!strcmp(key, "jobs")) return showJobs();

    /* show current allocations */
    if (!strcmp(key, "allocations")) return showAllocations();

    /* show running current steps */
    if (!strcmp(key, "steps")) return showSteps(false);

    /* show all current steps */
    if (!strcmp(key, "asteps")) return showSteps(true);

    /* show running HW threads */
    if (!strcmp(key, "hwThreads")) return showHWthreads(false);

    /* show all HW threads */
    if (!strcmp(key, "ahwThreads")) return showHWthreads(true);

    /* show nodeIDs for a list of hosts */
    if (!strncmp(key, "resolveIDs=", 11)) return resolveIDs(key+11);

    /* show tasks */
    if (!strcmp(key, "tasks")) return showTasks();

    /* show spank plugins */
    if (!strcmp(key, "spank")) return showSpank();

#ifdef HAVE_SPANK
    /* show spank plugins */
    if (!strcmp(key, "tainted")) {
	str2Buf("\nThe psid is ", &buf, &bufSize);
	if (!tainted) str2Buf("not ", &buf, &bufSize);
	return str2Buf("tainted\n", &buf, &bufSize);
    };
#endif

    str2Buf("\nInvalid key '", &buf, &bufSize);
    str2Buf(key, &buf, &bufSize);
    str2Buf("'\n", &buf, &bufSize);
    return showVirtualKeys(buf, &bufSize, false);
}
