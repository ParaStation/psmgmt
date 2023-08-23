/*
 * ParaStation
 *
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_NODEINFO
#define __PS_SLURM_NODEINFO

#include "psnodes.h"

#include "psslurmnodeinfotype.h"  // IWYU pragma: export
#include "psslurmjob.h"
#include "psslurmstep.h"

/**
 * @brief Get all basic hardware information of a node in a job
 *
 * Get a nodeinfo struct containing information on a node that are
 * usually hidden in the job credential.
 *
 * The returned nodeinfo struct needs to be freed using ufree.
 *
 * @param id	    ParaStation ID of the requested node
 * @param cred	    Slurm job credential holding HW cores
 * @param allocID   corresponding Allocation ID
 */
nodeinfo_t *getNodeinfo(PSnodes_ID_t id, const JobCred_t *cred,
			uint32_t allocID);

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
