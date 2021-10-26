/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
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

/** flag set to true if psslurm was successfully init */
extern bool isInit;

/**
 * @brief Finalize the initialisation of psslurm
 *
 * @doctodo briefly describe what's done inside
 *
 * @return Return on success otherwise false is returned
 */
bool finalizeInit(void);

#endif
