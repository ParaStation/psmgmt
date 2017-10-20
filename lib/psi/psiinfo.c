/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

#include "pscommon.h"
#include "psprotocol.h"
#include "pstask.h"

#include "psi.h"
#include "psilog.h"

#include "psiinfo.h"

/**
 * @brief Receive and handle info message.
 *
 * Receive and handle info messages and store the content into @a
 * buf. This is a helper function for the @ref PSI_infoInt(), @ref
 * PSI_infoInt64(), PSI_infoUInt(), PSI_infoString(), @ref
 * PSI_infoTaskID(), @ref PSI_infoNodeID() and @ref PSI_infoList()
 * functions.
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

recv_retry:
    if (PSI_recvMsg((DDMsg_t *)&msg, sizeof(msg))<0) {
	PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
	*size = 0;
	return PSP_INFO_UNKNOWN;
   }

    switch (msg.header.type) {
    case PSP_CD_INFORESPONSE:
    {
	ret = msg.type;
	switch (msg.type) {
	case PSP_INFO_LIST_END:
	case PSP_INFO_QUEUE_SEP:
	    *size = 0;
	    break;
	case PSP_INFO_NROFNODES:
	case PSP_INFO_INSTDIR:
	case PSP_INFO_DAEMONVER:
	case PSP_INFO_HOST:
	case PSP_INFO_NODE:
	case PSP_INFO_RDPSTATUS:
	case PSP_INFO_RDPCONNSTATUS:
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
	case PSP_INFO_LIST_MEMORY:
	case PSP_INFO_LIST_ALLJOBS:
	case PSP_INFO_LIST_NORMJOBS:
	case PSP_INFO_LIST_ALLOCJOBS:
	case PSP_INFO_LIST_EXCLUSIVE:
	case PSP_INFO_LIST_PARTITION:
	case PSP_INFO_CMDLINE:
	case PSP_INFO_RPMREV:
	case PSP_INFO_QUEUE_ALLTASK:
	case PSP_INFO_QUEUE_NORMTASK:
	case PSP_INFO_QUEUE_PARTITION:
	case PSP_INFO_QUEUE_PLUGINS:
	case PSP_INFO_QUEUE_ENVS:
	case PSP_INFO_STARTTIME:
	case PSP_INFO_STARTUPSCRIPT:
	case PSP_INFO_NODEUPSCRIPT:
	case PSP_INFO_NODEDOWNSCRIPT:
	case PSP_INFO_LIST_RESPORTS:
	case PSP_INFO_LIST_RESNODES:
	{
	    size_t s = msg.header.len - sizeof(msg.header) - sizeof(msg.type);
	    if (!buf) {
		PSI_log(PSI_LOG_INFO, "%s: No buffer provided\n", __func__);
		*size = 0;
		break;
	    }
	    if (*size < s) {
		PSI_log(-1, "%s: buffer to small (%ld/%ld/%s)\n", __func__,
			(long)*size, (long)s, PSP_printInfo(msg.type));
		*size = 0;
		break;
	    }
	    *size = s;
	    memcpy(buf, msg.buf, *size);
	    break;
	}
	case PSP_INFO_UNKNOWN:
	    PSI_log(verbose ? -1 : PSI_LOG_INFO,
		    "%s: daemon does not know info\n", __func__);
	    *size = 0;
	    break;
	default:
	    PSI_log(-1, "%s: received unexpected info type '%s'\n",
		    __func__, PSP_printInfo(msg.type));
	    *size = 0;
	    ret = PSP_INFO_UNKNOWN;
	}
	PSI_log(PSI_LOG_INFO, "%s: got info type '%s' message\n",
		__func__, PSP_printInfo(msg.type));
	break;
    }
    case PSP_CD_ERROR:
    {
	PSI_warn(verbose ? -1 : PSI_LOG_INFO, ((DDErrorMsg_t*)&msg)->error,
		 "%s: error in command '%s'",
		 __func__, PSP_printMsg(((DDErrorMsg_t*)&msg)->request));
	*size = 0;
	ret = PSP_INFO_UNKNOWN;
	break;
    }
    case PSP_CD_SENDSTOP:
    case PSP_CD_SENDCONT:
	goto recv_retry;
	break;
    default:
	PSI_log(-1, "%s: received unexpected msgtype '%s'\n",
		__func__, PSP_printMsg(msg.header.type));
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
    case PSP_INFO_HWINDEX:
	if (param) {
	    strncpy(msg.buf, (const char*)param, sizeof(msg.buf));
	    msg.buf[sizeof(msg.buf)-1] = '\0';
	    msg.header.len += strlen(msg.buf)+1;
	} else {
	    PSI_log(-1, "%s: %s request needs parameter\n", __func__,
		    PSP_printInfo(what));
	    errno = EINVAL;
	    return -1;
	}
	break;
    case PSP_INFO_TASKSIZE:
    case PSP_INFO_NROFNODES:
    case PSP_INFO_HWNUM:
    case PSP_INFO_TASKRANK:
	break;
    default:
	PSI_log(-1, "%s: don't know how to handle '%s' request\n", __func__,
		PSP_printInfo(what));
	errno = EINVAL;
	return -1;
    }

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s(%s): PSI_sendMsg", __func__,
		 PSP_printInfo(what));
	return -1;
    }

    if (receiveInfo(val, &size, verbose) == what && size) return 0;

    return -1;
}

int PSI_infoInt64(PSnodes_ID_t node, PSP_Info_t what, const void *param,
		  int64_t *val, int verbose)
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
    case PSP_INFO_STARTTIME:
	break;
    default:
	PSI_log(-1, "%s: don't know how to handle '%s' request\n", __func__,
		PSP_printInfo(what));
	errno = EINVAL;
	return -1;
    }

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s(%s): PSI_sendMsg", __func__,
		 PSP_printInfo(what));
	return -1;
    }

    if (receiveInfo(val, &size, verbose) == what && size) return 0;

    return -1;
}

int PSI_infoUInt(PSnodes_ID_t node, PSP_Info_t what, const void *param,
		 uint32_t *val, int verbose)
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
	    *(PSnodes_ID_t*)msg.buf = *(const PSnodes_ID_t*)param;
	    msg.header.len += sizeof(PSnodes_ID_t);
	} else {
	    PSI_log(-1, "%s: %s request needs parameter\n", __func__,
		    PSP_printInfo(what));
	    errno = EINVAL;
	    return -1;
	}
	break;
    default:
	PSI_log(-1, "%s: don't know how to handle '%s' request\n", __func__,
		PSP_printInfo(what));
	errno = EINVAL;
	return -1;
    }

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s(%s): PSI_sendMsg", __func__,
		 PSP_printInfo(what));
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
	    *(int32_t*)msg.buf = *(const int32_t*)param;
	    msg.header.len += sizeof(int32_t);
	} else {
	    PSI_log(-1, "%s: %s request needs parameter\n", __func__,
		    PSP_printInfo(what));
	    errno = EINVAL;
	    return -1;
	}
	break;
    case PSP_INFO_RDPSTATUS:
    case PSP_INFO_RDPCONNSTATUS:
    case PSP_INFO_MCASTSTATUS:
	if (param) {
	    *(PSnodes_ID_t*)msg.buf = *(const PSnodes_ID_t*)param;
	    msg.header.len += sizeof(PSnodes_ID_t);
	} else {
	    PSI_log(-1, "%s: %s request needs parameter\n", __func__,
		    PSP_printInfo(what));
	    errno = EINVAL;
	    return -1;
	}
	break;
    case PSP_INFO_CMDLINE:
	if (param) {
	    msg.header.dest = PSC_getTID(node, *(pid_t*)param);
	} else {
	    PSI_log(-1, "%s: %s request needs parameter\n", __func__,
		    PSP_printInfo(what));
	    errno = EINVAL;
	    return -1;
	}
	break;
    case PSP_INFO_DAEMONVER:
    case PSP_INFO_INSTDIR:
    case PSP_INFO_RPMREV:
    case PSP_INFO_STARTUPSCRIPT:
    case PSP_INFO_NODEUPSCRIPT:
    case PSP_INFO_NODEDOWNSCRIPT:
	break;
    default:
	PSI_log(-1, "%s: don't know how to handle '%s' request\n", __func__,
		PSP_printInfo(what));
	errno = EINVAL;
	return -1;
    }

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s(%s): PSI_sendMsg", __func__,
		 PSP_printInfo(what));
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
	if (param) msg.header.dest = *(PStask_ID_t *)param;
	break;
    default:
	PSI_log(-1, "%s: don't know how to handle '%s' request\n", __func__,
		PSP_printInfo(what));
	errno = EINVAL;
	return -1;
    }

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s(%s): PSI_sendMsg", __func__,
		 PSP_printInfo(what));
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
	    *(int32_t*)msg.buf = *(const int32_t*)param;
	    msg.header.len += sizeof(int32_t);
	} else {
	    PSI_log(-1, "%s: %s request needs parameter\n", __func__,
		    PSP_printInfo(what));
	    errno = EINVAL;
	    return -1;
	}
	break;
    case PSP_INFO_HOST:
	if (param) {
	    *(uint32_t*)msg.buf = *(const uint32_t*)param;
	    msg.header.len += sizeof(uint32_t);
	} else {
	    PSI_log(-1, "%s: %s request needs parameter\n", __func__,
		    PSP_printInfo(what));
	    errno = EINVAL;
	    return -1;
	}
	break;
    default:
	PSI_log(-1, "%s: don't know how to handle '%s' request\n", __func__,
		PSP_printInfo(what));
	errno = EINVAL;
	return -1;
    }

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s(%s): PSI_sendMsg", __func__,
		 PSP_printInfo(what));
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
    char *bufPtr = buf;

    switch (what) {
    case PSP_INFO_LIST_HOSTSTATUS:
    case PSP_INFO_LIST_VIRTCPUS:
    case PSP_INFO_LIST_PHYSCPUS:
    case PSP_INFO_LIST_HWSTATUS:
    case PSP_INFO_LIST_LOAD:
    case PSP_INFO_LIST_MEMORY:
    case PSP_INFO_LIST_ALLJOBS:
    case PSP_INFO_LIST_NORMJOBS:
    case PSP_INFO_LIST_ALLOCJOBS:
    case PSP_INFO_LIST_EXCLUSIVE:
    case PSP_INFO_LIST_RESPORTS:
	break;
    case PSP_INFO_LIST_PARTITION:
	if (param) msg.header.dest = *(PStask_ID_t *)param;
	break;
    case PSP_INFO_LIST_RESNODES:
	if (param) {
	    PSP_putTypedMsgBuf(&msg, __func__, "resID", param,
			       sizeof(PSrsrvtn_ID_t));
	} else {
	    PSI_log(-1, "%s: %s request needs a parameter\n", __func__,
		    PSP_printInfo(what));
	    errno = EINVAL;
	    return -1;
	}
	break;
    default:
	PSI_log(-1, "%s: don't know how to handle '%s' request\n", __func__,
		PSP_printInfo(what));
	errno = EINVAL;
	return -1;
    }

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s(%s): PSI_sendMsg", __func__,
		 PSP_printInfo(what));
	return -1;
    }

    do {
	size_t chunk = size;
	if (chunk) {
	    type = receiveInfo(bufPtr + recvd, &chunk, verbose);
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

int PSI_infoQueueReq(PSnodes_ID_t node, PSP_Info_t what, const void *param)
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
    case PSP_INFO_QUEUE_ALLTASK:
    case PSP_INFO_QUEUE_NORMTASK:
    case PSP_INFO_QUEUE_PLUGINS:
	break;
    case PSP_INFO_QUEUE_PARTITION:
	if (param) {
	    *(uint32_t*)msg.buf = *(const uint32_t*)param;
	    msg.header.len += sizeof(uint32_t);
	} else {
	    PSI_log(-1, "%s: %s request needs parameter\n", __func__,
		    PSP_printInfo(what));
	    errno = EINVAL;
	    return -1;
	}
	break;
    case PSP_INFO_QUEUE_ENVS:
	if (param) {
	    strncpy(msg.buf, param, sizeof(msg.buf));
	    msg.buf[sizeof(msg.buf)-1] = '\0';
	} else {
	    msg.buf[0] = '*';
	    msg.buf[1] = '\0';
	}
	msg.header.len += strlen(msg.buf)+1;
	break;
    default:
	PSI_log(-1, "%s: don't know how to handle '%s' request\n", __func__,
		PSP_printInfo(what));
	errno = EINVAL;
	return -1;
    }

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s(%s): PSI_sendMsg", __func__,
		 PSP_printInfo(what));
	return -1;
    }

    return 0;
}

int PSI_infoQueueNext(PSP_Info_t what, void *buf, size_t size, int verbose)
{
    PSP_Info_t type;
    size_t recvd = 0;
    char *bufPtr = buf;

    switch (what) {
    case PSP_INFO_QUEUE_ALLTASK:
    case PSP_INFO_QUEUE_NORMTASK:
    case PSP_INFO_QUEUE_PARTITION:
    case PSP_INFO_QUEUE_PLUGINS:
    case PSP_INFO_QUEUE_ENVS:
	break;
    default:
	PSI_log(-1, "%s: don't know how to handle '%s' request\n", __func__,
		PSP_printInfo(what));
	errno = EINVAL;
	return -1;
    }

    do {
	size_t chunk = size;
	if (chunk) {
	    type = receiveInfo(bufPtr + recvd, &chunk, verbose);
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

    if (type == PSP_INFO_QUEUE_SEP) return recvd;

    return -1;
}

int PSI_infoOption(PSnodes_ID_t node, int num, PSP_Option_t option[],
		   PSP_Optval_t value[], int verbose)
{
    DDOptionMsg_t msg;
    int i;

    if (num > DDOptionMsgMax) {
	PSI_log(-1, "%s: too many options\n", __func__);
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
	PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	return -1;
    }

recv_retry:
    if (PSI_recvMsg((DDMsg_t *)&msg, sizeof(msg))<0) {
	PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
	return -1;
    }

    switch (msg.header.type) {
    case PSP_CD_SETOPTION:
	if (msg.count > num) {
	    PSI_log(verbose ? -1 : PSI_LOG_INFO,
		    "%s: option-buffer to small.\n", __func__);
	    msg.count = num;
	}

	for (i=0; i<msg.count; i++) {
	    option[i] = msg.opt[i].option;
	    value[i] = msg.opt[i].value;
	}

	return msg.count;
    case PSP_CD_SENDSTOP:
    case PSP_CD_SENDCONT:
	goto recv_retry;
	break;
    case PSP_CD_ERROR:
	PSI_warn(verbose ? -1 : PSI_LOG_INFO, ((DDErrorMsg_t*)&msg)->error,
		 "%s: error", __func__);
	break;
    default:
	PSI_log(-1, "%s: unexpected msgtype '%s'",
		__func__, PSP_printMsg(msg.header.type));
    }

    return -1;
}

int PSI_infoOptionList(PSnodes_ID_t node, PSP_Option_t option)
{
    DDOptionMsg_t msg;

    msg.header = (DDMsg_t) {
	.type = PSP_CD_GETOPTION,
	.dest = PSC_getTID(node, 0),
	.sender = PSC_getMyTID(),
	.len = sizeof(msg) };

    msg.opt[0].option = option;
    msg.count = 1;

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	return -1;
    }

    return 0;
}

int PSI_infoOptionListNext(DDOption_t opts[], int num, int verbose)
{
    DDOptionMsg_t msg;
    int i;

recv_retry:
    if (PSI_recvMsg((DDMsg_t *)&msg, sizeof(msg))<0) {
	PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
	return -1;
    }

    switch (msg.header.type) {
    case PSP_CD_SETOPTION:
	if (msg.count > num) {
	    PSI_log(verbose ? -1 : PSI_LOG_INFO,
		    "%s: option-buffer to small.\n", __func__);
	    msg.count = num;
	}

	for (i=0; i<msg.count; i++) {
	    opts[i].option = msg.opt[i].option;
	    opts[i].value = msg.opt[i].value;
	}

	return msg.count;
    case PSP_CD_SENDSTOP:
    case PSP_CD_SENDCONT:
	goto recv_retry;
	break;
    case PSP_CD_ERROR:
	PSI_warn(verbose ? -1 : PSI_LOG_INFO, ((DDErrorMsg_t*)&msg)->error,
		 "%s: error", __func__);
	break;
    default:
	PSI_log(-1, "%s: unexpected msgtype '%s'",
		__func__, PSP_printMsg(msg.header.type));
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

PSnodes_ID_t PSI_resolveNodeID(const char *host)
{
    PSnodes_ID_t nodeID = -1;
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int rc;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
    hints.ai_flags = 0;
    hints.ai_protocol = 0;          /* Any protocol */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    rc = getaddrinfo(host, NULL, &hints, &result);
    if (rc != 0) {
	PSI_log(-1, "Unknown host '%s': %s\n", host, gai_strerror(rc));
	return -1;
    }

    /*
     * getaddrinfo() returns a list of address structures.
     * Try each address until we successfully resolve to ParaStation ID.
     */

    for (rp = result; rp != NULL; rp = rp->ai_next) {
	switch (rp->ai_family) {
	case AF_INET:
	    rc=PSI_infoNodeID(-1, PSP_INFO_HOST,
			      &((struct sockaddr_in *)rp->ai_addr)->sin_addr.s_addr,
			      &nodeID, 0);
	    break;
	case AF_INET6:
	    /* ignore -- don't handle IPv6 yet */
	    rc = -1;
	    break;
	}

	if (!rc && PSC_validNode(nodeID)) break;
    }

    freeaddrinfo(result);           /* No longer needed */

    if (nodeID < 0) {
	PSI_log(-1, "Cannot get PS_ID for host '%s'\n", host);
	return -1;
    } else if (!PSC_validNode(nodeID)) {
	PSI_log(-1, "PS_ID %d for node '%s' out of range\n", nodeID, host);
	return -1;
    }

    return nodeID;
}
