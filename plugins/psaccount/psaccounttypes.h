/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_ACCOUNT_TYPES
#define __PS_ACCOUNT_TYPES

#include <stdbool.h>
#include <stdint.h>
#include <sys/resource.h>
#include <sys/types.h>

#include "pstaskid.h"

/**
 * Indices for individual task IDs in @ref AccountDataExt_t's @ref
 * taskIds member
 */
typedef enum {
    ACCID_MAX_VSIZE,     /**< task owning maximum virtual memory space */
    ACCID_MAX_RSS,       /**< task owning maximum RSS */
    ACCID_MAX_PAGES,     /**< task owning maximum number of pages */
    ACCID_MIN_CPU,       /**< task owning minimum CPU time */
    ACCID_MAX_DISKREAD,  /**< task that read max amount of data from disk */
    ACCID_MAX_DISKWRITE  /**< task that wrote max amount of data to disk */
} ExAccTaskIds_t;

/** Various resources accounted for each client / aggregated for a job */
typedef struct {
    pid_t session;             /**< client's session ID*/
    pid_t pgroup;              /**< client's process group */
    uint64_t maxThreadsTotal;
    uint64_t maxVsizeTotal;
    uint64_t maxRssTotal;
    uint64_t maxThreads;
    uint64_t maxVsize;
    uint64_t maxRss;
    uint64_t avgThreadsTotal;
    uint64_t avgThreadsCount;
    uint64_t avgVsizeTotal;
    uint64_t avgVsizeCount;
    uint64_t avgRssTotal;
    uint64_t avgRssCount;
    uint64_t cutime;
    uint64_t cstime;
    uint64_t cputime;
    uint64_t minCputime;
    uint64_t pageSize;
    uint32_t numTasks;
    uint64_t maxMajflt;
    uint64_t totMajflt;
    uint64_t totCputime;
    uint64_t cpuFreq;
    double maxDiskRead;
    double totDiskRead;
    double maxDiskWrite;
    double totDiskWrite;
    uint64_t readBytes;
    uint64_t writeBytes;
    uint64_t cpuWeight;
    PStask_ID_t taskIds[6];
    struct rusage rusage;
} AccountDataExt_t;

/** Resource usage of individual processes */
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

/**
 * @brief Register batch jobscript
 *
 * Register a batch jobscript identified by its process ID @a
 * jsPid. Additional information deposited is the string @a jobid
 * which represent the job ID as given within the batch system.
 *
 * This function shall be called by batch-system plugins like psmom or
 * psslurm in order to deposit the process ID of the jobscript only
 * known by these plugins. By this means the psaccount plugin is then
 * able to identify all processes associated to this jobscript. This
 * is required in order to achieve valid accounting information for
 * all processes belonging to the corresponding job.
 *
 * @param jsPid Process ID of the jobscript to register
 *
 * @param jobid The batch system's jobid
 *
 * @return No return value
 */
typedef void(psAccountRegisterJob_t)(pid_t jsPid, char *jobid);

/**
 * @brief Deregister job
 *
 * Deregister a job identified by the process ID of its logger @a
 * loggerTID. Unregistration stops accounting of all processes using
 * @a loggerTID as their logger.
 *
 * This one is used by psslurm.
 *
 * @param loggerTID Task ID of the to be deregistered job's logger
 *
 * @return No return value
 */
typedef void(psAccountDelJob_t)(PStask_ID_t loggerTID);

/**
 * @brief Deregister batch jobscript
 *
 * Deregister a batch jobscript identified by its process ID @a
 * jsPid. This will eliminate all jobs associated to the given
 * jobscript.
 *
 * Once a job has finished the batch plugin shall deregister the
 * jobscript in order to stop accounting of the associated processes.
 *
 * This one is used by psmom since in Torque multiple jobs might be
 * associated to a jobscript. This is different from SLURM with its
 * concept of jobsteps.
 *
 * @param jsPid Process ID of the jobscript to deregister
 *
 * @return No return value
 */
typedef void(psAccountUnregisterJob_t)(pid_t jsPid);

/**
 * @brief Switch accounting
 *
 * Switch the active accounting of the client @a clientTID depending
 * on the flag @a enable. If @a enable is true, accounting is
 * enabled. Otherwise accounting for the given client is disabled.
 *
 * This function is for use in e.g. forwarder processes not living in
 * the actual ParaStation daemon. For switching the accounting for a
 * specific process a corresponding message is sent to the local
 * daemon. The return value indicates the success of sending the
 * message.
 *
 * @param clientTID Task ID of the client to switch
 *
 * @param enable Flag the accounting action, enable (true) or disable (false)
 *
 * @return Number of bytes written to the daemon or -1 on error
 */
typedef int(psAccountSwitchAccounting_t)(PStask_ID_t clientTID, bool enable);

/**
 * @brief Enable global collection of accounting data
 *
 * Enable the global collection of accounting data depending on the
 * flag @a active.
 *
 * This function shall be called by batch-system plugins like psmom or
 * psslurm in order to enable the global collection of accounting
 * data. This way all psaccount plugins will automatic forward all
 * necessary information to the node executing to job's logger.
 *
 * @param active Flag if the global collect mode is switched on (true)
 * or off (false)
 *
 * @return No return value
 */
