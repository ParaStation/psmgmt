/*
 *               ParaStation3
 * psi.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psi.h,v 1.14 2002/07/08 16:14:28 eicker Exp $
 *
 */
/**
 * @file
 * psi: User-functions for interaction with the ParaStation system.
 *
 * $Id: psi.h,v 1.14 2002/07/08 16:14:28 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSI_H
#define __PSI_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

extern int PSI_msock;                 /* master socket to connect psid */

extern unsigned int PSI_loggernode;   /* IP number of my loggernode (or 0) */
extern int PSI_loggerport;            /* port of my logger process */

extern int PSI_myrank;                /* rank inside my process-group */

extern char *PSI_psidversion;  /** CVS versionstring of psid */

/** @todo Documentation */

/**
 * @brief @todo Docu
 * @todo rename to PSI_initClient()
 *
 *
 */
int PSI_clientinit(unsigned short taskGroup);

/***************************************************************************
 *       PSI_clientexit()
 *
 *   reconfigs all variable so that a PSI_clientinit() will be successful
 */
int PSI_clientexit(void);

/**
 * @brief Send a message.
 * @todo rename to PSI_exitClient()
 *
 * Send the message @a amsg to the destination defined therein. This
 * is done by sending it to the local ParaStation daemon.
 *
 * @param msg Pointer to the message to send.
 *
 * @return On success, the number of bytes written are returned. On
 * error, -1 is returned, and errno is set appropriately.
 */
int PSI_sendMsg(void *msg);

/**
 * @brief Receive a message.
 *
 * Receive a message and store it to the place @a amsg points to. This
 * is done by receiving it from the local ParaStation daemon.
 *
 * @param msg Pointer to the place the message to store at.
 *
 * @return On success, the number of bytes written are returned. On
 * error, -1 is returned, and errno is set appropriately.
 */
int PSI_recvMsg(void *msg);

// int PSI_daemonsocket(unsigned int hostaddr);

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

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSI_H */
