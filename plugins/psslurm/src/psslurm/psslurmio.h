/*
 * ParaStation
 *
 * Copyright (C) 2015 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PS_SLURM_IO
#define __PS_SLURM_IO

#include "pslog.h"

#define MAX_SATTACH_SOCKETS 30

int stepForwarderMsg(void *data, char *ptr, int32_t cmd);
char *replaceStepSymbols(Step_t *step, int rank, char *path);
char *replaceJobSymbols(Job_t *job, char *path);
char *replaceSymbols(uint32_t jobid, uint32_t stepid, char *hostname,
			int nodeid, char *username, uint32_t arrayJobId,
			uint32_t arrayTaskId, int rank, char *path);
void redirectJobOutput(Job_t *job);
void redirectIORank(Step_t *step, int rank);
void redirectStepIO(Forwarder_Data_t *fwdata, Step_t *step);
void redirectStepIO2(Forwarder_Data_t *fwdata, Step_t *step);
void sendEnableSrunIO(Step_t *step);
void printChildMessage(Forwarder_Data_t *fwdata, char *msg, uint32_t msgLen,
			uint8_t type, uint16_t taskid);
int handleUserOE(int sock, void *data);
int setFilePermissions(Job_t *job);
void stepFinalize(void *data);
void sendFWfinMessage(Forwarder_Data_t *fwdata, PSLog_Msg_t *msg);
void reattachTasks(Forwarder_Data_t *fwdata, uint32_t addr,
		    uint16_t ioPort, uint16_t ctlPort, char *sig);
void sendFWtaskInfo(Forwarder_Data_t *fwdata, PS_Tasks_t *task);

#endif
