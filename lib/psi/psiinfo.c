/*
 *               ParaStation
 * psiinfo.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psiinfo.c,v 1.2 2003/11/28 15:50:50 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psiinfo.c,v 1.2 2003/11/28 15:50:50 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>

#include "pscommon.h"
#include "psprotocol.h"
#include "pstask.h"

#include "psi.h"
#include "psilog.h"

#include "psiinfo.h"

static char errtxt[128];

/**
 * @brief Receive and handle info message.
 *
 * Receive and handle info messages and store the content into @a
 * buf. This is a helper function for the @ref PSI_infoInt(), @ref
 * PSI_infoString(), @ref PSI_infoTaskID(), @ref PSI_infoNodeID() and
 * @ref PSI_infoList() functions.
 *
 * If the size @a size of the buffer @a buf is sufficiently large, the
 * content of the received indo message will be stored within @a
 * buf. @a size is set to the number of bytes stored to @a buf.
 *
 * The actual type of the info message will be returned.
 *
 *
 * @param buf The buffer to store the content of the info message to.
 *
 * @param size The actual size of the buffer @a buf. On return the
 * number of bytes received from the info message and stored to @a
 * buf.
 *
 * @param verbose Flag to enable more verbose output.
 *
 * @return On success the type of the info message received will be
 * returned. Otherwise PSP_INFO_UNKNOWN is returned.
 */
static PSP_Info_t receiveInfo(void *buf, size_t *size, int verbose)
{
    DDTypedBufferMsg_t msg;
    PSP_Info_t ret;

    if (PSI_recvMsg(&msg)<0) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "%s: PSI_recvMsg: %s", __func__,
		 errstr);
	PSI_errlog(errtxt, 0);
 	*size = 0;
	return PSP_INFO_UNKNOWN;
   }

    switch (msg.header.type) {
    case PSP_CD_INFORESPONSE:
    {
	ret = msg.type;
	switch (msg.type) {
	case PSP_INFO_LIST_END:
	    *size = 0;
	    break;
	case PSP_INFO_NROFNODES:
	case PSP_INFO_INSTDIR:
	case PSP_INFO_DAEMONVER:
	case PSP_INFO_HOST:
	case PSP_INFO_NODE:
	case PSP_INFO_RDPSTATUS:
	case PSP_INFO_MCASTSTATUS:
	case PSP_INFO_COUNTHEADER:
	case PSP_INFO_COUNTSTATUS:
	case PSP_INFO_HWNUM:
	case PSP_INFO_HWINDEX:
	case PSP_INFO_HWNAME:
	case PSP_INFO_RANKID:
	case PSP_INFO_TASKSIZE:
	case PSP_INFO_TASKRANK:
	case PSP_INFO_PARENTTID:
	case PSP_INFO_LOGGERTID:
	case PSP_INFO_LIST_HOSTSTATUS:
	case PSP_INFO_LIST_VIRTCPUS:
	case PSP_INFO_LIST_PHYSCPUS:
	case PSP_INFO_LIST_HWSTATUS:
	case PSP_INFO_LIST_LOAD:
	case PSP_INFO_LIST_ALLJOBS:
	case PSP_INFO_LIST_NORMJOBS:
	case PSP_INFO_LIST_ALLTASKS:
	case PSP_INFO_LIST_NORMTASKS:
	{
	    size_t s = msg.header.len - sizeof(msg.header) - sizeof(msg.type);
	    if (!buf) {
		snprintf(errtxt, sizeof(errtxt),
			 "%s: No buffer provided.", __func__);
		PSI_errlog(errtxt, 1);
		*size = 0;
		break;
	    }
	    if (*size < s) {
		snprintf(errtxt, sizeof(errtxt),
			 "%s: buffer to small.", __func__);
		PSI_errlog(errtxt, 0);
		*size = 0;
		break;
	    }
	    *size = s;
	    memcpy(buf, msg.buf, *size);
	    break;
	}
	case PSP_INFO_UNKNOWN:
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: daemon does not know info.", __func__);
	    PSI_errlog(errtxt, 0);
	    *size = 0;
	    break;
	default:
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: received unexpected info type '%s'.",
		     __func__, PSP_printInfo(msg.type));
	    PSI_errlog(errtxt, 0);
	    *size = 0;
	    ret = PSP_INFO_UNKNOWN;
	}
	break;
    }
    case PSP_CD_ERROR:
    {
	char *errstr = strerror(((DDErrorMsg_t*)&msg)->error);
	snprintf(errtxt, sizeof(errtxt), "%s: error in command %s : %s",
		 __func__, PSP_printMsg(((DDErrorMsg_t*)&msg)->request),
		 errstr ? errstr : "UNKNOWN");
	PSI_errlog(errtxt, verbose ? 0 : 1);
	*size = 0;
	ret = PSP_INFO_UNKNOWN;
	break;
    }
    default:
	snprintf(errtxt, sizeof(errtxt),
		 "%s: received unexpected msgtype '%s'.",
		 __func__, PSP_printMsg(msg.header.type));
	PSI_errlog(errtxt, 0);
	*size = 0;
	ret = PSP_INFO_UNKNOWN;
    }

    return ret;
}

