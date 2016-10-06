/*
 * ParaStation
 *
 * Copyright (C) 2015-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Stephan Krempel <krempel@par-tec.com>
 *
 */

#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <errno.h>

#include "pscommon.h"
#include "pluginmalloc.h"
#include "psidforwarder.h"
#include "pmilog.h"

#include "pmiclientspawn.h"

SpawnRequest_t *initSpawnRequest(int totalSpawns) {

    SpawnRequest_t *req;

    req = umalloc(sizeof(SpawnRequest_t));
    if (!req) return NULL;

    req->totalSpawns = totalSpawns;
    req->spawns = umalloc(totalSpawns*sizeof(SingleSpawn_t));

    /* set everything to zero */
    memset(req->spawns, 0, totalSpawns*sizeof(SingleSpawn_t));

    req->pmienvc = 0;
    req->pmienvv = NULL;

    return req;
}


SpawnRequest_t *copySpawnRequest(SpawnRequest_t *req) {

    SpawnRequest_t *ret;
    SingleSpawn_t *old, *new;
    int i, j;

    if (req == NULL) return NULL;

    ret = initSpawnRequest(req->totalSpawns);
    if (ret == NULL) return NULL;

    for (i=0; i < req->totalSpawns; i++) {
	old = &(req->spawns[i]);
	new = &(ret->spawns[i]);

	new->np = old->np;

	if (old->argv != NULL) {
	    new->argc = old->argc;
	    new->argv = umalloc(new->argc * sizeof(new->argc));
	    for (j = 0; j < old->argc; j++) {
		new->argv[j] = ustrdup(old->argv[j]);
	    }
	}
	if (old->preputv != NULL) {
	    new->preputc = old->preputc;
	    new->preputv = umalloc(new->preputc * sizeof(KVP_t));
	    for (j = 0; j < old->preputc; j++) {
		new->preputv[j].key = ustrdup(old->preputv[j].key);
		new->preputv[j].value = ustrdup(old->preputv[j].value);
	    }
	}
	if (old->infov != NULL) {
	    new->infoc = old->infoc;
	    new->infov = umalloc(new->infoc * sizeof(KVP_t));
	    for (j = 0; j < old->infoc; j++) {
		new->infov[j].key = ustrdup(old->infov[j].key);
		new->infov[j].value = ustrdup(old->infov[j].value);
	    }
	}
    }

    if (req->pmienvv != NULL) {
	ret->pmienvc = req->pmienvc;
	ret->pmienvv = umalloc(ret->pmienvc * sizeof(KVP_t));
	for (i = 0; i < req->pmienvc; i++) {
	    ret->pmienvv[i].key = ustrdup(req->pmienvv[i].key);
	    ret->pmienvv[i].value = ustrdup(req->pmienvv[i].value);
	}
    }

    return ret;
}

void freeSpawnRequest(SpawnRequest_t *req) {

    int i, j;

    for (i=0; i < req->totalSpawns; i++) {
	if (req->spawns[i].argv != NULL) {
	    for (j = 0; j < req->spawns[i].argc; j++) {
		ufree(req->spawns[i].argv[j]);
	    }
	    ufree(req->spawns[i].argv);
	}
	if (req->spawns[i].preputv != NULL) {
	    for (j = 0; j < req->spawns[i].preputc; j++) {
		ufree(req->spawns[i].preputv->key);
		ufree(req->spawns[i].preputv->value);
	    }
	    ufree(req->spawns[i].preputv);
	}
	if (req->spawns[i].infov != NULL) {
	    for (j = 0; j < req->spawns[i].infoc; j++) {
		ufree(req->spawns[i].infov->key);
		ufree(req->spawns[i].infov->value);
	    }
	    ufree(req->spawns[i].infov);
	}
    }

    if (req->pmienvv != NULL) {
	for (j = 0; j < req->pmienvc; j++) {
	    ufree(req->pmienvv->key);
	    ufree(req->pmienvv->value);
	}
	ufree(req->pmienvv);
    }

    ufree(req->spawns);
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
