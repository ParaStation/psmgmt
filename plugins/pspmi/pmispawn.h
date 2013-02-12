/*
 * ParaStation
 *
 * Copyright (C) 2013 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PS_PMI_SPAWN
#define __PS_PMI_SPAWN

/**
 * @brief Prepare the pmi environment.
 *
 * @return Always returns 0.
 */
int handleForwarderSpawn();

/**
 * @brief Close the pmi forwarder socket in the client process.
 *
 * @return Always returns 0.
 */
int handleClientSpawn();

#endif
