/*
 * ParaStation
 *
 * Copyright (C) 2014-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_MULTI_PROG
#define __PS_SLURM_MULTI_PROG

#include "psstrv.h"
#include "pluginforwarder.h"

#include "psslurmstep.h"

void setupArgsFromMultiProg(Step_t *step, Forwarder_Data_t *fwdata, strv_t argV);

#endif  /* __PS_SLURM_MULTI_PROG */
