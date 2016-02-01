/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PS_ACCOUNT_HANDLES
#define __PS_ACCOUNT_HANDLES

#include "psaccounttypes.h"
#include "pstask.h"

/**
 * @brief Send a signal to a session.
 *
 * @param session The session ID to send the signal to.
 *
 * @param sig The signal to send.
 *
 * @return Returns the number of children which the signal
 * was sent to.
 */
int (*psAccountsendSignal2Session)(pid_t, int);

/**
 * @brief Register a PBS jobscript via its pid.
 *
 * This function is called by the psmom, because only
 * the psmom knows the pid of the jobscript. The psaccount
 * plugin is then able to identify all processes associated
 * with this jobscript.
 *
 * This enables the psmom the get valid accounting data for
 * all processes in the job, although it only knows the jobscript.
 *
 * @param jsPid The pid of the jobscript to register.
 *
 * @param jobid The torque jobid.
 *
 * @return No return value.
 */
void (*psAccountRegisterJob)(pid_t, char *);

/**
 * @brief Unregister a PBS jobscript.
 *
 * The job has finished and the psmom is telling us to stop
 * accounting for this jobscript.
 *
 * @param jsPid The pid of the jobscript to un-register.
 *
 * @return No return value.
 */
void (*psAccountUnregisterJob)(pid_t);

/**
 * @brief Enable the global collection of accounting data.
 *
 * This function is called by the psmom to enable
 * the global collection of accounting data. This way all
 * psaccount plugins will automatic forward all information
 * to the node were the logger is executed.
 *
 * @param active If flag is 1 the global collect mode is switched
 * on. If the flag is 0 it is swichted off.
 *
 * @return No return value.
 */
void (*psAccountSetGlobalCollect)(int);

/**
 * @brief Get account info for a jobscript.
 *
 * @param jobscript The jobscript to get the info for.
 *
 * @param accData A pointer to an accountInfo structure which will receive the
 * requested information.
 *
 * @return Returns 1 on success and 0 on error.
 */
int (*psAccountGetJobInfo)(pid_t, psaccAccountInfo_t *);

int (*psAccountGetJobData)(pid_t, AccountDataExt_t *);

/**
 * @brief Register a PBS jobscript via its pid.
 *
 * This function is called by the psmom, because only
 * the psmom knows the pid of the jobscript. The psaccount
 * plugin is then able to identify all processes associated
 * with this jobscript.
 *
 * This enables the psmom the get valid accounting data for
 * all processes in the job, although it only knows the jobscript.
 *
 * @param jsPid The pid of the jobscript to register.
 *
 * @param jobid The torque jobid.
 *
 * @return No return value.
 */
void (*psAccountRegisterMOMJob)(pid_t, char *);

/**
 * @brief Unregister a PBS jobscript.
 *
 * The job has finished and the psmom is telling us to stop
 * accounting for this jobscript.
 *
 * @param jsPid The pid of the jobscript to un-register.
 *
 * @return No return value.
 */
void (*psAccountUnregisterMOMJob)(pid_t);

/**
 * @brief Provide information about currently active sessions.
 *
 * @param count Will be set to the number of active sessions in the system.
 *
 * @param buf The buffer to write the information to.
 *
 * @param bufsize The size of the buffer.
 *
 * @param userCount Will be set to the number of active users in the system.
 *
 * @return No return value.
 */
void (*psAccountGetSessionInfos)(int *, char *, size_t, int *);

/**
 * @brief Send a signal to a pid and all its children.
 *
 * @param mypid The pid of myself.
 *
 * @param child The pid of the child to send the signal to.
 *
 * @param pgroup The pgroup of the child to send the signal to.
 *
 * @param sig The signal to send.
 *
 * @return No return value.
 */
int (*psAccountSignalAllChildren)(pid_t, pid_t, pid_t, int);

/**
 * @brief Test if a pid is the parent of an other pid.
 *
 * @param parent The pid of the parent process.
 *
 * @param child The pid of the child process.
 *
 * @return Returns 1 if the second pid is a child of the
 * first pid, otherwise 0 is returned.
 */
int (*psAccountisChildofParent)(pid_t, pid_t);

/**
 * @brief Find all daemon processes for the specified user.
 *
 * @param userId The user ID of the daemons to find.
 *
 * @param kill If set to 1 the found daemons will be terminated.
 *
 * @param warn If set to 1 a log message for each daemon will be generated.
 *
 * @return No return value.
 */
void (*psAccountFindDaemonProcs)(uid_t, int, int);

/**
 * @brief Find an account client by its pid and return the corresponding logger.
 *
 * @param pid The pid of the client to find.
 *
 * @return Returns the task id of the clients logger or -1 on error.
 */
PStask_ID_t (*psAccountgetLoggerByClientPID)(pid_t);

/**
 * @brief Read selected informations from /proc/pid/stat.
 *
 * @param pid The pid to read the info for.
 *
 * @param pS A pointer to a ProcStat_t structure to save the result in.
 *
 * @return Returns 1 on success and 0 on error.
 */
int (*psAccountreadProcStatInfo)(pid_t, ProcStat_t *);

int (*psAccountGetDataByLogger)(PStask_ID_t, AccountDataExt_t *);

int (*psAccountGetPidsByLogger)(PStask_ID_t,  pid_t **, uint32_t *);

void (*psAccountDelJob)(PStask_ID_t);

int (*psAccountSwitchAccounting)(PStask_ID_t, int);

#endif
