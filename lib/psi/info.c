/*
 *               ParaStation3
 * info.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: info.c,v 1.28 2003/03/06 13:57:10 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: info.c,v 1.28 2003/03/06 13:57:10 eicker Exp $";
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
 * INOUT:
 * long* what : (for taskinfo) in: action to be performed
 *                            out: response
 * OUT:
 * void* buffer : buffer where data can be placed
 * IN:
 * int size: size of buffer
 * RETURN type of the msg received
 */
static int INFO_receive(INFO_info_t what, void *buffer, size_t size,
			int verbose)
{
    DDBufferMsg_t msg;
    if (PSI_recvMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: read", __func__);
	PSI_errexit(errtxt, errno);
    } else {
	switch (msg.header.type) {
	case PSP_CD_TASKINFO:
	{
	    DDTaskinfoMsg_t *timsg = (DDTaskinfoMsg_t *)&msg;

	    switch(what){
	    case INFO_ISALIVE:
		*(long *)buffer = 1;
		break;
	    case INFO_PTID:
		*(long *)buffer = timsg->ptid;
		break;
	    case INFO_LOGGERTID:
		*(long *)buffer = timsg->loggertid;
		break;
	    case INFO_UID:
		*(long *)buffer = timsg->uid;
		break;
	    case INFO_RANK:
		*(long *)buffer = timsg->rank;
		break;
	    case INFO_GETINFO:
	    {
		INFO_taskinfo_t *taskinfo = (INFO_taskinfo_t *) buffer;

		if (taskinfo) {
		    if (size < sizeof(*taskinfo)) {
			if (verbose) {
			    fprintf(stderr,
				    "%s: task-buffer to small\n", __func__);
			}
			break;
		    }
		    taskinfo->tid = timsg->tid;
		    taskinfo->ptid = timsg->ptid;
		    taskinfo->loggertid = timsg->loggertid;
		    taskinfo->uid = timsg->uid;
		    taskinfo->group = timsg->group;
		    taskinfo->rank = timsg->rank;
		    taskinfo->connected = timsg->connected;
		}
		break;
	    }
	    default:
		*(long *)buffer = -1;
		break;
	    }
	    errno = 0;
	    break;
	}
	case PSP_CD_TASKINFOEND:
	    break;
	case PSP_CD_COUNTSTATUSRESPONSE:
	case PSP_CD_RDPSTATUSRESPONSE:
	case PSP_CD_MCASTSTATUSRESPONSE:
	case PSP_CD_HOSTSTATUSRESPONSE:
	case PSP_CD_NODELISTRESPONSE:
	case PSP_CD_HOSTRESPONSE:
	case PSP_CD_NODERESPONSE:
	    memcpy(buffer, msg.buf, size);
	    break;
/*  	case PSP_CD_LOADRESPONSE: */
/*  	case PSP_CD_PROCRESPONSE: */
	    /* changed from 5min to 1 min avg load jh 2001-12-21 */
/*  	    *(double *)buffer = ((DDLoadMsg_t*)&msg)->load[0]; */
/*  	    break; */
	case PSP_DD_SETOPTION:
	{
	    int i;
	    DDOptionMsg_t *omsg = (DDOptionMsg_t *)&msg;

	    if (omsg->count*sizeof(omsg->opt[0].value) > size ) {
		if (verbose) {
		    fprintf(stderr, "%s: option-buffer to small\n", __func__);
		}
		break;
	    }
	    for (i=0; i<omsg->count; i++) {
		((long *)buffer)[i] = omsg->opt[i].value;
	    }
	    break;
	}
	case PSP_DD_SYSTEMERROR:
	{
	    if (verbose) {
		char* errtxt;
		errtxt = strerror(((DDErrorMsg_t*)&msg)->error);
		printf("%s: error in command %s : %s\n",
		       __func__, PSP_printMsg(((DDErrorMsg_t*)&msg)->request),
		       errtxt ? errtxt : "UNKNOWN");
	    }
	    break;
	}
	default:
	    fprintf(stderr, "%s: received unexpected msgtype '%s'.",
		    __func__, PSP_printMsg(msg.header.type));
	    }
    }

    return msg.header.type;
}

