/*
 *               ParaStation3
 * info.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: info.c,v 1.38 2003/10/30 16:36:56 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: info.c,v 1.38 2003/10/30 16:36:56 eicker Exp $";
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

#include "info.h"

static char errtxt[128];

/**
 * @todo Docu
 * @brief Receive and handle info message.
 *
 * OUT:
 * void* buffer : buffer where data can be placed
 * IN:
 * int size: size of buffer
 * RETURN type of the msg received
 */
static PSP_Info_t INFO_receive(void *buffer, size_t *size, int verbose)
{
    DDTypedBufferMsg_t msg;
    PSP_Info_t ret;

    if (PSI_recvMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: read", __func__);
	PSI_errexit(errtxt, errno);
    }

    switch (msg.header.type) {
    case PSP_CD_INFORESPONSE:
    {
	ret = msg.type;
	switch (msg.type) {
	case PSP_INFO_TASK:
	{
	    Taskinfo_t *ti = (Taskinfo_t *)msg.buf;

	    INFO_taskinfo_t *taskinfo = (INFO_taskinfo_t *) buffer;

	    if (taskinfo) {
		if (*size < sizeof(*taskinfo)) {
		    if (verbose) {
			fprintf(stderr,
				"%s: task-buffer to small\n", __func__);
		    }
		    *size = 0;
		    break;
		}
		taskinfo->tid = ti->tid;
		taskinfo->ptid = ti->ptid;
		taskinfo->loggertid = ti->loggertid;
		taskinfo->uid = ti->uid;
		taskinfo->group = ti->group;
		taskinfo->rank = ti->rank;
		taskinfo->connected = ti->connected;
		*size = sizeof(INFO_taskinfo_t);
	    }
	    break;
	}
	case PSP_INFO_TASKEND:
	    break;
	case PSP_INFO_NROFNODES:
	case PSP_INFO_INSTDIR:
	case PSP_INFO_DAEMONVER:
	case PSP_INFO_HOST:
	case PSP_INFO_NODE:
	case PSP_INFO_NODELIST:
	case PSP_INFO_PARTITION:
	case PSP_INFO_HOSTSTATUS:
	case PSP_INFO_RDPSTATUS:
	case PSP_INFO_MCASTSTATUS:
	case PSP_INFO_COUNTHEADER:
	case PSP_INFO_COUNTSTATUS:
	case PSP_INFO_HWNUM:
	case PSP_INFO_HWINDEX:
	case PSP_INFO_HWNAME:
	case PSP_INFO_RANKID:
	case PSP_INFO_TASKSIZE:
	{
	    size_t s = msg.header.len - sizeof(msg.header) - sizeof(msg.type);
	    *size = (s > *size) ? *size : s;
	    memcpy(buffer, msg.buf, *size);
	    break;
	}
	case PSP_INFO_UNKNOWN:
	    fprintf(stderr, "%s: daemon does not know info.\n", __func__);
	    *size = 0;
	    break;
	default:
 	    fprintf(stderr, "%s: received unexpected info type '%d'.\n",
		    __func__, msg.type);
	    *size = 0;
	}
	break;
    }
    case PSP_CD_ERROR:
	if (verbose) {
	    char* errtxt;
	    errtxt = strerror(((DDErrorMsg_t*)&msg)->error);
	    printf("%s: error in command %s : %s\n",
		   __func__, PSP_printMsg(((DDErrorMsg_t*)&msg)->request),
		   errtxt ? errtxt : "UNKNOWN");
	}
	*size = 0;
	ret = PSP_INFO_UNKNOWN;
	break;
    default:
	fprintf(stderr, "%s: received unexpected msgtype '%s'.",
		__func__, PSP_printMsg(msg.header.type));
	*size = 0;
	ret = PSP_INFO_UNKNOWN;
    }

    return ret;
}

int INFO_request_rdpstatus(PSnodes_ID_t node,
			   void *buf, size_t size, int verbose)
{
    DDTypedBufferMsg_t msg;

    msg.header.type = PSP_CD_INFOREQUEST;
    msg.header.dest = PSC_getTID(-1, 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg.header);
    msg.type = PSP_INFO_RDPSTATUS;
    msg.header.len += sizeof(msg.type);
    *(int *)msg.buf = node;
    msg.header.len += sizeof(int);

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (INFO_receive(buf, &size, verbose) == PSP_INFO_RDPSTATUS) {
	return size;
    }

    return -1;
}

