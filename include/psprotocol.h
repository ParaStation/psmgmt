/*
 *               ParaStation3
 * psp.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psprotocol.h,v 1.3 2002/07/11 16:51:03 eicker Exp $
 *
 */
/**
 * @file
 * psp: The ParaStation Protocol
 *      Used for daemon-daemon and client-daemon communication.
 *
 * $Id: psprotocol.h,v 1.3 2002/07/11 16:51:03 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSPROTOCOL_H
#define __PSPROTOCOL_H

#include "pstask.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

#define PSprotocolversion  313

/** @todo Documentation */

/*------------------------------------------------------------------------- 
* PSP_ctrl messages through the OS socket of the daemon
*/
#define PSP_CD_CLIENTCONNECT       0x0001
#define PSP_CD_CLIENTESTABLISHED   0x0002
#define PSP_CD_CLIENTREFUSED       0x0003 
#define PSP_CD_OLDVERSION          0x0004
#define PSP_CD_NOSPACE             0x0005
#define PSP_CD_REMOTECONNECT       0x0006
#define PSP_CD_UIDLIMIT            0x0007
#define PSP_CD_PROCLIMIT           0x0008
#define PSP_DD_SETOPTION           0x0009
#define PSP_DD_GETOPTION           0x000a
#define PSP_DD_CONTACTNODE         0x000b

#define PSP_CD_TASKLISTREQUEST     0x0010 /* Obsolete */
#define PSP_CD_TASKLIST            0x0011 /* Obsolete */
#define PSP_CD_TASKLISTEND         0x0012 /* Obsolete */
#define PSP_CD_TASKINFOREQUEST     0x0013
#define PSP_CD_TASKINFO            0x0014
#define PSP_CD_TASKINFOEND         0x0015

#define PSP_CD_HOSTREQUEST         0x0018
#define PSP_CD_HOSTRESPONSE        0x0019
#define PSP_CD_NODELISTREQUEST     0x001a
#define PSP_CD_NODELISTRESPONSE    0x001b
#define PSP_CD_LOADREQUEST         0x001c  /* Obsolete ? */
#define PSP_CD_LOADRESPONSE        0x001d  /* Obsolete ? */
#define PSP_CD_PROCREQUEST         0x001e  /* Obsolete ? */
#define PSP_CD_PROCRESPONSE        0x001f  /* Obsolete ? */

#define PSP_CD_HOSTSTATUSREQUEST   0x0020
#define PSP_CD_HOSTSTATUSRESPONSE  0x0021
#define PSP_CD_COUNTSTATUSREQUEST  0x0022
#define PSP_CD_COUNTSTATUSRESPONSE 0x0023
#define PSP_CD_RDPSTATUSREQUEST    0x0024
#define PSP_CD_RDPSTATUSRESPONSE   0x0025
#define PSP_CD_MCASTSTATUSREQUEST  0x0026
#define PSP_CD_MCASTSTATUSRESPONSE 0x0027

#define PSP_DD_SPAWNREQUEST        0x0030  /* Request to spawn a process */
#define PSP_DD_SPAWNSUCCESS        0x0031  /* Reply if spawn was successful */
#define PSP_DD_SPAWNFAILED         0x0032  /* Reply if spawn failed */
#define PSP_DD_SPAWNFINISH         0x0033  /* Reply after successfil end of
					      spawned process */

#define PSP_DD_TASKKILL            0x0040
#define PSP_DD_NOTIFYDEAD          0x0041
#define PSP_DD_NOTIFYDEADRES       0x0042
#define PSP_DD_RELEASE             0x0043
#define PSP_DD_RELEASERES          0x0044
#define PSP_DD_SIGNAL              0x0045
#define PSP_DD_WHODIED             0x0046

#define PSP_DD_SYSTEMERROR         0x0050
#define PSP_DD_STATENOCONNECT      0x0051

#define PSP_DD_RESET               0x0060
#define PSP_DD_RESET_START_REQ     0x0061
#define PSP_DD_RESET_START_RESP    0x0062
#define PSP_DD_RESET_DORESET       0x0063
#define PSP_DD_RESET_DONE          0x0064
#define PSP_DD_RESET_OK            0x0065
#define PSP_DD_RESET_ABORT         0x0066

#define PSP_CD_RESET               0x0070
#define PSP_CD_RESET_START_REQ     0x0071
#define PSP_CD_RESET_START_RESP    0x0072
#define PSP_CD_RESET_DORESET       0x0073
#define PSP_CD_RESET_DONE          0x0074
#define PSP_CD_RESET_OK            0x0075
#define PSP_CD_RESET_ABORT         0x0076

#define PSP_DD_DAEMONCONNECT       0x0100
#define PSP_DD_DAEMONESTABLISHED   0x0101
#define PSP_DD_DAEMONSTOP          0x0102
#define PSP_CD_DAEMONSTOP          0x0103

#define PSP_DD_NEWPROCESS          0x0110
#define PSP_DD_DELETEPROCESS       0x0111
#define PSP_DD_CHILDDEAD           0x0112

