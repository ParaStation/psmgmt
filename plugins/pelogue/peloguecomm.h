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

#ifndef __PELOGUE__COMM
#define __PELOGUE__COMM

#include <stdbool.h>

#include "psprotocol.h"
#include "pluginenv.h"
#include "peloguechild.h"

#include "peloguejob.h"

#define JOB_NAME_LEN	    256

typedef enum {
    PSP_PROLOGUE_START,	    /**< prologue script start */
    PSP_PROLOGUE_FINISH,    /**< result from prologue */
    PSP_EPILOGUE_START,	    /**< epilogue script start */
    PSP_EPILOGUE_FINISH,    /**< result from epilogue script */
    PSP_PELOGUE_SIGNAL,	    /**< send a signal to a PElogue script */
} PSP_PELOGUE_t;

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

    /*
    char *jobname;
    char *user;
    char *group;
    char *limits;
    char *queue;
    char *sessid;
    char *exec_host;
    char *tmpDir;
    char *resources_used;
    char *gpus;
    char *server;
    */
} PElogue_Data_t;

void handlePelogueMsg(DDTypedBufferMsg_t *msg);

void handleIntMsg(DDTypedBufferMsg_t *msg);

void handleDroppedMsg(DDTypedBufferMsg_t *msg);

int sendPElogueStart(Job_t *job, bool prologue, env_t *env);

int fwCallback(int32_t wstat, char *errMsg, size_t errLen, void *data);

int handleNodeDown(void *nodeID);

#endif
