/*
 * ParaStation
 *
 * Copyright (C) 2011-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_MOM_RECOVER
#define __PS_MOM_RECOVER

#include "psmomjob.h"

void saveJobInfo(Job_t *job);
void recoverJobInfo(void);

#endif  /* __PS_MOM_RECOVER */
