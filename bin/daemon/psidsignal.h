/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2007 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file
 * Functions for sending signals to ParaStation tasks within the Daemon
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDSIGNAL_H
#define __PSIDSIGNAL_H

#include <sys/types.h>

#include "pstask.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Send signal to process.
 *
 * Send the signal @a sig to the process or process group @a pid. Send
 * the signal as user @a uid.
 *
 * This is mainly a wrapper to the kill(2) system call.
 *
 *
 * @param pid Depending on the value, @a pid my have different meanings:
 *
 * -If pid is positive, then signal sig is sent to pid.
 *
 * -If pid equals 0, then sig is sent to every process in the process
 * group of the current process.
 *
 * -If pid equals -1, then sig is sent to every process except for
 * process 1 (init), but see below.
 *
 * -If pid is less than -1, then sig is sent to every process in the
 * process group -pid.
 *
 * @param sig Signal to send.
 *
 * @param uid Convert to this user via the setuid(2) system call
 * before really sendig the signal.
 *
 *
 * @return On success, zero is returned.  On error, -1 is returned,
 * and errno is set appropriately.
 */
int PSID_kill(pid_t pid, int sig, uid_t uid);

/**
 * @brief Send signal to task.
 *
 * Send the signal @a sig to the task with unique ID @a tid. The
 * signal will be send as user @a uid. The sender of the signal, which
 * might be determined from the process using the PSI_whodied()
 * function, has the unique ID @a senderTid.
 *
 * If @a pervasive is different from 0, i.e. if it's logical true, the
 * signal @a sig will also be send to all children of task @a tid,
 * including their children and so on.
 *
 * In contrast to PSID_kill(), the actual process to be signaled might
 * live on a remote node.
 *
 * @param tid The unique ID of the task to be signaled.
 *
 * @param uid The user ID of the user that (virtually) sends the
 * signal.
 * 
 * @param senderTid The unique ID of the task that (virtually) sends
 * the signal.
 * 
 * @param sig The signal to send.
 * 
 * @param pervasive Flag the signal to be pervasive. If different from
 * 0, all children of @a tid will be signaled, too. Otherwise, only @a
 * tid will be signaled.
 *
 * @param answer Flag the creation of an answer message.
 *
 * @return No return value.
 */
void PSID_sendSignal(PStask_ID_t tid, uid_t uid, PStask_ID_t senderTid,
		     int sig, int pervasive, int answer);

/**
 * @brief Send signals to all tasks which have asked for.
 *
 * Send signals to all tasks which have registered to get this signal
 * on exit of the task @a task. I.e. this function is usually called
 * after the process described by @a task has exited.
 *
 * The task that want to receive signals are determined from the @a
 * signalReceiver signal list member of @a task. This signal list will
 * be destroyed during execution of this function.
 *
 * @param task The task structure describing the task to be handled.
 *
 * @return No return value.
 */
void PSID_sendAllSignals(PStask_t *task);

/**
 * @brief Send signals to parent and childs.
 *
 * Send signals to parent process and all child processes of the task
 * described by @a task. If a parent process is existing, the signal
 * -1 will be sent to it (This will be replaced by SIGTERM as
 * default). Furthermore all children will be signaled with the same
 * signal.
 *
 * The parent task is determined via the @a ptid member of @a task,
 * the children will be taken from the @a childs signal list within @a
 * task. This signal list will be destroyed during execution of this
 * function.
 *
 * @param task The task structure describing the task to be handled.
 *
 * @return No return value.
 */
void PSID_sendSignalsToRelatives(PStask_t *task);

