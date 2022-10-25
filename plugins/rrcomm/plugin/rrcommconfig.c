/*
 * ParaStation
 *
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "rrcommconfig.h"

#include <stddef.h>

/** Description of rrcomm's configuration parameters */
static const pluginConfigDef_t confDef[] = {
    { "DebugMask", PLUGINCONFIG_VALUE_NUM, "Mask to steer debug output" },
    { NULL, PLUGINCONFIG_VALUE_NONE, NULL }
};

pluginConfig_t RRCommConfig = NULL;

void initRRCommConfig(void)
{
    pluginConfig_new(&RRCommConfig);
    pluginConfig_setDef(RRCommConfig, confDef);

    pluginConfig_load(RRCommConfig, "RRComm");
    pluginConfig_verify(RRCommConfig);
}

void finalizeRRCommConfig(void)
{
    pluginConfig_destroy(RRCommConfig);
    RRCommConfig = NULL;
}
