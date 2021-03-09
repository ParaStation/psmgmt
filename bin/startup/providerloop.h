/*
 * ParaStation
 *
 * Copyright (C) 2013-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file ParaStation global key value space -- provider part
 */
#ifndef __STARTUP_PROVIDERLOOP_H
#define __STARTUP_PROVIDERLOOP_H

#include <stdbool.h>

/**
 * @brief KVS provider's central loop
 *
 * Central loop of the KVS provider. After initialization and setting
 * up various message handlers it loops over Swait() in order to
 * handle all incoming messages.
 *
 * @param verbose Be more verbose while doing the work
 *
 * @return No return value
 */
__attribute__ ((noreturn))
void kvsProviderLoop(bool verbose);

#endif  /* __STARTUP_PROVIDERLOOP_H */
