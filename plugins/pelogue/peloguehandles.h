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

#ifndef __PELOGUE__HANDLES
#define __PELOGUE__HANDLES

#include <stdbool.h>

#include "pluginconfig.h"
#include "peloguetypes.h"

int (*psPelogueAddPluginConfig)(char *, Config_t *);

void (*psPelogueAddJob)(const char *, const char *, uid_t, gid_t, int,
			PSnodes_ID_t *,
			void (*pluginCallback)(char *, int, int));

int (*psPelogueStartPE)(const char *, const char *, bool, env_t *);

void (*psPelogueDeleteJob)(const char *, const char *);

int (*psPelogueSignalPE)(const char *, const char *, int, char *);

#endif
