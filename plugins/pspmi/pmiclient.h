/*
 * ParaStation
 *
 * Copyright (C) 2007-2016 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * Core functionality of PMI. This part lives within the forwarder and
 * is responsible for handling PMI messages received from the client
 * process.
 */
#ifndef __PS_PMI_CLIENT
#define __PS_PMI_CLIENT

#include "pstask.h"

#include "pmitypes.h"

/** Magic value to indicate proper finalization of PMI */
#define PMI_FINALIZED 55

/**
 * @brief Handle a new PMI message from the local MPI client.
 *
 * Handle a PMI message and call the appropriate protocol handler
 * function.
 *
 * @param msg The PMI message to handle.
 *
 * @return Returns 0 for success, 1 on error.
 */
int handlePMIclientMsg(char *msg);

/**
 * @brief Initialize the PMI interface
 *
 * This must be the first call to the PMI module before calling any
 * other PMI functions.
 *
 * @param childTask The task structure of the forwarders child
 *
 * @return Returns 0 on success and 1 on errors
 */
int pmi_init(int pmisocket, PStask_t *childTask);

/**
 * @brief Set the KVS provider's task ID
 *
 * Pass information on the KVS provider's task ID into the client
 * module. Further requests on KVS will be passed to the KVS provider
 * identified by the task ID @a tid.
 *
 * @param tid Task ID to set
 *
 * @return No return value
 */
void setKVSProviderTID(PStask_ID_t tid);

/**
 * @brief Set the KVS provider's socket
 *
 * Pass information on the file descriptor connecting the KVS
 * provider's forwarder to the actual provider into the client
 * module. The file descriptor @a fd will be closed upon request in
 * order to stop the actual KVS provider.
 *
 * @param fd File descriptor to register
 *
 * @return No return value
 */
void setKVSProviderSock(int fd);

/**
* @brief Acknowledge PMI client's finalize message
 *
 * Finalize the PMI connection and release the PMI client
 *
 * This must be called by the forwarder as soon as it's ensured that
 * preparations to receive the PMI client's SIGCHLD signal are made.
 *
 * @return No return value
 */
void ackFinalize(void);

/**
 * @brief Tell the kvsprovider we are leaving if necessary
 *
 * @return No return value
 */
void leaveKVS(void);

psPmiSetFillSpawnTaskFunction_t psPmiSetFillSpawnTaskFunction;

psPmiResetFillSpawnTaskFunction_t psPmiResetFillSpawnTaskFunction;

/**
 * @brief Initialize the client module
 *
 * Initialize the client module of the pspmi plugin.
 *
 * @return No return value
 */
void initClient(void);

/**
 * @brief Finalize the client module
 *
 * Finalize the client module the pspmi plugin. This includes
 * free()ing all dynamic memory not used any longer.
 *
 * @return No return value
 */
void finalizeClient(void);


#endif  /* __PS_PMI_CLIENT */
