/*
 *               ParaStation3
 * psi.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psi.h,v 1.12 2002/04/22 18:18:49 hauke Exp $
 *
 */
/**
 * @file
 * psi: User-functions for interaction with the ParaStation system.
 *
 * $Id: psi.h,v 1.12 2002/04/22 18:18:49 hauke Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSI_H
#define __PSI_H

#include <sys/types.h>

#include "psitask.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

extern int PSI_msock;                 /* master socket to connect psid */
extern int PSI_mygroup;               /* the group id of the process */

extern short PSI_nrofnodes;           /* total number of nodes */
extern unsigned short PSI_myid;       /* my node number */

extern long PSI_mytid;                /* The Task ID */
extern int PSI_mypid;                 /* The Process ID */

extern unsigned int PSI_loggernode;   /* IP number of my loggernode (or 0) */
extern int PSI_loggerport;            /* port of my logger process */

extern int PSI_myrank;                /* rank inside my process-group */

extern char *PSI_psidversion;  /** CVS versionstring of psid */

extern char *PSI_hoststatus;

extern long PSI_options;

#define PSI_isoption(op) (PSI_options & op)
int PSI_setoption(long option,char value);

extern enum TaskOptions PSI_mychildoptions;

/***************************************************************************
 *       PSI_clientinit()
 *
 *       MUST be call by every client process. It does all the necessary
 *       work to initialize the shm and contact the local daemon.
 */
int PSI_clientinit(unsigned short protocol);

/***************************************************************************
 *       PSI_clientexit()
 *
 *   reconfigs all variable so that a PSI_clientinit() will be successful
 */
int PSI_clientexit(void);

/****************************************
 *  PSI_gettid()
 *  returns the TID. This is necessary to have unique Task Identifiers in
 *  the cluster .The TID of a Task in the Cluster is a combination 
 *  of the Node number and the local pid on the node.
 *  If node=-1, the local node is used.
 */
long PSI_gettid(short node, pid_t pid);

/****************************************
 *  PSI_getnode()
 *  return the value of the node number of a TID. The TID of a Task in the 
 *  Cluster is a combination of the Node number and the local pid
 *  on the node.
 */
unsigned short PSI_getnode(long tid);

/****************************************
 *  PSI_getnrofnodes()
 * returns the number of nodes
 */
short PSI_getnrofnodes(void);

/****************************************
 *  PSI_getpid()
 *  returns the value of the local PID of the OS. The TID of a Task in the 
 *  Cluster is a combination of the Node number and the local pid
 *  on the node.
 */
pid_t PSI_getpid(long tid);

/***************************************************************************
 *       PSI_startdaemon()
 *
 *       starts the daemon via the inetd
 */
int PSI_startdaemon(unsigned int hostaddr);

/***************************************************************************
 *       PSI_mastersocket()
 */
int PSI_daemonsocket(unsigned int hostaddr);

/*----------------------------------------------------------------------*/
/* 
 * PSI_notifydead()
 *  
 *  PSI_notifydead requests the signal sig, when the child with task
 *  identifier tid dies.
 *  
 * PARAMETERS
 *  tid: the task identifier of the child whose death shall be signaled to you
 *  sig: the signal which should be sent to you when the child dies
 * RETURN  0 on success
 *         -1 on error
 */
int PSI_notifydead(long tid, int sig);

/*----------------------------------------------------------------------*/
/*
 * PSI_release()
 *
 *  PSI_release() helps parent to survive if task is exiting. Quite usefull
 *       this in PSE_finalize().
 *
 * PARAMETERS
 *  tid: the task identifier of the task that should *not* kill the parent
 *
 * RETURN  0 on success
 *         -1 on error
 */
int PSI_release(long tid);

int PSI_send_finish(long parenttid);

int PSI_recv_finish(int num);

/*----------------------------------------------------------------------*/
/* 
 * PSI_whodied()
 *  
 *  PSI_whodied asks the ParaStation system which child's death caused the 
 *  last signal to be delivered to you.
 *  
 * PARAMETERS
 * RETURN  0 on success
 *         -1 on error
 */
long PSI_whodied(int sig);

char * PSI_LookupInstalldir(void);

void PSI_SetInstalldir(char *installdir);

int PSI_GetServicePort( char *name , int def);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSI_H */
