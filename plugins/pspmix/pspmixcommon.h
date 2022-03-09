/*
 * ParaStation
 *
 * Copyright (C) 2019-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Definitions of small common pspmix helper functions
 */
#ifndef __PS_PMIX_COMMON
#define __PS_PMIX_COMMON

#include <stdbool.h>

#include "pstask.h"

/* decide if this job wants to use PMIx */
#define pspmix_common_usePMIx(t) __pspmix_common_usePMIx(t, __func__)
bool __pspmix_common_usePMIx(PStask_t *task, const char* func);

#endif  /* __PS_PMIX_COMMON */

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
