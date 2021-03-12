/*
 * ParaStation
 *
 * Copyright (C) 2010-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_MOM_SCRIPT
#define __PS_MOM_SCRIPT

#include "psmomjob.h"
#include "psmomspawn.h"
#include "psmompscomm.h"
#include "psidscripts.h"

/**
 * @brief Handle a PElogue start msg.
 *
 * This message can be from the local psmom (myself) or from a psmom on another
 * host.
 *
 * If we run PElogue scripts which is depending on the local configuration
 * then we create a data structure which holds all information needed for
 * the PElogue scripts to start. If not we just reply that the PElogue
 * scripts was running successfully.
 *
 * @param msg The last message received
 *
 * @param rData Full data buffer received
 *
 * @return No return value.
 */
void handlePELogueStart(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *rData);

/**
 * @brief Handle finish messages from PElogue runs.
 *
 * @param msg The last message received
 *
 * @param rData Full data buffer received
 *
 * @return No return value.
 */
void handlePELogueFinish(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *rData);

/**
 * @brief Send a signal to a running PElogue script.
 *
 * @return No return value.
 */
void handlePELogueSignal(DDTypedBufferMsg_t *msg);

/**
 * @brief Send a signal to all hosts running PElogue scripts
 *  for a selected job.
 *
 * @param job The job where the prologue/epilogue belongs to.
 *
 * @param signal The signal to send.
 *
 * @param reason The reason for sending the signal.
 *
 * @return No return value.
 */
void signalPElogue(Job_t *job, char *signal, char *reason);

/**
 * @brief Verify correct permissions of pelogue scripts.
 */
int checkPELogueFileStats(char *filename, int root);

int handleNodeDown(void *nodeID);

/**
 * @brief Monitor the PELogue timeout on the mother mom.
 *
 * @param job The job structure for the PELogue script.
 *
 * @return No return value.
 */
void monitorPELogueTimeout(Job_t *job);

/**
 * @brief Stop timeout monitoring of the PElogue scripts.
 *
 * @param job The job structure for the PELogue script.
 *
 * @return No return value.
 */
void removePELogueTimeout(Job_t *job);

/**
 * @brief Stop a PElogue script and obit the job.
 *
 * @param job The job structure for the PELogue script to stop.
 *
 * @return No return value.
 */
void stopPElogueExecution(Job_t *job);

#endif
