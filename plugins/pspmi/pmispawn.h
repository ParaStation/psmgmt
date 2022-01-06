/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * This part actually lives within the daemon and prepares the field
 * for the forwarder where most of the work happens.
 */
#ifndef __PS_PMI_SPAWN
#define __PS_PMI_SPAWN

/**
 * @brief Initialize the spawn module
 *
 * Initialize the spawn module of the pspmi plugin.
 *
 * @return No return value
 */
void initSpawn(void);

/**
 * @brief Finalize the spawn module
 *
 * Finalize the spawn module the pspmi plugin.
 *
 * @return No return value
 */
void finalizeSpawn(void);

#endif  /* __PS_PMI_SPAWN */
