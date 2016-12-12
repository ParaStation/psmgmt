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

#include "list.h"
#include "pluginforwarder.h"
#include "pluginenv.h"

typedef enum {
    PELOGUE_PROLOGUE = 1,
    PELOGUE_EPILOGUE,
} PELOGUE_child_types_t;

typedef struct {
    list_t next;
    Forwarder_Data_t *fwdata;
    PELOGUE_child_types_t type;	 /* type of the forwarder (e.g. interactive) */
    struct timeval start_time;	 /* the start time of the forwarder */
    char *jobid;		 /* the PBS jobid */
    char *plugin;		 /* the name of the plugin */
    int signalFlag;
} Child_t;

typedef struct {
    bool frontend;
    bool prologue;
    PStask_ID_t mainPelogue;
    char *dirScripts;
    char *plugin;
    char *jobid;
    int32_t timeout;
    int32_t exit;
    env_t env;
    char *scriptname;
    Child_t *child;
    uid_t uid;
    gid_t gid;
    time_t start_time;
} PElogue_Data_t;

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
} PElogue_Res_List_t;

typedef void Pelogue_JobCb_Func_t (char *, int, int, PElogue_Res_List_t *);

#endif  /* __PELOGUE__TYPES */
