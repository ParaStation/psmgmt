/*
 * ParaStation
 *
 * Copyright (C) 2015-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PELOGUE__TYPES
#define __PELOGUE__TYPES

#include <stdbool.h>
#include <time.h>

#include "list.h"
#include "pstaskid.h"
#include "pluginenv.h"
#include "pluginforwarder.h"

typedef enum {
    PELOGUE_PROLOGUE = 1,
    PELOGUE_EPILOGUE,
} PElogueType_t;

typedef struct {
    list_t next;
    char *plugin;
    char *jobid;
    PElogueType_t type;       /**< Type of pelogue to run */
    PSnodes_ID_t mainPElogue; /**< Node initiating the pelogue */
    char *dirScripts;
    int32_t timeout;
    int32_t rounds;
    env_t env;
    uid_t uid;
    gid_t gid;
    time_t startTime;         /**< Job's pelogue starttime identies answers */
    Forwarder_Data_t *fwData;
    int32_t signalFlag;
    int32_t exit;
} PElogueChild_t;

typedef enum {
    PELOGUE_PENDING = 1,
    PELOGUE_DONE,
    PELOGUE_FAILED,
    PELOGUE_TIMEDOUT,
} PElogueState_t;

typedef struct {
    PSnodes_ID_t id;
    PElogueState_t prologue;
    PElogueState_t epilogue;
} PElogueResList_t;

typedef void Pelogue_JobCb_Func_t (char *, int, int, PElogueResList_t *);

#endif  /* __PELOGUE__TYPES */
