/*
 * ParaStation
 *
 * Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSPAM_COMM
#define __PSPAM_COMM

#include <stdbool.h>

/**
 * @brief Initialize communication layer
 *
 * Initialize the plugin's communication layer. This will mainly
 * setup the master socket the plugin is listening to.
 *
 * @return On success true is returned. Or false in case of an error
 */
bool initComm(void);

/**
 * @brief Finalize communication layer
 *
 * Finalize the plugin's communication layer. This will cleanup the
 * master socket setup by @ref initComm().
 *
 * @return No return value
 */
void finalizeComm(void);

#endif /* __PSPAM_COMM */
