/*
 *               ParaStation3
 * rdp.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: rdp.h,v 1.3 2002/01/16 17:07:30 eicker Exp $
 *
 */
/**
 * \file
 * rdp: Reliable Datagram Protocol for ParaStation daemon
 *
 * $Id: rdp.h,v 1.3 2002/01/16 17:07:30 eicker Exp $
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __RDP_H
#define __RDP_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

typedef enum {
    CLOSED=0x1,
    SYN_SENT=0x2,
    SYN_RECVD=0x3,
    ACTIVE=0x4
} RDPState;

typedef struct RDPLoad_ {
  double	load[3];	/* Load parameters of that node */
} RDPLoad;

typedef struct RDP_ConInfo_ {
  RDPState	state;
  RDPLoad	load;
  int	 	misscounter;
} RDP_ConInfo;

#define T_INFO		0x01
#define T_CLOSE		0x02
#define T_LIC		0x10
#define T_KILL		0x20

typedef struct Mmsg_ {
  short         node;           /* Sender ID */
  short         type;
  RDPState      state;
  RDPLoad       load;
} Mmsg;   

typedef struct RDP_Deadbuf_{
  int dst;
  void *buf;
  int buflen;
} RDP_Deadbuf;


#define RDP_NEW_CONNECTION	0x1	/* buf == nodeno */
#define RDP_LOST_CONNECTION	0x2	/* buf == nodeno */
#define RDP_PKT_UNDELIVERABLE	0x3	/* buf == (dst,*buf,buflen) */

#define RDP_LIC_LOST		0x10	/* buf == sinaddr */ 
#define RDP_LIC_SHUTDOWN	0x20	/* buf == reason */ 

#define LIC_LOST_CONECTION	0x1
#define LIC_KILL_MSG		0x2

/*
 * Initialize RDP
 * Parameters:  1) nodes: Nr of Nodes in the Cluster
 *              2) mgroup: Id of Multicastgroup (0 < id < 255)
 *              3) usesyslog (1=yes,0=no): Use syslog() to log error/info messages
 *              4) address of callback function to handle RDP exceptions / infos
 *                 (func == NULL allowed to prevent callback) 
 *                 callback func is called with (type, void *buf)
 */
int initRDP(int nodes, int mgroup, int usesyslog, void (*func)(int, void*));
int initRDPMCAST(int nodes, int mgroup, int usesyslog,
		 void (*func)(int, void*));

/*
 * Shutdown RDP
 */
void exitRDP(void);

/*
 * Sent a msg[buf:len] to node <node> reliable
 */
int Rsendto(int node, void *buf, int len);

/*
 * RDP select() call
 */
int Rselect(int n, fd_set  *readfds,  fd_set  *writefds, fd_set *exceptfds, struct timeval *timeout);
int Mselect(int n, fd_set  *readfds,  fd_set  *writefds, fd_set *exceptfds, struct timeval *timeout);

/*
 * receive a RDP packet 
 * Parameters:	node: source node of msg (O)
 *              msg:  pointer to msg buffer (O)
 *              len:  max lenght of buffer (I)
 * Retval:	lenght of received msg (or -1 on error, errno is set)
 */
int Rrecvfrom(int *node, void *msg, int len);

/*
 * Get DebugLevel
 */
int getRDPDebugLevel(void);

/*
 * Set DebugLevel
 */
void setRDPDebugLevel(int level);

/*
 * Set Log Msg
 */
void setRDPLogMsg(int level);

/*
 * Get Dead Limit
 */
int getRDPDeadLimit(void);

/*
 * Set Dead Limit
 */
void setRDPDeadLimit(int limit);

/*
 * Get Info for node n
 */
void getRDPInfo(int n, RDP_ConInfo *info);

void getRDPStateInfo(int n, char *s);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __RDP_H */