int INFO_request_rdpstatus(int nodeno, void *buffer, size_t size, int verbose)
{
    DDBufferMsg_t msg;

    msg.header.type = PSP_CD_RDPSTATUSREQUEST;
    msg.header.dest = PSC_getTID(-1, 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg.header);
    *(int *)msg.buf = nodeno;
    msg.header.len += sizeof(int);

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (receive(INFO_GETINFO, buffer, size, verbose)
	== PSP_CD_RDPSTATUSRESPONSE) {
	return size;
    }

    return -1;
}

int INFO_request_mcaststatus(int nodeno,
			     void *buffer, size_t size, int verbose)
{
    DDBufferMsg_t msg;

    msg.header.type = PSP_CD_MCASTSTATUSREQUEST;
    msg.header.dest = PSC_getTID(-1, 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg.header);
    *(int *)msg.buf = nodeno;
    msg.header.len += sizeof(int);

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (receive(INFO_GETINFO, buffer, size, verbose)
	== PSP_CD_MCASTSTATUSRESPONSE) {
	return size;
    }

    return -1;
}

int INFO_request_countstatus(int nodeno,
			     void *buffer, size_t size, int verbose)
{
    DDMsg_t msg;

    msg.type = PSP_CD_COUNTSTATUSREQUEST;
    msg.dest = PSC_getTID(nodeno, 0);
    msg.sender = PSC_getMyTID();
    msg.len = sizeof(msg);

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (receive(INFO_GETINFO, buffer, size, verbose)
	== PSP_CD_COUNTSTATUSRESPONSE) {
	return size;
    }

    return -1;
}

int INFO_request_hoststatus(void *buffer, size_t size, int verbose)
{
    DDMsg_t msg;

    msg.type = PSP_CD_HOSTSTATUSREQUEST;
    msg.dest = PSC_getTID(-1, 0);
    msg.sender = PSC_getMyTID();
    msg.len = sizeof(msg);

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (receive(INFO_GETINFO, buffer, size, verbose)
	== PSP_CD_HOSTSTATUSRESPONSE) {
	return size;
    }

    return -1;
}

int INFO_request_host(unsigned int address, int verbose)
{
    DDBufferMsg_t msg;
    int host;

    msg.header.type = PSP_CD_HOSTREQUEST;
    msg.header.dest = PSC_getTID(-1, 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg.header);

    memcpy(msg.buf, &address, sizeof(address));
    msg.header.len += sizeof(address);

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (receive(INFO_GETINFO, &host, sizeof(host), verbose)
	== PSP_CD_HOSTRESPONSE) {
	return host;
    }

    return -1;
}

unsigned int INFO_request_node(int node, int verbose)
{
    DDBufferMsg_t msg;
    unsigned int address;

    msg.header.type = PSP_CD_NODEREQUEST;
    msg.header.dest = PSC_getTID(-1, 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg.header);

    memcpy(msg.buf, &node, sizeof(node));
    msg.header.len += sizeof(node);

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (receive(INFO_GETINFO, &address, sizeof(address), verbose)
	== PSP_CD_NODERESPONSE) {
	if (address == INADDR_ANY) {
	    return -1;
	} else {
	    return address;
	}
    }

    return -1;
}

int INFO_request_nodelist(NodelistEntry_t *buffer, size_t size, int verbose)
{
    DDMsg_t msg;

    msg.type = PSP_CD_NODELISTREQUEST;
    msg.dest = PSC_getTID(-1, 0);
    msg.sender = PSC_getMyTID();
    msg.len = sizeof(msg);

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    if (receive(INFO_GETINFO, buffer, size, verbose)
	== PSP_CD_NODELISTRESPONSE) {
	return size;
    }

    return -1;
}

