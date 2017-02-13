/*
 * ParaStation
 *
 * Copyright (C) 2015-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Stephan Krempel <krempel@par-tec.com>
 *
 */

#ifndef __PS_SLURM_SPAWN
#define __PS_SLURM_SPAWN

#include "pstask.h"
#include "pmiclientspawn.h"

void initSpawnFacility(Step_t *jobstep);

int fillSpawnTaskWithSrun(SpawnRequest_t *req, int usize, PStask_t *task);

#endif
