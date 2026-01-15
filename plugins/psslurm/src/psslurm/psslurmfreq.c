/*
 * ParaStation
 *
 * Copyright (C) 2025-2026 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmfreq.h"

#include <stdlib.h>

#include "plugingpufreq.h"
#include "pluginmalloc.h"

#include "psslurmlog.h"
#include "psslurmgres.h"
#include "psslurmenv.h"
#include "psslurmcomm.h"
#include "psslurmconfig.h"

/**
 * @brief Convert Slurm GPU frequency string
 *
 * @param freqStr Slurm frequency string to convert
 *
 * @param result Holding the converted frequency
 *
 * @return Returns true on success otherwise false is returned
 */
static bool convFreqStr(char *freqStr, uint32_t *result)
{
    if (!freqStr) return false;

    char *endptr;
    *result = strtoul(freqStr, &endptr, 10);

    /* all character in string consumed */
    if (freqStr[0] == '\0' || *endptr == '\0') return true;

    if (!strcmp(freqStr, "low")) {
	*result = GPU_FREQ_LOW;
	return true;
    } else if (!strcmp(freqStr, "medium")) {
	*result = GPU_FREQ_MEDIUM;
	return true;
    } else if (!strcmp(freqStr, "high")) {
	*result = GPU_FREQ_HIGH;
	return true;
    } else if (!strcmp(freqStr, "highm1")) {
	*result = GPU_FREQ_SEC_HIGH;
	return true;
    }

    flog("invalid frequency string %s\n", freqStr);
    return false;
}

bool Freq_gpuStr2Freq(char *freqStr, uint32_t *graFreq, uint32_t *memFreq)
{
    *graFreq = *memFreq = 0;
    char *toksave;
    const char delimiters[] =",";

    char *dup = ustrdup(freqStr);
    bool res = true;
    char *next = strtok_r(dup, delimiters, &toksave);
    while (next) {
	if (!strncasecmp(next, "memory=", 7)) {
	    if (!convFreqStr(next + 7, memFreq)) {
		flog("failed to extract memory frequency\n");
		res = false;
		break;
	    }
	} else if (!strcasecmp(next, "verbose")) {
	    /* TODO implement verbose option */
	} else if (!convFreqStr(next, graFreq)) {
		flog("failed to extract graphics frequency\n");
		res = false;
		break;
	}
	next = strtok_r(NULL, delimiters, &toksave);
    }

    ufree(dup);
    return res;
}

/**
 * @brief Change frequency of assigned GPUs
 *
 * @param assGPUs Assigned GPUs to operate on
 *
 * @param freqStr Slurm frequency string
 *
 * @return Returns true on success otherwise false is returned
 */
static bool setGpuFreq(PSCPU_set_t assGPUs, char *freqStr)
{
    if (!freqStr) return true;

    char *gpuFreq = strstr(freqStr, "gpu:");
    if (!gpuFreq) return true;

    char *end = strchr(gpuFreq, ';');
    if (end) end[0] = '\0';

    char *dup = ustrdup(gpuFreq + 4);
    if (end) end[0] = ';';

    uint32_t graFreq, memFreq;
    if (!Freq_gpuStr2Freq(dup, &graFreq, &memFreq)) {
	flog("unable to parse GPU frequency string\n");
	ufree(dup);
	return false;
    }
    ufree(dup);

    fdbg(PSSLURM_LOG_FREQ, "GPUs %s graphics %u memory %u\n",
	 PSCPU_print_part(assGPUs, PSCPU_MAX), graFreq, memFreq);

    bool res = true;
    if (graFreq && !GPUfreq_setGraFreq(assGPUs, PSCPU_MAX, graFreq)) {
	flog("failed setting graphics frequency %u for GPUs %s\n", graFreq,
		PSCPU_print_part(assGPUs, PSCPU_MAX));
	res = false;
    }
    if (memFreq && !GPUfreq_setMemFreq(assGPUs, PSCPU_MAX, memFreq)) {
	flog("failed setting memory frequency %u for GPUs %s\n", memFreq,
		PSCPU_print_part(assGPUs, PSCPU_MAX));
	res = false;
    }

    return res;
}

/**
 * @brief Reset GPU frequencies for given GPUs
 *
 * Reset GPU frequencies to Slurm configured values or
 * let graphics driver set default.
 *
 * @param assGPUs Assigned GPUs to operate on
 *
 * @return Returns true on success otherwise false is returned
 */
