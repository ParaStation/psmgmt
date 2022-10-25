/*
 * ParaStation
 *
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __RRCOMM_CONFIG_H
#define __RRCOMM_CONFIG_H

#include "pluginpsconfig.h"

/** rrcomm's configuration */
extern pluginConfig_t RRCommConfig;

/**
 * @brief Initialize configuration
 *
 * Initialize the configuration from psconfig's Psid.PluginCfg.RRComm
 * branch and save the result into @ref RRCommConfig.
 *
 * @return No return value
 */
void initRRCommConfig(void);

/**
 * @brief Finalize configuration
 *
 * Finalize the configuration and free all associated dynamic memory.
 *
 * @return No return value
 */
void finalizeRRCommConfig(void);

#endif  /* __RRCOMM_CONFIG_H */
