/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pscommon.h"
#include "psidhw.h"
#include "nodeinfo.h"

#include "nodeinfointer.h"

bool reinitNodeInfo(void)
{
    PSIDhw_reInit();

    updateGPUInfo();
    updateNICInfo();

    sendNodeInfoData(PSC_getMyID());

    return true;
}