int PSI_infoInt(PSnodes_ID_t node, PSP_Info_t what, const void *param,
		int32_t *val, int verbose)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_INFOREQUEST,
	    .dest = PSC_getTID(node, 0),
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg.header)+sizeof(msg.type) },
	.type = what,
	.buf = { 0 } };
    size_t size = sizeof(*val);

    switch (what) {
    case PSP_INFO_NODE:
	if (param) {
	    *(PSnodes_ID_t *)msg.buf = *(const PSnodes_ID_t *)param;
	    msg.header.len += sizeof(PSnodes_ID_t);
	} else {
	    snprintf(errtxt, sizeof(errtxt), "%s: %s request needs parameter.",
		     __func__, PSP_printInfo(what));
	    PSI_errlog(errtxt, 0);
	    return -1;
	}
	break;
    case PSP_INFO_HWINDEX:
	if (param) {
	    strncpy(msg.buf, (const char *)param, sizeof(msg.buf));
	    msg.buf[sizeof(msg.buf)-1] = '\0';
	    msg.header.len += strlen(msg.buf)+1;
	} else {
	    snprintf(errtxt, sizeof(errtxt), "%s: %s request needs parameter.",
		     __func__, PSP_printInfo(what));
	    PSI_errlog(errtxt, 0);
	    return -1;
	}
	break;
    case PSP_INFO_TASKSIZE:
    case PSP_INFO_NROFNODES:
    case PSP_INFO_HWNUM:
    case PSP_INFO_TASKRANK:
	break;
    default:
	snprintf(errtxt, sizeof(errtxt),
		 "%s: don't know how to handle '%s' request",
		 __func__, PSP_printInfo(what));
	PSI_errlog(errtxt, 0);
	return -1;
    }

    if (PSI_sendMsg(&msg)<0) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "%s(%s): PSI_sendMsg: %s", __func__,
		 PSP_printInfo(what), errstr);
	PSI_errlog(errtxt, 0);
	return -1;
    }

    if (receiveInfo(val, &size, verbose) == what && size) return 0;

    return -1;
}

int PSI_infoString(PSnodes_ID_t node, PSP_Info_t what, const void *param,
		   char *string, size_t size, int verbose)
{
    DDTypedBufferMsg_t msg = (DDTypedBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_INFOREQUEST,
	    .dest = PSC_getTID(node, 0),
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg.header)+sizeof(msg.type) },
	.type = what,
	.buf = { 0 } };

    switch (what) {
    case PSP_INFO_COUNTHEADER:
    case PSP_INFO_COUNTSTATUS:
    case PSP_INFO_HWNAME:
	if (param) {
	    *(int32_t *)msg.buf = *(const int32_t *)param;
	    msg.header.len += sizeof(int32_t);
	} else {
	    snprintf(errtxt, sizeof(errtxt), "%s: %s request needs parameter.",
		     __func__, PSP_printInfo(what));
	    PSI_errlog(errtxt, 0);
	    return -1;
	}
	break;
    case PSP_INFO_RDPSTATUS:
    case PSP_INFO_MCASTSTATUS:
	if (param) {
	    *(PSnodes_ID_t *)msg.buf = *(const PSnodes_ID_t *)param;
	    msg.header.len += sizeof(PSnodes_ID_t);
	} else {
	    snprintf(errtxt, sizeof(errtxt), "%s: %s request needs parameter.",
		     __func__, PSP_printInfo(what));
	    PSI_errlog(errtxt, 0);
	    return -1;
	}
	break;
    case PSP_INFO_INSTDIR:
    case PSP_INFO_DAEMONVER:
	break;
    default:
	snprintf(errtxt, sizeof(errtxt),
		 "%s: don't know how to handle '%s' request",
		 __func__, PSP_printInfo(what));
	PSI_errlog(errtxt, 0);
	return -1;
    }

    if (PSI_sendMsg(&msg)<0) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "%s(%s): PSI_sendMsg: %s", __func__,
		 PSP_printInfo(what), errstr);
	PSI_errlog(errtxt, 0);
	return -1;
    }

    if (receiveInfo(string, &size, verbose) == what && size) return 0;

    return -1;
}

