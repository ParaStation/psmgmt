/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
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
    list_t *pos, *tmp;
    Job_t *job;
    struct tm *ts;
    char start[50];

    if (list_empty(&JobList.list)) {
	return str2Buf("\nNo current jobs.\n", &buf, bufSize);
    }

    str2Buf("\njobs:\n\n", &buf, bufSize);

    list_for_each_safe(pos, tmp, &JobList.list) {
	if ((job = list_entry(pos, Job_t, list)) == NULL) break;

	snprintf(line, sizeof(line), "- jobid '%u' -\n", job->jobid);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "user '%s'\n", job->username);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "# nodes '%u'\n", job->nrOfNodes);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "nodes '%s'\n", job->slurmNodes);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "jobscript '%s'\n", job->jobscript);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "job state '%s'\n",
		    strJobState(job->state));
	str2Buf(line, &buf, bufSize);

	if (job->fwdata) {
	    snprintf(line, sizeof(line), "job pid '%u'\n",
			job->fwdata->childPid);
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
    list_t *pos, *tmp;
    Alloc_t *alloc;
    struct tm *ts;
    char start[50];

    if (list_empty(&AllocList.list)) {
	return str2Buf("\nNo current allocations.\n", &buf, bufSize);
    }

    str2Buf("\nallocations:\n\n", &buf, bufSize);

    list_for_each_safe(pos, tmp, &AllocList.list) {
	if ((alloc = list_entry(pos, Alloc_t, list)) == NULL) break;

	snprintf(line, sizeof(line), "- jobid '%u' -\n", alloc->jobid);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "user '%s'\n", alloc->username);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "# nodes '%u'\n", alloc->nrOfNodes);
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
static char *showHWthreads(char *buf, size_t *bufSize, int all)
{
    list_t *pos, *tmp;
    Step_t *step;
    uint32_t i;
    PSnodes_ID_t lastNode = -1;
    PStask_t *task;

    if (list_empty(&StepList.list)) {
	return str2Buf("\nNo current HW threads.\n", &buf, bufSize);
    }

    str2Buf("\nHW threads:\n\n", &buf, bufSize);

    list_for_each_safe(pos, tmp, &StepList.list) {
	if ((step = list_entry(pos, Step_t, list)) == NULL) break;

	if (step->state == JOB_COMPLETE && !all) continue;

	snprintf(line, sizeof(line), "- stepid '%u:%u' threads '%u' core "
		    "map '%s'-\n", step->jobid, step->stepid,
		    step->numHwThreads, step->cred->stepCoreBitmap);
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

	if (step->fwdata && step->fwdata->childPid != 0) {
	    task = PStasklist_find(&managedTasks,
				    PSC_getTID(-1, step->fwdata->childPid));
	    if (task) {
		snprintf(line, sizeof(line), "\n\npsid threads for logger %i:",
			    step->fwdata->childPid);
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
			    "logger %i:", step->fwdata->childPid);
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
static char *showSteps(char *buf, size_t *bufSize, int all)
{
    list_t *pos, *tmp;
    Step_t *step;
    struct tm *ts;
    char start[50], *ptr;

    if (list_empty(&StepList.list)) {
	return str2Buf("\nNo current steps.\n", &buf, bufSize);
    }

    str2Buf("\nsteps:\n\n", &buf, bufSize);

    list_for_each_safe(pos, tmp, &StepList.list) {
	if ((step = list_entry(pos, Step_t, list)) == NULL) break;

	if (step->state == JOB_COMPLETE && !all) continue;

	snprintf(line, sizeof(line), "- stepid '%u:%u' -\n", step->jobid,
		    step->stepid);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "user: '%s' uid: '%u' gid: '%u'\n",
		    step->username, step->uid, step->gid);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "%u nodes: '%s'\n", step->nrOfNodes,
		    step->slurmNodes);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "step state: '%s'\n",
		    strJobState(step->state));
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "tpp: '%u' numHwThreads: '%u'\n",
		    step->tpp, step->numHwThreads);
	str2Buf(line, &buf, bufSize);

	if (step->fwdata) {
	    snprintf(line, sizeof(line), "step pid: '%u'\n",
			step->fwdata->childPid);
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
    char empty[] = "";
    char *name, *val;
    int i = 0;

    str2Buf("\n", &buf, bufSize);

    while (CONFIG_VALUES[i].name != NULL) {
        name = CONFIG_VALUES[i].name;
        if (!(val = getConfValueC(&Config, name))) {
            val = empty;
        }
        snprintf(line, sizeof(line), "%21s = %s\n", name, val);
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
static void showVirtualKeys(char **buf, size_t *bufSize, int example)
{
    char *msg;

    str2Buf("\n# available keys #\n\n", buf, bufSize);

    snprintf(line, sizeof(line), "%12s\t%s\n", "config",
	    "show current configuration");
    str2Buf(line, buf, bufSize);

    snprintf(line, sizeof(line), "%12s\t%s\n", "jobs",
	    "show all jobs");
    str2Buf(line, buf, bufSize);

    snprintf(line, sizeof(line), "%12s\t%s\n", "allocations",
	    "show all allocations");
    str2Buf(line, buf, bufSize);

    snprintf(line, sizeof(line), "%12s\t%s\n", "steps",
	    "show running steps");
    str2Buf(line, buf, bufSize);

    snprintf(line, sizeof(line), "%12s\t%s\n", "asteps",
	    "show all steps");
    str2Buf(line, buf, bufSize);

    snprintf(line, sizeof(line), "%12s\t%s\n", "hwThreads",
	    "show active hwThreads");
    str2Buf(line, buf, bufSize);

    snprintf(line, sizeof(line), "%12s\t%s\n", "ahwThreads",
	    "show all hwThreads");
    str2Buf(line, buf, bufSize);

    if (example) {
	msg = "\nExample:\nUse 'plugin show psslurm key jobs'\n";
	str2Buf(msg, buf, bufSize);
    }
}

char *set(char *key, char *value)
{
    char *buf = NULL;
    size_t bufSize = 0;
    int ret;

    /* search in config for given key */
    if ((getConfigDef(key, CONFIG_VALUES))) {

        if ((ret = verifyConfigEntry(CONFIG_VALUES, key, value)) != 0) {
            if (ret == 1) {
                str2Buf("\nInvalid key '", &buf, &bufSize);
                str2Buf(key, &buf, &bufSize);
                str2Buf("' for cmd set : use 'plugin help psslurm' "
                                "for help.\n", &buf, &bufSize);
            } else if (ret == 2) {
                str2Buf("\nThe key '", &buf, &bufSize);
                str2Buf(key, &buf, &bufSize);
                str2Buf("' for cmd set has to be numeric.\n", &buf,
                                &bufSize);
            }
            return buf;
        }

	if (!(strcmp(key, "DEBUG_MASK"))) {
	    int32_t mask;

	    if ((sscanf(value, "%i", &mask)) != 1) {
		return str2Buf("\nInvalid debug mask: not a number\n", &buf,
			&bufSize);
	    }
	    maskLogger(mask);
	}

        /* save new config value */
	addConfigEntry(&Config, key, value);

	snprintf(line, sizeof(line), "\nsaved '%s = %s'\n", key, value);
        str2Buf(line, &buf, &bufSize);
        return buf;
    }

    str2Buf("\nInvalid key '", &buf, &bufSize);
    str2Buf(key, &buf, &bufSize);
    str2Buf("' for cmd set : use 'plugin help psslurm' for help.\n",
		    &buf, &bufSize);

    return buf;
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

    showVirtualKeys(&buf, &bufSize, 1);

    return buf;
}

char *show(char *key)
{
    char *buf = NULL, *tmp;
    size_t bufSize = 0;

    if (!key) {
	/* show all virtual keys */
	showVirtualKeys(&buf, &bufSize, 1);

	return buf;
    }

    /* search in config for given key */
    if ((tmp = getConfValueC(&Config, key))) {
	str2Buf(key, &buf, &bufSize);
	str2Buf(" = ", &buf, &bufSize);
	str2Buf(tmp, &buf, &bufSize);
	str2Buf("\n", &buf, &bufSize);

	return buf;
    }

    /* show current config */
    if (!(strcmp(key, "config"))) {
	return showConfig(buf, &bufSize);
    }

    /* show current jobs */
    if (!(strcmp(key, "jobs"))) {
	return showJobs(buf, &bufSize);
    }

    /* show current allocations */
    if (!(strcmp(key, "allocations"))) {
	return showAllocations(buf, &bufSize);
    }

    /* show running current steps */
    if (!(strcmp(key, "steps"))) {
	return showSteps(buf, &bufSize, 0);
    }

    /* show all current steps */
    if (!(strcmp(key, "asteps"))) {
	return showSteps(buf, &bufSize, 1);
    }

    /* show running HW threads */
    if (!(strcmp(key, "hwThreads"))) {
	return showHWthreads(buf, &bufSize, 0);
    }

    /* show all HW threads */
    if (!(strcmp(key, "ahwThreads"))) {
	return showHWthreads(buf, &bufSize, 1);
    }

    str2Buf("\nInvalid key '", &buf, &bufSize);
    str2Buf(key, &buf, &bufSize);
    str2Buf("'\n", &buf, &bufSize);
    showVirtualKeys(&buf, &bufSize, 0);

    return buf;
}
