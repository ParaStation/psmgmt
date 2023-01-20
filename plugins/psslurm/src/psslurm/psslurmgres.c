/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmgres.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include "pshostlist.h"

#include "pluginmalloc.h"

#include "slurmcommon.h"
#include "psslurmlog.h"

/** List of all GRES configurations */
static LIST_HEAD(GresConfList);

static uint32_t getGresId(char *name)
{
    uint32_t gresId = 0;
    for (uint32_t i = 0, x = 0; name[i]; i++) {
	gresId += (name[i] << x);
	x = (x + 8) % 32;
    }

    return gresId;
}

static bool setGresCount(Gres_Conf_t *gres, char *count)
{
    if (!count) {
	gres->count = 1;
	return true;
    }

    char *end;
    errno = 0;

    long gCount = strtol(count, &end, 10);
    if (!gCount && errno != 0) {
	mwarn(errno, "%s: invalid count '%s' for '%s'", __func__,
	      count, gres->name);
	return false;
    }
    if (gCount == LONG_MIN || gCount == LONG_MAX) {
	flog("invalid count '%s' for '%s'\n", count, gres->name);
	return false;
    }
    if (end[0] == 'k' || end[0] == 'K') {
	gCount *= 1024;
    } else if (end[0] == 'm' || end[0] == 'M') {
	gCount *= (1024 * 1024);
    } else if (end[0] == 'g' || end[0] == 'G') {
	gCount *= (1024 * 1024 * 1024);
    } else if (end[0] != '\0') {
	flog("invalid count '%s' for '%s'\n", count, gres->name);
	return false;
    }

    gres->count = gCount;
    return true;
}

static bool discoverDevices(char *file, void *info)
{
    struct stat sbuf;
    Gres_Conf_t *gres = info;

    if (stat(file, &sbuf) == -1) {
	flog("invalid GRes device '%s' for '%s'\n", file, gres->name);
    } else {
	GRes_Dev_t *gDev = umalloc(sizeof(*gDev));
	gDev->major = major(sbuf.st_rdev);
	gDev->minor = minor(sbuf.st_rdev);
	gDev->slurmIdx = gres->nextDevID + gres->count++;
	gDev->path = ustrdup(file);
	gDev->isBlock = S_ISBLK(sbuf.st_mode);
	list_add_tail(&gDev->next, &gres->devices);

	fdbg(PSSLURM_LOG_GRES, "GRes device %s major %u minor %u "
	     "index %i type %s\n", gDev->path, gDev->major, gDev->minor,
	     gDev->slurmIdx, gres->type);
    }

    return true;
}

static bool parseGresFile(Gres_Conf_t *gres)
{
    /* discover all devices */
    if (!traverseCompList(gres->file, discoverDevices, gres)) {
	flog("invalid gres file '%s' for '%s'\n", gres->file, gres->name);
	return false;
    }

    return true;
}

static void freeGresConf(Gres_Conf_t *gres)
{
    ufree(gres->name);
    ufree(gres->cpus);
    ufree(gres->file);
    ufree(gres->type);
    ufree(gres->cores);
    ufree(gres->strFlags);
    ufree(gres->links);

    list_t *d, *tmp;
    list_for_each_safe(d, tmp, &gres->devices) {
	GRes_Dev_t *dev = list_entry(d, GRes_Dev_t, next);
	ufree(dev->path);
	list_del(&dev->next);
	ufree(dev);
    }

    ufree(gres);
}

