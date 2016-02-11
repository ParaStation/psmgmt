/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PSEXEC__MAIN
#define __PSEXEC__MAIN

/**
 * @brief Constructor for psexec library.
 *
 * @return No return value.
 */
void __attribute__ ((constructor)) startPsexec(void);

/**
 * @brief Destructor for psexec library.
 *
 * @return No return value.
 */
void __attribute__ ((destructor)) stopPsexec(void);

/**
 * @brief Initialize the psexec plugin.
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
