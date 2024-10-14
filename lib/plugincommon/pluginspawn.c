/*
 * ParaStation
 *
 * Copyright (C) 2015-2017 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2023-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pluginspawn.h"

#include <string.h>

#include "pluginmalloc.h"

SpawnRequest_t *initSpawnRequest(int num)
{
    SpawnRequest_t *req = ucalloc(sizeof(*req));
    if (!req) return NULL;

    req->num = num;
    req->spawns = ucalloc(num * sizeof(*req->spawns));
    if (!req->spawns) {
	ufree(req);
	return NULL;
    }

    return req;
}

SpawnRequest_t *copySpawnRequest(SpawnRequest_t *req)
{
    if (!req) return NULL;

    SpawnRequest_t *ret = initSpawnRequest(req->num);
    if (!ret) return NULL;

    for (int i = 0; i < req->num; i++) {
	SingleSpawn_t *old = &(req->spawns[i]);
	SingleSpawn_t *new = &(ret->spawns[i]);

	new->np = old->np;
	new->argV = strvClone(old->argV);
	if (!strvInitialized(new->argV) && strvInitialized(old->argV)) {
	    freeSpawnRequest(ret);
	    return NULL;
	}
	new->env = envClone(old->env, NULL);
	if (!envInitialized(new->env) && envInitialized(old->env)) {
	    freeSpawnRequest(ret);
	    return NULL;
	}

	new->preputs = envClone(old->preputs, NULL);
	if (!envInitialized(new->preputs) && envInitialized(old->preputs)) {
	    freeSpawnRequest(ret);
	    return NULL;
	}

	new->infos = envClone(old->infos, NULL);
	if (!envInitialized(new->infos) && envInitialized(old->infos)) {
	    freeSpawnRequest(ret);
	    return NULL;
	}
    }

    ret->data = req->data;

    return ret;
}

void freeSpawnRequest(SpawnRequest_t *req)
{
    if (req->infov) {
	for (int i = 0; i < req->infoc; i++) {
	    ufree(req->infov[i].key);
	    ufree(req->infov[i].value);
	}
	ufree(req->infov);
    }
    for (int i = 0; i < req->num; i++) {
	strvDestroy(req->spawns[i].argV);
	envDestroy(req->spawns[i].env);
	envDestroy(req->spawns[i].preputs);
	envDestroy(req->spawns[i].infos);
    }

    ufree(req->spawns);
}

PStask_t* initSpawnTask(PStask_t *spawner, bool filter(const char*))
{
    PStask_t *task = PStask_new();
    if (!task) return NULL;

    /* copy data from my task */
    task->uid = spawner->uid;
    task->gid = spawner->gid;
    task->aretty = spawner->aretty;
    task->loggertid = spawner->loggertid;
    task->ptid = spawner->tid;
    task->group = TG_KVS;
    task->winsize = spawner->winsize;
    task->termios = spawner->termios;

    /* set work dir */
    if (spawner->workingdir) {
	task->workingdir = ustrdup(spawner->workingdir);
    } else {
	task->workingdir = NULL;
    }

    task->env = envClone(spawner->env, filter);

    return task;
}
