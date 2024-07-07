/*
 * ParaStation
 *
 * Copyright (C) 2016-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>

#include "list.h"
#include "pscommon.h"
#include "pscomplist.h"
#include "pscpu.h"
#include "psstrv.h"

#include "plugin.h"
#include "pluginconfig.h"
#include "pluginhelper.h"
#include "pluginmalloc.h"

#include "psidnodes.h"
#include "psidtask.h"

#include "psmungehandles.h"

#include "slurmcommon.h"
#include "psslurmalloc.h"
#include "psslurmcomm.h"
#include "psslurmconfig.h"
#include "psslurmlog.h"
#include "psslurmjob.h"
#include "psslurmjobcred.h"
#include "psslurmmsg.h"
#include "psslurmpin.h"
#include "psslurmproto.h"
#include "psslurmpscomm.h"
#include "psslurmstep.h"
#include "psslurmtasks.h"
#ifdef HAVE_SPANK
#include "psslurmspank.h"
#endif


typedef struct {
    strbuf_t strBuf;
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
 * @return Always returns false to loop over all jobs
 */
static bool addJobInfo(Job_t *job, const void *info)
{
    char start[50];
    struct tm *ts;
    strbuf_t buf = (strbuf_t)info;

    snprintf(line, sizeof(line), "- jobid %u -\n", job->jobid);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "user '%s'\n", job->username);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "# nodes %u\n", job->nrOfNodes);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "hosts '%s'\n", job->slurmHosts);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "jobscript '%s'\n", job->jobscript);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "job state '%s'\n", Job_strState(job->state));
    strbufAdd(buf, line);

    if (job->fwdata) {
	snprintf(line, sizeof(line), "job pid %u\n", job->fwdata->cPid);
	strbufAdd(buf, line);
    }

    /* format start time */
    ts = localtime(&job->startTime);
    strftime(start, sizeof(start), "%Y-%m-%d %H:%M:%S", ts);

    snprintf(line, sizeof(line), "start time '%s'\n", start);
    strbufAdd(buf, line);

    strbufAdd(buf, "-\n\n");

    return false;
}

/**
 * @brief Show current jobs.
 *
 * @return Returns the buffer with the updated job information.
 */
static char *showJobs(void)
{
    strbuf_t buf = strbufNew(NULL);
    if (!Job_count()) {
	strbufAdd(buf, "\nNo current jobs\n");
    } else {
	strbufAdd(buf, "\njobs:\n\n");
	Job_traverse(addJobInfo, buf);
    }

    return strbufSteal(buf);
}

/**
 * @brief Visitor to add information about a allocation to a buffer
 *
 * @param alloc The allocation to use
 *
 * @param info A StrBuffer structure to save the information
 *
 * @return Always returns false to loop over all allocations
 */
static bool Alloc_addInfo(Alloc_t *alloc, const void *info)
{
    struct tm *ts;
    char start[50];
    strbuf_t buf = (strbuf_t)info;

    snprintf(line, sizeof(line), "- jobid %u -\n", alloc->id);
    strbufAdd(buf, line);

    if (alloc->packID != NO_VAL) {
	snprintf(line, sizeof(line), "packid %u \n", alloc->packID);
	strbufAdd(buf, line);
    }

    snprintf(line, sizeof(line), "user '%s'\n", alloc->username);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "# nodes %u\n", alloc->nrOfNodes);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "hosts '%s'\n", alloc->slurmHosts);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "alloc state '%s'\n",
	     Alloc_strState(alloc->state));
    strbufAdd(buf, line);

    /* format start time */
    ts = localtime(&alloc->startTime);
    strftime(start, sizeof(start), "%Y-%m-%d %H:%M:%S", ts);

    snprintf(line, sizeof(line), "start time '%s'\n", start);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "local node id %u -\n", alloc->localNodeId);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "verified: %s\n",
	     alloc->verified ? "yes" : "no");
    strbufAdd(buf, line);

    strbufAdd(buf, "-\n\n");

    return false;
}

/**
 * @brief Visitor to add information about tasks of a step
 *
 * @param step The step to use
 *
 * @param info String buffer of type strbuf_t to store the information
 *
 * @return Always returns false to loop over all steps
 */
