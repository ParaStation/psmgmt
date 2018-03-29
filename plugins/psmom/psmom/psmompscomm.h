/*
 * ParaStation
 *
 * Copyright (C) 2010-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSMOM_PSCOMM
#define __PSMOM_PSCOMM

#include "pspluginprotocol.h"

#include "psserial.h"
#include "psmomjob.h"

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
    PSP_PSMOM_JOB_UPDATE,	    /**< new parallel job via mpiexec started */
} PSP_PSMOM_t;

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

void sendPSmomVersion(Job_t *job);

void sendJobInfo(Job_t *job, int start);

void initPSComm(void);

void finalizePSComm(void);

#endif /* __PSMOM_PSCOMM */
