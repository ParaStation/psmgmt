/*
 *               ParaStation3
 * info.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: info.c,v 1.14 2002/02/15 19:19:24 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: info.c,v 1.14 2002/02/15 19:19:24 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "psp.h"
#include "psitask.h"
#include "psi.h"

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
static int INFO_receive(INFO_info_t what, void* buffer, int size)
{
    DDBufferMsg_t msg;
    if (ClientMsgReceive(&msg)<0) {
	perror("INFO_receive: read");
	exit(-1);
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
			fprintf(stderr,
				"INFO_receive: task-buffer to small\n");
			break;
		    }
		    taskinfo->nodeno = task->nodeno;
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
	case PSP_CD_HOSTLISTRESPONSE:
	case PSP_CD_HOSTRESPONSE:
	    memcpy(buffer, msg.buf, size);
	    break;
	case PSP_CD_LOADRES:
	case PSP_CD_PROCRES:
	    /* changed from 5min to 1 min avg load jh 2001-12-21 */
	    *(double *)buffer = ((DDLoadMsg_t*)&msg)->load[0];
	    break;
	case PSP_DD_SETOPTION:
	{
	    int i;
	    DDOptionMsg_t *omsg = (DDOptionMsg_t *)&msg;

	    if (omsg->count*sizeof(omsg->opt[0].value) > size ) {
		fprintf(stderr, "INFO_receive: option-buffer to small\n");
		break;
	    }
	    for (i=0; i<omsg->count; i++) {
		((long *)buffer)[i] = omsg->opt[i].value;
	    }
	    break;
	}
	case PSP_DD_SYSTEMERROR:
	{
	    char* errtxt;
	    errtxt = strerror(((DDErrorMsg_t*)&msg)->err);
	    printf("INFO_receive: error in command %s : %s\n", 
		   PSPctrlmsg(((DDErrorMsg_t*)&msg)->request),
		   errtxt ? errtxt : "UNKNOWN");
	    break;
	}
	default:
	    fprintf(stderr, "INFO_receive: received msgtype '%s'."
		    " Don't know what to do!\n", PSPctrlmsg(msg.header.type));
	    }
    }

    return msg.header.type;
}

int INFO_request_rdpstatus(int nodeno, void* buffer, int size)
{
    DDBufferMsg_t msg;

    msg.header.type = PSP_CD_RDPSTATUSREQUEST;
    msg.header.dest = PSI_gettid(PSI_myid,0);
    msg.header.sender = PSI_mytid;
    msg.header.len = sizeof(msg.header);
    *(int *)msg.buf = nodeno;
    msg.header.len += sizeof(int);

    if (ClientMsgSend(&msg)<0) {
	perror("INFO_request_rdpstatus: write");
	exit(-1);
    }

    if (INFO_receive(INFO_GETINFO, buffer, size)==PSP_CD_RDPSTATUSRESPONSE) {
	return size;
    }

    return -1;
}

int INFO_request_mcaststatus(int nodeno, void* buffer, int size)
{
    DDBufferMsg_t msg;

    msg.header.type = PSP_CD_MCASTSTATUSREQUEST;
    msg.header.dest = PSI_gettid(PSI_myid,0);
    msg.header.sender = PSI_mytid;
    msg.header.len = sizeof(msg.header);
    *(int *)msg.buf = nodeno;
    msg.header.len += sizeof(int);

    if (ClientMsgSend(&msg)<0) {
	perror("INFO_request_rdpstatus: write");
	exit(-1);
    }

    if (INFO_receive(INFO_GETINFO, buffer, size)==PSP_CD_MCASTSTATUSRESPONSE) {
	return size;
    }

    return -1;
}

int INFO_request_countstatus(int nodeno, void* buffer, int size)
{
    DDMsg_t msg;

    msg.type = PSP_CD_COUNTSTATUSREQUEST;
    msg.dest = PSI_gettid(nodeno,0);
    msg.sender = PSI_mytid;
    msg.len = sizeof(msg);

    if (ClientMsgSend(&msg)<0) {
	perror("INFO_request_countstatus: write");
	exit(-1);
    }

    if (INFO_receive(INFO_GETINFO, buffer, size)==PSP_CD_COUNTSTATUSRESPONSE) {
	return size;
    }

    return -1;
}

int INFO_request_hoststatus(void* buffer, int size)
{
    DDMsg_t msg;

    msg.type = PSP_CD_HOSTSTATUSREQUEST;
    msg.dest = PSI_gettid(PSI_myid,0);
    msg.sender = PSI_mytid;
    msg.len = sizeof(msg);

    if (ClientMsgSend(&msg)<0) {
	perror("INFO_request_hoststatus: write");
	exit(-1);
    }

    if (INFO_receive(INFO_GETINFO, buffer, size)==PSP_CD_HOSTSTATUSRESPONSE) {
	return size;
    }

    return -1;
}