int INFO_request_mcaststatus(PSnodes_ID_t node,
			     void *buf, size_t size, int verbose)
{
    DDTypedBufferMsg_t msg;

    msg.header.type = PSP_CD_INFOREQUEST;
    msg.header.dest = PSC_getTID(-1, 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg.header);
    msg.type = PSP_INFO_MCASTSTATUS;
    msg.header.len += sizeof(msg.type);
    *(int *)msg.buf = node;
    msg.header.len += sizeof(int);

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (INFO_receive(buf, &size, verbose) == PSP_INFO_MCASTSTATUS) {
	return size;
    }

    return -1;
}

int INFO_request_countheader(PSnodes_ID_t node, int hwindex,
			     void *buf, size_t size, int verbose)
{
    DDTypedBufferMsg_t msg;

    msg.header.type = PSP_CD_INFOREQUEST;
    msg.header.dest = PSC_getTID(node, 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg.header);
    msg.type = PSP_INFO_COUNTHEADER;
    msg.header.len += sizeof(msg.type);
    *(int *)msg.buf = hwindex;
    msg.header.len += sizeof(int);

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (INFO_receive(buf, &size, verbose) == PSP_INFO_COUNTHEADER) {
	return size;
    }

    return -1;
}

int INFO_request_countstatus(PSnodes_ID_t node, int hwindex,
			     void *buf, size_t size, int verbose)
{
    DDTypedBufferMsg_t msg;

    msg.header.type = PSP_CD_INFOREQUEST;
    msg.header.dest = PSC_getTID(node, 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg.header);
    msg.type = PSP_INFO_COUNTSTATUS;
    msg.header.len += sizeof(msg.type);
    *(int *)msg.buf = hwindex;
    msg.header.len += sizeof(int);

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (INFO_receive(buf, &size, verbose) == PSP_INFO_COUNTSTATUS) {
	return size;
    }

    return -1;
}

int INFO_request_hoststatus(void *buf, size_t size, int verbose)
{
    DDTypedMsg_t msg;

    msg.header.type = PSP_CD_INFOREQUEST;
    msg.header.dest = PSC_getTID(-1, 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg);
    msg.type = PSP_INFO_HOSTSTATUS;

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (INFO_receive(buf, &size, verbose) == PSP_INFO_HOSTSTATUS) {
	return size;
    }

    return -1;
}

PSnodes_ID_t INFO_request_host(unsigned int address, int verbose)
{
    DDTypedBufferMsg_t msg;
    PSnodes_ID_t node;
    size_t size = sizeof(node);

    msg.header.type = PSP_CD_INFOREQUEST;
    msg.header.dest = PSC_getTID(-1, 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg.header);
    msg.type = PSP_INFO_HOST;
    msg.header.len += sizeof(msg.type);
    memcpy(msg.buf, &address, sizeof(address));
    msg.header.len += sizeof(address);

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (INFO_receive(&node, &size, verbose) == PSP_INFO_HOST) {
	return node;
    }

    return -1;
}

unsigned int INFO_request_node(PSnodes_ID_t node, int verbose)
{
    DDTypedBufferMsg_t msg = (DDTypedBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_INFOREQUEST,
	    .dest = PSC_getTID(-1, 0),
	    .sender = PSC_getMyTID(),
	    .len = sizeof(DDMsg_t) + sizeof(msg.type)},
	.type = PSP_INFO_NODE,
	.buf = { 0 } };
    unsigned int address;
    size_t size = sizeof(address);

    memcpy(msg.buf, &node, sizeof(node));
    msg.header.len += sizeof(node);

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (INFO_receive(&address, &size, verbose) == PSP_INFO_NODE) {
	if (address == INADDR_ANY) {
	    return -1;
	} else {
	    return address;
	}
    }

    return -1;
}

int INFO_request_nodelist(NodelistEntry_t *buf, size_t size, int verbose)
{
    DDTypedMsg_t msg = (DDTypedMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_INFOREQUEST,
	    .dest = PSC_getTID(-1, 0),
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg) },
	.type = PSP_INFO_NODELIST };

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (INFO_receive(buf, &size, verbose) == PSP_INFO_NODELIST) return size;

    return -1;
}

