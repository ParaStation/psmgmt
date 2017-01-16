/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PSPAM__MAIN
#define __PSPAM__MAIN

/**
 * @brief Constructor for pspam library.
 *
 * @return No return value.
 */
void __attribute__ ((constructor)) startPspam(void);

/**
 * @brief Destructor for pspam library.
 *
 * @return No return value.
 */
void __attribute__ ((destructor)) stopPspam(void);

/**
 * @brief Initialize the pspam plugin.
 *
 * @return Returns 1 on error and 0 on success.
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

#endif
