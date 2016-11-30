/*
 * ParaStation
 *
 * Copyright (C) 2013 - 2015 ParTec Cluster Competence Center GmbH, Munich
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
#include "peloguetypes.h"

#include "peloguejob.h"

#define JOB_NAME_LEN	    256

typedef enum {
    PSP_PROLOGUE_START,	    /**< prologue script start */
    PSP_PROLOGUE_FINISH,    /**< result from prologue */
    PSP_EPILOGUE_START,	    /**< epilogue script start */
    PSP_EPILOGUE_FINISH,    /**< result from epilogue script */
    PSP_PELOGUE_SIGNAL,	    /**< send a signal to a PElogue script */
} PSP_PELOGUE_t;

void handlePelogueMsg(DDTypedBufferMsg_t *msg);

void handleIntMsg(DDTypedBufferMsg_t *msg);

void handleDroppedMsg(DDTypedBufferMsg_t *msg);

int sendPElogueStart(Job_t *job, bool prologue, env_t *env);

int handleNodeDown(void *nodeID);

#endif
