/*
 *               ParaStation3
 * info.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: info.c,v 1.19 2002/07/08 16:15:38 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: info.c,v 1.19 2002/07/08 16:15:38 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "pscommon.h"
#include "psprotocol.h"
#include "pstask.h"

#include "psi.h"
#include "psilog.h"

#include "info.h"

/*--------------------------------------------------------------------
 * int INFO_receive()
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
	PSI_errexit("INFO_receive(): read", errno);
    } else {
	switch (msg.header.type) {
	case PSP_CD_TASKINFO:
	{
	    PStask_t* task;

	    task = PStask_new();

	    PStask_decode(msg.buf, task);
	    switch(what){
	    case INFO_UID:
		memcpy(buffer, &task->uid, size);
		break;
	    case INFO_PTID:
		memcpy(buffer, &task->ptid, size);
		break;
	    case INFO_ISALIVE:
		*(long *)buffer = 1;
		break;
	    case INFO_GETINFO:
	    {
		INFO_taskinfo_t *taskinfo = (INFO_taskinfo_t *) buffer;

		if (taskinfo) {
		    if (size < sizeof(*taskinfo)) {
			if (verbose) {
			    fprintf(stderr,
				    "INFO_receive: task-buffer to small\n");
			}
			break;
		    }
		    taskinfo->tid = task->tid;
		    taskinfo->ptid = task->ptid;
		    taskinfo->uid = task->uid;
		    taskinfo->group = task->group;
		}
		break;
	    }
	    default:
		*(long *)buffer = -1;
		break;
	    }
	    errno = 0;
	    PStask_delete(task);
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
	    memcpy(buffer, msg.buf, size);
	    break;
	case PSP_CD_LOADRESPONSE:
	case PSP_CD_PROCRESPONSE:
	    /* changed from 5min to 1 min avg load jh 2001-12-21 */
	    *(double *)buffer = ((DDLoadMsg_t*)&msg)->load[0];
	    break;
	case PSP_DD_SETOPTION:
	{
	    int i;
	    DDOptionMsg_t *omsg = (DDOptionMsg_t *)&msg;

	    if (omsg->count*sizeof(omsg->opt[0].value) > size ) {
		if (verbose) {
		    fprintf(stderr, "INFO_receive: option-buffer to small\n");
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
		errtxt = strerror(((DDErrorMsg_t*)&msg)->err);
		printf("INFO_receive: error in command %s : %s\n", 
		       PSPctrlmsg(((DDErrorMsg_t*)&msg)->request),
		       errtxt ? errtxt : "UNKNOWN");
	    }
	    break;
	}
	default:
	    fprintf(stderr, "INFO_receive: received msgtype '%s'."
		    " Don't know what to do!\n", PSPctrlmsg(msg.header.type));
	    }
    }

    return msg.header.type;
}

int INFO_request_rdpstatus(int nodeno, void* buffer, size_t size, int verbose)
{
    DDBufferMsg_t msg;

    msg.header.type = PSP_CD_RDPSTATUSREQUEST;
    msg.header.dest = PSC_getTID(-1, 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg.header);
    *(int *)msg.buf = nodeno;
    msg.header.len += sizeof(int);

    if (PSI_sendMsg(&msg)<0) {
	PSI_errexit("INFO_request_rdpstatus(): write", errno);
    }

    if (INFO_receive(INFO_GETINFO, buffer, size, verbose)
	== PSP_CD_RDPSTATUSRESPONSE) {
	return size;
    }

    return -1;
}

int INFO_request_mcaststatus(int nodeno,
			     void* buffer, size_t size, int verbose)
{
    DDBufferMsg_t msg;

    msg.header.type = PSP_CD_MCASTSTATUSREQUEST;
    msg.header.dest = PSC_getTID(-1, 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg.header);
    *(int *)msg.buf = nodeno;
    msg.header.len += sizeof(int);

    if (PSI_sendMsg(&msg)<0) {
	PSI_errexit("INFO_request_rdpstatus(): write", errno);
    }

    if (INFO_receive(INFO_GETINFO, buffer, size, verbose)
	== PSP_CD_MCASTSTATUSRESPONSE) {
	return size;
    }

    return -1;
}

int INFO_request_countstatus(int nodeno,
			     void* buffer, size_t size, int verbose)
{
    DDMsg_t msg;

    msg.type = PSP_CD_COUNTSTATUSREQUEST;
    msg.dest = PSC_getTID(nodeno, 0);
    msg.sender = PSC_getMyTID();
    msg.len = sizeof(msg);

    if (PSI_sendMsg(&msg)<0) {
	PSI_errexit("INFO_request_countstatus(): write", errno);
    }

    if (INFO_receive(INFO_GETINFO, buffer, size, verbose)
	== PSP_CD_COUNTSTATUSRESPONSE) {
	return size;
    }

    return -1;
}

