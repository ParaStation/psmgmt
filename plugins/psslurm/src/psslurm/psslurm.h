/*
 * ParaStation
 *
 * Copyright (C) 2014-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSSLURM_MAIN
#define __PSSLURM_MAIN

#include "psidcomm.h"

/* psslurm version number */
extern int version;

/** Flag plugin's shutdown state */
extern bool pluginShutdown;

extern time_t start_time;

extern uid_t slurmUserID;

extern int confAccPollTime;
extern uint32_t configHash;

extern PSnodes_ID_t slurmController;
extern PSnodes_ID_t slurmBackupController;

#endif
