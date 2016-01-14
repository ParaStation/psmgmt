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

#ifndef __PS_SLURM_PELOGUE
#define __PS_SLURM_PELOGUE

void startPElogue(uint32_t jobid, uid_t uid, gid_t gid, char *username,
		    uint32_t nrOfNodes, PSnodes_ID_t *nodes, env_t *env,
		    env_t *spankenv, int step, int prologue);

int handlePElogueFinish(void *data);

int handleTaskPrologue(char *taskPrologue, uint32_t rank,
	uint32_t jobid, pid_t task_pid, char *wdir);

int startTaskEpilogues(Step_t *step);

void execTaskEpilogues(void *data, int rerun);
#endif
