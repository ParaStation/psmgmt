/*
 * ParaStation
 *
 * Copyright (C) 2010-2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PS_ACCOUNT_MAIN
#define __PS_ACCOUNT_MAIN

#include "psidcomm.h"

/**
 * @brief Constructor for the psaccount library.
 *
 */
void __attribute__ ((constructor)) accountStart();

/**
 * @brief Destructor for the psaccount library.
 *
 */
void __attribute__ ((destructor)) accountStop();

/**
 * @brief Main loop to do all the work.
 *
 * @return No return value.
 */
void periodicMain(void);

/**
 * @brief Initialize the psaccount plugin.
 *
 * @return No return value.
 */
int initialize(void);

/**
 * @brief Free left memory, final cleanup.
 *
 * After this function we will be unloaded.
 *
 * @return No return value.
 */
void cleanup(void);

extern handlerFunc_t oldAccountHanlder;
extern int clockTicks;
extern int pageSize;

#endif
