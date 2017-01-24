/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_ACCOUNT_PROC
#define __PS_ACCOUNT_PROC

#include <stdbool.h>

#include "list.h"
#include "psaccounttypes.h"

/** Internal state of ProcSnapshot_t structure */
typedef enum {
    PROC_USED,                /**< In use */
    PROC_UNUSED,              /**< Unused and ready for re-use */
    PROC_DRAINED,             /**< Unused and ready for discard */
} ProcSnapshot_state_t;

/** Snapshot of a process' entry in the /proc filesystem */
typedef struct {
    list_t next;              /**< used to put into list */
    uid_t uid;                /**< Process' user ID */
    pid_t pid;                /**< Process' process ID */
    pid_t ppid;               /**< Process' parent process ID */
    pid_t pgrp;               /**< Process' process group ID */
    pid_t session;            /**< Process' session ID */
    unsigned long cutime;     /**< Time process has consumed in user space */
    unsigned long cstime;     /**< Time process has consumed for system calls */
    unsigned long threads;    /**< Process' number of threads */
    unsigned long vmem;       /**< Process' virtual memory */
    unsigned long mem;        /**< Process' resident set size */
    unsigned long majflt;     /**< Process' major page faults */
    uint16_t cpu;             /**< Last CPU the process last executed on */
    ProcSnapshot_state_t state; /**< flag internal state of structure */
} ProcSnapshot_t;

/** Resource usage of individual processes directly from /proc/<PID>/stat */
typedef struct {
    pid_t ppid;        /**< parent process ID */
    pid_t pgrp;        /**< process group */
    pid_t session;     /**< session ID */
    char state[1];     /**< process' state (R,S,D,Z or T) */
    uint64_t utime;    /**< user time consumed by process */
    uint64_t stime;    /**< system time consumed by process */
    uint64_t cutime;   /**< user time consumed by process' descendants */
    uint64_t cstime;   /**< system time consumed by process' descendants */
    uint64_t threads;  /**< process' number of threads */
    uint64_t vmem;     /**< process' virtual address space */
    uint64_t mem;      /**< process' RSS */
    uint64_t majflt;   /**< # of major pagefaults triggered by process */
    uint64_t cmajflt;  /**< # of major pagefaults triggered by descendants  */
    uint16_t cpu;      /**< CPU the process was scheduled on lately */
    uid_t uid;         /**< process' user ID */
} ProcStat_t;

/** Some I/O resources consumed by a process */
typedef struct {
    uint64_t diskRead;        /**< # bytes read */
    uint64_t diskWrite;       /**< # bytes written */
    uint64_t readBytes;       /**< # bytes read from disk */
    uint64_t writeBytes;      /**< # bytes written to disk */
} ProcIO_t;

/**
 * @brief Initialize the proc module
 *
 * Initialize the proc module of the psaccount plugin.
 *
 * @return No return value
 */
void initProc(void);

/**
 * @brief Finalize the proc module
 *
 * Finalize the proc module the psaccount plugin. This includes
 * free()ing all dynamic memory not used any longer.
 *
 * @return No return value
 */
void finalizeProc(void);

/**
 * @brief Collect resource data from all descendant processes
 *
 * Collect resource data from all descendant processs of the process @a
 * pid. This will utilize the /proc snapshot which shall be updated
 * before calling this function in order to guarantee up to date
 * data. The resource data is collected in the ProcSnapshot_t
 * structure @a res is pointing to.
 *
 * @param pid PID of the process to get the data for
 *
 * @param res Pointer to a ProcSnapshot_t structure to collect data
 *
 * @return No return value
 */
void getDescendantData(pid_t pid, ProcSnapshot_t *res);

/**
 * @brief Create new snapshot of the /proc filesystem.
 *
 * Create a new snapshot of the /proc filesystem. This snapshot will
 * be used to calculate all accounting data for processes to be
 * monitored. The aim is to reduce the overhead of traversing /proc.
 *
 * @return No return value
 */
void updateProcSnapshot(void);

/**
 * @brief Find proc snapshot
 *
 * Find the snapshot of the process identified by the pid.
 *
 * @param pid PID of the requested process
 *
 * @return Return a pointer to the corresponding snapshot or NULL if
 * no snapshot was found
 */
