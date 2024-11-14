/*
 * ParaStation
 *
 * Copyright (C) 2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2019 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pspartition.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "list.h"
#include "pscommon.h"
#include "psserial.h"

PSpart_request_t* PSpart_newReq(void)
{
    PSpart_request_t* request = malloc(sizeof(PSpart_request_t));

    if (request) PSpart_initReq(request);

    return request;
}

void PSpart_initReq(PSpart_request_t* request)
{
    if (!request) {
	PSC_flog("no request\n");
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
    request->nodes = NULL;
    request->slots = NULL;
    request->deleted = false;
    request->suspended = false;
    request->freed = false;
}

void PSpart_reinitReq(PSpart_request_t* request)
{
    if (!request) {
	PSC_flog("no request\n");
	return;
    }

    free(request->nodes);
    free(request->slots);

    PSpart_initReq(request);
}

bool PSpart_delReq(PSpart_request_t* request)
{
    if (!request) {
	PSC_flog("no request\n");
	return false;
    }

    PSpart_reinitReq(request);
    free(request);

    return true;
}

void PSpart_clrQueue(list_t *queue)
{
    PSC_fdbg(PSC_LOG_PART, "queue %p\n", queue);

    list_t *r, *tmp;
    list_for_each_safe(r, tmp, queue) {
	PSpart_request_t *req = list_entry(r, PSpart_request_t, next);
	list_del(&req->next);
	PSC_fdbg(PSC_LOG_PART, "request %s\n", PSC_printTID(req->tid));
	PSpart_delReq(req);
    }
}

void PSpart_snprintf(char* txt, size_t size, PSpart_request_t* request)
{
    if (!request) {
	PSC_flog("no request\n");
	return;
    }

    snprintf(txt, size, "%stid %s size %u tpp %d hwType 0x%x uid %d gid %d"
	     " sort 0x%x options 0x%x priority %u num %d",
	     request->deleted ? "!DELETED! " : "", PSC_printTID(request->tid),
	     request->size, request->tpp, request->hwType,
	     request->uid, request->gid,
	     request->sort, request->options, request->priority, request->num);
    if (strlen(txt)+1 == size) return;

    snprintf(txt+strlen(txt), size-strlen(txt), " candidates (");
    if (strlen(txt)+1 == size) return;

    if (request->nodes) {
	/* raw request (no partition yet) */
	for (uint32_t n = 0; n < request->num; n++) {
	    snprintf(txt+strlen(txt), size-strlen(txt),
		     "%s%d", n ? " " : "",request->nodes[n]);
	    if (strlen(txt)+1 == size) return;
	}
    } else if (request->slots) {
	/* processed request */
	for (uint32_t s = 0; s < request->size; s++) {
	    snprintf(txt+strlen(txt), size-strlen(txt), "%s%d/%s", s ? " " : "",
		     request->slots[s].node,
		     PSCPU_print(request->slots[s].CPUset));
	    if (strlen(txt)+1 == size) return;
	}
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
    uint32_t num;
    uint16_t tpp;
    int64_t start;
} tmpRequest;

static char partString[256];

bool PSpart_encodeReq(DDBufferMsg_t *msg, PSpart_request_t* request)
{
    size_t off = msg->header.len - DDBufferMsgOffset;

    if (!request) {
	PSC_flog("no request\n");
	return 0;
    }

    PSpart_snprintf(partString, sizeof(partString), request);
    PSC_fdbg(PSC_LOG_PART, "msg %p request %s\n", msg, partString);

    if (sizeof(tmpRequest) > sizeof(msg->buf) - off) {
	PSC_flog("request %s too large\n", partString);
	return false;
    }

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

    memcpy(msg->buf + off, &tmpRequest, sizeof(tmpRequest));

    msg->header.len += sizeof(tmpRequest);

    return true;
}

size_t PSpart_decodeReqOld(char* buffer, PSpart_request_t* request)
{
    size_t length =  sizeof(tmpRequest);

    if (!request) {
	PSC_flog("no request\n");
	return 0;
    }

    PSC_fdbg(PSC_LOG_PART, "buffer %p request %p", buffer, request);

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
    PSC_dbg(PSC_LOG_PART, " received request = (%s)\n", partString);

    return length;
}

bool PSpart_addToMsg(PSpart_request_t* request, PS_SendDB_t *msg)
{
    if (!request) {
	PSC_flog("no request\n");
	return false;
    }

    if (PSC_getDebugMask() & PSC_LOG_PART) {
	PSpart_snprintf(partString, sizeof(partString), request);
	PSC_flog("msg %p request %s\n", msg, partString);
    }

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

    if (!addDataToMsg(&tmpRequest, sizeof(tmpRequest), msg)) return false;

    return true;
}


bool PSpart_decodeReq(void *data, size_t len, PSpart_request_t *req)
{
    if (!req) {
	PSC_flog("no request\n");
	return false;
    }

    if (!data || len != sizeof(tmpRequest)) {
	PSC_flog("insufficient data\n");
	return false;
    }

    PSC_fdbg(PSC_LOG_PART, "buffer %p request %p", data, req);

    PSpart_reinitReq(req);

    /* unpack buffer */
    memcpy(&tmpRequest, data, sizeof(tmpRequest));

    req->size = tmpRequest.size;
    req->hwType = tmpRequest.hwType;
    req->uid = tmpRequest.uid;
    req->gid = tmpRequest.gid;
    req->sort = tmpRequest.sort;
    req->options = tmpRequest.options;
    req->priority = tmpRequest.priority;
    req->num = tmpRequest.num;
    req->tpp = tmpRequest.tpp;
    req->start = tmpRequest.start;

    if (PSC_getDebugMask() & PSC_LOG_PART) {
	PSpart_snprintf(partString, sizeof(partString), req);
	PSC_log(" received request = (%s)\n", partString);
    }

    return true;
}
