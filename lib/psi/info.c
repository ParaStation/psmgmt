/*
 *               ParaStation3
 * info.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: info.c,v 1.6 2002/01/08 21:33:40 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: info.c,v 1.6 2002/01/08 21:33:40 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pshal.h>

#include "psp.h"
#include "psitask.h"
#include "psi.h"

#include "info.h"

/*--------------------------------------------------------------------
 * int INFO_request_receive()
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
int
INFO_request_receive(long *what, void* buffer,int size)
{
    DDBufferMsg_t msg;
    if(ClientMsgReceive(&msg)<0){
	perror("read");
	exit(-1);
    }else{
	switch(msg.header.type){
	case PSP_CD_TASKINFO:
	{
	    PStask_t* task;
	    task = PStask_new();

	    PStask_decode(msg.buf,task);
	    switch(*what){
	    case INFO_UID:
		*what = task->uid;
		break;
	    case INFO_PTID:
		*what = task->ptid;
		break;
	    case INFO_ISALIVE:
		*what = 1;
		break;
	    case INFO_PRINTINFO:
		printf("%6d %8ld 0x%08lx %8ld 0x%08lx",
		       task->nodeno,task->tid,task->tid,task->ptid,
		       task->ptid);
		if((short)task->uid==-1)
		    printf("   NONE\n");
		else
		    printf("   %5d%s\n",task->uid,task->group==TG_ADMIN?"(A)":"");
		break;
	    default:
		*what = -1;
		break;
	    }
	    errno = 0;
	    PStask_delete(task);
	    break;
	}
	case PSP_CD_TASKINFOEND:
	    break;
	case PSP_CD_COUNTSTATUSRESPONSE:
	{
	    PSHALInfoCounter_t *ic;
	    int i;

	    ic = (PSHALInfoCounter_t *) msg.buf;

	    /* Print Header if requested */
	    if(*what){
		printf("%8s ", "NODE");
		for (i=0;i<ic->n;i++){
		    printf("%8s ",ic->counter[i].name);
		}
		printf("\n");
	    }

	    printf("%8u ", PSI_getnode(msg.header.sender));
	    for (i=0;i<ic->n;i++){
		char ch[10];
		/* calc column size from name length */
		sprintf(ch,"%%%du ",(int) MAX(strlen(ic->counter[i].name),8));
		printf(ch,ic->counter[i].value);
	    }
	    printf("\n");
	    break;
	}
	case PSP_CD_RDPSTATUSRESPONSE:
	    bcopy(((DDTagedBufferMsg_t*)&msg)->buf,buffer,size);
	    break;
	case PSP_CD_HOSTSTATUSRESPONSE:
	    bcopy(((DDBufferMsg_t*)&msg)->buf,buffer,size);
	    break;
	case PSP_CD_HOSTRESPONSE:
	    *what = *(int *)((DDBufferMsg_t*) &msg)->buf;
	    break;
	case PSP_DD_SYSTEMERROR:
	{
	    char* errtxt;
	    errtxt=strerror(((DDErrorMsg_t*)&msg)->err);
	    printf("ParaStation system error in command %s : %s\n", 
		   PSPctrlmsg(((DDErrorMsg_t*)&msg)->request),
		   errtxt?errtxt:"UNKNOWN");
	    break;
	}
	default:
	    printf("received msgtype %lx. Don't know want to do!?!?\n",
		   msg.header.type);
	}
    }
    return msg.header.type;
}

/*****************************
 *
 * request_rdpstatus(int nodeno)
 *
 * requests the status of RDP on the local PSID to the node nodeno
 * RETURN: filled buffer
 *
 */
int INFO_request_rdpstatus(int nodeno,void* buffer, int size)
{
    DDTagedBufferMsg_t msg;
    long what=0;

    msg.header.type = PSP_CD_RDPSTATUSREQUEST;
    msg.header.dest = PSI_gettid(PSI_myid,0);
    msg.header.sender = PSI_mytid;
    msg.header.len = sizeof(msg);
    msg.tag[0] = nodeno;

    if(ClientMsgSend(&msg)<0){
	perror("write");
	exit(-1);
    }

    if(INFO_request_receive(&what,buffer,size)== PSP_CD_RDPSTATUSRESPONSE)
	return size;
    else
	return -1;
}

