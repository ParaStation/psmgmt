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

#include "pluginmalloc.h"

SpawnRequest_t *initSpawnRequest(int num) {

    SpawnRequest_t *req;

    req = umalloc(sizeof(*req));
    if (!req) return NULL;

    req->num = num;
    req->spawns = umalloc(num * sizeof(*req->spawns));

    /* set everything to zero */
    memset(req->spawns, 0, num * sizeof(*req->spawns));

    req->env = envNew(NULL);

    return req;
}

SpawnRequest_t *copySpawnRequest(SpawnRequest_t *req) {

    SpawnRequest_t *ret;
    SingleSpawn_t *old, *new;
    int i, j;

    if (!req) return NULL;

    ret = initSpawnRequest(req->num);
    if (!ret) return NULL;

    for (i=0; i < req->num; i++) {
	old = &(req->spawns[i]);
	new = &(ret->spawns[i]);

	new->np = old->np;

	if (old->argv) {
	    new->argc = old->argc;
	    new->argv = umalloc(new->argc * sizeof(*new->argv));
	    for (j = 0; j < old->argc; j++) {
		new->argv[j] = ustrdup(old->argv[j]);
	    }
	}
	if (old->preputv) {
	    new->preputc = old->preputc;
	    new->preputv = umalloc(new->preputc * sizeof(*new->preputv));
	    for (j = 0; j < old->preputc; j++) {
		new->preputv[j].key = ustrdup(old->preputv[j].key);
		new->preputv[j].value = ustrdup(old->preputv[j].value);
	    }
	}
	if (old->infov) {
	    new->infoc = old->infoc;
	    new->infov = umalloc(new->infoc * sizeof(*new->infov));
	    for (j = 0; j < old->infoc; j++) {
		new->infov[j].key = ustrdup(old->infov[j].key);
		new->infov[j].value = ustrdup(old->infov[j].value);
	    }
	}
    }

    ret->env = envClone(req->env, NULL);
    if (!ret->env) {
	freeSpawnRequest(ret);
	return NULL;
    }

    return ret;
}

void freeSpawnRequest(SpawnRequest_t *req) {

    int i, j;

    for (i=0; i < req->num; i++) {
	if (req->spawns[i].argv) {
	    for (j = 0; j < req->spawns[i].argc; j++) {
		ufree(req->spawns[i].argv[j]);
	    }
	    ufree(req->spawns[i].argv);
	}
	if (req->spawns[i].preputv) {
	    for (j = 0; j < req->spawns[i].preputc; j++) {
		ufree(req->spawns[i].preputv[j].key);
		ufree(req->spawns[i].preputv[j].value);
	    }
	    ufree(req->spawns[i].preputv);
	}
	if (req->spawns[i].infov) {
	    for (j = 0; j < req->spawns[i].infoc; j++) {
		ufree(req->spawns[i].infov[j].key);
		ufree(req->spawns[i].infov[j].value);
	    }
	    ufree(req->spawns[i].infov);
	}
    }

    envDestroy(req->env);

    ufree(req->spawns);
}
