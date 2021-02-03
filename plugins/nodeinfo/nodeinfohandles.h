/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __NODEINFO_HANDLES_H
#define __NODEINFO_HANDLES_H

#include "nodeinfotypes.h"

/*
 * This file contains definitions of function pointer for each of the
 * functions the nodeinfo plugin exports to foreign plugins. In order
 * to initialize those handles used within a foreign module, a
 * corresponding call to @ref dlsym() must be executed there.
 */

/* For documentation of the specific funtions refer to nodeinfotypes.h */

reinitNodeInfo_t *reinitNodeInfo;

#endif  /* __NODEINFO_HANDLES_H */
