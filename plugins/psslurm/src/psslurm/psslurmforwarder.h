/*
 * ParaStation
 *
 * Copyright (C) 2014-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_SLURM_FORWARDER
#define __PS_SLURM_FORWARDER

#include "psslurmjob.h"
#include "psslurmbcast.h"

typedef enum {
    CMD_PRINT_CHILD_MSG = 100,
    CMD_ENABLE_SRUN_IO,
    CMD_FW_FINALIZE,
    CMD_REATTACH_TASKS,
    CMD_INFO_TASKS,
    CMD_STEP_TIMEOUT,
    CMD_BROKE_IO_CON,
} PSSLURM_Fw_Cmds_t;

int execUserStep(Step_t *step);
bool execUserJob(Job_t *job);
int execUserBCast(BCast_t *bcast);
int execStepFWIO(Step_t *step);
int handleExecClient(void *data);
int handleExecClientUser(void * data);
int handleForwarderInit(void * data);
int handleForwarderClientStatus(void * data);

void switchUser(char *username, uid_t uid, gid_t gid, char *cwd);

#endif
