/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_NODEINFO
#define __PS_SLURM_NODEINFO

#include "psnodes.h"
#include "psslurmnodeinfotype.h"
#include "psslurmjob.h"
#include "psslurmstep.h"

/**
 * @brief Get all basic hardware information of a node in a job
 *
 * Get a nodeinfo struct containing information on a node that are
 * usually hidden in the job struct.
 *
 * The returned nodeinfo struct needs to be freed using ufree.
 *
 * @param id    ParaStation ID of the requested node
 * @param job   Job
 */
nodeinfo_t *getJobNodeinfo(PSnodes_ID_t id, const Job_t *job);

/**
 * @brief Creates a nodeinfo array for the step
 *
 * Get all basic hardware information of all nodes of step from the
 * credentials included in @a step and returns the nodeinfo array.
 *
 * The returned nodeinfo array needs to be freed using ufree.
 *
 * @param step  Step or NULL
 */
nodeinfo_t *getStepNodeinfoArray(const Step_t *step);

#endif  /* __PS_SLURM_NODEINFO */
/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
