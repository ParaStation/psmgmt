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
#include "psslurmpin.h"
#include "psidtask.h"

#include "psslurmkvs.h"

static char line[256];

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
    struct tm *ts;
    char start[50];
    list_t *j;

    if (list_empty(&JobList.list)) {
	return str2Buf("\nNo current jobs.\n", &buf, bufSize);
    }

    str2Buf("\njobs:\n\n", &buf, bufSize);

    list_for_each(j, &JobList.list) {
	Job_t *job = list_entry(j, Job_t, list);

	snprintf(line, sizeof(line), "- jobid %u -\n", job->jobid);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "user '%s'\n", job->username);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "# nodes %u\n", job->nrOfNodes);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "nodes '%s'\n", job->slurmNodes);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "jobscript '%s'\n", job->jobscript);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "job state '%s'\n",
		 strJobState(job->state));
	str2Buf(line, &buf, bufSize);

	if (job->fwdata) {
	    snprintf(line, sizeof(line), "job pid %u\n", job->fwdata->cPid);
	    str2Buf(line, &buf, bufSize);
	}

	/* format start time */
	ts = localtime(&job->start_time);
	strftime(start, sizeof(start), "%Y-%m-%d %H:%M:%S", ts);

	snprintf(line, sizeof(line), "start time '%s'\n", start);
	str2Buf(line, &buf, bufSize);

	str2Buf("-\n\n", &buf, bufSize);
    }

    return buf;
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
    struct tm *ts;
    char start[50];
    list_t *a;

    if (list_empty(&AllocList.list)) {
	return str2Buf("\nNo current allocations.\n", &buf, bufSize);
    }

    str2Buf("\nallocations:\n\n", &buf, bufSize);

    list_for_each(a, &AllocList.list) {
	Alloc_t *alloc = list_entry(a, Alloc_t, list);

	snprintf(line, sizeof(line), "- jobid %u -\n", alloc->jobid);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "user '%s'\n", alloc->username);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "# nodes %u\n", alloc->nrOfNodes);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "nodes '%s'\n", alloc->slurmNodes);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "alloc state '%s'\n",
		 strJobState(alloc->state));
	str2Buf(line, &buf, bufSize);

	/* format start time */
	ts = localtime(&alloc->start_time);
	strftime(start, sizeof(start), "%Y-%m-%d %H:%M:%S", ts);

	snprintf(line, sizeof(line), "start time '%s'\n", start);
	str2Buf(line, &buf, bufSize);

	str2Buf("-\n\n", &buf, bufSize);
    }

    return buf;
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
    uint32_t i;
    PSnodes_ID_t lastNode = -1;
    list_t *s;

    if (list_empty(&StepList.list)) {
	return str2Buf("\nNo current HW threads.\n", &buf, bufSize);
    }

    str2Buf("\nHW threads:\n\n", &buf, bufSize);

    list_for_each(s, &StepList.list) {
	Step_t *step = list_entry(s, Step_t, list);

	if (step->state == JOB_COMPLETE && !all) continue;

	snprintf(line, sizeof(line), "- stepid %u:%u threads %u core "
		 "map '%s'-\n", step->jobid, step->stepid, step->numHwThreads,
		 step->cred->stepCoreBitmap);
	str2Buf(line, &buf, bufSize);

	if (!step->hwThreads) {
	    str2Buf("\nno HW threads\n-\n\n", &buf, bufSize);
	    continue;
	}

	str2Buf("\npsslurm threads:", &buf, bufSize);
	for (i=0; i<step->numHwThreads; i++) {
	    if (lastNode != step->hwThreads[i].node) {
		snprintf(line, sizeof(line), "\nnode %i: ",
			 step->hwThreads[i].node);
		str2Buf(line, &buf, bufSize);
	    }
	    lastNode = step->hwThreads[i].node;

	    snprintf(line, sizeof(line), "%i ", step->hwThreads[i].id);
	    str2Buf(line, &buf, bufSize);
	}

	if (step->fwdata && step->fwdata->cPid != 0) {
	    PStask_ID_t cTID = PSC_getTID(-1, step->fwdata->cPid);
	    PStask_t *task = PStasklist_find(&managedTasks, cTID);
	    if (task) {
		snprintf(line, sizeof(line), "\n\npsid threads for logger %s:",
			 PSC_printTID(cTID));
		str2Buf(line, &buf, bufSize);

		for (i=0; i<task->totalThreads; i++) {
		    if (lastNode != task->partThrds[i].node) {
			snprintf(line, sizeof(line), "\nnode %i: ",
				 task->partThrds[i].node);
			str2Buf(line, &buf, bufSize);
		    }
		    lastNode = task->partThrds[i].node;

		    snprintf(line, sizeof(line), "%i ", task->partThrds[i].id);
		    str2Buf(line, &buf, bufSize);
		}
	    } else {
		snprintf(line, sizeof(line), "\n\nno psid threads for "
			 "logger %s:", PSC_printTID(cTID));
		str2Buf(line, &buf, bufSize);
	    }
	}

	str2Buf("\n-\n\n", &buf, bufSize);
    }

    return buf;
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
    struct tm *ts;
    char start[50], *ptr;
    list_t *s;

    if (list_empty(&StepList.list)) {
	return str2Buf("\nNo current steps.\n", &buf, bufSize);
    }

    str2Buf("\nsteps:\n\n", &buf, bufSize);

    list_for_each(s, &StepList.list) {
	Step_t *step = list_entry(s, Step_t, list);

	if (step->state == JOB_COMPLETE && !all) continue;

	snprintf(line, sizeof(line), "- stepid %u:%u -\n", step->jobid,
		 step->stepid);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "user: '%s' uid: %u gid: %u\n",
		 step->username, step->uid, step->gid);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "%u nodes: '%s'\n", step->nrOfNodes,
		 step->slurmNodes);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "step state: '%s'\n",
		 strJobState(step->state));
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "tpp: %u numHwThreads: %u\n",
		 step->tpp, step->numHwThreads);
	str2Buf(line, &buf, bufSize);

	if (step->fwdata) {
	    snprintf(line, sizeof(line), "step PID: %u\n", step->fwdata->cPid);
	    str2Buf(line, &buf, bufSize);
	}

	/* format start time */
	ts = localtime(&step->start_time);
	strftime(start, sizeof(start), "%Y-%m-%d %H:%M:%S", ts);

	snprintf(line, sizeof(line), "start time: '%s'\n", start);
	str2Buf(line, &buf, bufSize);

	ptr = genCPUbindString(step);
	snprintf(line, sizeof(line), "cpuBind: '%s'\n", ptr);
	ufree(ptr);
	str2Buf(line, &buf, bufSize);

	ptr = genMemBindString(step);
	snprintf(line, sizeof(line), "memBind: '%s'\n", ptr);
	ufree(ptr);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "logger TID: %s\n",
		 PSC_printTID(step->loggerTID));
	str2Buf(line, &buf, bufSize);

	str2Buf("-\n\n", &buf, bufSize);
    }

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