static bool addTaskInfo(Step_t *step, const void *info)
{
    strbuf_t buf = (strbuf_t)info;
    list_t *tasks = &step->tasks;

    if (step->state == JOB_COMPLETE) return false;

    if (!countTasks(tasks)) {
	snprintf(line, sizeof(line), "\nno tasks for %s\n", Step_strID(step));
	strbufAdd(buf, line);
	return false;
    }

    snprintf(line, sizeof(line), "\n%u tasks for %s\n", countTasks(tasks),
	     Step_strID(step));
    strbufAdd(buf, line);

    list_t *t;
    list_for_each(t, tasks) {
	PS_Tasks_t *task = list_entry(t, PS_Tasks_t, next);

	snprintf(line, sizeof(line), "child %s ", PSC_printTID(task->childTID));
	strbufAdd(buf, line);

	snprintf(line, sizeof(line), "forwarder %s jobRank %i globalRank %i"
		 " exit %i sent exit %u\n", PSC_printTID(task->forwarderTID),
		 task->jobRank, task->globalRank, task->exitCode, task->sentExit);
	strbufAdd(buf, line);
    }

    strbufAdd(buf, "-\n\n");

    return false;
}

/**
 * @brief Show tasks of all local steps
 *
 * @return Returns the buffer with the updated task information
 */
static char *showTasks(void)
{
    strbuf_t buf = strbufNew(NULL);
    if (!Step_count()) {
	strbufAdd(buf, "\nNo current tasks\n");
    } else {
	strbufAdd(buf, "\ntasks for all steps:\n");
	Step_traverse(addTaskInfo, buf);
    }

    return strbufSteal(buf);
}

#ifdef HAVE_SPANK

/**
 * @brief Visitor to add information about spank plugins
 *
 * @param sp The spank plugin to use
 *
 * @param info String buffer of type strbuf_t to store the information
 *
 * @return Always returns false to loop over all steps
 */
static bool addSpankInfo(Spank_Plugin_t *sp, const void *info)
{
    strbuf_t buf = (strbuf_t)info;

    snprintf(line, sizeof(line), "plugin %s: type=%s ver=%u optional=%s "
	     "path=%s\n", sp->name, sp->type, sp->version,
	     (sp->optional ? "true" : false), sp->path);
    strbufAdd(buf, line);

    if (strvSize(sp->argV)) {
	strbufAdd(buf, "\t");
	int cnt = 0;
	for (char **str = strvGetArray(sp->argV); *str; str++, cnt++) {
	    snprintf(line, sizeof(line), "argv%i %s ", cnt, *str);
	    strbufAdd(buf, line);
	}
	strbufAdd(buf, "\n");
    }

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
    strbuf_t buf = strbufNew(NULL);
#ifdef HAVE_SPANK
    strbufAdd(buf, "\nactive spank plugins:\n\n");
    SpankTraversePlugins(addSpankInfo, buf);
#else
    strbufAdd(buf, "\npsmgmt was compiled without spank support\n\n");
#endif

    return strbufSteal(buf);
}

/**
 * @brief Show slurm.conf configuration hash and its read time
 *
 * @return Returns the buffer with the updated configuration information
 */
static char *showConfHash(void)
{
    strbuf_t buf = strbufNew(NULL);
    snprintf(line, sizeof(line), "\nslurm.conf hash: %#.08x updated %s\n",
	     getSlurmConfHash(), getSlurmUpdateTime());
    strbufAdd(buf, line);

    return strbufSteal(buf);
}

/**
 * @brief Show how many time the Slurm healthcheck was executed
 *
 * @return Returns the buffer with the updated HC information
 */
static char *showHealthCheck(void)
{
    strbuf_t buf = strbufNew(NULL);
    snprintf(line, sizeof(line), "\nSlurm health-check runs: %lu\n",
	     getSlurmHCRuns());
    strbufAdd(buf, line);

    return strbufSteal(buf);
}

/**
 * @brief Show current allocations
 *
 * @return Returns the buffer with the updated allocation information
 */