int INFO_request_host(unsigned int address)
{
    DDBufferMsg_t msg;
    int host;

    msg.header.type = PSP_CD_HOSTREQUEST;
    msg.header.dest = PSI_gettid(PSI_myid,0);
    msg.header.sender = PSI_mytid;
    msg.header.len = sizeof(msg.header);
    memcpy(msg.buf, &address, sizeof(unsigned int));
    msg.header.len += sizeof(address);

    if (ClientMsgSend(&msg)<0) {
	perror("INFO_request_host: write");
	exit(-1);
    }

    if (INFO_receive(INFO_GETINFO, &host, sizeof(host))==PSP_CD_HOSTRESPONSE) {
	return host;
    }

    return -1;
}

int INFO_request_hostlist(void *buffer, int size)
{
    DDMsg_t msg;

    msg.type = PSP_CD_HOSTLISTREQUEST;
    msg.dest = PSI_gettid(PSI_myid,0);
    msg.sender = PSI_mytid;
    msg.len = sizeof(msg);

    if (ClientMsgSend(&msg)<0) {
	perror("INFO_request_hostlist: write");
	exit(-1);
    }

    if (INFO_receive(INFO_GETINFO, buffer, size)==PSP_CD_HOSTLISTRESPONSE) {
	return size;
    }

    return -1;
}

int INFO_request_tasklist(int nodeno, INFO_taskinfo_t taskinfo[], int size)
{
    DDMsg_t msg;
    int msgtype, tasknum, maxtask;

    msg.type = PSP_CD_TASKLISTREQUEST;
    msg.dest = PSI_gettid(nodeno,0);
    msg.sender = PSI_mytid;
    msg.len = sizeof(msg);

    if (ClientMsgSend(&msg)<0) {
	perror("INFO_request_tasklist: write");
	exit(-1);
    }

    maxtask = size/sizeof(*taskinfo);
    tasknum = 0;
    msgtype = PSP_CD_TASKINFO;
    while(msgtype == PSP_CD_TASKINFO){
	if (tasknum<maxtask) {
	    msgtype = INFO_receive(INFO_GETINFO,
				   &taskinfo[tasknum], sizeof(*taskinfo));
	} else {
	    msgtype = INFO_receive(INFO_GETINFO, NULL, 0);
	}
	tasknum++;
    }

    return tasknum-1;
}

long INFO_request_taskinfo(long tid, INFO_info_t what)
{
    int msgtype;
    DDMsg_t msg;
    long answer;

    msg.type = PSP_CD_TASKINFOREQUEST;
    msg.dest = tid;
    msg.sender = PSI_mytid;
    msg.len = sizeof(msg);

    if (ClientMsgSend(&msg)<0) {
	perror("INFO_request_taskinfo: write");
	exit(-1);
    }

    errno = 8888;
    msgtype = PSP_CD_TASKINFO;
    while (msgtype == PSP_CD_TASKINFO) {
	msgtype = INFO_receive(what, &answer, sizeof(answer));
    }

    return answer;
}

double INFO_request_load(unsigned short node)
{
    int msgtype;
    double answer;
    DDBufferMsg_t msg;

    msg.header.type = PSP_CD_LOADREQ;
    msg.header.sender = PSI_mytid;
    msg.header.dest = PSI_gettid(node, 0);
    msg.header.len = sizeof(msg.header);

    if (ClientMsgSend(&msg)<0) {
	perror("INFO_request_load: write");
	exit(-1);
    }

    msgtype = INFO_receive(INFO_GETINFO, &answer, sizeof(answer));

    if (msgtype == PSP_CD_LOADRES) {
	return answer;
    } else {
	return -1.0;
    }
}

double INFO_request_proc(unsigned short node)
{
    int msgtype;
    double answer;
    DDBufferMsg_t msg;

    msg.header.type = PSP_CD_PROCREQ;
    msg.header.sender = PSI_mytid;
    msg.header.dest = PSI_gettid(node, 0);
    msg.header.len = sizeof(msg.header);

    if (ClientMsgSend(&msg)<0) {
	perror("INFO_request_proc: write");
	exit(-1);
    }

    msgtype = INFO_receive(INFO_GETINFO, &answer, sizeof(answer));

    if (msgtype == PSP_CD_PROCRES) {
	return answer;
    } else {
	return -1.0;
    }
}

int INFO_request_option(unsigned short node, int num, long option[],
			 long value[])
{
    int msgtype, i;
    DDOptionMsg_t msg;

    if (num > DDOptionMsgMax) {
	fprintf(stderr, "INFO_request_options: too many options.\n");
	return -1;
    }

    msg.header.type = PSP_DD_GETOPTION;
    msg.header.len = sizeof(msg);
    msg.header.sender = PSI_mytid;
    msg.header.dest = PSI_gettid(node, 0);

    for (i=0; i<num; i++) {
	msg.opt[i].option = option[i];
    }
    msg.count = num;

    if (ClientMsgSend(&msg)<0) {
	perror("INFO_request_option: write");
	exit(-1);
    }

    msgtype = INFO_receive(INFO_GETINFO, value, sizeof(*value)*num);

    if (msgtype == PSP_DD_SETOPTION) {
	return num;
    } else {
	return -1;
    }
}
