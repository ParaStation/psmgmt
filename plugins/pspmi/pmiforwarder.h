/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_PMI_FORWARDER
#define __PS_PMI_FORWARDER

#include "pmitypes.h"

/**
 * @brief Set PMI connection information
 *
 * @param type PMI connection type
 *
 * @param sock Socket to use for PMI communication
 *
 * @return No return value
 */
void setConnectionInfo(PMItype_t type, int sock);

/**
 * @brief Initialize the forwarder module
 *
 * Initialize the forwarder module of the pspmi plugin. This will
 * register various hooks called within the psidforwarder process.
 *
 * @return No return value
 */
void initForwarder(void);

/**
 * @brief Finalize the forwarder module
 *
 * Finalize the forwarder module within the pspmi plugin. This will
 * cleanup various hooks registered in @ref initForwarder().
 *
 * @return No return value
 */
void finalizeForwarder(void);


#endif  /* __PS_PMI_FORWARDER */
