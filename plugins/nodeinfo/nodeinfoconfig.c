/*
 * ParaStation
 *
 * Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "nodeinfoconfig.h"

const ConfDef_t confDef[] =
{
    { "DEBUG_MASK", true, "mask", "0", "Mask to steer debug output" },
    { NULL, false, NULL, NULL, NULL},
};

Config_t nodeInfoConfig;

void initNodeInfoConfig(void)
{
    initConfig(&nodeInfoConfig);
    setConfigDefaults(&nodeInfoConfig, confDef);
}