Gres_Conf_t *saveGresConf(Gres_Conf_t *gres, char *count)
{
    gres->id = getGresId(gres->name);
    /* use continuing device IDs */
    gres->nextDevID = GRes_countDevices(gres->id);

    /* parse file */
    INIT_LIST_HEAD(&gres->devices);
    if (gres->file) {
	if (!parseGresFile(gres)) goto GRES_ERROR;
	gres->flags |= GRES_CONF_HAS_FILE;
    } else {
	/* parse count */
	if (!setGresCount(gres, count)) goto GRES_ERROR;
    }

    if (gres->type) gres->flags |= GRES_CONF_HAS_TYPE;

    if (gres->cores) {
	flog("GRES cores feature currently unsupported, ignoring it\n");
    }

    flog("%s id=%u count=%lu%s%s%s%s%s%s%s%s\n",
	 gres->name, gres->id, gres->count,
	 gres->file ? " file=" : "", gres->file ? gres->file : "",
	 gres->type ? " type=" : "", gres->type ? gres->type : "",
	 gres->cores ? " cores=" : "", gres->cores ? gres->cores : "",
	 gres->links ? " links=" : "", gres->links ? gres->links : "");

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

Gres_Conf_t *findGresConf(uint32_t id)
{
    list_t *g;
    list_for_each(g, &GresConfList) {
	Gres_Conf_t *gres = list_entry(g, Gres_Conf_t, next);
	if (gres->id == id) return gres;
    }
    return NULL;
}

void clearGresConf(void)
{
    list_t *c, *tmp;
    list_for_each_safe(c, tmp, &GresConfList) {
	Gres_Conf_t *conf = list_entry(c, Gres_Conf_t, next);

	list_del(&conf->next);
	freeGresConf(conf);
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
	for (uint32_t i = 0; i < gres->nodeCount; i++) ufree(gres->bitAlloc[i]);
	ufree(gres->bitAlloc);
    }

    if (gres->bitStepAlloc) {
	for (uint32_t i = 0; i < gres->nodeCount; i++) ufree(gres->bitStepAlloc[i]);
	ufree(gres->bitStepAlloc);
    }

    ufree(gres->countStepAlloc);
    ufree(gres->nodeInUse);
    ufree(gres->typeModel);
    ufree(gres);
}

void freeGresCred(list_t *gresList)
{
    if (!gresList) return;

    list_t *g, *tmp;
    list_for_each_safe(g, tmp, gresList) {
	Gres_Cred_t *gres = list_entry(g, Gres_Cred_t, next);

	list_del(&gres->next);
	releaseGresCred(gres);
    }
}

bool traverseGresConf(GresConfVisitor_t visitor, void *info)
{
    list_t *c, *tmp;
    list_for_each_safe(c, tmp, &GresConfList) {
	Gres_Conf_t *conf = list_entry(c, Gres_Conf_t, next);

	if (visitor(conf, info)) return true;
    }

    return false;
}

bool traverseGResDevs(uint32_t id, GResDevVisitor_t visitor, void *info)
{
    list_t *c, *tmp;
    list_for_each_safe(c, tmp, &GresConfList) {
	Gres_Conf_t *conf = list_entry(c, Gres_Conf_t, next);
	if (conf->id != id) continue;

	list_t *d, *tmp2;
	list_for_each_safe(d, tmp2, &conf->devices) {
	    GRes_Dev_t *dev = list_entry(d, GRes_Dev_t, next);
	    if (visitor(dev, id, info)) return true;
	}
    }

    return false;
}

void freeGresJobAlloc(list_t *gresList)
{
    if (!gresList) return;

    list_t *g, *tmp;
    list_for_each_safe(g, tmp, gresList) {
	Gres_Job_Alloc_t *gres = list_entry(g, Gres_Job_Alloc_t, next);

	list_del(&gres->next);
	if (gres->bitAlloc) {
	    for (uint32_t i=0; i<gres->nodeCount; i++) {
		ufree(gres->bitAlloc[i]);
	    }
	}
	ufree(gres->bitAlloc);
	ufree(gres->nodeAlloc);
	ufree(gres);
    }
}

uint32_t GRes_countDevices(uint32_t pluginID)
{
    uint32_t numDev = 0;

    list_t *c;
    list_for_each(c, &GresConfList) {
	Gres_Conf_t *conf = list_entry(c, Gres_Conf_t, next);
	if (conf->id == pluginID) numDev += conf->count;
    }

    return numDev;
}
