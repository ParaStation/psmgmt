/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Handle option requests to the ParaStation daemon
 */
#ifndef __PSIDOPTIONS_H
#define __PSIDOPTIONS_H

#include "psnodes.h"

/**
 * @brief Initialize option stuff
 *
 * Initialize the options framework. This registers the necessary
 * message handlers.
 *
 * @return No return value
 */
void initOptions(void);

/**
 * @brief Send some options.
 *
 * Send some options upon startup of a daemon-daemon connection to @a
 * destnode's daemon.
 *
 * @param destnode Destionation node the options shall be send to
 *
 * @return No return value
 */
void send_OPTIONS(PSnodes_ID_t destnode);

#endif  /* __PSIDOPTIONS_H */
