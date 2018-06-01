/*
 * ParaStation
 *
 * Copyright (C) 2016-2017 ParTec Cluster Competence Center GmbH, Munich
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

#include "psslurmconfig.h"
#include "psslurmlog.h"
#include "psslurmjob.h"
#include "psslurmalloc.h"
#include "psslurmpin.h"
#include "psidtask.h"

#include "plugin.h"

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

    addStrBuf("-\n\n", strBuf);

    return false;
}

/**
 * @brief Show current jobs.
 *
 * @param buf The buffer to write the information to.
 *
 * @param bufSize The size of the buffer.
 *
 * @return Returns the buffer with the updated job information.
 */
static char *showJobs(char *buf, size_t *bufSize)
{
    StrBuffer_t strBuf;

    if (!countJobs()) {
	return str2Buf("\nNo current jobs.\n", &buf, bufSize);
    }

    str2Buf("\njobs:\n\n", &buf, bufSize);

    strBuf.buf = buf;
    strBuf.bufSize = *bufSize;
    traverseJobs(addJobInfo, &strBuf);

    *bufSize = strBuf.bufSize;
    return buf;
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

    snprintf(line, sizeof(line), "user '%s'\n", alloc->username);
    addStrBuf(line, strBuf);

    snprintf(line, sizeof(line), "# nodes %u\n", alloc->nrOfNodes);
    addStrBuf(line, strBuf);

    snprintf(line, sizeof(line), "hosts '%s'\n", alloc->slurmHosts);
    addStrBuf(line, strBuf);

    snprintf(line, sizeof(line), "alloc state '%s'\n",
	    strJobState(alloc->state));
    addStrBuf(line, strBuf);

    /* format start time */
    ts = localtime(&alloc->startTime);
    strftime(start, sizeof(start), "%Y-%m-%d %H:%M:%S", ts);

    snprintf(line, sizeof(line), "start time '%s'\n", start);
    addStrBuf(line, strBuf);

    addStrBuf("-\n\n", strBuf);

    return false;
}

/**
 * @brief Show current allocations.
 *
 * @param buf The buffer to write the information to.
 *
 * @param bufSize The size of the buffer.
 *
 * @return Returns the buffer with the updated allocation information.
 */