int PSI_infoTaskID(PSnodes_ID_t node, PSP_Info_t what, const void *param,
		   PStask_ID_t *tid, int verbose)
{
    DDTypedBufferMsg_t msg = (DDTypedBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_INFOREQUEST,
	    .dest = PSC_getTID(node, 0),
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg.header)+sizeof(msg.type) },
	.type = what,
	.buf = { 0 } };
    size_t size = sizeof(*tid);

    switch (what) {
    case PSP_INFO_PARENTTID:
    case PSP_INFO_LOGGERTID:
	break;
    default:
	snprintf(errtxt, sizeof(errtxt),
		 "%s: don't know how to handle '%s' request",
		 __func__, PSP_printInfo(what));
	PSI_errlog(errtxt, 0);
	return -1;
    }

    if (PSI_sendMsg(&msg)<0) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "%s(%s): PSI_sendMsg: %s", __func__,
		 PSP_printInfo(what), errstr);
	PSI_errlog(errtxt, 0);
	return -1;
    }

    if (receiveInfo(tid, &size, verbose) == what && size) return 0;

    return -1;
}

int PSI_infoNodeID(PSnodes_ID_t node, PSP_Info_t what, const void *param,
		   PSnodes_ID_t *nid, int verbose)
{
    DDTypedBufferMsg_t msg = (DDTypedBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_INFOREQUEST,
	    .dest = PSC_getTID(node, 0),
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg.header)+sizeof(msg.type) },
	.type = what,
	.buf = { 0 } };
    size_t size = sizeof(*nid);

    switch (what) {
    case PSP_INFO_RANKID:
	if (param) {
	    *(uint32_t *)msg.buf = *(const uint32_t *)param;
	    msg.header.len += sizeof(uint32_t);
	} else {
	    snprintf(errtxt, sizeof(errtxt), "%s: %s request needs parameter.",
		     __func__, PSP_printInfo(what));
	    PSI_errlog(errtxt, 0);
	    return -1;
	}
	break;
    case PSP_INFO_HOST:
	if (param) {
	    *(uint32_t *)msg.buf = *(const uint32_t *)param;
	    msg.header.len += sizeof(uint32_t);
	} else {
	    snprintf(errtxt, sizeof(errtxt), "%s: %s request needs parameter.",
		     __func__, PSP_printInfo(what));
	    PSI_errlog(errtxt, 0);
	    return -1;
	}
	break;
    default:
	snprintf(errtxt, sizeof(errtxt),
		 "%s: don't know how to handle '%s' request",
		 __func__, PSP_printInfo(what));
	PSI_errlog(errtxt, 0);
	return -1;
    }

    if (PSI_sendMsg(&msg)<0) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "%s(%s): PSI_sendMsg: %s", __func__,
		 PSP_printInfo(what), errstr);
	PSI_errlog(errtxt, 0);
	return -1;
    }

    if (receiveInfo(nid, &size, verbose) == what && size) return 0;

    return -1;
}