/*----------------------------------------------------------------------*/
/* global options to be sent in the DD protocol                         */
/*----------------------------------------------------------------------*/
#define PSP_OP_SMALLPACKETSIZE     0x0001
#define PSP_OP_RESENDTIMEOUT       0x0002
#define PSP_OP_HNPEND              0x0003
#define PSP_OP_ACKPEND             0x0004

#define PSP_OP_PSIDDEBUG           0x0010
#define PSP_OP_PSIDSELECTTIME      0x0011
#define PSP_OP_PROCLIMIT           0x0012
#define PSP_OP_UIDLIMIT            0x0013

#define PSP_OP_RDPDEBUG            0x0020
#define PSP_OP_RDPPKTLOSS          0x0021
#define PSP_OP_RDPMAXRETRANS       0x0022

#define PSP_OP_MCASTDEBUG          0x0028

/*----------------------------------------------------------------------*/
/* global reset actions to be sent in the DD/CD protocol                */
/*----------------------------------------------------------------------*/
#define PSP_RESET_HW              0x0001

/*----------------------------------------------------------------------*/
/* types of communication-hardware supported by ParaStation             */
/*----------------------------------------------------------------------*/
#define PSP_HW_ETHERNET            0x0001
#define PSP_HW_MYRINET             0x0002
#define PSP_HW_GIGAETHERNET        0x0004

/***************************************************************************
 *       PSPctrlmsg()
 *
 *       outputs description of the type of a psid message
 */
char* PSPctrlmsg(int msgtype);

/*----------------------------------------------------------------------*/
/* Daemon-Daemon Protocol Message Types                                 */
/*----------------------------------------------------------------------*/

/* Message primitive. This is also the header of more complex messages */
typedef struct {
    long type;              /* msg type */
    long sender;            /* sender of the message */ 
    long dest;              /* final destination of the message */
    long len;               /* total length of the message */
} DDMsg_t;

/* Load Message */
typedef struct {
    DDMsg_t header;         /* header of the message */
    double load[3];         /* three load values */
} DDLoadMsg_t;

/* Error Message */
typedef struct {
    DDMsg_t header;         /* header of the message */
    int error;              /* error number */
    long request;           /* request which caused the error */
} DDErrorMsg_t;

/* Reset Message */
typedef struct {
    DDMsg_t header;         /* header of the message */
    int first;              /* first node to be reset */
    int last;               /* last node to be reset */
    long action;            /* request which caused the error */
} DDResetMsg_t;

/* Contact Node Message */
typedef struct {
    DDMsg_t header;         /* header of the message */
    long partner;           /* node which should be contacted by header.dest */
} DDContactMsg_t;

/* Connect Node Message */
typedef struct {
    DDMsg_t header;         /* header of the message */
    unsigned int hwStatus;  /* Flag to show which hw on nodes is active */
    short numCPU;           /* Number of CPUs in that node */
} DDConnectMsg_t;

/* untyped Buffer Message */
typedef struct {
    DDMsg_t header;         /* header of the message */
    char buf[8000];         /* buffer for Message */
} DDBufferMsg_t;

/* Init Message */
typedef struct {
    DDMsg_t header;         /* header of the message */
    long reason;            /* reason for unaccepted connect */
    PStask_group_t group;   /* process group of the task */
    long version;           /* version of the PS library */
    int nrofnodes;          /* # of nodes */
    int myid;               /* PS id of this node */
    unsigned int loggernode;/* @todo Obsolete as soon as new forwarder works */
    short loggerport;       /* @todo Obsolete as soon as new forwarder works */
    int rank;               /* rank of client passed by spawn */
    int uid;                /* user id */
    int pid;                /* process id */
    char instdir[80];       /** Installation directory of ParaStation stuff */
    char psidvers[80];      /** CVS version-string of the ParaStation daemon */
} DDInitMsg_t;

#define DDOptionMsgMax 16

/* Options Message */
typedef struct {
    DDMsg_t header;         /* header of the message */
    char count;             /* no of options in opt[] */
    struct {
	long option;        /* option to be set/requested */
	long value;         /* value of option to be set */
    }opt[DDOptionMsgMax];
} DDOptionMsg_t;

/* Signal Message */
typedef struct {
    DDMsg_t header;         /* header of the message */
    int signal;             /* signal to be sent */
    int senderuid;          /* uid of the sender task */
} DDSignalMsg_t;

/* Taskinfo Message */
typedef struct {
    DDMsg_t header;         /* header of the message */
    long tid;               /* ID of the task */
    long ptid;              /* ID of the parent-task */
    int uid;                /* user id of the task */
    long group;             /* process group of the task */
} DDTaskinfoMsg_t;

/* Array of this struct is returned to NODELIST_REQUEST */
typedef struct {
    short up;               /**< Flag if nodes is up */
    short numCPU;           /**< Number of CPUs in this node */
    unsigned int hwType;    /**< HW available on this node */
    float load[3];          /**< load on this node */
    short totalJobs;        /**< number of jobs */
    short normalJobs;       /**< number of "normal" jobs (i.e. without
			         logger & admin) */
} NodelistEntry_t;

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSPROTOCOL_H */
