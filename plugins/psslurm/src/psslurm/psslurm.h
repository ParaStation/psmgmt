/*
 * ParaStation
 *
 * Copyright (C) 2014 - 2015 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PSSLURM_MAIN
#define __PSSLURM_MAIN

#include "psidcomm.h"

/* psslurm version number */
extern int version;

extern time_t start_time;

extern uid_t slurmUserID;

extern handlerFunc_t oldChildBornHandler;
extern handlerFunc_t oldCCMsgHandler;
extern handlerFunc_t oldCCErrorHandler;

/**
 * @brief Constructor for psslurm library.
 *
 * @return No return value.
 */
void __attribute__ ((constructor)) startPsslurm();

/**
 * @brief Destructor for psslurm library.
 *
 * @return No return value.
 */
void __attribute__ ((destructor)) stopPsslurm();

/**
 * @brief Initialize the psslurm plugin.
 *
 * @return Returns 1 on error and 0 on success.
 */
int initialize(void);

/**
 * @brief Prepare and beginn shutdown.
 *
 * @return No return value.
 */
void finalize(void);

/**
 * @brief Free left memory, final cleanup.
 *
 * After this function we will be unloaded.
 *
 * @return No return value.
 */
void cleanup(void);

#endif