int INFO_request_tasklist(int nodeno, INFO_taskinfo_t taskinfo[], size_t size,
			  int verbose)
{
    DDMsg_t msg;
    int msgtype;
    unsigned int tasknum;
    size_t maxtask;

    msg.type = PSP_CD_TASKINFOREQUEST;
    msg.dest = PSC_getTID(nodeno, 0); /* Get info on all task on this node */
    msg.sender = PSC_getMyTID();
    msg.len = sizeof(msg);

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    maxtask = size/sizeof(*taskinfo);
    tasknum = 0;
    do {
	if (tasknum<maxtask) {
	    msgtype = receive(INFO_GETINFO, &taskinfo[tasknum],
				   sizeof(*taskinfo), verbose);
	} else {
	    msgtype = receive(INFO_GETINFO, NULL, 0, verbose);
	}
	tasknum++;
    } while (msgtype == PSP_CD_TASKINFO);

    return tasknum-1;
}

long INFO_request_taskinfo(long tid, INFO_info_t what, int verbose)
{
    int msgtype;
    DDMsg_t msg;
    long answer = 0;

    msg.type = PSP_CD_TASKINFOREQUEST;
    msg.dest = tid;
    msg.sender = PSC_getMyTID();
    msg.len = sizeof(msg);

    if (PSI_sendMsg(&msg)<0) {
	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__);
	PSI_errexit(errtxt, errno);
    }

    errno = 8888;
    do {
	msgtype = receive(what, &answer, sizeof(answer), verbose);
    } while (msgtype == PSP_CD_TASKINFO);


    return answer;
}

/*  double INFO_request_load(unsigned short node, int verbose) */
/*  { */
/*      int msgtype; */
/*      double answer; */
/*      DDBufferMsg_t msg; */

/*      msg.header.type = PSP_CD_LOADREQUEST; */
/*      msg.header.dest = PSC_getTID(node, 0); */
/*      msg.header.sender = PSC_getMyTID(); */
/*      msg.header.len = sizeof(msg.header); */

/*      if (PSI_sendMsg(&msg)<0) { */
/* 	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__); */
/* 	PSI_errexit(errtxt, errno); */
/*      } */

/*      msgtype = receive(INFO_GETINFO, &answer, sizeof(answer), verbose); */

/*      if (msgtype == PSP_CD_LOADRESPONSE) { */
/*  	return answer; */
/*      } else { */
/*  	return -1.0; */
/*      } */
/*  } */

/*  double INFO_request_proc(unsigned short node, int verbose) */
/*  { */
/*      int msgtype; */
/*      double answer; */
/*      DDBufferMsg_t msg; */

/*      msg.header.type = PSP_CD_PROCREQUEST; */
/*      msg.header.dest = PSC_getTID(node, 0); */
/*      msg.header.sender = PSC_getMyTID(); */
/*      msg.header.len = sizeof(msg.header); */

/*      if (PSI_sendMsg(&msg)<0) { */
/* 	snprintf(errtxt, sizeof(errtxt), "%s: write", __func__); */
/* 	PSI_errexit(errtxt, errno); */
/*      } */

/*      msgtype = receive(INFO_GETINFO, &answer, sizeof(answer), verbose); */

/*      if (msgtype == PSP_CD_PROCRESPONSE) { */
/*  	return answer; */
/*      } else { */
/*  	return -1.0; */
/*      } */
/*  } */

int INFO_request_option(unsigned short node, int num, long option[],
			 long value[], int verbose)
{
    int msgtype, i;
    DDOptionMsg_t msg;

    if (num > DDOptionMsgMax) {
	snprintf(errtxt, sizeof(errtxt), "%s: too many options", __func__);
	PSI_errlog(errtxt, 0);
	return -1;
    }

    msg.header.type = PSP_DD_GETOPTION;
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

    msgtype = receive(INFO_GETINFO, value, sizeof(*value)*num, verbose);

    if (msgtype == PSP_DD_SETOPTION) {
	return num;
    } else {
	return -1;
    }
}
