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

#ifndef __PS_PMI_SPAWN
#define __PS_PMI_SPAWN

/**
 * @brief Prepare the PMI environment.
 *
 * @param data Pointer to the PStask structure of the client.
 *
 * @return Always returns 0.
 */
int handleForwarderSpawn(void *data);

/**
 * @brief Close the PMI forwarder socket in the client process.
 *
 * @return Always returns 0.
 */
int handleClientSpawn(void *data);

#endif