int INFO_request_hoststatus(void* buffer, size_t size, int verbose)
{
    DDMsg_t msg;

    msg.type = PSP_CD_HOSTSTATUSREQUEST;
    msg.dest = PSC_getTID(-1, 0);
    msg.sender = PSC_getMyTID();
    msg.len = sizeof(msg);

    if (PSI_sendMsg(&msg)<0) {
	PSI_errexit("INFO_request_hoststatus(): write", errno);
    }

    if (INFO_receive(INFO_GETINFO, buffer, size, verbose)
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

    memcpy(msg.buf, &address, sizeof(unsigned int));
    msg.header.len += sizeof(address);

    if (PSI_sendMsg(&msg)<0) {
	PSI_errexit("INFO_request_host(): write", errno);
    }

    if (INFO_receive(INFO_GETINFO, &host, sizeof(host), verbose)
	== PSP_CD_HOSTRESPONSE) {
	return host;
    }

    return -1;
}

/**
 * @brief Get a tested and sorted nodelist from the daemon
 *
 * Bla blub @todo Move to info.h
 *
 * @param hwType The kind of communication hardware the requested
 * nodes have to have.
 * @param sort The sorting criterium used to build the list
 * @param nodesLen The number of preset nodes in @a nodes.
 * @param nodes The nodes from which the list should be build. If @a nodes
 * is empty (i.e. @a nodesLen is 0), the list is build from all nodes.
 * Actually @a nodes has to be @a numNodes long to make a correct receive
 * possible.
 * @param numNodes The number of nodes that should be returned in @a nodes.
 *
 */
int INFO_request_nodelist(NodelistEntry_t *buffer, size_t size, int verbose)
{
    DDMsg_t msg;

    msg.type = PSP_CD_NODELISTREQUEST;
    msg.dest = PSC_getTID(-1, 0);
    msg.sender = PSC_getMyTID();
    msg.len = sizeof(msg);

    if (PSI_sendMsg(&msg)<0) {
	PSI_errexit("INFO_request_hostlist(): write", errno);
    }

    if (INFO_receive(INFO_GETINFO, buffer, size, verbose)
	== PSP_CD_NODELISTRESPONSE) {
	return size;
    }

    return -1;
}

int INFO_request_tasklist(int nodeno, INFO_taskinfo_t taskinfo[], int size,
			  int verbose)
{
    DDMsg_t msg;
    int msgtype, tasknum, maxtask;

    msg.type = PSP_CD_TASKLISTREQUEST;
    msg.dest = PSC_getTID(nodeno, 0);
    msg.sender = PSC_getMyTID();
    msg.len = sizeof(msg);

    if (PSI_sendMsg(&msg)<0) {
	PSI_errexit("INFO_request_tasklist(): write", errno);
    }

    maxtask = size/sizeof(*taskinfo);
    tasknum = 0;
    msgtype = PSP_CD_TASKINFO;
    while(msgtype == PSP_CD_TASKINFO){
	if (tasknum<maxtask) {
	    msgtype = INFO_receive(INFO_GETINFO, &taskinfo[tasknum],
				   sizeof(*taskinfo), verbose);
	} else {
	    msgtype = INFO_receive(INFO_GETINFO, NULL, 0, verbose);
	}
	tasknum++;
    }

    return tasknum-1;
}

long INFO_request_taskinfo(long tid, INFO_info_t what, int verbose)
{
    int msgtype;
    DDMsg_t msg;
    long answer;

    msg.type = PSP_CD_TASKINFOREQUEST;
    msg.dest = tid;
    msg.sender = PSC_getMyTID();
    msg.len = sizeof(msg);

    if (PSI_sendMsg(&msg)<0) {
	PSI_errexit("INFO_request_taskinfo(): write", errno);
    }

    errno = 8888;
    msgtype = PSP_CD_TASKINFO;
    while (msgtype == PSP_CD_TASKINFO) {
	msgtype = INFO_receive(what, &answer, sizeof(answer), verbose);
    }

    return answer;
}

double INFO_request_load(unsigned short node, int verbose)
{
    int msgtype;
    double answer;
    DDBufferMsg_t msg;

    msg.header.type = PSP_CD_LOADREQUEST;
    msg.header.dest = PSC_getTID(node, 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg.header);

    if (PSI_sendMsg(&msg)<0) {
	PSI_errexit("INFO_request_load(): write", errno);
    }

    msgtype = INFO_receive(INFO_GETINFO, &answer, sizeof(answer), verbose);

    if (msgtype == PSP_CD_LOADRESPONSE) {
	return answer;
    } else {
	return -1.0;
    }
}

double INFO_request_proc(unsigned short node, int verbose)
{
    int msgtype;
    double answer;
    DDBufferMsg_t msg;

    msg.header.type = PSP_CD_PROCREQUEST;
    msg.header.dest = PSC_getTID(node, 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg.header);

    if (PSI_sendMsg(&msg)<0) {
	PSI_errexit("INFO_request_proc(): write", errno);
    }

    msgtype = INFO_receive(INFO_GETINFO, &answer, sizeof(answer), verbose);

    if (msgtype == PSP_CD_PROCRESPONSE) {
	return answer;
    } else {
	return -1.0;
    }
}

int INFO_request_option(unsigned short node, int num, long option[],
			 long value[], int verbose)
{
    int msgtype, i;
    DDOptionMsg_t msg;

    if (num > DDOptionMsgMax) {
	PSI_errlog("INFO_request_options(): too many options.", 0);
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
	PSI_errexit("INFO_request_option(): write", errno);
    }

    msgtype = INFO_receive(INFO_GETINFO, value, sizeof(*value)*num, verbose);

    if (msgtype == PSP_DD_SETOPTION) {
	return num;
    } else {
	return -1;
    }
}