ProcSnapshot_t *findProcSnapshot(pid_t pid);

/**
 * @brief Test kinship of processes
 *
 * Test if @a parent is actually an ancestor process of @a child
 *
 * @param parent PID of the ancestor process
 *
 * @param child PID of the descendant process
 *
 * @return Returns true if @a child is the PID is a descendant of the
 * process with PID @a parent; otherwise false is returned
 */
bool isDescendant(pid_t parent, pid_t child);

/**
 * @brief Provide information on active sessions
 *
 * Provide information on sessions currently active. Upon return @a
 * count will contain the number of active session in the system, @a
 * buf will hold a list of these sessions, and @a userCount provides
 * the number f active users in the system. @a size is used to provide
 * information on the size of @a buf.
 *
 * @param count Number of active sessions in the system upon return
 *
 * @param buf Buffer used to store a list of sessions
 *
 * @param size Size of @ref buf
 *
 * @param userCount Number of active users in the system upon return
 *
 * @return No return value
 */
void getSessionInfo(int *count, char *buf, size_t size, int *userCount);

/**
 * @brief Send signal to process and all descendants
 *
 * Send the signal @a sig to the process @a child and all its
 * descendants. At the same time it is ensured that the signal will
 * not be sent to the process @a mypid. This is to ensure that a
 * process will not kill itself by accident. Beyond that the not only
 * the processes itself are killed but also the corresponding process
 * group @a pgrp unless it is 0. In the latter case the process group
 * of the process itself as determined by psaccount will receive the
 * signal.
 *
 * @param mypid My own PID to protect myself
 *
 * @param child PID of the process to receive the first signal
 *
 * @param pgrp Process group to also receive the signal
 *
 * @param sig Signal to send
 *
 * @return Total number of signals sent
 */
int signalChildren(pid_t mypid, pid_t child, pid_t pgroup, int sig);

/**
 * @brief Send signal to session
 *
 * Send the signal @a sig to all processes being part of the session
 * identified by @a session and all their descendants.
 *
 * @param session Session ID to receive the signal
 *
 * @param sig Signal to send.
 *
 * @return total number of signals sent
 */
int signalSession(pid_t session, int sig);

/**
 * @brief Find daemon processes for the specified user
 *
 * Find all processes of the user with ID @a uid being found in a
 * daemonized state. If the @a warn flag is set, a series of messages
 * is sent to the plugin's log. If the @a kill flag is set, each
 * daemonized process found is killed.
 *
 * @param uid User ID of the daemons to find
 *
 * @param kill Flag killing the found daemons
 *
 * @param warn Flag generating a log message for each daemon
 *
 * @return No return value
 */
void findDaemonProcs(uid_t uid, bool kill, bool warn);

/**
 * @brief Read selected information from /proc/<pid>/stat
 *
 * Read selected information from /proc/<pid>/stat for a given process
 * identified by its PID @a pid. Information is stored in a structure
 * of type ProcStat_t @a pS is pointing to.
 *
 * @param pid PID to collect data for
 *
 * @param pS Pointer to a ProcStat_t structure used to store results
 *
 * @return Returns true on success and false in the case of an error
 */
bool readProcStat(pid_t pid, ProcStat_t *pS);


/**
 * @brief Read selected information from /proc/<pid>/io
 *
 * Read selected information from /proc/<pid>/io for a given process
 * identified by its PID @a pid. Information is stored in a structure
 * of type ProcIO_t @a io is pointing to.
 *
 * @param pid PID of the process to get information for
 *
 * @param io Pointer to a ProcIO_t structure used to store results
 *
 * @return Returns true on success and false in the case of an error
 */
bool readProcIO(pid_t pid, ProcIO_t *io);

/**
 * @brief Get CPU frequency
 *
 * Get the frequency of the CPU identified by @a cpuID.
 *
 * @param cpuID ID of the CPU to get the frequency for
 *
 * @return Actual frequency of the CPU
 */
int getCpuFreq(int cpuID);

#endif  /* __PS_ACCOUNT_PROC */
