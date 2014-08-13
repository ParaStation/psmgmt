/*
 * ParaStation
 *
 * Copyright (C) 2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pscommon.h"
#include "list.h"

#include "pspartition.h"

PSpart_request_t* PSpart_newReq()
{
    PSpart_request_t* request = malloc(sizeof(PSpart_request_t));

    if (request) PSpart_initReq(request);

    return request;
}

void PSpart_initReq(PSpart_request_t* request)
{
    if (!request) {
	PSC_log(-1, "%s: request is NULL\n", __func__);
	return;
    }

    INIT_LIST_HEAD(&request->next);
    request->tid = 0;
    request->size = 0;
    request->hwType = 0;
    request->uid = -1;
    request->gid = -1;
    request->sort = PART_SORT_UNKNOWN;
    request->options = 0;
    request->priority = 0;
    request->num = -1;
    request->tpp = 1;
    request->numGot = -1;
    request->sizeGot = 0;
    request->nodes = NULL;
    request->slots = NULL;
    request->deleted = 0;
    request->suspended = 0;
    request->freed = 0;
    request->resPorts = NULL;
}

void PSpart_reinitReq(PSpart_request_t* request)
{
    if (!request) {
	PSC_log(-1, "%s: request is NULL\n", __func__);
	return;
    }

    if (request->nodes) free(request->nodes);
    if (request->slots) free(request->slots);
    if (request->resPorts) free(request->resPorts);

    PSpart_initReq(request);
}

int PSpart_delReq(PSpart_request_t* request)
{
    if (!request) {
	PSC_log(-1, "%s: request is NULL\n", __func__);
	return 0;
    }

    PSpart_reinitReq(request);
    free(request);
    request = NULL;

    return 1;
}

void PSpart_snprintf(char* txt, size_t size, PSpart_request_t* request)
{
    int i;
    unsigned int u;

    if (!request) {
	PSC_log(-1, "%s: request is NULL\n", __func__);
	return;
    }

    snprintf(txt, size, "%stid 0x%08x size %d tpp %d hwType 0x%x uid %d gid %d"
	     " sort 0x%x options 0x%x priority %d num %d",
	     request->deleted ? "!DELETED! " : "",
	     request->tid, request->size, request->tpp, request->hwType,
	     request->uid, request->gid,
	     request->sort, request->options, request->priority, request->num);
    if (strlen(txt)+1 == size) return;

    snprintf(txt+strlen(txt), size-strlen(txt), " candidates (");
    if (strlen(txt)+1 == size) return;

    if (request->nodes) {
	/* raw request (no partition yet) */
	for (i=0; i<request->numGot; i++) {
	    snprintf(txt+strlen(txt), size-strlen(txt),
		     "%s%d", i ? " " : "",request->nodes[i]);
	    if (strlen(txt)+1 == size) return;
	}
    } else if (request->slots) {
	/* processed request */
	for (u=0; u<request->sizeGot; u++) {
	    snprintf(txt+strlen(txt), size-strlen(txt),
		     "%s%d/%s", u ? " " : "",
		     request->slots[u].node,
		     PSCPU_print(request->slots[u].CPUset));
	    if (strlen(txt)+1 == size) return;
	}
    }

    snprintf(txt+strlen(txt), size-strlen(txt), ")");
}

typedef struct {
    uint32_t size;
    uint32_t hwType;
    uid_t uid;
    gid_t gid;
    PSpart_sort_t sort;
    PSpart_option_t options;
    uint32_t priority;
    int32_t num;
} tmpRequest_400_t;

typedef struct {
    uint32_t size;
    uint32_t hwType;
    uid_t uid;
    gid_t gid;
    PSpart_sort_t sort;
    PSpart_option_t options;
    uint32_t priority;
    int32_t num;
    uint16_t tpp;
} tmpRequest_406_t;

static struct {
    uint32_t size;
    uint32_t hwType;
    uid_t uid;
    gid_t gid;
    PSpart_sort_t sort;
    PSpart_option_t options;
    uint32_t priority;
    int32_t num;
    uint16_t tpp;
    int64_t start;
} tmpRequest;

static char partString[256];

size_t PSpart_encodeReq(char* buffer, size_t size, PSpart_request_t* request,
			int daemonProtoVersion)
{
    size_t length =  sizeof(tmpRequest);

    if (!request) {
	PSC_log(-1, "%s: request is NULL\n", __func__);
	return 0;
    }

    PSpart_snprintf(partString, sizeof(partString), request);
    PSC_log(PSC_LOG_PART, "%s(%p, %ld, request (%s), %d)\n",
	    __func__, buffer, (long)size, partString, daemonProtoVersion);

    if (size >= sizeof(tmpRequest)) {
	tmpRequest.size = request->size;
	tmpRequest.hwType = request->hwType;
	tmpRequest.uid = request->uid;
	tmpRequest.gid = request->gid;
	tmpRequest.sort = request->sort;
	tmpRequest.options = request->options;
	tmpRequest.priority = request->priority;
	tmpRequest.num = request->num;
	tmpRequest.tpp = request->tpp;
	tmpRequest.start = request->start;

	memcpy(buffer, &tmpRequest, sizeof(tmpRequest));
    } else {
	PSC_log(-1, "%s: Buffer (size %ld) too small\n", __func__, (long)size);
    }

    /* size settings for compatibility */
    if (daemonProtoVersion < 401) {
	length = sizeof(tmpRequest_400_t);
    } else if (daemonProtoVersion < 407) {
	length = sizeof(tmpRequest_406_t);
    }

    return length;
}

size_t PSpart_decodeReq(char* buffer, PSpart_request_t* request,
			int daemonProtoVersion)
{
    size_t length =  sizeof(tmpRequest);

    if (!request) {
	PSC_log(-1, "%s: request is NULL\n", __func__);
	return 0;
    }

    PSC_log(PSC_LOG_PART, "%s(%p, %p, %d)", __func__, buffer, request,
	    daemonProtoVersion);

    PSpart_reinitReq(request);

    /* unpack buffer */
    memcpy(&tmpRequest, buffer, sizeof(tmpRequest));

    request->size = tmpRequest.size;
    request->hwType = tmpRequest.hwType;
    request->uid = tmpRequest.uid;
    request->gid = tmpRequest.gid;
    request->sort = tmpRequest.sort;
    request->options = tmpRequest.options;
    request->priority = tmpRequest.priority;
    request->num = tmpRequest.num;
    request->tpp = tmpRequest.tpp;
    request->start = tmpRequest.start;

    PSpart_snprintf(partString, sizeof(partString), request);
    PSC_log(PSC_LOG_PART, " received request = (%s)\n", partString);

    /* size settings for compatibility */
    if (daemonProtoVersion < 401) {
	request->tpp = 1;
	length = sizeof(tmpRequest_400_t);
    } else if (daemonProtoVersion < 407) {
	request->start = time(NULL);
	length = sizeof(tmpRequest_406_t);
    }

    return length;
}
