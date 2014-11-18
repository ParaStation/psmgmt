/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
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

uint8_t *getCPUsForPartition(PSpart_slot_t *slots, Step_t *step);

void setCPUset(uint16_t cpuBindType, PSCPU_set_t *CPUset, uint32_t coreMapIndex,
		uint32_t cpuCount, int32_t *lastCpu, uint8_t *coreMap,
		uint32_t nodeid, int *thread, int hwThreads,
		uint32_t tasksPerNode);
#endif
