/*
 * Copyright (c) 1995 Regents of the University of Karlsruhe / Germany.
 * All rights reserved.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      @(#)psp.c    1.00 (Karlsruhe) 10/4/95
 *
 *      written by Joachim Blum
 *
 *
 * This is the key module for the ParaStationProtocol.
 * It manages the receipt of messages and give them to the appropriate 
 * input- routines of the protocol.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "psi.h"

#include "psp.h"

/*----------------------------------------------------------------------*/
/* string identification of message IDs.                                */
/* Nicer output for errrors and debugging.                              */
/*----------------------------------------------------------------------*/
struct PSPctrlmessages_t{
    long id;
    char* message;
};
extern struct PSPctrlmessages_t PSPctrlmessages[];

struct PSPctrlmessages_t PSPctrlmessages[]=
{
   { PSP_CD_CLIENTCONNECT      ,"PSP_CD_CLIENTCONNECT      "},
   { PSP_CD_CLIENTESTABLISHED  ,"PSP_CD_CLIENTESTABLISHED  "},
   { PSP_CD_CLIENTREFUSED      ,"PSP_CD_CLIENTREFUSED      "},
   { PSP_CD_OLDVERSION         ,"PSP_CD_OLDVERSION         "},
   { PSP_CD_NOSPACE            ,"PSP_CD_NOSPACE            "}, 
   { PSP_CD_REMOTECONNECT      ,"PSP_CD_REMOTECONNECT      "},
   { PSP_CD_UIDLIMIT           ,"PSP_CD_UIDLIMIT           "},
   { PSP_CD_PROCLIMIT          ,"PSP_CD_PROCLIMIT          "}, 
   { PSP_DD_SETOPTION          ,"PSP_DD_SETOPTION          "}, 
   { PSP_DD_GETOPTION          ,"PSP_DD_GETOPTION          "}, 
   { PSP_DD_CONTACTNODE        ,"PSP_DD_CONTACTNODE        "},
   
   { PSP_CD_TASKINFO           ,"PSP_CD_TASKINFO           "}, 
   { PSP_CD_TASKINFOEND        ,"PSP_CD_TASKINFOEND        "}, 
   { PSP_CD_TASKLISTREQUEST    ,"PSP_CD_TASKLISTREQUEST    "}, 
   { PSP_CD_TASKINFOREQUEST    ,"PSP_CD_TASKINFOREQUEST    "},
   { PSP_CD_HOSTSTATUSREQUEST  ,"PSP_CD_HOSTSTATUSREQUEST  "},
   { PSP_CD_HOSTSTATUSRESPONSE ,"PSP_CD_HOSTSTATUSRESPONSE "},

   { PSP_CD_HOSTREQUEST        ,"PSP_CD_HOSTREQUEST        "},
   { PSP_CD_HOSTRESPONSE       ,"PSP_CD_HOSTRESPONSE       "},

   { PSP_CD_LOADREQ            ,"PSP_CD_LOADREQ            "},
   { PSP_CD_LOADRES            ,"PSP_CD_LOADRES            "},

   { PSP_CD_COUNTSTATUSREQUEST ,"PSP_CD_COUNTSTATUSREQUEST "},
   { PSP_CD_COUNTSTATUSRESPONSE,"PSP_CD_COUNTSTATUSRESPONSE"},

   { PSP_CD_RDPSTATUSREQUEST   ,"PSP_CD_RDPSTATUSREQUEST   "},  
   { PSP_CD_RDPSTATUSRESPONSE  ,"PSP_CD_RDPSTATUSRESPONSE  "},  

   { PSP_CD_MCASTSTATUSREQUEST ,"PSP_CD_MCASTSTATUSREQUEST "},  
   { PSP_CD_MCASTSTATUSRESPONSE,"PSP_CD_MCASTSTATUSRESPONSE"},  

   { PSP_DD_SPAWNREQUEST       ,"PSP_DD_SPAWNREQUEST       "}, 
   { PSP_DD_SPAWNSUCCESS       ,"PSP_DD_SPAWNSUCCESS       "}, 
   { PSP_DD_SPAWNFAILED        ,"PSP_DD_SPAWNFAILED        "}, 
   