static char *showAllocations(void)
{
    strbuf_t buf = strbufNew(NULL);
    if (!Alloc_count()) {
	strbufAdd(buf, "\nNo current allocations\n");
    } else {
	strbufAdd(buf, "\nallocations:\n\n");
	Alloc_traverse(Alloc_addInfo, buf);
    }

    return strbufSteal(buf);
}

/**
 * @brief Resolve a given host-list
 *
 * Resolve a compressed host-list and show the single hosts including
 * the corresponding ParaStation node IDs and the local node ID.
 *
 * @return Returns the buffer with the nodeID and host pairs
 */
static char *resolveIDs(char *hosts)
{
    strbuf_t buf = strbufNew(NULL);

    PSnodes_ID_t *nodes;
    uint32_t nrOfNodes;
    if (!hosts) {
	strbufAdd(buf, "\nSpecify hosts to resolve\n");
    } else if (!convHLtoPSnodes(hosts, getNodeIDbySlurmHost, &nodes, &nrOfNodes)) {
	strbufAdd(buf, "\nResolving PS nodeIDs failed\n");
    } else {
	uint32_t localNodeId = getLocalID(nodes, nrOfNodes);
	if (localNodeId == NO_VAL) {
	    strbufAdd(buf, "\nCould not find my local ID\n");
	} else {
	    snprintf(line, sizeof(line), "\nLocal node ID is %u\n", localNodeId);
	    strbufAdd(buf, line);
	}

	for (uint32_t i = 0; i < nrOfNodes; i++) {
	    snprintf(line, sizeof(line), "%u nodeID %i hostname %s\n",
		     i, nodes[i], getSlurmHostbyNodeID(nodes[i]));
	    strbufAdd(buf, line);
	}
    }

    return strbufSteal(buf);
}

/**
 * @brief Visitor to add information about a step to a buffer
 *
 * @param step The step to use
 *
 * @param info A StepInfo structure to save the information
 *
 * @return Always returns false to loop over all steps
 */
static bool addHwthreadsInfo(Step_t *step, const void *info)
{
    StepInfo_t *stepInfo = (StepInfo_t *) info;
    strbuf_t buf = stepInfo->strBuf;
    PSnodes_ID_t lastNode = -1;

    if (step->state == JOB_COMPLETE && !stepInfo->all) return false;

    PSCPU_set_t *cpuset = &(step->nodeinfos[step->localNodeId].stepHWthreads);
    short numCPUs = step->nodeinfos[step->localNodeId].threadCount;
    snprintf(line, sizeof(line), "- %s threads %u coremap '%s'-\n",
	    Step_strID(step), step->numHwThreads,
	    PSCPU_print_part(*cpuset, PSCPU_bytesForCPUs(numCPUs)));
    strbufAdd(buf, line);

    if (!step->slots) {
	strbufAdd(buf, "\nno HW threads\n-\n\n");
	return false;
    }

    strbufAdd(buf, "\npsslurm threads:");
    for (uint32_t i = 0; i < step->np; i++) {
	if (lastNode != step->slots[i].node) {
	    snprintf(line, sizeof(line), "\nnode %i: ", step->slots[i].node);
	    strbufAdd(buf, line);
	}
	lastNode = step->slots[i].node;

	for (uint16_t cpu = 0;
	     cpu < PSIDnodes_getNumThrds(step->slots[i].node); cpu++) {
	    if (PSCPU_isSet(step->slots[i].CPUset, cpu)) {
		snprintf(line, sizeof(line), "%hu ", cpu);
		strbufAdd(buf, line);
	    }
	}
    }

    if (step->fwdata && step->fwdata->cPid != 0) {
	PStask_ID_t cTID = PSC_getTID(-1, step->fwdata->cPid);
	PStask_t *task = PStasklist_find(&managedTasks, cTID);
	if (task) {
	    snprintf(line, sizeof(line), "\n\npsid threads for logger %s:",
		    PSC_printTID(cTID));
	    strbufAdd(buf, line);

	    for (uint32_t i = 0; i < task->totalThreads; i++) {
		if (lastNode != task->partThrds[i].node) {
		    snprintf(line, sizeof(line), "\nnode %i: ",
			     task->partThrds[i].node);
		    strbufAdd(buf, line);
		}
		lastNode = task->partThrds[i].node;

		snprintf(line, sizeof(line), "%i ", task->partThrds[i].id);
		strbufAdd(buf, line);
	    }
	} else {
	    snprintf(line, sizeof(line), "\n\nno psid threads for logger %s:",
		     PSC_printTID(cTID));
	    strbufAdd(buf, line);
	}
    }
    strbufAdd(buf, "\n-\n\n");

    return false;
}

