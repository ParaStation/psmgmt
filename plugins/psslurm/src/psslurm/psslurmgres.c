/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
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

void addGresData(PS_SendDB_t *msg, int version)
{
    int count=0, cpus;
    list_t *g;
    size_t startGresData;
    uint32_t len;
    char *ptr;

    cpus = getConfValueI(&Config, "SLURM_CPUS");

    /* add placeholder for gres info size */
    startGresData = msg->bufUsed;
    addUint32ToMsg(0, msg);
    /* add placeholder again for gres info size in pack_mem() */
    addUint32ToMsg(0, msg);

    /* add slurm version */
    addUint16ToMsg(version, msg);

    list_for_each(g, &GresConfList) count++;
    addUint16ToMsg(count, msg);

    list_for_each(g, &GresConfList) {
	Gres_Conf_t *gres = list_entry(g, Gres_Conf_t, next);

	addUint32ToMsg(GRES_MAGIC, msg);
	addUint64ToMsg(gres->count, msg);
	addUint32ToMsg(cpus, msg);
	addUint8ToMsg((gres->file ? 1 : 0), msg);
	addUint32ToMsg(gres->id, msg);
	addStringToMsg(gres->cpus, msg);
	addStringToMsg(gres->name, msg);
	addStringToMsg(gres->type, msg);
    }

    /* set real gres info size */
    ptr = msg->buf + startGresData;
    len = msg->bufUsed - startGresData - (2 * sizeof(uint32_t));

    *(uint32_t *)ptr = htonl(len);
    ptr += sizeof(uint32_t);
    *(uint32_t *)ptr = htonl(len);
}

static int setGresCount(Gres_Conf_t *gres, char *count)
{
    char *end;
    long gCount;

    if (!count) return 1;

    errno = 0;
    gCount = strtol(count, &end, 10);
    if (!gCount && errno != 0) {
	mwarn(errno, "%s: invalid count '%s' for '%s'", __func__, count,
	      gres->name);
	return 0;
    }
    if (gCount == LONG_MIN || gCount == LONG_MAX) {
	mlog("%s: invalid count '%s' for '%s'\n", __func__, count, gres->name);
	return 0;
    }
    if (end[0] == 'k' || end[0] == 'K') {
	gCount *= 1024;
    } else if (end[0] == 'm' || end[0] == 'M') {
	gCount *= (1024 * 1024);
    } else if (end[0] == 'g' || end[0] == 'G') {
	gCount *= (1024 * 1024 * 1024);
    } else if (end[0] != '\0') {
	mlog("%s: invalid count '%s' for '%s'\n", __func__, count, gres->name);
	return 0;
    }

    gres->count = gCount;
    return 1;
}

static int parseGresFile(Gres_Conf_t *gres, char *file)
{
    char *toksave, *next, *files;
    const char delimiters[] =",\n";
    struct stat sbuf;
    uint32_t count;

    gres->file = ustrdup(file);
    files = expandHostList(gres->file, &count);
    if (!files) {
	mlog("%s: invalid gres file '%s' for '%s'\n", __func__,
		gres->file, gres->name);
	return 0;
    }

    /* test single devices */
    count = 0;
    next = strtok_r(files, delimiters, &toksave);
    while (next) {
	if (stat(next, &sbuf) == -1) {
	    mlog("%s: invalid device '%s' for '%s'\n", __func__, next,
		 gres->name);
	} else {
	    count++;
	}
	next = strtok_r(NULL, delimiters, &toksave);
    }

    gres->count = count;
    ufree(files);
    return 1;
}

Gres_Conf_t *addGresConf(char *name, char *count, char *file, char *cpus)
{
    Gres_Conf_t *gres = umalloc(sizeof(*gres));

    gres->count = 1;
    gres->name = ustrdup(name);
    gres->id = getGresId(name);

    /* TODO support CPUs in gres */
    gres->cpus = NULL;
    gres->type = NULL;

    /* parse file */
    if (file) {
	if (!parseGresFile(gres, file)) goto GRES_ERROR;
    } else {
	gres->file = NULL;

	/* parse count */
	if (!setGresCount(gres, count)) goto GRES_ERROR;
    }

    mlog("%s: gres conf '%s' count '%lu' file '%s' cpus '%s' "
	 "id '%u'\n", __func__, gres->name, gres->count, gres->file,
	 gres->cpus, gres->id);
    list_add_tail(&gres->next, &GresConfList);

    return gres;

GRES_ERROR:
    ufree(gres->name);
    ufree(gres->cpus);
    ufree(gres->file);
    ufree(gres->type);
    return NULL;
}

void clearGresConf(void)
{
    list_t *g, *tmp;
    list_for_each_safe(g, tmp, &GresConfList) {
	Gres_Conf_t *gres = list_entry(g, Gres_Conf_t, next);
	ufree(gres->name);
	ufree(gres->cpus);
	ufree(gres->file);
	ufree(gres->type);

	list_del(&gres->next);
	ufree(gres);
    }
}

Gres_Cred_t* getGresCred(void)
{
    Gres_Cred_t *gres = ucalloc(sizeof(*gres));
    INIT_LIST_HEAD(&gres->next);

    return gres;
}

Gres_Cred_t * findGresCred(list_t *gresList, uint32_t id, int job)
{
    list_t *g;
    list_for_each(g, gresList) {
	Gres_Cred_t *gres = list_entry(g, Gres_Cred_t, next);
	if (gres->job == job && gres->id == id) return gres;
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
