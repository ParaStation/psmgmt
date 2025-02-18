/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSSLURM_MAIN
#define __PSSLURM_MAIN

#include <stdbool.h>
#include <sys/types.h>

/** psslurm version number */
extern int version;

/** Flag plugin's shutdown state */
extern bool pluginShutdown;

/** time plugin was loaded */
extern time_t start_time;

/** name of the user slurmctld executes as */
extern uid_t slurmUserID;

/** flag set to true if psslurm was successfully init */
extern bool isInit;

/** FPE execption mask found when plugin is loaded; to be reset upon
 * fork()ing processes */
extern int oldExceptions;

/**
 * @brief Accomplish the initialisation of psslurm
 *
 * Do further initialisation which is dependent on options specified
 * in the Slurm configuration. In config-less mode the configuration
 * needs to be fetched from the slurmctld in a request/response
 * manner. After receiving and parsing the configuration final
 * initialisation (e.g. pinning and Slurm health-check) can be done.
 *
 * @return Return on success otherwise false is returned
 */
bool accomplishInit(void);

/**
 * @brief Initialize Slurm options
 *
 * Initialize Slurm options from various configuration files. On
 * success the communication facility is started and the node is
 * registered to the slurmctld. Additionally all spank plugins are
 * initialized and the global spank API is loaded.
 *
 * @return Returns true on success or false otherwise
 */
bool initSlurmOpt(void);

#endif
