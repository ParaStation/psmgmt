/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2023-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Handle info requests to the ParaStation daemon
 */
#ifndef __PSIDINFO_H
#define __PSIDINFO_H

#include <stdbool.h>

/**
 * @brief Initialize info stuff
 *
 * Initialize the info request framework. This registers the necessary
 * message handlers.
 *
 * @return Return true on successful initialization or false on failure
 */
bool PSIDinfo_init(void);

#endif  /* __PSIDINFO_H */