PSnodes_ID_t INFO_request_rankID(unsigned int rank, int verbose)
{
    DDTypedBufferMsg_t msg = (DDTypedBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_INFOREQUEST,
	    .dest = PSC_getTID(-1, 0),
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg.header) + sizeof(msg.type) },
	.type = PSP_INFO_RANKID,
	.buf = { 0 } };
    PSnodes_ID_t node;
    size_t size = sizeof(node);

    memcpy(msg.buf, &rank, sizeof(rank));
    msg.header.len += sizeof(rank);

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (INFO_receive(&node, &size, verbose) == PSP_INFO_RANKID) return node;

    return -1;
}

int INFO_request_taskSize(int verbose)
{
    DDTypedMsg_t msg = (DDTypedMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_INFOREQUEST,
	    .dest = PSC_getTID(-1, 0),
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg) },
	.type = PSP_INFO_TASKSIZE };
    int tasksize;
    size_t size = sizeof(tasksize);

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (INFO_receive(&tasksize, &size, verbose) == PSP_INFO_TASKSIZE)
	return tasksize;

    return -1;
}

int INFO_request_partition(unsigned int hwType,
			   NodelistEntry_t *buf, size_t size, int verbose)
{
    DDTypedBufferMsg_t msg = (DDTypedBufferMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_INFOREQUEST,
	    .dest = PSC_getTID(-1, 0),
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg.header) + sizeof(msg.type) },
	.type = PSP_INFO_PARTITION,
	.buf = {0}};
    *(unsigned int *)msg.buf = hwType;
    msg.header.len += sizeof(hwType);

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (INFO_receive(buf, &size, verbose) == PSP_INFO_PARTITION) {
	return size;
    }

    return -1;
}

int INFO_request_tasklist(PSnodes_ID_t node,
			  INFO_taskinfo_t taskinfo[], size_t size, int verbose)
{
    DDTypedMsg_t msg;
    PSP_Info_t type;
    unsigned int task;
    size_t maxtask;

    msg.header.type = PSP_CD_INFOREQUEST;
    msg.header.dest = PSC_getTID(node, 0); /* Get info on all tasks */
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg);
    msg.type = PSP_INFO_TASK;

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    maxtask = size/sizeof(*taskinfo);
    task = 0;
    do {
	if (task<maxtask) {
	    size_t size = sizeof(*taskinfo);
	    type = INFO_receive(&taskinfo[task], &size, verbose);
	} else {
	    type = INFO_receive(NULL, 0, verbose);
	}
	task++;
    } while (type == PSP_INFO_TASK);

    if (type == PSP_INFO_TASKEND) {
	return task-1;
    } else {
	return -1;
    }
}

long INFO_request_taskinfo(PStask_ID_t tid, INFO_info_t what, int verbose)
{
    DDTypedMsg_t msg;
    INFO_taskinfo_t taskinfo;
    PSP_Info_t type;
    size_t size = sizeof(taskinfo);

    msg.header.type = PSP_CD_INFOREQUEST;
    msg.header.dest = tid;
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg);
    msg.type = PSP_INFO_TASK;

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    type = INFO_receive(&taskinfo, &size, verbose);

    if (INFO_receive(NULL, 0, 0) != PSP_INFO_TASKEND) return -1;

    if (type == PSP_INFO_TASK) {
	switch(what){
	case INFO_ISALIVE:
	    return 1;
	    break;
	case INFO_PTID:
	    return taskinfo.ptid;
	    break;
	case INFO_LOGGERTID:
	    return taskinfo.loggertid;
	    break;
	case INFO_UID:
	    return taskinfo.uid;
	    break;
	case INFO_RANK:
	    return taskinfo.rank;
	    break;
	default:
	    break;
	}
    }

    return -1;
}

int INFO_request_nrofnodes(int verbose)
{
    DDTypedMsg_t msg;
    int nrofnodes;
    size_t size = sizeof(nrofnodes);

    msg.header.type = PSP_CD_INFOREQUEST;
    msg.header.dest = PSC_getTID(-1, 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg);
    msg.type = PSP_INFO_NROFNODES;

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (INFO_receive(&nrofnodes, &size, verbose) == PSP_INFO_NROFNODES) {
	return nrofnodes;
    }

    return -1;
}

char *INFO_request_instdir(int verbose)
{
    DDTypedMsg_t msg;
    static char instdir[1000];
    size_t size = sizeof(instdir);

    msg.header.type = PSP_CD_INFOREQUEST;
    msg.header.dest = PSC_getTID(-1, 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg);
    msg.type = PSP_INFO_INSTDIR;

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (INFO_receive(instdir, &size, verbose) == PSP_INFO_INSTDIR) {
	return instdir;
    }

    return NULL;
}