typedef void(psAccountSetGlobalCollect_t)(bool active);

/**
 * @brief Get account data for logger
 *
 * Get accounting data for all processes associated to the logger @a
 * logger and store the resulting data to @a accData. The content of
 * @a accData is cleared before any information is collected.
 *
 * @param logger Logger to collect account data for
 *
 * @param accData Data structure used to accumulate accounting data
 *
 * @return Return true on success and false on error
 */
typedef bool(psAccountGetDataByLogger_t)(PStask_ID_t logger,
					 AccountDataExt_t *accData);

/**
 * @brief Get account data for jobscript
 *
 * Get accounting data for all processes associated to the jobscript
 * @a jobscript and store the resulting data to @a accData. The
 * content of @a accData is cleared before any information is
 * collected.
 *
 * @param jobscript Jobscript to collect account data for
 *
 * @param accData Data structure used to accumulate accounting data
 *
 * @return Return true on success and false on error
 */
typedef bool(psAccountGetDataByJob_t)(pid_t jobscript,
				      AccountDataExt_t *accData);

/**
 * @brief Get information on sessions
 *
 * Fetch information on sessions that are currently active. This
 * includes the number of sessions in @a count, the number of active
 * users in @a userCount. Furthermore, a space separated list of
 * session IDs is written to buf.
 *
 * @param count Will hold the number of active sessions upon return
 *
 * @param buf Buffer to write information to
 *
 * @param bufSize Size of the buffer @a buf
 *
 * @param userCount Will hold the number of active users upon return
 *
 * @return No return value
 */
typedef void(psAccountGetSessionInfos_t)(int *count, char *buf, size_t bufSize,
					 int *userCount);

/**
 * @brief Test if PID is a descendant of another PID
 *
 * Check if the process with PID @a child is a descendant of the
 * process with PID @a parent.
 *
 * @param parent PID of the predecessor process
 *
 * @param child PID of the descendant process
 *
 * @return Return true if the @a child is a descendant of @a parent;
 * otherwise false is returned
 */
typedef bool(psAccountIsDescendant_t)(pid_t parent, pid_t child);

/**
 * @brief Find account client by PID and return its logger
 *
 * Find an account client by its process ID @a pid and return the
 * PID of the corresponding logger.
 *
 * @param pid PID of the client to seach for
 *
 * @return PID of the client's logger or -1 on error
 */
typedef PStask_ID_t(psAccountGetLoggerByClient_t)(pid_t pid);

/**
 * @brief Get PIDs associated to logger
 *
 * Get the process ID of all clients associated to the logger @a
 * logger and store them to @a pids. Upon return @a pids will point to
 * a memory region allocated via @ref malloc(). It is the obligation
 * of the calling function to release this memory using @ref
 * free(). Furthermore, upon return @a cnt will hold the number of
 * processes found and thus the size of @a pids.
 *
 * @param logger Logger to search for
 *
 * @param pids Pointer to dynamically allocated array of process IDs
 * upon return
 *
 * @param cnt Number of processes found upon return
 *
 * @return No return value
 */
typedef void(psAccountGetPidsByLogger_t)(PStask_ID_t logger, pid_t **pids,
					 uint32_t *cnt);

/**
 * @brief Read selected information from /proc/pid/stat.
 *
 * Get selected information concerning the process @a pid direct from
 * the /proc filesystem and store them to @a pS.
 *
 * @param pid Process ID to read the info for
 *
 * @param pS Pointer to ProcStat_t structure to store result in
 *
 * @return Returns true on success and false on error.
 */
typedef bool(psAccountReadProcStatInfo_t)(pid_t pid, ProcStat_t *pS);

/**
 * @brief Find all daemon processes of a specific user
 *
 * Find all daemonized processes of the user @a uid. If the flag @a
 * kill is true, the corresponding processes will be killed. If the
 * flag @a warn is true, one message per daemon process found is
 * written to the plugin's logger.
 *
 * @param userId User ID of the daemonized processes to find
 *
 * @param kill Flag termination of daemons found
 *
 * @param warn Flag generation of warnings for each daemon found
 *
 * @return No return value
 */
typedef void(psAccountFindDaemonProcs_t)(uid_t uid, bool kill, bool warn);

/**
 * @brief Send signal to PID and all its descendants
 *
 * Send the signal @a sig to the process @a child and all its
 * descendants. If the process to be signaled has the process ID @a
 * mypid no signal will be sent to this process. If @a pgroup is
 * larger than 0, the process group of the signaled processes receives a
 * signal, too.
 *
 * @param mypid Process ID of a process to survive
 *
 * @param child Process ID to be killed including all descendants
 *
 * @param pgroup Kill process group, too, if larger than 0
 *
 * @param sig Signal to send.
 *
 * @return Number of children getting a signal
 */
typedef int(psAccountSignalChildren_t)(pid_t mypid, pid_t child, pid_t pgroup,
				       int sig);

/**
 * @brief Send signal to session
 *
 * Send the signal @a sig to the session identified by its session ID
 * @a session.
 *
 * @param session Session ID to send signal to
 *
 * @param sig Signal to send
 *
 * @return Number of children getting a signal
 */
typedef int(psAccountSignalSession_t)(pid_t session, int sig);

#endif  /* __PS_ACCOUNT_TYPES */
