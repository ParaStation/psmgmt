/*
 *               ParaStation3
 * psidsignal.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidsignal.h,v 1.2 2003/04/10 17:44:51 eicker Exp $
 *
 */
/**
 * @file
 * Functions for sending signals to ParaStation tasks within the Daemon
 *
 * $Id: psidsignal.h,v 1.2 2003/04/10 17:44:51 eicker Exp $
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

/** @todo more docu */

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
 * PSID_sendSignal
 */
void PSID_sendSignal(long tid, uid_t uid, long senderTid, int signal,
		     int pervasive);

/**
 * Send the signals to all task which have asked for
 */
void PSID_sendAllSignals(PStask_t *task);

/**
 * Send the signals to parent and childs
 */
void PSID_sendSignalsToRelatives(PStask_t *task);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSIDSIGNAL_H */
