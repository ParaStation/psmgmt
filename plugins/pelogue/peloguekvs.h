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

#include <stdint.h>

#ifndef __PS_PELOGUE_KVS
#define __PS_PELOGUE_KVS

/* count successful local prologue executions */
extern uint32_t stat_lProlog;

/* count successful remote prologue executions */
extern uint32_t stat_rProlog;

/* count failed local prologue executions */
extern uint32_t stat_failedlProlog;

/* count failed remote prologue executions */
extern uint32_t stat_failedrProlog;

#endif
