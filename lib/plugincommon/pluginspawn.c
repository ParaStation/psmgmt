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

#include "psenv.h"
#include "pluginstrv.h"

#include "pluginmalloc.h"

SpawnRequest_t *initSpawnRequest(int num)
{
    SpawnRequest_t *req = umalloc(sizeof(*req));
    if (!req) return NULL;

    req->num = num;
    req->spawns = umalloc(num * sizeof(*req->spawns));

    /* set everything to zero */
    memset(req->spawns, 0, num * sizeof(*req->spawns));

    for (int i = 0; i < num; i++) req->spawns[i].env = envNew(NULL);

    req->data = NULL;

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

	if (old->argv) {
	    new->argc = old->argc;
	    new->argv = umalloc((new->argc + 1) * sizeof(*new->argv));
	    for (int j = 0; j < old->argc; j++) {
		new->argv[j] = ustrdup(old->argv[j]);
	    }
	    new->argv[new->argc] = NULL;
	}
	new->env = envClone(old->env, NULL);
	if (!envInitialized(new->env)) {
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
    for (int i = 0; i < req->num; i++) {
	if (req->spawns[i].argv) {
	    for (int j = 0; j < req->spawns[i].argc; j++) {
		ufree(req->spawns[i].argv[j]);
	    }
	    ufree(req->spawns[i].argv);
	}
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

    /* build environment */
    strv_t env;
    strvInit(&env, NULL, 0);
    for (int i = 0; spawner->environ[i]; i++) {
	char *cur = spawner->environ[i];

	if (!filter(cur)) continue;
	strvAdd(&env, cur);
    }
    task->environ = env.strings;
    task->envSize = env.count;

    return task;
}