static char *showAllocations(char *buf, size_t *bufSize)
{
    StrBuffer_t strBuf;

    if (!countAllocs()) {
	return str2Buf("\nNo current allocations.\n", &buf, bufSize);
    }

    str2Buf("\nallocations:\n\n", &buf, bufSize);

    strBuf.buf = buf;
    strBuf.bufSize = *bufSize;
    traverseAllocs(addAllocInfo, &strBuf);

    *bufSize = strBuf.bufSize;

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

    if (step->state == JOB_COMPLETE && !stepInfo->all) return false;

    snprintf(line, sizeof(line), "- stepid %u:%u threads %u core "
	    "map '%s'-\n", step->jobid, step->stepid, step->numHwThreads,
	    step->cred->stepCoreBitmap);
    addStrBuf(line, strBuf);

    if (!step->hwThreads) {
	addStrBuf("\nno HW threads\n-\n\n", strBuf);
	return false;
    }

    addStrBuf("\npsslurm threads:", strBuf);
    for (i=0; i<step->numHwThreads; i++) {
	if (lastNode != step->hwThreads[i].node) {
	    snprintf(line, sizeof(line), "\nnode %i: ",
		    step->hwThreads[i].node);
	    addStrBuf(line, strBuf);
	}
	lastNode = step->hwThreads[i].node;

	snprintf(line, sizeof(line), "%i ", step->hwThreads[i].id);
	addStrBuf(line, strBuf);
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
 * @param buf The buffer to write the information to.
 *
 * @param bufSize The size of the buffer.
 *
 * @return Returns the buffer with the updated information.
 */
static char *showHWthreads(char *buf, size_t *bufSize, bool all)
{

    StepInfo_t stepInfo;

    if (!countSteps()) {
	return str2Buf("\nNo current HW threads.\n", &buf, bufSize);
    }

    str2Buf("\nHW threads:\n\n", &buf, bufSize);

    stepInfo.all = all;
    stepInfo.strBuf.buf = buf;
    stepInfo.strBuf.bufSize = *bufSize;

    traverseSteps(addHwthreadsInfo, &stepInfo);

    *bufSize = stepInfo.strBuf.bufSize;
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

    addStrBuf("-\n\n", strBuf);

    return false;
}

/**
 * @brief Show current steps.
 *
 * @param buf The buffer to write the information to.
 *
 * @param bufSize The size of the buffer.
 *
 * @return Returns the buffer with the updated step information.
 */
static char *showSteps(char *buf, size_t *bufSize, bool all)
{
    StepInfo_t stepInfo;

    if (!countSteps()) {
	return str2Buf("\nNo current steps.\n", &buf, bufSize);
    }

    str2Buf("\nsteps:\n\n", &buf, bufSize);

    stepInfo.all = all;
    stepInfo.strBuf.buf = buf;
    stepInfo.strBuf.bufSize = *bufSize;

    traverseSteps(addStepInfo, &stepInfo);

    *bufSize = stepInfo.strBuf.bufSize;
    return buf;
}

/**
 * @brief Show current configuration.
 *
 * @param buf The buffer to write the information to.
 *
 * @param bufSize The size of the buffer.
 *
 * @return Returns the buffer with the updated configuration information.
 */
static char *showConfig(char *buf, size_t *bufSize)
{
    int i = 0;

    str2Buf("\n", &buf, bufSize);

    while (CONFIG_VALUES[i].name != NULL) {
	char *name = CONFIG_VALUES[i].name;
	char *val = getConfValueC(&Config, name);
	snprintf(line, sizeof(line), "%21s = %s\n", name, val ? val:"<empty>");
	str2Buf(line, &buf, bufSize);
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

    if (example) str2Buf("\nExample:\nUse 'plugin show psslurm key jobs'\n",
			 &buf, bufSize);
    return buf;
}

char *set(char *key, char *value)
{
    char *buf = NULL;
    size_t bufSize = 0;

    /* search in config for given key */
    if (getConfigDef(key, CONFIG_VALUES)) {
	int ret = verifyConfigEntry(CONFIG_VALUES, key, value);
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

	/* save new config value */
	addConfigEntry(&Config, key, value);

	snprintf(line, sizeof(line), "\nsaved '%s = %s'\n", key, value);
	return str2Buf(line, &buf, &bufSize);
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

    if (unsetConfigEntry(&Config, CONFIG_VALUES, key)) return buf;

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

    while (CONFIG_VALUES[i].name != NULL) {
	snprintf(type, sizeof(type), "<%s>", CONFIG_VALUES[i].type);
	snprintf(line, sizeof(line), "%21s\t%8s    %s\n", CONFIG_VALUES[i].name,
		type, CONFIG_VALUES[i].desc);
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
    if (!strcmp(key, "config")) return showConfig(buf, &bufSize);

    /* show current jobs */
    if (!strcmp(key, "jobs")) return showJobs(buf, &bufSize);

    /* show current allocations */
    if (!strcmp(key, "allocations")) return showAllocations(buf, &bufSize);

    /* show running current steps */
    if (!strcmp(key, "steps")) return showSteps(buf, &bufSize, false);

    /* show all current steps */
    if (!strcmp(key, "asteps")) return showSteps(buf, &bufSize, true);

    /* show running HW threads */
    if (!strcmp(key, "hwThreads")) return showHWthreads(buf, &bufSize, false);

    /* show all HW threads */
    if (!strcmp(key, "ahwThreads")) return showHWthreads(buf, &bufSize, true);

    str2Buf("\nInvalid key '", &buf, &bufSize);
    str2Buf(key, &buf, &bufSize);
    str2Buf("'\n", &buf, &bufSize);
    return showVirtualKeys(buf, &bufSize, false);
}
