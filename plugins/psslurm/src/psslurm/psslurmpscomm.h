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

#ifndef __PS_SLURM_PSCOMM
#define __PS_SLURM_PSCOMM

#include "psprotocol.h"

int handleCreatePart(void *msg);

int handleCreatePartNL(void *msg);

int handleNodeDown(void *nodeID);

void handleDroppedMsg(DDTypedBufferMsg_t *msg);

void handlePsslurmMsg(DDTypedBufferMsg_t *msg);

void callbackPElogue(char *jobid, int exit_status, int timeout);

void handleChildBornMsg(DDErrorMsg_t *msg);

#endif
