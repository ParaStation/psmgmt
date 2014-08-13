/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PELOGUE__MAIN
#define __PELOGUE__MAIN

#include "psidcomm.h"

/* pelogue version number */
extern int version;

extern handlerFunc_t oldSpawnReqHandler;

/**
 * @brief Constructor for pelogue library.
 *
 * @return No return value.
 */
void __attribute__ ((constructor)) startPelogue();

/**
 * @brief Destructor for pelogue library.
 *
 * @return No return value.
 */
void __attribute__ ((destructor)) stopPelogue();

/**
 * @brief Initialize the pelogue plugin.
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

/** set to the home directory of root */
extern char rootHome[100];

#endif
