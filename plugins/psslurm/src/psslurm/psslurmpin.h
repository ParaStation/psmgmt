/*
 * ParaStation
 *
 * Copyright (C) 2014 - 2015 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PS_SLURM_PIN
#define __PS_SLURM_PIN

int setHWthreads(Step_t *step);

void verboseCpuPinningOutput(Step_t *step, PS_Tasks_t *task);

void verboseMemPinningOutput(Step_t *step, PStask_t *task);

void doMemBind(Step_t *step, PStask_t *task);

char *genCPUbindString(Step_t *step);

char *genMemBindString(Step_t *step);

#endif