char *INFO_request_psidver(int verbose)
{
    DDTypedMsg_t msg;
    static char version[80];
    size_t size = sizeof(version);

    msg.header.type = PSP_CD_INFOREQUEST;
    msg.header.dest = PSC_getTID(-1, 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg);
    msg.type = PSP_INFO_DAEMONVER;

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (INFO_receive(version, &size, verbose) == PSP_INFO_DAEMONVER) {
	return version;
    }

    return NULL;
}

int INFO_request_hwnum(int verbose)
{
    DDTypedMsg_t msg;
    int num;
    size_t size = sizeof(num);

    msg.header.type = PSP_CD_INFOREQUEST;
    msg.header.dest = PSC_getTID(-1, 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg.header);
    msg.type = PSP_INFO_HWNUM;
    msg.header.len += sizeof(msg.type);

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (INFO_receive(&num, &size, verbose) == PSP_INFO_HWNUM) {
	if (size == sizeof(num)) {
	    return num;
	}
    }

    return -1;
}

int INFO_request_hwindex(char *hwType, int verbose)
{
    DDTypedBufferMsg_t msg;
    int index;
    size_t size = sizeof(index);

    msg.header.type = PSP_CD_INFOREQUEST;
    msg.header.dest = PSC_getTID(-1, 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg.header);
    msg.type = PSP_INFO_HWINDEX;
    msg.header.len += sizeof(msg.type);
    strncpy(msg.buf, hwType, sizeof(msg.buf));
    msg.buf[sizeof(msg.buf)-1] = '\0';
    msg.header.len += strlen(msg.buf)+1;

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (INFO_receive(&index, &size, verbose) == PSP_INFO_HWINDEX) {
	if (size == sizeof(index)) {
	    return index;
	}
    }

    return -1;
}

char *INFO_request_hwname(int index, int verbose)
{
    DDTypedBufferMsg_t msg;
    static char hwname[80];
    size_t size = sizeof(hwname);

    msg.header.type = PSP_CD_INFOREQUEST;
    msg.header.dest = PSC_getTID(-1, 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg.header);
    msg.type = PSP_INFO_HWNAME;
    msg.header.len += sizeof(msg.type);
    *(int *)msg.buf = index;
    msg.header.len += sizeof(index);

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (INFO_receive(hwname, &size, verbose) == PSP_INFO_HWNAME) {
	if (size) {
	    return hwname;
	}
    }

    return NULL;
}

char *INFO_printHWType(unsigned int hwType)
{
    int index = 0;
    static char txt[80];

    txt[0] = '\0';

    if (!hwType) snprintf(txt, sizeof(txt), "none ");

    while (hwType) {
	if (hwType & 1) {
	    char *name = INFO_request_hwname(index, 1);

	    if (name) {
		snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt),
			 "%s ", name);
	    } else {
		snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), "unknown ");
	    }
	}

	hwType >>= 1;
	index++;
    }

    txt[strlen(txt)-1] = '\0';

    return txt;
}

int INFO_request_option(PSnodes_ID_t node, int num, PSP_Option_t option[],
			PSP_Optval_t value[], int verbose)
{
    DDOptionMsg_t msg;
    int i;

    if (num > DDOptionMsgMax) {
	snprintf(errtxt, sizeof(errtxt), "%s: too many options", __func__);
	PSI_errlog(errtxt, 0);
	return -1;
    }

    msg.header.type = PSP_CD_GETOPTION;
    msg.header.dest = PSC_getTID(node, 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg);

    for (i=0; i<num; i++) {
	msg.opt[i].option = option[i];
    }
    msg.count = num;

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (PSI_recvMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: read", __func__);
	PSI_errexit(errtxt, errno);
    }

    switch (msg.header.type) {
    case PSP_CD_SETOPTION:
	if (msg.count > num) {
	    if (verbose) {
		fprintf(stderr, "%s: option-buffer to small\n", __func__);
	    }
	    msg.count = num;
	}

	for (i=0; i<msg.count; i++) {
	    value[i] = msg.opt[i].value;
	}

	return msg.count;
    case PSP_CD_ERROR:
	if (verbose) {
	    char* errtxt;
	    errtxt = strerror(((DDErrorMsg_t*)&msg)->error);
	    printf("%s: error: %s\n", __func__,errtxt ? errtxt : "UNKNOWN");
	}
	break;
    default:
	fprintf(stderr, "%s: received unexpected msgtype '%s'.",
		__func__, PSP_printMsg(msg.header.type));
    }

    return -1;
}
