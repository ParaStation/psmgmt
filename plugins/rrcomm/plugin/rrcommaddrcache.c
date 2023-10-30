/*
 * ParaStation
 *
 * Copyright (C) 2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file RRComm address cache used within the psidforwarder. This
 * allows sending directly to the destination task without the need of
 * extra routing steps once a message was received from there
 */
#include "rrcommaddrcache.h"

#include <stdlib.h>

#include "list.h"

typedef struct {
    list_t next;             /**< used to put into jobCaches */
    PStask_ID_t jobID;       /**< job this cache belongs to */
    int32_t size;            /**< current size of this cache */
    PStask_ID_t *addrCache;  /**< cache's entries */
} RRC_cache_t;

/** Local jobs address cache */
static RRC_cache_t thisJobCache = { .size = 0, .addrCache = NULL};

/** List of address caches of sister jobs in the same session */
static LIST_HEAD(jobCaches);

void initAddrCache(PStask_ID_t jobID)
{
    thisJobCache.jobID = jobID;
}

static RRC_cache_t * findCache(PStask_ID_t jobID)
{
    if (!jobID || jobID == thisJobCache.jobID) return &thisJobCache;

    list_t *c;
    list_for_each(c, &jobCaches) {
	RRC_cache_t *thisC = list_entry(c, RRC_cache_t, next);
	if (thisC->jobID == jobID) return thisC;
    }

    return NULL;
}

static RRC_cache_t * getCache(PStask_ID_t jobID)
{
    RRC_cache_t *thisC = findCache(jobID);

    if (!thisC) {
	thisC = malloc(sizeof(*thisC));

	*thisC = (RRC_cache_t) {
	    .jobID = jobID,
	    .size = 0,
	    .addrCache = NULL,
	};

	list_add_tail(&thisC->next, &jobCaches);
    }

    return thisC;
}

void updateAddrCache(PStask_ID_t jobID, int32_t rank, PStask_ID_t taskID)
{
    RRC_cache_t *thisC = getCache(jobID);

    if (rank >= thisC->size) {
	size_t newSize = (rank / 256 + 1) * 256;
	PStask_ID_t *tmp = realloc(thisC->addrCache, sizeof(*tmp) * newSize);
	if (!tmp) return;
	for (size_t i = thisC->size; i < newSize; i++) tmp[i] = -1;
	thisC->addrCache = tmp;
	thisC->size = newSize;
    }
    thisC->addrCache[rank] = taskID;
}

PStask_ID_t getAddrFromCache(PStask_ID_t jobID, int32_t rank)
{
    RRC_cache_t *thisC = findCache(jobID);
    if (thisC && rank >= 0 && rank < thisC->size) return thisC->addrCache[rank];
    return -1;
}

void clearAddrCache(void)
{
    list_t *c, *tmp;
    list_for_each_safe(c, tmp, &jobCaches) {
	RRC_cache_t *thisCache = list_entry(c, RRC_cache_t, next);

	free(thisCache->addrCache);
	list_del(&thisCache->next);
	free(thisCache);
    }
    free(thisJobCache.addrCache);
    thisJobCache = (RRC_cache_t) { .size = 0, .addrCache = NULL};
}
