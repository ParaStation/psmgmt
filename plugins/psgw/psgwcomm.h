/*
 * ParaStation
 *
 * Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PSGW_COMM
#define __PSGW_COMM

int handlePelogueOE(void *data);

/**
* @brief Handle a PSP_CC_PLUG_PSSLURM message
*
* @param msg The message to handle
*/
void handlePSGWmsg(DDTypedBufferMsg_t *msg);

#endif /* __PSGW_COMM */
