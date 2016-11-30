/*
 * ParaStation
 *
 * Copyright (C) 2011-2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "peloguekvs.h"

uint32_t stat_lProlog = 0;
uint32_t stat_rProlog = 0;
uint32_t stat_failedrProlog = 0;
uint32_t stat_failedlProlog = 0;
