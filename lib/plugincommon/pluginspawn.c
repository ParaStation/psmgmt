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

	if (old->preputv) {
	    new->preputc = old->preputc;
	    new->preputv = umalloc(new->preputc * sizeof(*new->preputv));
	    for (int j = 0; j < old->preputc; j++) {
		new->preputv[j].key = ustrdup(old->preputv[j].key);
		new->preputv[j].value = ustrdup(old->preputv[j].value);
	    }
	}

	if (old->infov) {
	    new->infoc = old->infoc;
	    new->infov = umalloc(new->infoc * sizeof(*new->infov));
	    for (int j = 0; j < old->infoc; j++) {
		new->infov[j].key = ustrdup(old->infov[j].key);
		new->infov[j].value = ustrdup(old->infov[j].value);
	    }
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
	if (req->spawns[i].preputv) {
	    for (int j = 0; j < req->spawns[i].preputc; j++) {
		ufree(req->spawns[i].preputv[j].key);
		ufree(req->spawns[i].preputv[j].value);
	    }
	    ufree(req->spawns[i].preputv);
	}
	if (req->spawns[i].infov) {
	    for (int j = 0; j < req->spawns[i].infoc; j++) {
		ufree(req->spawns[i].infov[j].key);
		ufree(req->spawns[i].infov[j].value);
	    }
	    ufree(req->spawns[i].infov);
	}
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
