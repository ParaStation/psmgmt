/*
 * ParaStation
 *
 * Copyright (C) 2018-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psgwrequest.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "pluginmalloc.h"
#include "psidsignal.h"

#include "peloguehandles.h"

#include "psgwlog.h"

/** List of all requests */
static LIST_HEAD(ReqList);

PSGW_Req_t *Request_add(PElogueResource_t *res, char *packID)
{
    if (!res) return NULL;

    char *user = envGet(res->env, "SLURM_USER");
    if (!user) {
	flog("missing SLURM_USER in environment\n");
	return NULL;
    }

    PSGW_Req_t *req = ucalloc(sizeof(*req));
    *req = (PSGW_Req_t) {
	.jobid = ustrdup(res->jobid),
	.packID = ustrdup(packID),
	.res = res,
	.routeRes = -1,
	.routePID = -2,
	.pelogueState = -1,
	.psgwdPerNode = 1,
	.timerRouteScript = -1,
	.uid = res->uid,
	.gid = res->gid,
	.username = ustrdup(user),
	.cleanup = envGet(res->env, "SLURM_SPANK_PSGW_CLEANUP") ? true : false, };

    envClone(res->env, &req->env, NULL);

    list_add_tail(&req->next, &ReqList);
    return req;
}

void Request_setNodes(PSGW_Req_t *req, PSnodes_ID_t *nodes, uint32_t numNodes)
{
    req->gwNodes = nodes;
    req->numGWnodes = numNodes;

    req->numPSGWD =  numNodes * req->psgwdPerNode;
    req->psgwd = umalloc(sizeof(*req->psgwd) * req->numPSGWD);

    uint32_t i;
    for(i=0; i<req->numPSGWD; i++) {
	req->psgwd[i].addr = NULL;
	req->psgwd[i].pid = -1;
	req->psgwd[i].node = -1;
    }
}

void Request_delete(PSGW_Req_t *req)
{
    if (!req) return;

    if (req->cleanup && req->routeFile) {
	fdbg(PSGW_LOG_DEBUG, "unlink %s\n", req->routeFile);
	if (unlink(req->routeFile) == -1) {
	    mwarn(errno, "%s: unlink %s failed: ", __func__, req->routeFile);
	}
	ufree(req->routeFile);
	req->routeFile = NULL;
    }
    psPelogueDeleteJob("psgw", req->jobid);

    /* stop psgw forwarder */
    if (req->fwdata) pskill(PSC_getPID(req->fwdata->tid), SIGKILL, 0);

    for(uint32_t i = 0; i < req->numPSGWD; i++) ufree(req->psgwd[i].addr);
    ufree(req->psgwd);

    ufree(req->jobid);
    ufree(req->packID);
    ufree(req->username);
    envDestroy(&req->env);

    list_del(&req->next);
    ufree(req);
}

void Request_clear(void)
{
    list_t *j, *tmp;

    list_for_each_safe(j, tmp, &ReqList) {
	PSGW_Req_t *req = list_entry(j, PSGW_Req_t, next);
	Request_delete(req);
    }
}

PSGW_Req_t *Request_findByFW(PStask_ID_t tid)
{
    list_t *j;

    list_for_each(j, &ReqList) {
	PSGW_Req_t *req = list_entry(j, PSGW_Req_t, next);
	if (req->fwdata && req->fwdata->tid == tid) return req;
    }
    return NULL;
}

PSGW_Req_t *Request_find(char *jobid)
{
    list_t *j;

    if (!jobid) return NULL;
    list_for_each(j, &ReqList) {
	PSGW_Req_t *req = list_entry(j, PSGW_Req_t, next);
	if (!strcmp(req->jobid, jobid)) return req;
	if (!strcmp(req->packID, jobid)) return req;
    }
    return NULL;
}

PSGW_Req_t *Request_verify(PSGW_Req_t *reqPtr)
{
    list_t *j;

    if (!reqPtr) return NULL;
    list_for_each(j, &ReqList) {
	PSGW_Req_t *req = list_entry(j, PSGW_Req_t, next);
	if (req == reqPtr) return req;
    }
    return NULL;
}