/**
 * @brief Show HW threads
 *
 * @return Returns the buffer with the updated information
 */
static char *showHWthreads(bool all)
{
    StepInfo_t stepInfo = {
	.strBuf = strbufNew(NULL),
	.all = all, };

    if (!Step_count()) {
	strbufAdd(stepInfo.strBuf, "\nNo current HW threads\n");
    } else {
	strbufAdd(stepInfo.strBuf, "\nHW threads:\n\n");
	Step_traverse(addHwthreadsInfo, &stepInfo);
    }

    return strbufSteal(stepInfo.strBuf);
}

/**
 * @brief Visitor to add information about a step to a buffer
 *
 * @param step The step to use
 *
 * @param info A StepInfo structure to save the information
 *
 * @return Always returns false to loop over all steps
 */
static bool addStepInfo(Step_t *step, const void *info)
{
    StepInfo_t *stepInfo = (StepInfo_t *) info;
    strbuf_t buf = stepInfo->strBuf;
    struct tm *ts;
    char start[50], *ptr;

    if (step->state == JOB_COMPLETE && !stepInfo->all) return false;

    snprintf(line, sizeof(line), "- %s -\n", Step_strID(step));
    strbufAdd(buf, line);

    if (step->packJobid != NO_VAL) {
	snprintf(line, sizeof(line), "pack ID: %u\n", step->packJobid);
	strbufAdd(buf, line);

	snprintf(line, sizeof(line), "pack hosts: %s\n", step->packHostlist);
	strbufAdd(buf, line);
    }

    snprintf(line, sizeof(line), "user: '%s' uid: %u gid: %u\n",
	     step->username, step->uid, step->gid);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "%u hosts: '%s'\n", step->nrOfNodes,
	     step->slurmHosts);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "state: %s\n", Job_strState(step->state));
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "tpp: %u numHwThreads: %u\n",
	     step->tpp, step->numHwThreads);
    strbufAdd(buf, line);

    if (step->fwdata) {
	snprintf(line, sizeof(line), "step PID: %u\n", step->fwdata->cPid);
	strbufAdd(buf, line);
    }

    /* format start time */
    ts = localtime(&step->startTime);
    strftime(start, sizeof(start), "%Y-%m-%d %H:%M:%S", ts);

    snprintf(line, sizeof(line), "start time: '%s'\n", start);
    strbufAdd(buf, line);

    ptr = genCPUbindString(step);
    snprintf(line, sizeof(line), "cpuBind: '%s'\n", ptr);
    ufree(ptr);
    strbufAdd(buf, line);

    ptr = genMemBindString(step);
    snprintf(line, sizeof(line), "memBind: '%s'\n", ptr);
    ufree(ptr);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "logger TID: %s\n",
	     PSC_printTID(step->loggerTID));
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "local node ID: %u\n", step->localNodeId);
    strbufAdd(buf, line);

    strbufAdd(buf, "-\n\n");

    return false;
}

/**
 * @brief Show current steps
 *
 * @return Returns the buffer with the updated step information
 */
static char *showSteps(bool all)
{
    StepInfo_t stepInfo = {
	.strBuf = strbufNew(NULL),
	.all = all, };

    if (!Step_count()) {
	strbufAdd(stepInfo.strBuf, "\nNo current steps\n");
    } else {
	strbufAdd(stepInfo.strBuf, "\nsteps:\n\n");
	Step_traverse(addStepInfo, &stepInfo);
    }

    return strbufSteal(stepInfo.strBuf);
}

