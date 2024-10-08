/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
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

#include "pscomplist.h"

#include "pluginmalloc.h"

#include "slurmcommon.h"
#include "psslurmlog.h"

/** List of all GRES configurations */
static LIST_HEAD(GresConfList);

uint32_t GRes_getHash(char *name)
{
    if (!name) return 0;

    uint32_t hash = 0;
    for (uint32_t i = 0, x = 0; name[i]; i++) {
	hash += (name[i] << x);
	x = (x + 8) % 32;
    }

    return hash;
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
    gres->hash = GRes_getHash(gres->name);
    /* use continuing device IDs */
    gres->nextDevID = GRes_countDevices(gres->hash);

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
	flog("error: GRES cores feature is unsupported and could lead to "
	     "performance issues\n");
	goto GRES_ERROR;
    }

    flog("%s hash=%u count=%lu%s%s%s%s%s%s%s%s\n",
	 gres->name, gres->hash, gres->count,
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

const char *GRes_getNamebyHash(uint32_t hash)
{
    list_t *g;
    list_for_each(g, &GresConfList) {
	Gres_Conf_t *gres = list_entry(g, Gres_Conf_t, next);
	if (gres->hash == hash) return gres->name;
    }

    return "unknown";
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

Gres_Cred_t *findGresCred(list_t *gresList, uint32_t hash,
			  GRes_Cred_type_t credType)
{
    Gres_Cred_t *ret = NULL;

    list_t *g;
    list_for_each(g, gresList) {
	Gres_Cred_t *gres = list_entry(g, Gres_Cred_t, next);
	if (gres->credType == credType &&
	    (gres->hash == hash || hash == NO_VAL)) {
	    ret = gres;
	    break;
	}
    }

    if (mset(PSSLURM_LOG_GRES)) {
	if (ret) {
	    flog("credType %d hash %u cpusPerGres %u gresPerStep %lu"
		 " gresPerNode %lu gresPerSocket %lu gresPerTask %lu"
		 " memPerGres %lu totalGres %lu nodeInUse %s typeName %s"
		 " typeID %u\n", credType, hash,
		 ret->cpusPerGRes, ret->gresPerStep, ret->gresPerNode,
		 ret->gresPerSocket, ret->gresPerTask, ret->memPerGRes,
		 ret->totalGres, ret->nodeInUse, ret->typeName, ret->typeID);

	    if (ret->bitAlloc) {
		for (size_t i = 0; i < ret->nodeCount; i++) {
		    flog("node '%zu' bit_alloc '%s'\n", i, ret->bitAlloc[i]);
		}
	    }
	} else {
	    flog("no GRes credential of type %d found\n", credType);
	}
    }

    return ret;
}

void releaseGresCred(Gres_Cred_t *gres)
{
    if (!gres) return;

    if (gres->bitAlloc) {
	for (uint32_t i = 0; i < gres->nodeCount; i++) ufree(gres->bitAlloc[i]);
	ufree(gres->bitAlloc);
    }

    if (gres->perBitAlloc) {
	for (uint32_t i = 0; i < gres->nodeCount; i++) {
	    ufree(gres->perBitAlloc[i]);
	}
	ufree(gres->perBitAlloc);
    }

    if (gres->bitStepAlloc) {
	for (uint32_t i = 0; i < gres->nodeCount; i++) {
	    ufree(gres->bitStepAlloc[i]);
	}
	ufree(gres->bitStepAlloc);
    }

    if (gres->stepPerBitAlloc) {
	for (uint32_t i = 0; i < gres->nodeCount; i++) {
	    ufree(gres->stepPerBitAlloc[i]);
	}
	ufree(gres->stepPerBitAlloc);
    }

    ufree(gres->countStepAlloc);
    ufree(gres->nodeInUse);
    ufree(gres->typeModel);
    ufree(gres->typeName);
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

bool traverseGResDevs(uint32_t hash, GResDevVisitor_t visitor, void *info)
{
    list_t *c;
    list_for_each(c, &GresConfList) {
	Gres_Conf_t *conf = list_entry(c, Gres_Conf_t, next);
	if (conf->hash != hash) continue;

	list_t *d, *tmp;
	list_for_each_safe(d, tmp, &conf->devices) {
	    GRes_Dev_t *dev = list_entry(d, GRes_Dev_t, next);
	    if (visitor(dev, hash, info)) return true;
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

uint32_t GRes_countDevices(uint32_t hash)
{
    uint32_t numDev = 0;

    list_t *c;
    list_for_each(c, &GresConfList) {
	Gres_Conf_t *conf = list_entry(c, Gres_Conf_t, next);
	if (conf->hash == hash) numDev += conf->count;
    }

    return numDev;
}

const char *GRes_strType(GRes_Cred_type_t type)
{
    switch (type) {
	case GRES_CRED_STEP:
	    return "STEP";
	case GRES_CRED_JOB:
	    return "JOB";
	default:
	    ;static char buf[128];
	    snprintf(buf, sizeof(buf), "unknown(%i)", type);
	    return buf;
    }
}
