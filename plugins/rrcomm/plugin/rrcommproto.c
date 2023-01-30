/*
 * ParaStation
 *
 * Copyright (C) 2022-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "rrcommproto.h"

#include "pscommon.h"
#include "pspluginprotocol.h"
#include "psserial.h"

#include "rrcomm_common.h"
#include "rrcommlog.h"

bool __dropHelper(DDTypedBufferMsg_t *msg, ssize_t (sendFunc)(void *msg),
		  const char *caller)
{
    RRComm_hdr_t *hdr;
    uint8_t fragType;
    size_t used = 0, hdrSize;
    fetchFragHeader(msg, &used, &fragType, NULL, (void **)&hdr, &hdrSize);

    switch (msg->type) {
    case RRCOMM_ERROR:
	mdbg(RRCOMM_LOG_ERR, "%s: error %s->", caller,
	     PSC_printTID(msg->header.sender));
	mdbg(RRCOMM_LOG_ERR, "%s\n", PSC_printTID(msg->header.dest));
	return true;
    case RRCOMM_DATA:
	break;
    default:
	mlog("%s: unknown type %d\n", caller, msg->type);
	return true;
    }

    /* Silently drop non-data fragments and all fragments but the last */
    if (msg->type != RRCOMM_DATA || fragType != FRAGMENT_END) return true;

    DDTypedBufferMsg_t answer = {
	.header = {
	    .type = PSP_PLUG_RRCOMM,
	    .sender = msg->header.dest,
	    .dest = msg->header.sender,
	    .len = 0, /* to be set by PSP_putTypedMsgBuf */ },
	.type = RRCOMM_ERROR,
	.buf = { '\0' } };
    /* Add all information we have concerning the message */
    PSP_putTypedMsgBuf(&answer, "hdr", hdr, sizeof(*hdr));

    mdbg(RRCOMM_LOG_ERR, "%s: send error on %d to %s\n", caller, hdr->dest,
	 PSC_printTID(msg->header.sender));

    sendFunc(&answer);
    return true;
}
