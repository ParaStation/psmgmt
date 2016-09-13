/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_ACCOUNT_COMM
#define __PS_ACCOUNT_COMM

/**
 * @brief Initialize comm layer
 *
 * Initialize the plugin's comm-layer. This will mainly register an
 * alternative handler for accounting messages of type PSP_CD_ACCOUNT.
 *
 * @Return No return value
*/
void initAccComm(void);

/**
 * @brief Finalize comm layer
 *
 * Finalize the plugin's comm-layer. This will unregister the handler
 * for accounting messages registered by @ref psAccountInitComm().
 *
 * @Return No return value
*/
void finalizeAccComm(void);

#endif  /* __PS_ACCOUNT_COMM */
