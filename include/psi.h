/*
 *               ParaStation3
 * psi.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psi.h,v 1.20 2003/02/27 18:29:54 eicker Exp $
 *
 */
/**
 * @file
 * User-functions for interaction with the ParaStation system.
 *
 * $Id: psi.h,v 1.20 2003/02/27 18:29:54 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSI_H
#define __PSI_H

#include <sys/types.h>
#include "pstask.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** @todo Documentation */

/**
 * @brief @todo Docu
 *
 *
 */
int PSI_initClient(PStask_group_t taskGroup);

/***************************************************************************
 *       PSI_clientexit()
 *
 *   reconfigs all variable so that a PSI_clientinit() will be successful
 */
int PSI_exitClient(void);

/**
 * @brief Get psid's version string.
 *
 * Get the CVS version string of the local psid that is contacted.
 *
 * @return The version string is returned.
 */
char *PSI_getPsidVersion(void);

/**
 * @brief Send a message.
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

int PSI_sendFinish(long parenttid);

int PSI_recvFinish(int num);

/**
 * @brief Transform to psilogger
 *
 * Transforms the current process to a psilogger process. If @a
 * command is different from NULL, it will be executed using the
 * system(3) call after the logger has done his job, i.e. after all
 * clients have finished.
 *
 * @param command Command to execute after by the logger after all
 * clients have closed their connection.
 *
 * @return No return value.
 */
void PSI_execLogger(const char *command);


#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSI_H */
