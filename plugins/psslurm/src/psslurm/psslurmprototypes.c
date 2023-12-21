/*
 * ParaStation
 *
 * Copyright (C) 2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "psslurmprototypes.h"

#include <ctype.h>

#include "pluginmalloc.h"

void freeRespJobInfo(Resp_Job_Info_t *resp)
{
    if (!resp) return;

    for (uint32_t i = 0; i < resp->numJobs; i++) {
	Slurm_Job_Rec_t *rec = &resp->jobs[i];

	ufree(rec->arrayTaskStr);
	ufree(rec->hetJobIDset);
	ufree(rec->container);
	ufree(rec->cluster);
	ufree(rec->nodes);
	ufree(rec->schedNodes);
	ufree(rec->partition);
	ufree(rec->account);
	ufree(rec->adminComment);
	ufree(rec->network);
	ufree(rec->comment);
	ufree(rec->batchFeat);
	ufree(rec->batchHost);
	ufree(rec->burstBuffer);
	ufree(rec->burstBufferState);
	ufree(rec->systemComment);
	ufree(rec->qos);
	ufree(rec->licenses);
	ufree(rec->stateDesc);
	ufree(rec->resvName);
	ufree(rec->mcsLabel);
	ufree(rec->containerID);
	ufree(rec->failedNode);
	ufree(rec->extra);
    }
    ufree(resp->jobs);
    ufree(resp);
}
