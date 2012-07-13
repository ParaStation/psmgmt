/*
 * ParaStation
 *
 * Copyright (C) 2010-2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PS_ACCOUNT_PROC
#define __PS_ACCOUNT_PROC

#include "list.h"

typedef struct {
    pid_t session;
    uid_t uid;
    struct list_head list;
} Session_Info_t;

typedef struct {
    pid_t ppid;
    pid_t pgroup;
    pid_t session;
    char state[1];
    uint64_t ctime;
    uint64_t stime;
    uint64_t cutime;
    uint64_t cstime;
    uint64_t threads;
    uint64_t vmem;
    uint64_t mem;
    uid_t uid;
} ProcStat_t;

typedef struct {
    uid_t uid;
    pid_t pid;
    pid_t ppid;
    pid_t pgroup;
    pid_t session;
    unsigned long cutime;
    unsigned long cstime;
    unsigned long threads;
    unsigned long vmem;
    unsigned long mem;
    char *cmdline;
    struct list_head list;
} Proc_Snapshot_t;

Proc_Snapshot_t ProcList;

Session_Info_t SessionList;

/**
 * @brief Initialize the proc list.
 *
 * @return No return value.
 */
void initProcList();

/**
 * @brief Collect all mem/vmem from every related child process.
 *
 * Collect all mem/vmem from every related child process. It will
 * use the /proc snapshot which should be updated before calling
 * this function.
 *
 * @param pid The pid of the child to get the memory for.
 *
 * @return Returns a proc snapshot structure which contains the
 * accounting data for the requested pid.
 */
Proc_Snapshot_t *getAllClientChildsMem(pid_t pid);

/**
 * @brief Create a snapshot of the /proc filesystem.
 *
 * Create a snapshot of the /proc filesystem. This snapshot will
 * be used to calculate every accounting data for processes which
 * are monitored. This way the expensive task of /proc file reading
 * will only be done once.
 *
 * @return No return value.
 */
void updateProcSnapshot(int extended);

/**
 * @brief Find a proc snapshot identified by the pid.
 *
 * @param pid The pid of the snapshot.
 *
 * @return Returns the found snapshot or 0 on error.
 */
Proc_Snapshot_t *findProcSnapshot(pid_t pid);

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
int isChildofParent(pid_t parent, pid_t child);

/**
 * @brief Delete all proc snapshots.
 *
 * @return No return value.
 */
void clearAllProcSnapshots();

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
void getSessionInformation(int *count, char *buf, size_t bufsize, int *userCount);

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
int sendSignal2AllChildren(pid_t mypid, pid_t child, pid_t pgroup, int sig);

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
int sendSignal2Session(pid_t session, int sig);

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
void findDaemonProcesses(uid_t userId, int kill, int warn);

/**
 * @brief Read selected informations from /proc/pid/stat.
 *
 * @param pid The pid to read the info for.
 *
 * @param pS A pointer to a ProcStat_t structure to save the result in.
 *
 * @return Returns 1 on success and 0 on error.
 */
int readProcStatInfo(pid_t pid, ProcStat_t *pS);

#endif
