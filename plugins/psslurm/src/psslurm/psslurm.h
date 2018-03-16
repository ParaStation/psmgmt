/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSSLURM_MAIN
#define __PSSLURM_MAIN

#include "psidcomm.h"

/** psslurm version number */
extern int version;

/** Flag plugin's shutdown state */
extern bool pluginShutdown;

/** time plugin was loaded */
extern time_t start_time;

/** name of the user slurmctld executes as */
extern uid_t slurmUserID;

/** default account poll time */
extern int confAccPollTime;

/** hash value of the SLURM config file */
extern uint32_t configHash;

/** node ID of the main slurmctld controller */
extern PSnodes_ID_t slurmController;

/** node ID of the backup slurmctld controller */
extern PSnodes_ID_t slurmBackupController;

#endif
