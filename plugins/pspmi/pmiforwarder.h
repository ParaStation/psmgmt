/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_PMI_FORWARDER
#define __PS_PMI_FORWARDER

#include "pstask.h"

#include "pmitypes.h"

/**
 * @brief Set the pmi connection information.
 *
 * @param type The pmi connection type.
 *
 * @param sock The socket to use for pmi communication.
 *
 * @return No return value.
 */
void setConnectionInfo(PMItype_t type, int sock);

/**
 * @brief Init the pmi interface.
 *
 * Init the pmi interface and start listening for new connection from
 * the mpi client.
 *
 * @param data Pointer to the task structure of the child.
 *
 * @return Returns 0 on success and -1 on error.
 */
int setupPMIsockets(void *data);

/**
 * @brief Release the mpi client.
 *
 * @param data When this flag is set to 1 pmi_finalize() will be called.
 *
 * @return Always returns 0.
 */
int releasePMIClient(void *data);

/**
 * @brief Get the pmi status.
 *
 * @param data Unsed parameter.
 *
 * @return Returns the status of the pmi connection.
 */
int getClientStatus(void *data);

/**
 * @brief Get the task structure of the PMI client.
 *
 * @return Returns a pointer to the requested task structure
 * or NULL on error.
 */
PStask_t *getChildTask(void);

#endif  /* __PS_PMI_FORWARDER */
