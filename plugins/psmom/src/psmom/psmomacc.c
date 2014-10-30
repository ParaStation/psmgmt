/*
 * ParaStation
 *
 * Copyright (C) 2012 - 2014 ParTec Cluster Competence Center GmbH, Munich
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
#include <time.h>
#include <inttypes.h>

#include "psmomjob.h"
#include "psmomlog.h"

#include "psaccounthandles.h"

#include "psmomacc.h"

/**
 * @brief Calculate the walltime for a job.
 *
 * @param job The job to set the walltime for.
 *
 * @return No return value.
 */
static void setJobWalltime(Job_t *job)
{
    time_t now = time(NULL);
    time_t wspan=0, whour=0, wmin=0, wsec=0;
    char walltime[100];

    if (!job) {
	mlog("%s: invalid job!\n", __func__);
	return;
    }

    if (!job->start_time || job->start_time > now) {
	mlog("%s: walltime calc error\n", __func__);
	return;
    }

    wspan = now - job->start_time;
    job->res.walltime = wspan;

    whour = wspan / 3600;
    wspan = wspan % 3600;
    wmin = wspan / 60;
    wsec = wspan % 60;

    snprintf(walltime, 100, "%02zu:%02zu:%02zu", whour, wmin, wsec);
    setEntry(&job->status.list, "resources_used", "walltime", walltime);
}

/**
 * @brief Calculate the cputime from utime/stime.
 *
 * @param job The job to set the cputime for.
 *
 * @param stime The time spend in system mode.
 *
 * @param utime The time spend in user mode.
 *
 * @return No return value.
 */
static void calcJobPollCpuTime(Job_t *job, uint64_t stime, uint64_t utime)
{
    uint32_t chour=0, cmin=0, csec=0;
    uint64_t cspan;

    cspan = stime + utime;
    if (cspan < (uint64_t) job->res.a_chour + job->res.a_cmin +
		job->res.a_csec) {
	return;
    }
    chour = cspan / 3600;
    cspan = cspan % 3600;
    cmin = cspan / 60;
    csec = cspan % 60;

    mdbg(PSMOM_LOG_ACC, "%s: cspan:'%zu' new:'%i:%i:%i' old:'%i:%i:%i'\n",
	    __func__, cspan, chour, cmin, csec, job->res.a_chour,
	    job->res.a_cmin, job->res.a_csec);

    job->res.a_chour = chour;
    job->res.a_cmin = cmin;
    job->res.a_csec = csec;
}

void addJobWaitCpuTime(Job_t *job, uint64_t cputime)
{
    uint32_t chour=0, cmin=0, csec=0;
    uint64_t cput, cspan;

    if (!job) {
	mlog("%s: invalid job!\n", __func__);
	return;
    }

    if (!cputime) return;

    cput = (uint64_t ) job->res.r_csec + job->res.r_cmin * 60 +
		job->res.r_chour * 3600;

    cspan = cput + cputime;
    chour = cspan / 3600;
    cspan = cspan % 3600;
    cmin = cspan / 60;
    csec = cspan % 60;

    mdbg(PSMOM_LOG_ACC, "%s: cput:%li new:'%i:%i:%i' old:'%i:%i:%i'\n",
	    __func__, cputime, chour, cmin, csec, job->res.r_chour,
	    job->res.r_cmin, job->res.r_csec);

    job->res.r_chour = chour;
    job->res.r_cmin = cmin;
    job->res.r_csec = csec;
}

/**
 * @brief Set the cputime for a job.
 *
 * @param job The job to set the cputime for.
 *
 * @return No return value.
 */
static void setJobCpuTime(Job_t *job)
{
    char cputime[50];

    if (!job) {
	mlog("%s: invalid job!\n", __func__);
	return;
    }

    if (job->res.r_chour + job->res.r_cmin + job->res.r_csec >
	job->res.a_chour + job->res.a_cmin + job->res.a_csec) {
	snprintf(cputime, sizeof(cputime), "%02i:%02i:%02i", job->res.r_chour,
	    job->res.r_cmin, job->res.r_csec);
    } else {
	snprintf(cputime, sizeof(cputime), "%02i:%02i:%02i", job->res.a_chour,
	    job->res.a_cmin, job->res.a_csec);
    }
    setEntry(&job->status.list, "resources_used", "cput", cputime);
}

/**
* @brief Add used memory to the job account information.
*
* @param job The job to add the memory information to.
*
* @param mem The used physical memory to add.
*
* @param vmem The used virtual memory to add.
*
* @return No return value.
*/
static void setJobMemUsage(Job_t *job, uint64_t mem, uint64_t vmem)
{
    char memory[100];

    if (!job) {
	mlog("%s: got invalid job!\n", __func__);
	return;
    }

    mdbg(PSMOM_LOG_ACC, "%s: new_mem:%lu new_vmem:%lu old_mem:%lu "
	    "old_vmem:%lu\n", __func__, mem, vmem, job->res.mem, job->res.vmem);

    if (mem > job->res.mem) {
	job->res.mem = mem;
	snprintf(memory, sizeof(memory), "%" PRIu64 "kb", mem / 1024);
	setEntry(&job->status.list, "resources_used", "mem", memory);
    }

    if (vmem > job->res.vmem) {
	job->res.vmem = vmem;
	snprintf(memory, sizeof(memory), "%" PRIu64 "kb", vmem / 1024);
	setEntry(&job->status.list, "resources_used", "vmem", memory);
    }
}

void fetchAccInfo(Job_t *job)
{
    psaccAccountInfo_t accInfo;

    if (job->pid == -1) return;

    mdbg(PSMOM_LOG_ACC, "%s: requesting job info for '%i'\n", __func__,
			    job->pid);
    psAccountGetJobInfo(job->pid, &accInfo);

    calcJobPollCpuTime(job, accInfo.stime, accInfo.utime);
    addJobWaitCpuTime(job, accInfo.cputime);
    setJobCpuTime(job);
    setJobMemUsage(job, accInfo.mem, accInfo.vmem);
    setJobWalltime(job);
}

void updateJobInfo(Job_t *job)
{
    if (!job) {
	mlog("%s: looping throw all jobs not implemented yet\n", __func__);
	return;
    }

    fetchAccInfo(job);
}