/**
 * @brief Show current configuration
 *
 * @return Returns the buffer with the updated configuration information
 */
static char *showConfig(void)
{
    strbuf_t buf = strbufNew(NULL);
    strbufAdd(buf, "\n");

    for (int i = 0; confDef[i].name; i++) {
	char *cName = confDef[i].name;
	char *cVal = getConfValueC(Config, cName);
	snprintf(line, sizeof(line), "%21s = %s\n",
		 cName, cVal ? cVal : "<empty>");
	strbufAdd(buf, line);
    }

    return strbufSteal(buf);
}

/**
 * @brief Show all supported virtual keys
 *
 * @param buf String buffer that already might contain heading
 * information; will be initialized if required
 *
 * @param example Flag to present also some examples
 *
 * @return Returns the buffer with the updated forwarder information
 */
static char *showVirtualKeys(strbuf_t buf, bool example)
{
    if (!strbufInitialized(buf)) buf = strbufNew(NULL);

    strbufAdd(buf, "\n# available keys #\n\n");
    strbufAdd(buf, "     config\tshow current configuration\n");
    strbufAdd(buf, "       jobs\tshow all jobs\n");
    strbufAdd(buf, "allocations\tshow all allocations\n");
    strbufAdd(buf, "      steps\tshow running steps\n");
    strbufAdd(buf, "     asteps\tshow all steps\n");
    strbufAdd(buf, " ahwThreads\tshow all hwThreads\n");
    strbufAdd(buf, "      tasks\tshow all tasks\n");
    strbufAdd(buf, " resolveIDs\tresolve a Slurm host-list\n");
    strbufAdd(buf, "      spank\tshow active spank plugins\n");
    strbufAdd(buf, "    tainted\tshow if a spank plugin taints psid\n");
    strbufAdd(buf, "  slurmHash\tshow slurm.conf hash and read time\n");
    strbufAdd(buf, "    slurmHC\tshow Slurm health-check runs\n");
    strbufAdd(buf, "connections\tshow Slurm connections\n");

    if (example) {
	strbufAdd(buf, "\nExamples:\n * Use 'plugin show psslurm key jobs'"
		  " to display jobs\n");
	strbufAdd(buf, " * Use 'plugin set psslurm CLEAR_CONF_CACHE 1' to"
		  " clear config cache\n");
    }

    return strbufSteal(buf);
}

