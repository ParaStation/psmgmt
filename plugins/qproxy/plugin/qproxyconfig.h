/*
 * ParaStation
 *
 * Copyright (C) 2026 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __QPROXY_CONFIG_H
#define __QPROXY_CONFIG_H

#include "pluginpsconfig.h"

/** qproxy's configuration */
extern pluginConfig_t QProxyConfig;

/**
 * @brief Initialize configuration
 *
 * Initialize the configuration from psconfig's Psid.PluginCfg.QProxy
 * branch and save the result into @ref QProxyConfig.
 *
 * @return No return value
 */
void initQProxyConfig(void);

/**
 * @brief Finalize configuration
 *
 * Finalize the configuration and free all associated dynamic memory.
 *
 * @return No return value
 */
void finalizeQProxyConfig(void);

#endif  /* __QPROXY_CONFIG_H */