/*****************************
 *
 * request_hoststatus(void *buffer, int size)
 *
 * requests the status of all hosts on the local PSID
 * RETURN: filled buffer
 *
 */
int INFO_request_hoststatus(void* buffer, int size)
{
    DDMsg_t msg;
    long what=0;

    msg.type = PSP_CD_HOSTSTATUSREQUEST;
    msg.dest = PSI_gettid(PSI_myid,0);
    msg.sender = PSI_mytid;
    msg.len = sizeof(msg);

    if(ClientMsgSend(&msg)<0){
	perror("write");
	exit(-1);
    }

    if(INFO_request_receive(&what,buffer,size)== PSP_CD_HOSTSTATUSRESPONSE)
	return size;
    else
	return -1;
}

/*****************************
 *
 * request_host(unsigned int address)
 *
 * requests the PS id for host with IP-address address
 * RETURN: the PS id
 *
 */
int INFO_request_host(unsigned int address)
{
    DDBufferMsg_t msg;
    long what=0;

    msg.header.type = PSP_CD_HOSTREQUEST;
    msg.header.dest = PSI_gettid(PSI_myid,0);
    msg.header.sender = PSI_mytid;
    msg.header.len = sizeof(msg.header);
    bcopy(&address, msg.buf, sizeof(unsigned int));
    msg.header.len += sizeof(address);

    if(ClientMsgSend(&msg)<0){
	perror("write");
	exit(-1);
    }

    if(INFO_request_receive(&what, NULL, 0)== PSP_CD_HOSTRESPONSE)
	return what;
    else
	return -1;
}

/*****************************
 *
 * request_countstatus(int nodeno)
 *
 */
int INFO_request_countstatus(int nodeno, int header)
{
    DDBufferMsg_t msg;
    long what=header;

    msg.header.type = PSP_CD_COUNTSTATUSREQUEST;
    msg.header.dest = PSI_gettid(nodeno,0);
    msg.header.sender = PSI_mytid;
    msg.header.len = sizeof(msg);

    if(ClientMsgSend(&msg)<0){
	perror("write");
	exit(-1);
    }

    INFO_request_receive(&what, NULL, 0);
    return 0;
}

/*****************************
 *
 * request_countstatus(int nodeno)
 *
 */
int
INFO_request_tasklist(int nodeno)
{
    DDBufferMsg_t msg;
    int msgtype;
    long what;

    msg.header.type = PSP_CD_TASKLISTREQUEST;
    msg.header.dest = PSI_gettid(nodeno,0);
    msg.header.sender = PSI_mytid;
    msg.header.len = sizeof(msg);

    if(ClientMsgSend(&msg)<0){
	perror("write");
	exit(-1);
    }

    printf("----node %2d----------------------------------------------\n",
	   nodeno);
    msgtype= PSP_CD_TASKINFO;
    while(msgtype == PSP_CD_TASKINFO){
	what = INFO_PRINTINFO;
	msgtype = INFO_request_receive(&what,NULL,0);
    }

    return 0;
}

/*----------------------------------------------------------------------*/
/*
 * INFO_request_taskinfo(PSTID tid,what)
 *
 *  gets the user id of the given task identifier tid
 *
 *  RETURN the uid of the task
 */
long INFO_request_taskinfo(long tid,long what)
{
    int msgtype;
    DDBufferMsg_t msg;

    msg.header.type = PSP_CD_TASKINFOREQUEST;
    msg.header.dest = tid;
    msg.header.sender = PSI_mytid;
    msg.header.len = sizeof(msg);

    if(ClientMsgSend(&msg)<0){
	perror("write");
	exit(-1);
    }

    errno = 8888;
    msgtype= PSP_CD_TASKINFO;
    while(msgtype == PSP_CD_TASKINFO){
	msgtype = INFO_request_receive(&what,NULL,0);
    }
    return what;
}
