/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>

#include "pshostlist.h"

#include "pluginmalloc.h"

#include "slurmcommon.h"
#include "psslurmlog.h"
#include "psslurmconfig.h"
#include "psslurmcomm.h"

#include "psslurmgres.h"

/** List of all GRES configurations */
static LIST_HEAD(GresConfList);

static uint32_t getGresId(char *name)
{
    int i, x;
    uint32_t gresId = 0;

    for (i=0, x=0; name[i]; i++) {
	gresId += (name[i] << x);
	x = (x + 8) % 32;
    }

    return gresId;
}

static int setGresCount(Gres_Conf_t *gres, char *count)
{
    if (!count) {
	gres->count = 1;
	return 1;
    }

    char *end;
    errno = 0;

    long gCount = strtol(count, &end, 10);
    if (!gCount && errno != 0) {
	mwarn(errno, "%s: invalid count '%s' for '%s'", __func__,
	      count, gres->name);
	return 0;
    }
    if (gCount == LONG_MIN || gCount == LONG_MAX) {
	flog("invalid count '%s' for '%s'\n", count, gres->name);
	return 0;
    }
    if (end[0] == 'k' || end[0] == 'K') {
	gCount *= 1024;
    } else if (end[0] == 'm' || end[0] == 'M') {
	gCount *= (1024 * 1024);
    } else if (end[0] == 'g' || end[0] == 'G') {
	gCount *= (1024 * 1024 * 1024);
    } else if (end[0] != '\0') {
	flog("invalid count '%s' for '%s'\n", count, gres->name);
	return 0;
    }

    gres->count = gCount;
    return 1;
}

static bool testDevices(char *file, void *info)
{
    struct stat sbuf;
    Gres_Conf_t *gres = info;

    if (stat(file, &sbuf) == -1) {
	mlog("%s: invalid device '%s' for '%s'\n", __func__, file, gres->name);
    } else {
	gres->count++;
    }

    return true;
}

static int parseGresFile(Gres_Conf_t *gres)
{
    /* test every devices */
    if (!traverseHostList(gres->file, testDevices, gres)) {
	mlog("%s: invalid gres file '%s' for '%s'\n", __func__,
	     gres->file, gres->name);
	return 0;
    }

    return 1;
}

static void freeGresConf(Gres_Conf_t *gres)
{
    ufree(gres->name);
    ufree(gres->cpus);
    ufree(gres->file);
    ufree(gres->type);
    ufree(gres->cores);
    ufree(gres);
}

Gres_Conf_t *saveGresConf(Gres_Conf_t *gres, char *count)
{
    gres->id = getGresId(gres->name);

    /* parse file */
    if (gres->file) {
	if (!parseGresFile(gres)) goto GRES_ERROR;
    } else {
	/* parse count */
	if (!setGresCount(gres, count)) goto GRES_ERROR;
    }

    if (gres->cores) {
	flog("GRES cores feature currently unsupported, ignoring it\n");
    }

    flog("%s id=%u count=%lu%s%s%s%s%s%s\n",
	 gres->name, gres->id, gres->count,
	 gres->file ? " file=" : "", gres->file ? gres->file : "",
	 gres->type ? " type=" : "", gres->type ? gres->type : "",
	 gres->cores ? " cores=" : "", gres->cores ? gres->cores : "");

    list_add_tail(&gres->next, &GresConfList);
    return gres;

GRES_ERROR:
    freeGresConf(gres);
    return NULL;
}

int countGresConf(void)
{
    int count = 0;
    list_t *g;

    list_for_each(g, &GresConfList) count++;
    return count;
}

void clearGresConf(void)
{
    list_t *g, *tmp;
    list_for_each_safe(g, tmp, &GresConfList) {
	Gres_Conf_t *gres = list_entry(g, Gres_Conf_t, next);

	list_del(&gres->next);
	freeGresConf(gres);
    }
}

Gres_Cred_t *getGresCred(void)
{
    Gres_Cred_t *gres = ucalloc(sizeof(*gres));
    INIT_LIST_HEAD(&gres->next);

    return gres;
}

Gres_Cred_t *findGresCred(list_t *gresList, uint32_t id, int credType)
{
    list_t *g;
    list_for_each(g, gresList) {
	Gres_Cred_t *gres = list_entry(g, Gres_Cred_t, next);
	if (gres->credType == credType && gres->id == id) return gres;
	if (id == NO_VAL && gres->credType == credType) return gres;
    }
    return NULL;
}

void releaseGresCred(Gres_Cred_t *gres)
{

    if (!gres) return;

    if (gres->bitAlloc) {
	unsigned int i;
	for (i=0; i<gres->nodeCount; i++) ufree(gres->bitAlloc[i]);
	ufree(gres->bitAlloc);
    }

    if (gres->bitStepAlloc) {
	unsigned int i;
	for (i=0; i<gres->nodeCount; i++) ufree(gres->bitStepAlloc[i]);
	ufree(gres->bitStepAlloc);
    }

    ufree(gres->countStepAlloc);
    ufree(gres->nodeInUse);
    ufree(gres->typeModel);
    ufree(gres);
}

void freeGresCred(list_t *gresList)
{
    list_t *g, *tmp;

    if (!gresList) return;

    list_for_each_safe(g, tmp, gresList) {
	Gres_Cred_t *gres = list_entry(g, Gres_Cred_t, next);

	list_del(&gres->next);
	releaseGresCred(gres);
    }
}

bool traverseGresConf(GresConfVisitor_t visitor, void *info)
{
    list_t *g, *tmp;
    list_for_each_safe(g, tmp, &GresConfList) {
	Gres_Conf_t *gres = list_entry(g, Gres_Conf_t, next);

	if (visitor(gres, info)) return true;
    }

    return false;
}
