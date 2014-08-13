/*
 * ParaStation
 *
 * Copyright (C) 2010-2013 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PS_MOM_PSCOMM
#define __PS_MOM_PSCOMM

#include <stdbool.h>

#include "psprotocol.h"
#include "psidcomm.h"
#include "pscommon.h"
#include "psmomjob.h"
#include "plugincomm.h"

#define PSMOM_PSCOMM_VERSION 101

#define PSP_CC_PSMOM 0x0200  /**< psmom message */

typedef enum {
    PSP_PSMOM_VERSION = 0x0010,	    /**< req psmom version information */
    PSP_PSMOM_SHUTDOWN,		    /**< request to shut down */
    PSP_PSMOM_PROLOGUE_START,	    /**< prologue script start */
    PSP_PSMOM_PROLOGUE_FINISH,	    /**< result from prologue */
    PSP_PSMOM_EPILOGUE_START,	    /**< epilogue script start */
    PSP_PSMOM_EPILOGUE_FINISH,	    /**< result from epilogue script */
    PSP_PSMOM_PELOGUE_SIGNAL,	    /**< send a signal to a PElogue script */
    PSP_PSMOM_JOB_INFO,		    /**< a job is started/finished and our
				         node is involved */
    PSP_PSMOM_JOB_UPDATE,	    /**< a new parallel job via mpiexec started */
} PSP_PSMOM_t;

/**
 * @brief Get the string name for a PSP message type.
 *
 * @param type The message type to convert.
 *
 * @return Returns the requested string or NULL on error.
 */
char *pspMsgType2Str(PSP_PSMOM_t type);

/**
 * @brief Update remote job information.
 *
 * Send information about new started parallel processes to all nodes involed.
 *
 * @param job The job to send the info for.
 *
 * @return No return value.
 */
void sendJobUpdate(Job_t *job);

/**
 * @brief Handle a received PS DDTypedBuffer message.
 *
 * This is the main message switch for PS messages.
 *
 * @param msg The message to handle.
 *
 * @return No return value.
 */
void handlePSMsg(DDTypedBufferMsg_t *msg);

/**
 * @brief Handle a dropped message.
 *
 * @param msg The message to handle.
 *
 * @return No return value.
 */
void handleDroppedMsg(DDTypedBufferMsg_t *msg);

void sendFragMsgToHostList(Job_t *job, PS_DataBuffer_t *data, int32_t type,
			    int myself);

void sendPSMsgToHostList(Job_t *job, DDTypedBufferMsg_t *msg, int myself);

void sendPSmomVersion(Job_t *job);

void sendJobInfo(Job_t *job, int start);

#endif
