/*
 * ParaStation
 *
 * Copyright (C) 2013 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PELOGUE__INTER
#define __PELOGUE__INTER

#include <stdbool.h>

#include "pluginenv.h"
#include "pelogueconfig.h"

int psPelogueAddPluginConfig(char * name, Config_t *configList);

void psPelogueAddJob(const char *plugin, const char *jobid, uid_t uid,
			gid_t gid, int nrOfNodes, PSnodes_ID_t *nodes,
			Pelogue_JobCb_Func_t *pluginCallback);

int psPelogueStartPE(const char *plugin, const char *jobid, bool prologue,
			env_t *env);

int psPelogueSignalPE(const char *plugin, const char *jobid, int signal,
			char *reason);

#endif
