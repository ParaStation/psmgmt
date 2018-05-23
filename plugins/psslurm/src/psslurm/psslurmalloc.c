/*
 * ParaStation
 *
 * Copyright (C) 2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <time.h>

#include "pluginmalloc.h"
#include "pspamhandles.h"
#include "peloguehandles.h"

#include "psslurmalloc.h"
#include "psslurmproto.h"
#include "psslurmlog.h"
#include "psslurmenv.h"
#include "psslurmpscomm.h"

/** List of all allocations */
static LIST_HEAD(AllocList);

Alloc_t *addAlloc(uint32_t id, uint32_t nrOfNodes, char *slurmHosts,
			    env_t *env, env_t *spankenv, uid_t uid, gid_t gid,
			    char *username)
{
    Alloc_t *alloc = findAlloc(id);

    if (alloc) return alloc;

    alloc = umalloc(sizeof(Alloc_t));
    alloc->id = id;
    alloc->state = A_INIT;
    alloc->uid = uid;
    alloc->gid = gid;
    alloc->terminate = 0;
    alloc->slurmHosts = ustrdup(slurmHosts);
    alloc->username = ustrdup(username);
    alloc->firstKillRequest = 0;
    alloc->motherSup = -1;
    alloc->start_time = time(0);

    /* init nodes */
    getNodesFromSlurmHL(slurmHosts, &alloc->nrOfNodes, &alloc->nodes,
			&alloc->localNodeId);
    if (alloc->nrOfNodes != nrOfNodes) {
	mlog("%s: mismatching nrOfNodes '%u:%u'\n", __func__, nrOfNodes,
		alloc->nrOfNodes);
    }

    /* init env */
    if (env) {
	envClone(env, &alloc->env, envFilter);
    } else {
	envInit(&alloc->env);
    }

    if (spankenv) {
	envClone(spankenv, &alloc->spankenv, envFilter);
    } else {
	envInit(&alloc->spankenv);
    }

    list_add_tail(&alloc->next, &AllocList);

    /* add user in pam for ssh access */
    psPamAddUser(alloc->username, strJobID(id), PSPAM_STATE_PROLOGUE);

    return alloc;
}

bool traverseAllocs(AllocVisitor_t visitor, const void *info)
{
    list_t *a, *tmp;
    list_for_each_safe(a, tmp, &AllocList) {
	Alloc_t *alloc = list_entry(a, Alloc_t, next);

	if (visitor(alloc, info)) return true;
    }

    return false;
}

int countAllocs(void)
{
    int count=0;
    list_t *a;
    list_for_each(a, &AllocList) count++;

    return count;
}

void clearAllocList(void)
{
    list_t *a, *tmp;
    list_for_each_safe(a, tmp, &AllocList) {
	Alloc_t *alloc = list_entry(a, Alloc_t, next);
	deleteAlloc(alloc->id);
    }
}

Alloc_t *findAlloc(uint32_t id)
{
    list_t *a;
    list_for_each(a, &AllocList) {
	Alloc_t *alloc = list_entry(a, Alloc_t, next);
	if (alloc->id == id) return alloc;
    }
    return NULL;
}

int deleteAlloc(uint32_t id)
{
    Alloc_t *alloc;

    /* delete all corresponding steps */
    clearStepList(id);
    clearBCastByJobid(id);

    if (!(alloc = findAlloc(id))) return 0;

    /* free corresponding pelogue job */
    psPelogueDeleteJob("psslurm", strJobID(alloc->id));

    /* tell sisters the allocation is revoked */
    if (alloc->motherSup == PSC_getMyTID()) {
	send_PS_JobExit(alloc->id, SLURM_BATCH_SCRIPT,
		alloc->nrOfNodes, alloc->nodes);
    }

    psPamDeleteUser(alloc->username, strJobID(id));

    ufree(alloc->nodes);
    ufree(alloc->slurmHosts);
    ufree(alloc->username);
    envDestroy(&alloc->env);
    envDestroy(&alloc->spankenv);

    list_del(&alloc->next);
    ufree(alloc);

    return 1;
}

int signalAllocs(int signal)
{
    int count = 0;
    list_t *a, *tmp;
    list_for_each_safe(a, tmp, &AllocList) {
	Alloc_t *alloc = list_entry(a, Alloc_t, next);
	count += signalStepsByJobid(alloc->id, signal, 0);
    }

    return count;
}