static bool doResetFreq(PSCPU_set_t *assGPUs)
{
    char *freqDef = getConfValueC(SlurmConfig, "GpuFreqDef");
    if (freqDef && *freqDef) {
	uint32_t graFreq, memFreq;
	if (!Freq_gpuStr2Freq(freqDef, &graFreq, &memFreq)) {
	    flog("invalid GpuFreqDef=%s in slurm.conf\n", freqDef);
	} else {
	    bool res = true;
	    if (!GPUfreq_setGraFreq(*assGPUs, PSCPU_MAX, graFreq)) res = false;
	    if (!GPUfreq_setMemFreq(*assGPUs, PSCPU_MAX, memFreq)) res = false;
	    return res;
	}
    }

    return GPUfreq_resetFreq(*assGPUs, GPU_FREQ_TYPE_GRA | GPU_FREQ_TYPE_MEM);
}

/**
 * @brief Extract asssigned GPUs from a step
 *
 * @param step Step to extract GPUs from
 *
 * @param assGPUs Holding the result
 *
 * @return Returns true on success otherwise false is returned
 */
static bool getStepGPUs(Step_t *step, PSCPU_set_t *assGPUs)
{
    PSCPU_clrAll(*assGPUs);

    /* no frequency change requested */
    if (!step->tresFreq || !*step->tresFreq) return true;

    Gres_Cred_t *gres;
    gres = findGresCred(&step->gresList, GRES_PLUGIN_GPU, GRES_CRED_STEP);

    /* no GPUs assigned */
    if (!gres || !gres->bitAlloc || !gres->bitAlloc[0]) {
	fdbg(PSSLURM_LOG_FREQ, "warning: step has no GPUs, but requested "
	     "frequency '%s'\n", step->tresFreq);
	return true;
    }

    if (!hexBitstr2Set(gres->bitAlloc[0], *assGPUs)) {
	flog("failed to get assigned GPUs from bitstring\n");
	return false;
    }

    return true;
}

/**
 * @brief Extract asssigned GPUs from a job
 *
 * @param job Job to extract GPUs from
 *
 * @param assGPUs Holding the result
 *
 * @return Returns true on success otherwise false is returned
 */
static bool getJobGPUs(Job_t *job, PSCPU_set_t *assGPUs)
{
    PSCPU_clrAll(*assGPUs);

    /* no frequency change requested */
    if (!job->tresFreq || !*job->tresFreq) return true;

    Gres_Cred_t *gres;
    gres = findGresCred(&job->gresList, GRES_PLUGIN_GPU, GRES_CRED_JOB);

    /* no GPUs assigned */
    if (!gres || !gres->bitAlloc || !gres->bitAlloc[job->localNodeId]) {
	fdbg(PSSLURM_LOG_FREQ, "warning: job has no GPUs, but requested "
	     "frequency '%s'\n", job->tresFreq);
	return true;
    }

    if (!hexBitstr2Set(gres->bitAlloc[job->localNodeId], *assGPUs)) {
	flog("failed to get assigned GPUs from bitstring\n");
	return false;
    }

    return true;
}

bool Freq_adjustStepGPUs(Step_t *step)
{
    PSCPU_set_t assGPUs;
    if (!getStepGPUs(step, &assGPUs)) return false;
    if (!PSCPU_any(assGPUs, PSCPU_MAX)) return true;

    return setGpuFreq(assGPUs, step->tresFreq);
}

bool Freq_adjustJobGPUs(Job_t *job)
{
    PSCPU_set_t assGPUs;
    if (!getJobGPUs(job, &assGPUs)) return false;
    if (!PSCPU_any(assGPUs, PSCPU_MAX)) return true;

    return setGpuFreq(assGPUs, job->tresFreq);
}

bool Freq_resetJob(Job_t *job)
{
    PSCPU_set_t assGPUs;
    if (!getJobGPUs(job, &assGPUs)) return false;
    if (!PSCPU_any(assGPUs, PSCPU_MAX)) return true;

    return doResetFreq(&assGPUs);
}

bool Freq_resetStep(Step_t *step)
{
    PSCPU_set_t assGPUs;
    if (!getStepGPUs(step, &assGPUs)) return false;
    if (!PSCPU_any(assGPUs, PSCPU_MAX)) return true;

    return doResetFreq(&assGPUs);
}
