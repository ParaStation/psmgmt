/*
 * ParaStation
 *
 * Copyright (C) 2018-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSGW_RESOURCE
#define __PSGW_RESOURCE

#include <stdbool.h>

#include "peloguetypes.h"
#include "psgwrequest.h"

/**
 * @brief Handle pelogue resource hook
 *
 * Handle PSIDHOOK_PELOGUE_RES hook called by the pelogue plugin.
 * The given environment is used to identify if additional gateway
 * resources should be requested. If additional resources are requested
 * the callback @ref PElogueResourceCb_t will be invoked if they become ready.
 *
 * @param data Pointer to a pelogue resource structure
 *
 * @return Returns 0 if no additional resources were requested otherwise 1
 * is returned. On errors 2 is returned.
 */
int handlePElogueRes(void *data);

/**
 * @brief Finalize the allocation
 *
 * The allocation was revoked by the slurmctld and psslurm called
 * the hook PSIDHOOK_PSSLURM_FINALLOC. Stop the psgwd on the gateway nodes and
 * delete request.
 *
 * @param data Pointer to the psslurm allocation structure
 *
 * @return Always return 1
 */
int handleFinAlloc(void *data);

/**
 * @brief Start prologue/epilogue executed on the gateway nodes
 *
 * @param req The request management structure
 *
 * @return Returns true on success otherwise false is returned
 */
bool startPElogue(PSGW_Req_t *req, PElogueType_t type);

/**
 * @brief Start the psgwd on the gateway nodes
 *
 * @param req The request management structure
 *
 * @return Returns true on success otherwise false is returned
 */
bool startPSGWD(PSGW_Req_t *req);

/**
 * @brief Cancel a psgw request
 *
 * Kill all started psgwd processes and prologue scripts. Stop all
 * associated timer and start the psgw_error script on the head
 * node. The psgw_error script will usually be used to re-queue the job.
 *
 * Use the pelogue callback to terminate the waiting slurmctld prologue.
 *
 * @param req The request to cancel
 *
 * @param reason The reason why the request is canceled
 */
void __cancelReq(PSGW_Req_t *req, char *reason, const char *func);

#define cancelReq(req, reason) __cancelReq(req, reason, __func__)

/**
 * @brief Write a message to an error file
 *
 * To inform the user about a startup error a error file
 * is written to the jobs submit directory.
 *
 * @param req The request to write the error file for
 *
 * @param header Preceed msg with error header
 *
 * @param file File to write, if null default job error file
 * is used
 *
 * @param msg The message to write
 */
void writeErrorFile(PSGW_Req_t *req, char *msg, char *file, bool header);

#endif