int PSI_infoList(PSnodes_ID_t node, PSP_Info_t what, const void *param,
		 void *buf, size_t size, int verbose)
{
    DDTypedBufferMsg_t msg = (DDTypedBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_INFOREQUEST,
	    .dest = PSC_getTID(node, 0),
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg.header)+sizeof(msg.type) },
	.type = what,
	.buf = { 0 } };
    PSP_Info_t type;
    size_t recvd = 0;

    switch (what) {
    case PSP_INFO_LIST_HOSTSTATUS:
    case PSP_INFO_LIST_VIRTCPUS:
    case PSP_INFO_LIST_PHYSCPUS:
    case PSP_INFO_LIST_HWSTATUS:
    case PSP_INFO_LIST_LOAD:
    case PSP_INFO_LIST_ALLJOBS:
    case PSP_INFO_LIST_NORMJOBS:
    case PSP_INFO_LIST_ALLTASKS:
    case PSP_INFO_LIST_NORMTASKS:
	break;
    default:
	snprintf(errtxt, sizeof(errtxt),
		 "%s: don't know how to handle '%s' request",
		 __func__, PSP_printInfo(what));
	PSI_errlog(errtxt, 0);
	return -1;
    }

    if (PSI_sendMsg(&msg)<0) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "%s(%s): PSI_sendMsg: %s", __func__,
		 PSP_printInfo(what), errstr);
	PSI_errlog(errtxt, 0);
	return -1;
    }

    do {
	size_t chunk = size;
	if (chunk) {
	    type = receiveInfo(buf+recvd, &chunk, verbose);
	} else {
	    type = receiveInfo(NULL, &chunk, verbose);
	}
	if (chunk) {
	    size -= chunk;
	    recvd += chunk;
	} else {
	    size = 0;
	}
    } while (type == what);

    if (type == PSP_INFO_LIST_END) return recvd;

    return -1;
}

int PSI_infoOption(PSnodes_ID_t node, int num, PSP_Option_t option[],
		   PSP_Optval_t value[], int verbose)
{
    DDOptionMsg_t msg;
    int i;

    if (num > DDOptionMsgMax) {
	snprintf(errtxt, sizeof(errtxt), "%s: too many options", __func__);
	PSI_errlog(errtxt, 0);
	return -1;
    }

    msg.header = (DDMsg_t) {
	.type = PSP_CD_GETOPTION,
	.dest = PSC_getTID(node, 0),
	.sender = PSC_getMyTID(),
	.len = sizeof(msg) };

    for (i=0; i<num; i++) {
	msg.opt[i].option = option[i];
    }
    msg.count = num;

    if (PSI_sendMsg(&msg)<0) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "%s: PSI_sendMsg: %s", __func__,
		 errstr);
	PSI_errlog(errtxt, 0);
	return -1;
    }

    if (PSI_recvMsg(&msg)<0) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt), "%s: PSI_recvMsg: %s", __func__,
		 errstr);
	PSI_errlog(errtxt, 0);
	return -1;
    }

    switch (msg.header.type) {
    case PSP_CD_SETOPTION:
	if (msg.count > num) {
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: option-buffer to small.", __func__);
	    PSI_errlog(errtxt, verbose ? 0 : 1);
	    msg.count = num;
	}

	for (i=0; i<msg.count; i++) {
	    value[i] = msg.opt[i].value;
	}

	return msg.count;
    case PSP_CD_ERROR:
    {
	char* errstr = strerror(((DDErrorMsg_t*)&msg)->error);
	snprintf(errtxt, sizeof(errtxt), "%s: error: %s",
		 __func__,errstr ? errstr : "UNKNOWN");
	PSI_errlog(errtxt, verbose ? 0 : 1);
	break;
    }
    default:
	snprintf(errtxt, sizeof(errtxt), "%s: unexpected msgtype '%s'",
		 __func__, PSP_printMsg(msg.header.type));
	PSI_errlog(errtxt, 0);
    }

    return -1;
}

char *PSI_printHWType(unsigned int hwType)
{
    int hwNum = 0;
    static char txt[80], name[40];

    txt[0] = '\0';

    if (!hwType) snprintf(txt, sizeof(txt), "none ");

    while (hwType) {
	if (hwType & 1) {
	    int err = PSI_infoString(-1, PSP_INFO_HWNAME,
				     &hwNum, name, sizeof(name), 1);

	    if (!err) {
		snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt),
			 "%s ", name);
	    } else {
		snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), "unknown ");
	    }
	}

	hwType >>= 1;
	hwNum++;
    }

    txt[strlen(txt)-1] = '\0';

    return txt;
}
