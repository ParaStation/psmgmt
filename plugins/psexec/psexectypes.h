/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PSEXEC__TYPES
#define __PSEXEC__TYPES

#include <stdint.h>
#include "psnodes.h"

/**
 * @doctodo
 */
typedef int psExec_Script_CB_t(uint32_t, int32_t, PSnodes_ID_t, uint16_t);

#define PSEXEC_CONT 2

#endif
