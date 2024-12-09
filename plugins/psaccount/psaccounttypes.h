/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
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
    ACCID_MAX_DISKWRITE, /**< task that wrote max amount of data to disk */
    ACCID_MIN_ENERGY,    /**< (task) node consumed minimum energy */
    ACCID_MAX_ENERGY,	 /**< (task) node consumed maximum energy */
    ACCID_MIN_POWER,     /**< (task) node consumed minimum power */
    ACCID_MAX_POWER,	 /**< (task) node consumed maximum power */
    ACCID_MIN_IC_RECV,   /**< (task) node received minimum bytes over IC */
    ACCID_MAX_IC_RECV,   /**< (task) node received maximum bytes over IC */
    ACCID_MIN_IC_SEND,   /**< (task) node send minimum bytes over IC */
    ACCID_MAX_IC_SEND    /**< (task) node send maximum bytes over IC */
} ExAccTaskIds_t;

/** Various resources accounted for each client / aggregated for a job */
typedef struct {
    pid_t session;             /**< client's session ID*/
    pid_t pgroup;              /**< client's process group */
    uint64_t maxThreadsTotal;  /**< accumulated max. number of threads */
    uint64_t maxVsizeTotal;    /**< accumulated max. virt. mem. size (in kB) */
    uint64_t maxRssTotal;      /**< accumulated max. RSS (in kB) */
    uint64_t maxThreads;       /**< maximum number of threads */
    uint64_t maxVsize;         /**< maximum virtual memory size (in kB) */
    uint64_t maxRss;           /**< maximum resident set memory size (in kB) */
    uint64_t avgThreadsTotal;  /**< sum of number of threads samples */
    uint64_t avgThreadsCount;  /**< number of addends in @ref avgThreadsTotal */
    uint64_t avgVsizeTotal;    /**< sum of virtual memory size samples (in kB)*/
    uint64_t avgVsizeCount;    /**< number of addends in @ref avgVsizeTotal */
    uint64_t avgRssTotal;      /**< sum of res. set mem. size samples (in kB)*/
    uint64_t avgRssCount;      /**< number of addends in @ref avgRssTotal */
    uint64_t cutime;	       /**< user time consumed by process'
				    descendants */
    uint64_t cstime;	       /**< system time consumed by process'
				    descendants */
    uint64_t minCputime;
    uint64_t pageSize;	       /**< System's page size. */
    uint32_t numTasks;	       /**< number of tasks */
    uint64_t maxMajflt;	       /**< maximum major page faults */
    uint64_t totMajflt;	       /**< total major page faults */
    uint64_t totCputime;       /**< total CPU time */
    uint64_t cpuFreq;	       /**< CPU frequency */
    double maxDiskRead;	       /**< maximum local disk read */
    double totDiskRead;	       /**< total local disk read */
    double maxDiskWrite;       /**< maximum local disk write */
    double totDiskWrite;       /**< total local disk write */
    uint64_t diskReadBytes;    /**< number of bytes read from local disk */
    uint64_t diskWriteBytes;   /**< number of bytes written to local disk */
    uint64_t cpuWeight;        /**< needed to calculate the CPU frequency */
    PStask_ID_t taskIds[14];   /**< Indices for individual task IDs,
				    see ExAccTaskIds_t */
    struct rusage rusage;      /**< resource usage collect upon client's dead */
    uint64_t energyTot;	       /**< total consumed energy in joules */
    uint64_t energyMin;	       /**< minimum consumed energy in joules */
    uint64_t energyMax;	       /**< maximum consumed energy in joules */
    uint64_t powerAvg;	       /**< average power consumption in watts */
    uint64_t powerMin;	       /**< minimum power consumption in watts */
    uint64_t powerMax;         /**< maximum power consumption in watts */
    uint64_t IC_recvBytesTot;  /**< total bytes received from interconnect */
    uint64_t IC_recvBytesMin;  /**< minimum bytes received from interconnect */
    uint64_t IC_recvBytesMax;  /**< maximum bytes received from interconnect */
    uint64_t IC_sendBytesTot;  /**< total bytes send using interconnect */
    uint64_t IC_sendBytesMin;  /**< minimum bytes send using interconnect */
    uint64_t IC_sendBytesMax;  /**< maximum bytes send using interconnect */
    uint64_t FS_writeBytes;    /**< bytes written to file-system */
    uint64_t FS_readBytes;     /**< bytes read from file-system */
} AccountDataExt_t;

/** Option (sub-module) to influence/query */
typedef enum {
    PSACCOUNT_OPT_MAIN,		/**< main account timer */
    PSACCOUNT_OPT_IC,		/**< interconnect options */
    PSACCOUNT_OPT_ENERGY,	/**< energy options */
    PSACCOUNT_OPT_FS,		/**< file-system options */
} psAccountOpt_t;

/** Option to control an account script */
typedef enum {
    PSACCOUNT_SCRIPT_START = 0x300, /**< start an account script */
    PSACCOUNT_SCRIPT_STOP,	    /**< stop an account script */
    PSACCOUNT_SCRIPT_ENV_SET,	    /**< add environment variable */
    PSACCOUNT_SCRIPT_ENV_UNSET,	    /**< delete environment variable */
} psAccountCtl_t;

/** Node energy and power consumption data */
typedef struct {
    uint32_t powerMin;		/**< minimum power consumption */
    uint32_t powerMax;		/**< maximal power consumption */
    uint32_t powerCur;		/**< current power consumption */
    uint32_t powerAvg;		/**< average power consumption */
    uint64_t energyBase;	/**< energy base when psaccount was started */
    uint64_t energyCur;		/**< energy consumption since last update */
    time_t lastUpdate;		/**< time stamp of the last update */
} psAccountEnergy_t;

