/*
 * ParaStation
 *
 * Copyright (C) 2015-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_IO
#define __PS_SLURM_IO

#include "pslog.h"
#include "psslurmjob.h"
#include "psslurmtasks.h"

#define MAX_SATTACH_SOCKETS 30

#define SLURM_IO_HEAD_SIZE 3 * sizeof(uint16_t) + sizeof(uint32_t)

typedef struct {
    uint16_t type;  /* type */
    uint16_t gtid;  /* global task ID */
    uint16_t ltid;  /* local task ID */
    uint32_t len;   /* data length */
} Slurm_IO_Header_t;

typedef enum {
    IO_UNDEF = 0x05,	/** I/O not defined */
    IO_SRUN,		/** I/O via srun */
    IO_SRUN_RANK,	/** I/O via srun to a single task/rank  */
    IO_GLOBAL_FILE,	/** I/O to global file */
    IO_RANK_FILE,	/** separate I/O file per rank */
    IO_NODE_FILE,	/** separate I/O file per node */
} IO_Opt_t;

void writeIOmsg(char *msg, uint32_t msgLen, uint32_t taskid,
			uint8_t type, Forwarder_Data_t *fwdata, Step_t *step,
			uint32_t lrank);
int stepForwarderMsg(PSLog_Msg_t *msg, Forwarder_Data_t *fwData);
char *replaceStepSymbols(Step_t *step, int rank, char *path);
char *replaceJobSymbols(Job_t *job, char *path);
char *replaceSymbols(uint32_t jobid, uint32_t stepid, char *hostname,
			int nodeid, char *username, uint32_t arrayJobId,
			uint32_t arrayTaskId, int rank, char *path);
void redirectJobOutput(Job_t *job);
int redirectIORank(Step_t *step, int rank);
void redirectStepIO(Forwarder_Data_t *fwdata, Step_t *step);
void redirectStepIO2(Forwarder_Data_t *fwdata, Step_t *step);
void sendEnableSrunIO(Step_t *step);
void printChildMessage(Step_t *step, char *msg, uint32_t msgLen,
		       uint8_t type, int32_t rank);
int handleUserOE(int sock, void *data);
int setFilePermissions(Job_t *job);
void stepFinalize(Forwarder_Data_t *fwData);
void sendFWfinMessage(Forwarder_Data_t *fwdata, PSLog_Msg_t *msg);
void reattachTasks(Forwarder_Data_t *fwdata, uint32_t addr,
		    uint16_t ioPort, uint16_t ctlPort, char *sig);
void sendFWtaskInfo(Forwarder_Data_t *fwdata, PS_Tasks_t *task);
void initStepIO(Step_t *step);
void sendStepTimeout(Forwarder_Data_t *fwdata);
int hookFWmsg(PSLog_Msg_t *msg, Forwarder_Data_t *fwData);
void sendBrokeIOcon(Step_t *step);

#endif  /* __PS_SLURM_IO */
