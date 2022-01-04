/*
 * ParaStation
 *
 * Copyright (C) 2020-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "nodeinfoconfig.h"

#include <stddef.h>

/** Description of nodeinfo's configuration parameters */
static const pluginConfigDef_t confDef[] = {
    { "DebugMask", PLUGINCONFIG_VALUE_NUM, "Mask to steer debug output" },
    { "GPUDevices", PLUGINCONFIG_VALUE_LST, "PCIe IDs of NICs"
      " (list of \"vendorID:deviceID[:subVendorID:subDeviceID]\"" },
    { "GPUSort", PLUGINCONFIG_VALUE_STR,
      "GPUs' sort order (\"BIOS\"|\"PCI\")" },
    { "NICDevices", PLUGINCONFIG_VALUE_LST, "PCIe IDs of NICs"
      " (list of \"vendorID:deviceID[:subVendorID:subDeviceID]\"" },
    { "NICSort", PLUGINCONFIG_VALUE_STR,
      "NICs' sort order (\"BIOS\"|\"PCI\")" },
    { NULL, PLUGINCONFIG_VALUE_NONE, NULL }
};

pluginConfig_t nodeInfoConfig = NULL;

void initNodeInfoConfig(void)
{
    pluginConfig_new(&nodeInfoConfig);
    pluginConfig_setDef(nodeInfoConfig, confDef);

    pluginConfig_load(nodeInfoConfig, "NodeInfo");
    pluginConfig_verify(nodeInfoConfig);
}

void finalizeNodeInfoConfig(void)
{
    pluginConfig_destroy(nodeInfoConfig);
    nodeInfoConfig = NULL;
}