/** Node interconnect I/O data */
typedef struct {
    int16_t port;		/**< port number */
    uint64_t recvBytes;		/**< number of bytes red */
    uint64_t sendBytes;		/**< number of bytes written */
    uint64_t recvPkts;		/**< number of packets red */
    uint64_t sendPkts;		/**< number of packets written */
    time_t lastUpdate;		/**< time stamp of the last update */
} psAccountIC_t;

/** Node file-system I/O data */
typedef struct {
    uint64_t readBytes;		/**< number of bytes red */
    uint64_t writeBytes;	/**< number of bytes written */
    uint64_t numReads;		/**< number of reads */
    uint64_t numWrites;		/**< number of writes */
    time_t lastUpdate;		/**< time stamp of the last update */
} psAccountFS_t;

/** Holding all local node informations for exchange with other plugins */
typedef struct {
    psAccountFS_t filesytem;	/**< local file-system counter */
    psAccountIC_t interconnect;	/**< local interconnect counter */
    psAccountEnergy_t energy;   /**< local energy data */
} psAccountInfo_t;

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
 * Deregister a job identified by the task ID of its root task @a
 * rootTID. Unregistration stops accounting of all processes having
 * @a rootTID as their root process.
 *
 * Typically, a job's root task is the corresponding logger
 * task. Nevertheless, this might be different for Slurm steps spawned
 * via PMI(x)_Spawn() when the corresponding step forwarder might be
 * used.
 *
 * This one is used by psslurm.
 *
 * @param rootTID Task ID of the to be deregistered job's root task
 *
 * @return No return value
 */
typedef void(psAccountDelJob_t)(PStask_ID_t rootTID);

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
 * necessary information to the node executing to job's root task.
 *
 * @param active Flag if the global collect mode is switched on (true)
 * or off (false)
 *
 * @return No return value
 */
typedef void(psAccountSetGlobalCollect_t)(bool active);

/**
 * @brief Get account data for job
 *
 * Get accounting data for all processes associated to a job
 * identified by its root task @a rootTID and store the resulting data
 * to @a accData. The content of @a accData is cleared before any
 * information is collected.
 *
 * Typically, a job's root task is the corresponding logger
 * task. Nevertheless, this might be different for Slurm steps spawned
 * via PMI(x)_Spawn() when the corresponding step forwarder might be
 * used.
 *
 * @param rootTID Task ID of the job's root task that uniquely
 * identifies the job to collect account data for
 *
 * @param accData Data structure used to accumulate accounting data
 *
 * @return Return true on success and false on error
 */
typedef bool(psAccountGetDataByLogger_t)(PStask_ID_t rootTID,
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
 * @brief Find account client by PID and return ID of its root task
 *
 * Find an account client by its process ID @a pid and return the ID
 * of the corresponding root task.
 *
 * @param pid PID of the client to search for
 *
 * @return Task ID of the client's root task or -1 on error
 */
typedef PStask_ID_t(psAccountGetLoggerByClient_t)(pid_t pid);

/**
 * @brief Get PIDs associated to a job
 *
 * Get the process ID of all clients associated to the job identified
 * by its root task @a rootTID and store them to @a pids. Upon return
 * @a pids will point to a memory region allocated via @ref
 * malloc(). It is the obligation of the calling function to release
 * this memory using @ref free(). Furthermore, upon return @a cnt will
 * hold the number of processes found and thus the size of @a pids.
 *
 * Typically, a job's root task is the corresponding logger
 * task. Nevertheless, this might be different for Slurm steps spawned
 * via PMI(x)_Spawn() when the corresponding step forwarder might be
 * used.
 *
 * @param rootTID Task ID of the job's root task
 *
 * @param pids Pointer to dynamically allocated array of process IDs
 * upon return
 *
 * @param cnt Number of processes found upon return
 *
 * @return No return value
 */
typedef void(psAccountGetPidsByLogger_t)(PStask_ID_t rootTID, pid_t **pids,
					 uint32_t *cnt);

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

/**
 * @brief Get various local node information
 *
 * The information currently holds energy, interconnect and file-system
 * counters. If no information is collect for a monitor all its counters
 * will be 0.
 *
 * @param info Will hold the nodes local information on return
 */
typedef void(psAccountGetLocalInfo_t)(psAccountInfo_t *info);

/**
 * @brief Get various poll intervals
 *
 * @param type The option type to get the interval for
 *
 * @return Current general poll interval or -1 on error
 */
typedef int(psAccountGetPoll_t)(psAccountOpt_t type);

/**
 * @brief Set various poll intervals
 *
 * Set the plugin's general poll interval to @a poll seconds
 *
 * @param type The option type to set the interval for
 *
 * @param poll General poll interval to be set
 *
 * @return Returns true if @a poll is valid or false otherwise
 */
typedef bool(psAccountSetPoll_t)(psAccountOpt_t type, int poll);

/**
 * @brief Control an accounting script
 *
 * @param action The action to invoke for the script
 *
 * @param type The type of script to control
 *
 * @return Returns true on success or false otherwise
 */
typedef bool(psAccountCtlScript_t)(psAccountCtl_t action, psAccountOpt_t type);

/**
 * @brief Set/unset environment variable for an accounting script
 *
 * @param action Specifies if a variable should be added or removed
 *
 * @param type The type of script to control
 *
 * @param name Name of the environment variable to set or unset
 *
 * @param val Value to be set; ignored in case of variable unset
 *
 * @return Returns true on success or false otherwise
 */
typedef bool (psAccountScriptEnv_t)(psAccountCtl_t action, psAccountOpt_t type,
				    char *name, char *val);

#endif  /* __PS_ACCOUNT_TYPES */
