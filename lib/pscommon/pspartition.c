/*
 *               ParaStation
 *
 * Copyright (C) 2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pscommon.h"

#include "pspartition.h"

static char errtxt[512]; /**< General string to create error messages */

PSpart_request_t *PSpart_newReq()
{
    PSpart_request_t *request = malloc(sizeof(PSpart_request_t));

    if (request) PSpart_initReq(request);

    return request;
}

void PSpart_initReq(PSpart_request_t *request)
{
    if (request) *request = (PSpart_request_t) {
	.next = NULL,
	.tid = 0,
	.size = 0,
	.hwType = 0,
	.uid = -1,
	.gid = -1,
	.sort = PART_SORT_UNKNOWN,
	.options = 0,
	.priority = 0,
	.num = -1,
	.numGot = -1,
	.nodes = NULL,
	.deleted = 0,
	.suspended = 0,
	.freed = 0, };
}

void PSpart_reinitReq(PSpart_request_t *request)
{
    if (!request) return;

    if (request->nodes) free(request->nodes);

    PSpart_initReq(request);
}

int PSpart_delReq(PSpart_request_t * request)
{
    if (!request) return 0;

    PSpart_reinitReq(request);
    free(request);
    request = NULL;

    return 1;
}

void PSpart_snprintf(char *txt, size_t size, PSpart_request_t *request)
{
    int i;

    if (!request) return;

    snprintf(txt, size, "%stid 0x%08x size %d hwType 0x%x uid %d gid %d"
	     " sort 0x%x options 0x%x priority %d num %d",
	     request->deleted ? "!DELETED! " : "",
	     request->tid, request->size, request->hwType,
	     request->uid, request->gid,
	     request->sort, request->options, request->priority, request->num);
    if (strlen(txt)+1 == size) return;

    snprintf(txt+strlen(txt), size-strlen(txt), " candidates (");
    if (strlen(txt)+1 == size) return;

    for (i=0; i<request->numGot; i++) {
	snprintf(txt+strlen(txt), size-strlen(txt),
		 "%s%d", i ? " " : "",request->nodes[i]);
	if (strlen(txt)+1 == size) return;
    }

    snprintf(txt+strlen(txt), size-strlen(txt), ")");
}

static struct {
    uint32_t size;
    uint32_t hwType;
    uid_t uid;
    gid_t gid;
    PSpart_sort_t sort;
    PSpart_option_t options;
    uint32_t priority;
    int32_t num;
} tmpRequest;
    
size_t PSpart_encodeReq(char *buffer, size_t size, PSpart_request_t *request)
{
    snprintf(errtxt, sizeof(errtxt), "%s(%p, %ld, request(", __func__,
	     buffer, (long)size);
    PSpart_snprintf(errtxt+strlen(errtxt),
		    sizeof(errtxt)-strlen(errtxt), request);
    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt), ")");
    PSC_errlog(errtxt, 10);

    if (size >= sizeof(tmpRequest)) {
	tmpRequest.size = request->size;
	tmpRequest.hwType = request->hwType;
	tmpRequest.uid = request->uid;
	tmpRequest.gid = request->gid;
	tmpRequest.sort = request->sort;
	tmpRequest.options = request->options;
	tmpRequest.priority = request->priority;
	tmpRequest.num = request->num;

	memcpy(buffer, &tmpRequest, sizeof(tmpRequest));
    }

    return sizeof(tmpRequest);
}

int PSpart_decodeReq(char *buffer, PSpart_request_t *request)
{
    int msglen, len, count, i;

    snprintf(errtxt, sizeof(errtxt), "%s(%p)", __func__, buffer);
    PSC_errlog(errtxt, 10);

    if (!request) return 0;

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

    return sizeof(tmpRequest);
}