/**
 * @brief Handle a PSP_CD_SIGNAL message.
 *
 * Handle the message @a msg of type PSP_CD_SIGNAL.
 *
 * With this kind of message signals might be send to remote
 * processes. On the receiving node a process is fork()ed in order to
 * set up the requested user and group IDs and than to actually send
 * the signal to the receiving process.
 *
 * Furthermore if the signal sent is marked to be pervasive, this
 * signal is also forwarded to all child processes of the receiving
 * process, local or remote.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_SIGNAL(DDSignalMsg_t *msg);

/**
 * @brief Handle a PSP_CD_NOTIFYDEAD message.
 *
 * Handle the message @a msg of type PSP_CD_NOTIFYDEAD.
 *
 * With this kind of message the sender requests to get receive the
 * signal defined within the message as soon as the recipient task
 * dies. Therefore the corresponding information is store in two
 * locations, within the controlled task and within the requester
 * task.
 *
 * The result, i.e. if registering the signal was successful, is sent
 * back to the requester within a answering PSP_CD_NOTIFYDEADRES
 * message.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_NOTIFYDEAD(DDSignalMsg_t *msg);

/**
 * @brief Handle a PSP_CD_NOTIFYDEADRES message.
 *
 * Handle the message @a msg of type PSP_CD_NOTIFYDEADRES.
 *
 * The message will be forwarded to its final destination, which
 * usually is a client of the local daemon.
 *
 * Furthermore this client task will be marked to expect the
 * corresponding signal.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_NOTIFYDEADRES(DDSignalMsg_t *msg);

/**
 * @brief Handle a PSP_CD_RELEASE message.
 *
 * Handle the message @a msg of type PSP_CD_RELEASE.
 *
 * The actual task to be done is to release a task, i.e. to tell the
 * task not to send a signal to the sender upon exit.
 *
 * Two different cases have to be distinguished:
 *
 * - The releasing task will release a different task, which might be
 * local or remote. In the latter case, the message @a msg will be
 * forwarded to the corresponding daemon.
 * 
 * - The task to release is identical to the releasing tasks. This
 * special case tells the local daemon to expect the corresponding
 * process to disappear, i.e. not to signal the parent task upon exit
 * as long as no error occured. The corresponding action are
 * undertaken within the @ref releaseTask() function called.
 *
 * In all cases adequate PSP_CD_RELEASERES message are send to the
 * task requesting the release.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_RELEASE(DDSignalMsg_t *msg);

/**
 * @brief Handle a PSP_CD_RELEASERES message.
 *
 * Handle the message @a msg of type PSP_CD_RELEASERES.
 *
 * The message will be forwarded to its final destination, which
 * usually is a client of the local daemon, unless there are further
 * pending PSP_CD_RELEASERES messages to the same client. In this
 * case, the current message @a msg is thrown away. Only the last
 * message will be actually delivered to the client requesting for
 * release.
 *
 * Furthermore this client task will be marked to not expect the
 * corresponding signal any longer.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_RELEASERES(DDSignalMsg_t *msg);

/**
 * @brief Handle a PSP_CD_WHODIED message.
 *
 * Handle the message @a msg of type PSP_CD_WHODIED.
 *
 * With this kind of message a client to the local daemon might
 * request the sender of a signal received. Therefor all signals send
 * to local clients are stored to the corresponding task ID and looked
 * up within this function. The result is sent back to the requester
 * within a answering PSP_CD_WHODIED message.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_WHODIED(DDSignalMsg_t *msg);

/**
 * @brief Handle a PSP_DD_NEWPARENT message.
 *
 * Handle the message @a msg of type PSP_DD_NEWPARENT.
 *
 * This kind of message is send to a task's child in order to update
 * the information concerning the parent task. From now on the
 * receiving task will handle its grandparent as its own parent,
 * i.e. it will send and expect signals if one of the corresponding
 * processes dies.
 *
 * In all cases adequate PSP_CD_RELEASERES message are send to the
 * task requesting passing over their child.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_NEWPARENT(DDErrorMsg_t *msg);

/**
 * @brief Handle a PSP_DD_NEWCHILD message.
 *
 * Handle the message @a msg of type PSP_DD_NEWCHILD.
 *
 * This kind of message is send to a task's parent task in order to
 * pass over a child. From now on the receiving task will handle its
 * grandchild as its own child, i.e. it will send and expect signals
 * if one of the corresponding processes dies.
 *
 * In all cases adequate PSP_CD_RELEASERES message are send to the
 * task requesting passing over their child.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_NEWCHILD(DDErrorMsg_t *msg);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSIDSIGNAL_H */
