/*
 * ParaStation
 *
 * Copyright (C) 2014-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * ParaStation client-daemon environment consent.
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSPROTOCOLENV_H
#define __PSPROTOCOLENV_H

#include <stddef.h>
#include <sys/resource.h>

/**
 * A list of resource limits to be forwarded during PSI_spawn() and friends
 */
static struct {
    int resource;    /**< resource limit as used by get/setrlimit(2) */
    char *envName;   /**< environment used to push information */
} PSP_rlimitEnv[] = {
    { RLIMIT_CORE    , "__PSI_CORESIZE" },
    { RLIMIT_DATA    , "__PSI_DATASIZE" },
    { RLIMIT_AS      , "__PSI_ASSIZE" },
    { RLIMIT_NOFILE  , "__PSI_NOFILE" },
    { RLIMIT_STACK   , "__PSI_STACKSIZE" },
    { RLIMIT_FSIZE   , "__PSI_FSIZE" },
    { RLIMIT_CPU     , "__PSI_CPU" },
    { RLIMIT_NPROC   , "__PSI_NPROC" },
    { RLIMIT_RSS     , "__PSI_RSS" },
    { RLIMIT_MEMLOCK , "__PSI_MEMLOCK" },
    {0,NULL}
};

#endif /* __PSPROTOCOLENV_H */
