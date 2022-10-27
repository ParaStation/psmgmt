/*
 * ParaStation
 *
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __RRCOMM_FORWARDER_H
#define __RRCOMM_FORWARDER_H

#include <stdbool.h>

/**
 * @brief Register all hooks of rrcomm plugin's psidforwarder part
 *
 * @return If all hooks were registered successfully true is returned;
 * or false in case of error
 */
bool attachRRCommForwarderHooks(void);

/**
 * @brief Unregister all hooks of rrcomm plugin's psidforwarder part
 *
 * @param verbose Flag display of error message when unregistering a
 * hook fails
 *
 * @return No return value
 */
void detachRRCommForwarderHooks(bool verbose);

#endif  /* __RRCOMM_FORWARDER_H */