char *set(char *key, char *value)
{
    char *buf = NULL;
    size_t bufSize = 0;

    /* load a Spank plugin */
    if (!strcmp(key, "SPANK_LOAD")) {
#ifdef HAVE_SPANK
	Spank_Plugin_t *sp = SpankNewPlug(value);
	if (!sp) {
	    snprintf(line, sizeof(line),
		    "\nerror loading plugin %s\n", value);
	    return str2Buf(line, &buf, &bufSize);
	}
	int ret = SpankLoadPlugin(sp, true);

	switch (ret) {
	    case -1:
		snprintf(line, sizeof(line),
			 "\nerror loading plugin %s\n", sp->name);
		ufree(sp);
		break;
	    case 0:
		snprintf(line, sizeof(line),
			 "\nsuccessfully loaded plugin %s\n", sp->name);
		sp->path = ustrdup(value);
		SpankSavePlugin(sp);
		break;
	    case 1:
		snprintf(line, sizeof(line),
			 "\nloading plugin %s was skipped without errors\n",
			 sp->name);
		ufree(sp);
		break;
	}
#else
	snprintf(line, sizeof(line),
		 "\npsmgmt was compiled without spank support\n\n");
#endif
	return str2Buf(line, &buf, &bufSize);
    }

    /* unload a Spank plugin */
    if (!strcmp(key, "SPANK_UNLOAD") || !strcmp(key, "SPANK_FIN")) {
#ifdef HAVE_SPANK
	bool fin = !strcmp(key, "SPANK_FIN") ? true : false;
	if (!SpankUnloadPlugin(value, fin)) {
	    snprintf(line, sizeof(line), "\nunloading plugin %s failed\n",
		     value);
	} else {
	    snprintf(line, sizeof(line), "\nunloaded plugin %s successfully\n",
		     value);
	}
#else
	snprintf(line, sizeof(line),
		 "\npsmgmt was compiled without spank support\n\n");
#endif
	return str2Buf(line, &buf, &bufSize);
    }

    if (!strcmp(key, "DEL_ALLOC")) {
	int id = atoi(value);
	if (Alloc_delete(id)) {
	    snprintf(line, sizeof(line), "\ndeleted allocation %i\n", id);
	} else {
	    snprintf(line, sizeof(line), "\nfailed to delete allocation %i\n",
		     id);
	}
	return str2Buf(line, &buf, &bufSize);
    } else if (!strcmp(key, "DEL_JOB")) {
	Job_t *job = Job_findByIdC(value);
	if (Job_destroy(job)) {
	    snprintf(line, sizeof(line), "\ndeleted job %s\n", value);
	} else {
	    snprintf(line, sizeof(line), "\nfailed to delete job %s\n",
		     value);
	}
	return str2Buf(line, &buf, &bufSize);
    } else if (!strcmp(key, "DEL_STEP")) {
	int id = atoi(value);
	if (Step_findByJobid(id)) {
	    Step_destroyByJobid(id);
	    snprintf(line, sizeof(line), "\ndeleted steps with jobid %i\n", id);
	} else {
	    snprintf(line, sizeof(line), "\nfailed to delete steps with jobid "
		     "%i\n", id);
	}
	return str2Buf(line, &buf, &bufSize);
    }

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
	addConfigEntry(Config, key, value);

	snprintf(line, sizeof(line), "\nsaved '%s = %s'\n", key, value);
	return str2Buf(line, &buf, &bufSize);
    }

    if (!strcmp(key, "CLEAR_CONF_CACHE")) {
	char *confDir = getConfValueC(Config, "SLURM_CONF_CACHE");
	removeDir(confDir, false);
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

    if (unsetConfigEntry(Config, confDef, key)) return buf;

    str2Buf("\nInvalid key '", &buf, &bufSize);
    str2Buf(key, &buf, &bufSize);
    str2Buf("' for cmd unset : use 'plugin help psslurm' for help.\n",
	    &buf, &bufSize);

    return buf;
}

char *help(char *key)
{
    strbuf_t buf = strbufNew(NULL);
    char type[10];

    if (key && !strcmp(key, "set")) {
	strbufAdd(buf, "\n# psslurm set options #\n\n");
	strbufAdd(buf, "\nTo change configuration parameters use "
		  "'plugin set psslurm config_name config_value\n");
	strbufAdd(buf, " * Use 'plugin set psslurm DEL_ALLOC ID' to delete "
		  "an allocation\n");
	strbufAdd(buf, " * Use 'plugin set psslurm DEL_JOB ID' to delete a"
		  " job\n");
	strbufAdd(buf, " * Use 'plugin set psslurm DEL_STEP ID' to delete a"
		  " step\n");
	strbufAdd(buf, " * Use 'plugin set psslurm CLEAR_CONF_CACHE 1' to clear"
		  " config cache\n");
	strbufAdd(buf, " * Use 'plugin set psslurm SPANK_LOAD \"path <args>\"'"
		  " to load a Spank plugin from absolute or relative path\n");
	strbufAdd(buf, "   Optional arguments for the Spank plugin may be"
		  " separated using spaces\n");
	strbufAdd(buf, " * Use 'plugin set psslurm SPANK_UNLOAD name' to unload"
		  " a Spank plugin without calling SLURMD_EXIT hook\n");
	strbufAdd(buf, " * Use 'plugin set psslurm SPANK_FIN name' to unload"
		  " a Spank plugin after executing SLURMD_EXIT hook\n");

	return strbufSteal(buf);
    }

    strbufAdd(buf, "\n# configuration options #\n\n");

    for (int i = 0; confDef[i].name; i++) {
	snprintf(type, sizeof(type), "<%s>", confDef[i].type);
	snprintf(line, sizeof(line), "%21s\t%8s    %s\n", confDef[i].name,
		type, confDef[i].desc);
	strbufAdd(buf, line);
    }

    return showVirtualKeys(buf, true);
}

