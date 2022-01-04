/*
 * ParaStation
 *
 * Copyright (C) 2012-2017 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psmomacc.h"

#include <stdio.h>
#include <inttypes.h>
#include <sys/resource.h>
#include <time.h>

#include "psaccounthandles.h"

#include "psmomlist.h"
#include "psmomlog.h"

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
	snprintf(cputime, sizeof(cputime), "%02u:%02u:%02u", job->res.r_chour,
	    job->res.r_cmin, job->res.r_csec);
    } else {
	snprintf(cputime, sizeof(cputime), "%02u:%02u:%02u", job->res.a_chour,
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
	snprintf(memory, sizeof(memory), "%" PRIu64 "kb", mem);
	setEntry(&job->status.list, "resources_used", "mem", memory);
    }

    if (vmem > job->res.vmem) {
	job->res.vmem = vmem;
	snprintf(memory, sizeof(memory), "%" PRIu64 "kb", vmem);
	setEntry(&job->status.list, "resources_used", "vmem", memory);
    }
}

void fetchAccInfo(Job_t *job)
{
    AccountDataExt_t accData;

    if (job->pid == -1) return;

    mdbg(PSMOM_LOG_ACC, "%s: request for job-pid %i\n", __func__, job->pid);
    psAccountGetDataByJob(job->pid, &accData);

    uint64_t avgVsize = accData.avgVsizeCount ?
	accData.avgVsizeTotal / accData.avgVsizeCount : 0;
    uint64_t avgRss = accData.avgRssCount ?
	accData.avgRssTotal / accData.avgRssCount : 0;

    mdbg(PSMOM_LOG_ACC, "%s: account data for pid %d: maxVsize %zu maxRss %zu"
	 " pageSize %lu utime %lu.%06lu stime %lu.%06lu num_tasks %u"
	 " avgVsize %lu avgRss %lu minCPUtime %lu totCPUtime %lu"
	 " maxRssTotal %lu maxVsizeTotal %lu avg cpufrq %.2fG\n", __func__,
	 job->pid, accData.maxVsize, accData.maxRss, accData.pageSize,
	 accData.rusage.ru_utime.tv_sec, accData.rusage.ru_utime.tv_usec,
	 accData.rusage.ru_stime.tv_sec, accData.rusage.ru_stime.tv_usec,
	 accData.numTasks, avgVsize, avgRss,
	 accData.minCputime, accData.totCputime,
	 accData.maxRssTotal, accData.maxVsizeTotal,
	 (double) accData.cpuFreq / ((double) accData.numTasks * 1048576));

    calcJobPollCpuTime(job, accData.cstime, accData.cutime);
    addJobWaitCpuTime(job, accData.totCputime);
    setJobCpuTime(job);
    setJobMemUsage(job, accData.maxRssTotal, accData.maxVsizeTotal);
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