   { PSP_DD_TASKKILL           ,"PSP_DD_TASKKILL           "}, 
   { PSP_DD_NOTIFYDEAD         ,"PSP_DD_NOTIFYDEAD         "},
   { PSP_DD_RELEASE            ,"PSP_DD_RELEASE            "},
   { PSP_DD_WHODIED            ,"PSP_DD_WHODIED            "},
   { PSP_DD_NOTIFYDEADRES      ,"PSP_DD_NOTIFYDEADRES      "},
   { PSP_DD_RELEASERES         ,"PSP_DD_RELEASERES         "},

   { PSP_DD_SYSTEMERROR        ,"PSP_DD_SYSTEMERROR        "}, 
   { PSP_DD_STATENOCONNECT     ,"PSP_DD_STATENOCONNECT     "},

   { PSP_DD_RESET              ,"PSP_DD_RESET              "},
   { PSP_DD_RESET_START_REQ    ,"PSP_DD_RESET_START_REQ    "},
   { PSP_DD_RESET_START_RESP   ,"PSP_DD_RESET_START_RESP   "},
   { PSP_DD_RESET_DORESET      ,"PSP_DD_RESET_DORESET      "},
   { PSP_DD_RESET_DONE         ,"PSP_DD_RESET_DONE         "},
   { PSP_DD_RESET_ABORT        ,"PSP_DD_RESET_ABORT        "},
   { PSP_DD_RESET_OK           ,"PSP_DD_RESET_OK           "},

   { PSP_CD_RESET              ,"PSP_CD_RESET              "},
   { PSP_CD_RESET_START_REQ    ,"PSP_CD_RESET_START_REQ    "},
   { PSP_CD_RESET_START_RESP   ,"PSP_CD_RESET_START_RESP   "},
   { PSP_CD_RESET_DORESET      ,"PSP_CD_RESET_DORESET      "},
   { PSP_CD_RESET_DONE         ,"PSP_CD_RESET_DONE         "},
   { PSP_CD_RESET_ABORT        ,"PSP_CD_RESET_ABORT        "},
   { PSP_CD_RESET_OK           ,"PSP_CD_RESET_OK           "},

   { PSP_DD_DAEMONCONNECT      ,"PSP_DD_DAEMONCONNECT      "}, 
   { PSP_DD_DAEMONESTABLISHED  ,"PSP_DD_DAEMONESTABLISHED  "}, 
   { PSP_DD_DAEMONSTOP         ,"PSP_DD_DAEMONSTOP         "}, 
   { PSP_CD_DAEMONSTOP         ,"PSP_CD_DAEMONSTOP         "}, 

   { PSP_DD_NEWPROCESS         ,"PSP_DD_NEWPROCESS         "},
   { PSP_DD_DELETEPROCESS      ,"PSP_DD_DELETEPROCESS      "}, 
   { PSP_DD_CHILDDEAD          ,"PSP_DD_CHILDDEAD          "},

   {0,NULL}
};

char PSPctrlmsgtxt[80];

char*
PSPctrlmsg(int msgtype)
{
    int i=0;
    while ((PSPctrlmessages[i].id!=0)            /* end symbol */
	   &&(PSPctrlmessages[i].id != msgtype)) {
	i++;
    }

    if (PSPctrlmessages[i].id!=0) {
	return PSPctrlmessages[i].message;
    } else {
	sprintf(PSPctrlmsgtxt,"msgtype 0x%x UNKNOWN",msgtype);
	return PSPctrlmsgtxt;
    }
}

/******************************************
 * int ClientMsgSend(void* amsg)
 * send a message to the destination.  This is done by sending it
 * to the local daemon. 
 */
int ClientMsgSend(void* amsg)
{
    DDMsg_t* msg = (DDMsg_t*)amsg;
    int ret = 0;

    if ((ret = write(PSI_msock, msg, msg->len))==0) {
	perror("PANIC in Send: Lost connection to ParaStation daemon");
	exit(-1);
    }
    return ret;
}

/******************************************
*  ClientMsgReceive()
*  Receive a msg from the local daemon
*/
int ClientMsgReceive(void * amsg)
{
    DDMsg_t* msg = (DDMsg_t*)amsg;
    int n;
    int count =0;
    do{
	if(count==0)
	    n = read(PSI_msock,msg,sizeof(*msg));
	else
	    n = read(PSI_msock,&((char*)msg)[count], msg->len-count);
	if(n>0)count+=n;
	if(n==0){
	    perror("PANIC in Recv: Lost connection to ParaStation daemon");
	    exit(-1);
	}
    }while((msg->len >count) && n>0);
    if(count==msg->len)
	return msg->len;
    else
	return n;
}