/**
 * @brief Visitor to add information about a connection to a buffer
 *
 * @param conn The connection to add
 *
 * @param info A StrBuffer structure to save the information
 *
 * @return Always returns false to loop over all connections
 */
static bool addConnInfo(Connection_t *conn, const void *info)
{
    strbuf_t buf = (strbuf_t)info;

    snprintf(line, sizeof(line), "\n- socket %i -\n", conn->sock);
    strbufAdd(buf, line);

    time_t oTime = conn->openTime.tv_sec;
    struct tm *ts = localtime(&oTime);
    strftime(line, sizeof(line), "opened: %Y-%m-%d %H:%M:%S\n", ts);
    strbufAdd(buf, line);

    ts = localtime(&conn->recvTime);
    strftime(line, sizeof(line), "received: %Y-%m-%d %H:%M:%S\n", ts);
    strbufAdd(buf, line);

    if (conn->fw.head.fwNodeList) {
	snprintf(line, sizeof(line), "message %s forward to %s returned %u of "
		 "%u\n", msgType2String(conn->fw.head.type),
		 conn->fw.head.fwNodeList, conn->fw.head.returnList,
		 conn->fw.head.fwResSize);
	strbufAdd(buf, line);
    }

    if (conn->step && Step_verifyPtr(conn->step)) {
	snprintf(line, sizeof(line), "step %s\n", Step_strID(conn->step));
	strbufAdd(buf, line);
    }

    struct sockaddr_in sockLocal, sockRemote;
    socklen_t lenLoc = sizeof(sockLocal), lenRem = sizeof(sockRemote);

    if (getsockname(conn->sock, (struct sockaddr*)&sockLocal, &lenLoc) == -1) {
	mwarn(errno, "%s: getsockname(%i)", __func__, conn->sock);
    } else if (getpeername(conn->sock, (struct sockaddr*)&sockRemote,
	       &lenRem) == -1) {
	mwarn(errno, "%s: getpeername(%i)", __func__, conn->sock);
    } else {
	snprintf(line, sizeof(line), "connected local %s:%u remote %s:%u\n",
		 inet_ntoa(sockRemote.sin_addr), ntohs(sockRemote.sin_port),
		 inet_ntoa(sockLocal.sin_addr), ntohs(sockLocal.sin_port));
	strbufAdd(buf, line);
    }

    return false;
}

/**
 * @brief Show current connections
 *
 * @return Returns the buffer with the updated connection information
 */
static char *showConnections(void)
{
    strbuf_t buf = strbufNew(NULL);
    strbufAdd(buf, "\nconnections:\n\n");
    Connection_traverse(addConnInfo, buf);

    return strbufSteal(buf);
}

char *show(char *key)
{
    if (!key) return showVirtualKeys(NULL, true);

    /* search in config for given key */
    char *tmp = getConfValueC(Config, key);
    if (tmp) {
	strbuf_t buf = strbufNew(NULL);

	strbufAdd(buf, key);
	strbufAdd(buf, " = ");
	strbufAdd(buf, tmp);
	strbufAdd(buf, "\n");

	return strbufSteal(buf);
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

    /* show config hash */
    if (!strcmp(key, "slurmHash")) return showConfHash();

    /* show Slurm healthcheck runs */
    if (!strcmp(key, "slurmHC")) return showHealthCheck();

    /* show Slurm connections */
    if (!strcmp(key, "connections")) return showConnections();

#ifdef HAVE_SPANK
    /* show spank plugins */
    if (!strcmp(key, "tainted")) {
	strbuf_t buf = strbufNew(NULL);
	strbufAdd(buf, "\nThe psid is ");
	if (!tainted) strbufAdd(buf, "not ");
	strbufAdd(buf, "tainted\n");
	return strbufSteal(buf);
    };
#endif

    strbuf_t buf = strbufNew(NULL);
    strbufAdd(buf, "\nInvalid key '");
    strbufAdd(buf, key);
    strbufAdd(buf, "'\n");
    return showVirtualKeys(buf, false);
}
