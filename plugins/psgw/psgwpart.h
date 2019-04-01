/*
 * ParaStation
 *
 * Copyright (C) 2018-2019 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PSGW_PART
#define __PSGW_PART

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
 * @brief Register PSP_DD_PROVIDEPART and PSP_DD_PROVIDEPARTSL
 */
void regPartMsg(void);

#endif
