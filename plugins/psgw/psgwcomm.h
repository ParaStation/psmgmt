/*
 * ParaStation
 *
 * Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSGW_COMM
#define __PSGW_COMM

#include <stdbool.h>

#include "psprotocol.h"

int handlePelogueOE(void *data);

/**
 * @brief Handle a PSP_PLUG_PSGW message
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
bool handlePSGWmsg(DDTypedBufferMsg_t *msg);

#endif /* __PSGW_COMM */
