/*
 *               ParaStation3
 * psi.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psi.h,v 1.22 2003/10/23 16:27:35 eicker Exp $
 *
 */
/**
 * @file
 * User-functions for interaction with the ParaStation system.
 *
 * $Id: psi.h,v 1.22 2003/10/23 16:27:35 eicker Exp $
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

/**
 * @brief Initialize PSI.
 *
 * Initialize the PSI stuff within the current process. This includes
 * to try to connect the local daemon and to register to this
 * daemon. Furthermore some basic setup is made.
 *
 * If it is impossible to connect the local daemon, an error is returned.
 *
 * The fashion which is used to connect the daemon depends on @a
 * taskGroup. If this is @ref TG_ADMIN which usually denotes a
 * psiadmin(1) process, it will be first tried to contact the
 * daemon. If this fails, the attempt to start the daemon via the
 * (x)inetd(8) and to contact it again will be made for 5 times. If
 * there is finally no contact, an error is returned.
 *
 * For all other values of @a taskgroup only one attempt to contact
 * the local daemon is made without trying to start the daemon.
 *
 * @param taskGroup The kind of task trying to initialize the PSI
 * stuff. This is used to register to the local daemon.
 *
 * @return If a connection to the local daemon could be established, 1
 * is returned, or 0 otherwise.
 */
int PSI_initClient(PStask_group_t taskGroup);

/**
 * @brief Exit PSI.
 *
 * Shutdown PSI. This will close the connection to the local daemon
 * and reset everything in a way that the daemon might be connected
 * again. Since no releasing towards the daemon is done, other
 * processes within the parallel task might be killed after calling
 * this function.
 *
 * @return 1 is always returned.
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

/**
 * @brief Register for notification of foreign processes death.
 *
 * Register for notification of a foreign processes death. I.e. send
 * me the signal @a sig, as soon as the foreign process with task ID
 * @a tid dies, expectedly or unexpectedly.
 *
 * A process dies expectedly, if it has called PSI_release() befor
 * terminating its execution.
 *
 * It is not necessary to register child processes since they will
 * send a special signal, which is SIGTERM as default, to their
 * parents anyway. The type of signal might be changed via the
 * registration of the wanted signal to the @a tid of 0L using this
 * function.
 *
 * @param tid The task ID of the process whose death I'm interested
 * in.
 *
 * @param sig The signal that should be send when the process dies.
 *
 * @return On success, 0 is returned. Or -1, if an error occurred.
 */
int PSI_notifydead(PStask_ID_t tid, int sig);

/**
 * @brief Release a process.
 *
 * Release the process with task ID @a tid from sending a signal to me
 * on its death. This might be used to cancel prior PSI_notifydead()
 * calls with the same task ID.
 *
 * The special case where @a tid is PSC_getMyTID() will release the
 * local process from receiving any signal and furthermore from
 * sending a signal to its parent process. Ususally the parent process
 * will get a special signal if any child will die. A call to this
 * function will suppress this signal and usually keep the parent
 * alive.
 *
 * This is typically used upon the correct end of a processes being
 * part of a parallel task.
 *
 * @param tid The task ID of the process to get released.
 *
 * @return On success, 0 is returned. Or -1, if an error occurred.
 */
int PSI_release(PStask_ID_t tid);

/**
 * @brief Request signal's sender.
 *
 * Request which local or foreign process sent the signal @a sig to me
 * recently. This will only work when the signal was send via
 * ParaStation, i.e. if the signal was initiated by the death of the
 * sending process or via an explicite call to PSI_kill() from the
 * sending process.
 *
 * @param sig The signal recently received which sender should be
 * determined.
 *
 * @return On success, the senders task ID is returned, or -1 if the
 * sender could not be determined.
 */
PStask_ID_t PSI_whodied(int sig);

/**
 * @brief Send a finish message.
 *
 * Send a PSP_CD_SPAWNFINISH message to the process with task ID @a
 * parenttid, which is usually the parent process listening to this
 * kind of messages within PSI_recvFinish().
 *
 * This might be used in order to inform the parent process about the
 * successful finalization of a childs process as it might needed
 * within e.g. a MPI_Finalize().
 *
 * @param parenttid The task ID of the process sending to.
 *
 * @return On success, 0 is returned. Or -1, if an error occured.
 */
int PSI_sendFinish(PStask_ID_t parenttid);

/**
 * @brief Receive finish messages.
 *
 * Receive a total of @a num PSP_CD_SPAWNFINISH messages from various
 * processes, which are usually child processes of the actual process
 * sending them via PSI_sendFinish().
 *
 * This might be used in order to inform the parent process about the
 * successful finalization of child processes as it might needed
 * within e.g. a MPI_Finalize().
 *
 * @param num The number of messages expected to receive.
 *
 * @return On success, 0 is returned. Or 1, if an error occured.
 */
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
