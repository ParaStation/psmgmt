/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PLUGIN_LIB_PARTITION
#define __PLUGIN_LIB_PARTITION

#include "pstask.h"
#include "psprotocol.h"

void rejectPartitionRequest(PStask_ID_t dest);
int isPSAdminUser(uid_t uid, gid_t gid);
int injectNodelist(DDBufferMsg_t *inmsg, int32_t nrOfNodes, PSnodes_ID_t *nodes);
void grantPartitionRequest(PSpart_HWThread_t *hwThreads, uint32_t numHWthreads,
				PStask_ID_t dest, PStask_t *task);

#endif
