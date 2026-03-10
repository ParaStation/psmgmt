/*
 * ParaStation
 *
 * Copyright (C) 2026 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "qproxyconfig.h"

#include <stddef.h>

/** Description of qproxy's configuration parameters */
static const pluginConfigDef_t confDef[] = {
    { "SchedulerURL", PLUGINCONFIG_VALUE_STR, "URL to reach the PSQSched" },
    { "DebugMask", PLUGINCONFIG_VALUE_NUM, "Mask to steer debug output" },
    { NULL, PLUGINCONFIG_VALUE_NONE, NULL }
};

pluginConfig_t QProxyConfig = NULL;

void initQProxyConfig(void)
{
    pluginConfig_new(&QProxyConfig);
    pluginConfig_setDef(QProxyConfig, confDef);

    pluginConfig_load(QProxyConfig, "QProxy");
    pluginConfig_verify(QProxyConfig);
}

void finalizeQProxyConfig(void)
{
    pluginConfig_destroy(QProxyConfig);
    QProxyConfig = NULL;
}
