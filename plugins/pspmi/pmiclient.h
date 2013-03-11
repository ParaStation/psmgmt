/*
 * ParaStation
 *
 * Copyright (C) 2007-2013 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PS_PMI_CLIENT
#define __PS_PMI_CLIENT

#define PMI_FINALIZED 55

#include "pslog.h"

/**
 * @brief Handle a new pmi message from the local mpi client.
 *
 * Handle a pmi message and call the appropriate protocol handler
 * function.
 *
 * @param msg The pmi message to handle.
 *
 * @return Returns 0 for success, 1 on error.
 */
int handlePMIclientMsg(char *msg);

/**
 * @brief Init the pmi interface.
 *
 * This must be the first call before calling any other pmi functions.
 *
 * @param pmisocket The socket witch is connect to the local mpi client.
 *
 * @param loggertaskid The task id of the logger.
 *
 * @param pRank The parastation rank of the mpi client.
 *
 * @return Returns 0 on success and 1 on errors.
 */
int pmi_init(int pmisocket, PStask_ID_t loggertaskid, int pRank);

/**
 * @brief Set basic pmi informations.
 *
 * Set needed pmi informations including predecessor, successor and
 * the pmi rank. This is information only the logger knows.
 *
 * @param data Pointer holding the neccessary information to set.
 *
 * @return Always returns 0.
 */
int setPMIclientInfo(void *data);

/**
 * @brief Handle a kvs message from logger.
 *
 * @param vmsg The pslog kvs message to handle.
 *
 * @return Always returns 0.
 */
int handleKVSMessage(void *vmsg);

/**
 * @brief Send finalize ack to the mpi client.
 * Finalize is called by the forwarder if the deamon has released
 * the mpi client. This message allows the mpi client to exit.
 *
 * @return No return value.
 */
void pmi_finalize(void);

#endif
