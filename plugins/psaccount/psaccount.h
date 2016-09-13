/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_ACCOUNT_MAIN
#define __PS_ACCOUNT_MAIN

#include "psidcomm.h"

/** save default handler for accouting msgs */
extern handlerFunc_t oldAccountHandler;

/** the linux system page size */
extern int pageSize;

#endif
