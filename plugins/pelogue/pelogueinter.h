/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PELOGUE_INTER
#define __PELOGUE_INTER

#include <stdbool.h>
#include <sys/types.h>

#include "psnodes.h"
#include "pluginenv.h"
#include "pelogueconfig.h"

int psPelogueAddPluginConfig(char *name, Config_t *configList);

int psPelogueAddJob(const char *plugin, const char *jobid, uid_t uid, gid_t gid,
		    int nrOfNodes, PSnodes_ID_t *nodes,
		    Pelogue_JobCb_Func_t *pluginCallback);

int psPelogueStartPE(const char *plugin, const char *jobid, bool prologue,
		     env_t *env);

int psPelogueSignalPE(const char *plugin, const char *jobid, int signal,
		      char *reason);

void psPelogueDeleteJob(const char *plugin, const char *jobid);

#endif  /* __PELOGUE__INTER */
