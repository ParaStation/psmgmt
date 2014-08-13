/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * Functions for sending signals to ParaStation tasks within the daemon
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
 * @brief Initialize signal sending stuff
 *
 * Initialize the signal sending framework. This registers the
 * necessary message handlers.
 *
 * @return No return value.
 */
void initSignal(void);

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
 * @brief Send signals to parent and children.
 *
 * Send signals to parent process and all child processes of the task
 * described by @a task. If a parent process is existing, the signal
 * -1 will be sent to it (This will be replaced by SIGTERM as
 * default). Furthermore all children will be signaled with the same
 * signal.
 *
 * The parent task is determined via the @a ptid member of @a task,
 * the children will be taken from the @a childList signal list within
 * @a task. This signal list will be destroyed during execution of
 * this function.
 *
 * @param task The task structure describing the task to be handled.
 *
 * @return No return value.
 */
void PSID_sendSignalsToRelatives(PStask_t *task);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSIDSIGNAL_H */
