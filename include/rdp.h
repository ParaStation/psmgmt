/*
 *               ParaStation3
 * rdp.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: rdp.h,v 1.6 2002/01/23 11:25:49 eicker Exp $
 *
 */
/**
 * @file
 * rdp: Reliable Datagram Protocol for ParaStation daemon
 *
 * $Id: rdp.h,v 1.6 2002/01/23 11:25:49 eicker Exp $
 *
 * @author
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

typedef struct RDPDeadbuf_{
  int dst;
  void *buf;
  int buflen;
} RDPDeadbuf;


#define RDP_NEW_CONNECTION	0x1	/* buf == nodeno */
#define RDP_LOST_CONNECTION	0x2	/* buf == nodeno */
#define RDP_PKT_UNDELIVERABLE	0x3	/* buf == (dst,*buf,buflen) */

#define RDP_LIC_LOST		0x10	/* buf == sinaddr */ 
#define RDP_LIC_SHUTDOWN	0x20	/* buf == reason */ 

#define LIC_LOST_CONECTION	0x1
#define LIC_KILL_MSG		0x2

/**
 * @brief Initializes the RDP and MCast modules.
 *
 * Initializes the RDP and MCast machinery for @a nodes nodes.
 *
 * @param nodes Number of nodes to handle.
 * @param mgroup The MultiCast group to use.
 * @param usesyslog If true, all error-messages are printed via syslog().
 * @param hosts An array of size @a nodes containing the IP-addresses of the
 * participating nodes in network-byteorder.
 * @param func Pointer to a callback-function. This function is called if
 * something exceptional happens. If NULL, no callbacks will be done.
 *
 * @return On success, the filedescriptor of the RDP socket is returned.
 * On error, exit() is called within this function.
 */
int initRDP(int nodes, int mgroup, int usesyslog, unsigned int hosts[],
	    void (*func)(int, void*));

/**
 * @brief Initializes the MCast module.
 *
 * Initializes the MCast machinery for @a nodes nodes.
 *
 * @param nodes Number of nodes to handle.
 * @param mgroup The MultiCast group to use.
 * @param usesyslog If true, all error-messages are printed via syslog().
 * @param hosts An array of size @a nodes containing the IP-addresses of the
 * participating nodes in network-byteorder.
 * @param func Pointer to a callback-function. This function is called if
 * something exceptional happens. If NULL, no callbacks will be done.
 *
 * @return On success, the filedescriptor of the MCast socket is returned.
 * On error, exit() is called within this function.
 */
int initRDPMCAST(int nodes, int mgroup, int usesyslog, unsigned int hosts[],
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

void getRDPStateInfo(int n, char *s, size_t len);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __RDP_H */
