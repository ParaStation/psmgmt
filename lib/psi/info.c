#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "psp.h"
#include "psitask.h"
#include "psi.h"

#include "info.h"

void INFO_printCount(INFO_COUNT info)
{
    printf("T1:%d[%x] T2:%d[%x] T3:%d[%x] T4:%d[%x] T5:%d[%x]"
	   " T6:%d[%x] T7:%d[%x] T8:%d[%x]\n",
	   info.t[0], info.t[0], info.t[1], info.t[1],
	   info.t[2], info.t[2], info.t[3], info.t[3],
	   info.t[4], info.t[4], info.t[5], info.t[5],
	   info.t[6], info.t[6], info.t[7], info.t[7]);
    printf("C1:%d[%x] C2:%d[%x] C3:%d[%x] C4:%d[%x] C5:%d[%x]"
	   " C6:%d[%x] C7:%d[%x] C8:%d[%x]\n",
	   info.c[0], info.c[0], info.c[1], info.c[1],
	   info.c[2], info.c[2], info.c[3], info.c[3],
	   info.c[4], info.c[4], info.c[5], info.c[5],
	   info.c[6], info.c[6], info.c[7], info.c[7]);
    return;
}

void INFO_printStatus(INFO_STATUS info)
{
    printf("Myid: %d, NrOfNodes: %d, Interface: %s [%d loops],"
	   " Err[%d,%d,%d,%d,%d]\n",
	   info.myid, info.nrofnodes, (info.speed>0.0)?"UP":"DOWN", info.speed,
	   info.aos_err, info.da_err, info.nack_err, info.crc_err,
	   info.rsa_err);
    printf("Buff: Send[%d|%d], MCPRecv[%d|%d], HostRecv[%d|%d]\n",
	   info.sbuf, info.maxsbuf, info.irbuf, info.maxirbuf,
	   info.rbuf, info.maxrbuf);
    return;
}

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
		    printf("   %d\n",task->uid);
		break;
	    default:
		*what = -1;
		break;
	    }
	    errno = 0;
	    break;
	}
	case PSP_CD_TASKINFOEND:
	    break;
	case PSP_CD_COUNTSTATUSRESPONSE:
	{
	    INFO_COUNT info;
	    bcopy(msg.buf,&info,sizeof(info));

	    INFO_printCount(info);
	    break;
	}
	case PSP_CD_PSISTATUSRESPONSE:
	{
	    INFO_STATUS info;
	    bcopy(msg.buf,&info,sizeof(info));

	    INFO_printStatus(info);
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
 * request_psistatus(int nodeno)
 *
 */
int INFO_request_psistatus(int nodeno)
{
    DDBufferMsg_t msg;
    long what=0;

    msg.header.type = PSP_CD_PSISTATUSREQUEST;
    msg.header.dest = PSI_gettid(nodeno,0);
    msg.header.sender = PSI_mytid;
    msg.header.len = sizeof(msg);

    if(ClientMsgSend(&msg)<0){
	perror("write");
	exit(-1);
    }

    printf("----node %2d----------------------------------------------\n",
	   nodeno);
    INFO_request_receive(&what,NULL,0);
    return 0;
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
int INFO_request_countstatus(int nodeno)
{
  DDBufferMsg_t msg;
  long what=0;
  
  msg.header.type = PSP_CD_COUNTSTATUSREQUEST;
  msg.header.dest = PSI_gettid(nodeno,0);
  msg.header.sender = PSI_mytid;
  msg.header.len = sizeof(msg);

  if(ClientMsgSend(&msg)<0)
    {
	perror("write");
	exit(-1);
    }
    
  printf("----node %2d----------------------------------------------\n",
	 nodeno);
  INFO_request_receive(&what,NULL,0);
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
