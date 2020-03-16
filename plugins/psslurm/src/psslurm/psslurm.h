/*
 * ParaStation
 *
 * Copyright (C) 2014-2020 ParTec Cluster Competence Center GmbH, Munich
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

/** flag set to true if psslurm was successfully init */
extern bool isInit;

/**
 * @brief Initialize Slurm options
 *
 * Initialize Slurm options from various configuration files. On
 * success the communication facility is started and the node
 * is registered to the slurmctld. Additional.y all spank are
 * initialized and the global spank API is loaded.
 *
 * @return Returns true on success or false otherwise
 */
bool initSlurmOpt(void);

#endif
