/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_NODEINFOTYPE
#define __PS_SLURM_NODEINFOTYPE

#include <stdint.h>

#include "psnodes.h"
#include "pscpu.h"

typedef struct {
    PSnodes_ID_t id;           /**< parastation node id */
    uint16_t socketCount;      /**< number of sockets */
    uint16_t coresPerSocket;   /**< number of cores per socket */
    uint16_t threadsPerCore;   /**< number of hardware threads per core */
    uint32_t coreCount;        /**< number of cores */
    uint32_t threadCount;      /**< number of hardware threads */
    PSCPU_set_t stepHWthreads; /**< hw threads to use in step on this node */
    PSCPU_set_t jobHWthreads;  /**< hw threads to use in job on this node */
} nodeinfo_t;

#endif  /* __PS_SLURM_NODEINFOTYPE */
/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
