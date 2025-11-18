/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "nodeinfointer.h"

#include <stdbool.h>

#include "pscommon.h"
#include "psidhw.h"
#include "nodeinfo.h"

bool reinitNodeInfo(void)
{
    if (!PSIDhw_reInit()) return false;

    updateGPUInfo();
    updateNICInfo();

    sendNodeInfoData(PSC_getMyID());

    return true;
}
