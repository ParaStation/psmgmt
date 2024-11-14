/*
 * ParaStation
 *
 * Copyright (C) 2018-2019 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSGW_PART
#define __PSGW_PART

#include <stdbool.h>

#include "psgwrequest.h"

/**
 * @brief Request gateway nodes
 *
 * Request gateway nodes from the master psid. In the current
 * psid resource management implemenation a task structure is required.
 * Therefore a psgw forwarder is started since in this early stage
 * no other forwarders are present.
 *
 * @param req Request management structure
 *
 * @param numNodes Number of gateway nodes to request
 *
 * @return Returns true on success otherwise false is returned
 */
bool requestGWnodes(PSGW_Req_t *req, int numNodes);

/**
 * @brief Receive a partition
 *
 * The master daemon provided a requested partition that was attached
 * to the requesting task. psid called PSIDHOOK_RECEIVEPART to allow
 * psgw to take further actions and to suppress the sending of answer
 * messages to the assumed initiator.
 *
 * @param data Pointer to the task structure assumed to have initiate
 * to partition request
 *
 * @return Return 0 if the partition was requested by psgw in order to
 * suppress further actions in the daemon or 1 otherwise
 */
int handleReceivePart(void *data);

#endif
