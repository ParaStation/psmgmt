/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PS_PMI_MAIN
#define __PS_PMI_MAIN

/**
 * @brief Constructor for pspmi plugin.
 *
 * @return No return value;
 */
void __attribute__ ((constructor)) startPMI(void);

/**
 * @brief Destructor for the pspmi plugin.
 *
 * @return No return value;
 */
void __attribute__ ((destructor)) stopPMI(void);

/**
 * @brief Initialize the pspmi plugin.
 *
 * @return Returns 1 on error and 0 on success.
 */
int initialize(void);

void PluginInitialize();
void PluginFinalize();

#endif
