/*
 *               ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2006 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file
 * ParaStation client-daemon high-level protocol.
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSPROTOCOL_H
#define __PSPROTOCOL_H

#include <stdint.h>
#include <sys/types.h>
#include "psnodes.h"
#include "pstask.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** Unique version number of the high-level protocol */
#define PSprotocolVersion 332

/** The location of the UNIX socket used to contact the daemon. */
#define PSmasterSocketName "/var/run/parastation.sock"

/** IDs of the various message types */

/** First messages used for setting up connections between client and daemon */
#define PSP_CD_CLIENTCONNECT     0x0001  /**< Request to connect daemon */
#define PSP_CD_CLIENTESTABLISHED 0x0002  /**< Connection request accepted */
#define PSP_CD_CLIENTREFUSED     0x0003  /**< Connection request denied */

/** Error code within a #PSP_CD_CLIENTREFUSED message */
typedef enum {
    PSP_CONN_ERR_NONE = 0,        /**< No error, but TG_RESET task group */
    PSP_CONN_ERR_VERSION = 1,     /**< Protocol version mismatch */
    PSP_CONN_ERR_NOSPACE,         /**< No space to create task struct */
    PSP_CONN_ERR_UIDLIMIT,        /**< Node is limited to different user */
    PSP_CONN_ERR_PROCLIMIT,       /**< Number of processes exceeded */
    PSP_CONN_ERR_STATENOCONNECT,  /**< No connections accepted */
    PSP_CONN_ERR_GIDLIMIT,        /**< Node is limited to different group */
    PSP_CONN_ERR_LICEND,          /**< Daemon's license is expired */
} PSP_ConnectError_t;

/* We will keep this message type for compatibility with older executables */
// #define PSP_CD_OLDVERSION          0x0004

/** Messages used for setting and getting option values */
#define PSP_CD_SETOPTION         0x0008  /**< Set one or more options */
#define PSP_CD_GETOPTION         0x0009  /**< Get one or more options */

/**
 * Type of option to get/set within a #PSP_CD_GETOPTION /
 * #PSP_CD_SETOPTION message
 */
typedef enum {
    PSP_OP_UNKNOWN,               /**< Unknown option */
    PSP_OP_HWSTATUS = 0x0001,     /**< Hardware status */
    /* PSP_OP_CPUS,*/ /* unused *//**< Number of CPUs */
    PSP_OP_PROCLIMIT = 0x0003,    /**< Maximum number of processes */
    PSP_OP_UIDLIMIT,              /**< uid the node is restricted to */
    PSP_OP_PSIDDEBUG,             /**< psid's debug level */
    PSP_OP_PSIDSELECTTIME,        /**< Time (sec) in psid's select() */
    PSP_OP_GIDLIMIT,              /**< gid the node is restricted to */

    PSP_OP_MASTER = 0x0008,       /**< current master of the cluster */

    PSP_OP_ADD_ACCT = 0x0010,     /**< add a new accounting process */
    PSP_OP_REM_ACCT,              /**< sign off accounting process */
    PSP_OP_ACCT,                  /**< current master of the cluster */

    PSP_OP_RDPDEBUG = 0x0020,     /**< RDP's debug level */
    PSP_OP_RDPPKTLOSS,            /**< Paket loss within RDP (debugging) */
    PSP_OP_RDPMAXRETRANS,         /**< Max. retransmissions in RDP */

    PSP_OP_MCASTDEBUG = 0x0028,   /**< MCast's debug level */

    PSP_OP_PSM_SPS = 0x0030,      /**< */
    PSP_OP_PSM_RTO,               /**< */
    PSP_OP_PSM_HNPEND,            /**< */
    PSP_OP_PSM_ACKPEND,           /**< */

    PSP_OP_FREEONSUSP = 0x0038,   /**< Free suspended job's resources? */
    PSP_OP_HANDLEOLD,             /**< Notice old binaries? */
    PSP_OP_NODESSORT,             /**< Sorting strategy for nodes */
    PSP_OP_OVERBOOK,              /**< (Dis-)Allow overbooking this node */
    PSP_OP_STARTER,               /**< (Dis-)Allow starting from this node */
    PSP_OP_RUNJOBS,               /**< (Dis-)Allow running on this node */
    PSP_OP_EXCLUSIVE,             /**< (Dis-)Allow assign node exclusively */

    PSP_OP_ADMINUID = 0x0040,     /**< user allowed starting admin
				     jobs, i.e. unaccounted jobs */
    PSP_OP_ADMINGID,              /**< group allowed starting admin
				     jobs, i.e. unaccounted jobs */
} PSP_Option_t;

/** Messages used for information retrieval */
#define PSP_CD_INFOREQUEST       0x0010  /**< Get info from daemon */
#define PSP_CD_INFORESPONSE      0x0011  /**< The requested info */

/**
 * Type of information to get within a #PSP_CD_INFOREQUEST /
 * #PSP_CD_INFORESPONSE message
 */
typedef enum {
    PSP_INFO_UNKNOWN = 0x0000,    /**< Unknown info type */

    PSP_INFO_NROFNODES,           /**< Number of cluster nodes */
    PSP_INFO_INSTDIR,             /**< ParaStation installation directory */
    PSP_INFO_DAEMONVER,           /**< Version string of the daemon */

    PSP_INFO_HOST,                /**< ParaStation ID from IP */
    PSP_INFO_NODE,                /**< IP from ParaStation ID */
    PSP_INFO_NODELIST,            /**< Up to date nodelist @deprecated */
    PSP_INFO_PARTITION,           /**< Nodelist according limits @deprecated */

    PSP_INFO_LIST_TASKS,          /**< info about tasks @deprecated */
    PSP_INFO_LIST_END = 0x0009,   /**< end of list info replies */

    PSP_INFO_LIST_HOSTSTATUS,     /**< Complete status of all cluster nodes */
    PSP_INFO_RDPSTATUS,           /**< Status of the RDP */
    PSP_INFO_MCASTSTATUS,         /**< Status of the MCast */

    PSP_INFO_COUNTHEADER,         /**< Header for communication counters */
    PSP_INFO_COUNTSTATUS,         /**< Communication counters */

    PSP_INFO_HWNUM,               /**< Number of supported hardware types */
    PSP_INFO_HWINDEX,             /**< Internal index from hardware name */
    PSP_INFO_HWNAME,              /**< Name of hardware from internal index */

    PSP_INFO_RANKID,              /**< ParaStation ID from rank */
    PSP_INFO_TASKSIZE,            /**< Actual task's number of processes */
    PSP_INFO_TASKRANK,            /**< Local process' rank within task */

    PSP_INFO_PARENTTID,           /**< Parent's task ID */
    PSP_INFO_LOGGERTID,           /**< Logger's task ID */

    PSP_INFO_LIST_VIRTCPUS,       /**< List of virtual CPU numbers */
    PSP_INFO_LIST_PHYSCPUS,       /**< List of physical CPU numbers */
    PSP_INFO_LIST_HWSTATUS,       /**< List of hardware stati */
    PSP_INFO_LIST_LOAD,           /**< List of load average values */
    PSP_INFO_LIST_ALLJOBS,        /**< List of job numbers (all jobs) */
    PSP_INFO_LIST_NORMJOBS,       /**< List of job numbers (normal jobs) */
    PSP_INFO_LIST_ALLTASKS,       /**< List of all tasks @deprecated */
    PSP_INFO_LIST_NORMTASKS,      /**< List of normal tasks  @deprecated */
    PSP_INFO_LIST_ALLOCJOBS,      /**< List of allocated job slots (per node)*/
    PSP_INFO_LIST_EXCLUSIVE,      /**< List of flags of exclusive allocation */

    PSP_INFO_CMDLINE,             /**< Task's command line (if available) */
    PSP_INFO_RPMREV,              /**< Daemon's RPM revision */

    PSP_INFO_QUEUE_SEP,           /**< Queue separator (end of info item) */
    PSP_INFO_QUEUE_ALLTASK,       /**< Queue of tasks (all tasks) */
    PSP_INFO_QUEUE_NORMTASK,      /**< Queue of tasks (normal tasks tasks) */
    PSP_INFO_QUEUE_PARTITION,     /**< Queue of partitions */

    PSP_INFO_LIST_PARTITION,      /**< Task's list of allocated slots */

} PSP_Info_t;

/** Messages concerning spawning of tasks. */
#define PSP_CD_SPAWNREQUEST      0x0020  /**< Request to spawn a process
					      @deprecated */
#define PSP_CD_SPAWNSUCCESS      0x0021  /**< Reply on successful spawn */
#define PSP_CD_SPAWNFAILED       0x0022  /**< Reply on failed spawn */
#define PSP_CD_SPAWNFINISH       0x0023  /**< Reply after successful end of
					      spawned process */
#define PSP_CD_SPAWNREQ          0x0024  /**< Request to spawn a process */
#define PSP_CD_ACCOUNT           0x0025  /**< Accounting message */

/** Kind of content within #PSP_CD_SPAWNREQ message */
typedef enum {
    PSP_SPAWN_TASK = 0x0000,      /**< Content is task (and workdir) */
    PSP_SPAWN_ARG,                /**< Content is arguments (argv-vector) */
    PSP_SPAWN_ENV,                /**< Content is chunk of environment */
    PSP_SPAWN_END,                /**< Content is last chunk of environment */
} PSP_Spawn_t;


/** All the signal handling stuff. */
#define PSP_CD_NOTIFYDEAD        0x0040  /**< Register to get a signal */
#define PSP_CD_NOTIFYDEADRES     0x0041  /**< Reply registration's success */
#define PSP_CD_RELEASE           0x0042  /**< Cancal the signal */
#define PSP_CD_RELEASERES        0x0043  /**< Reply cancelation's success */
#define PSP_CD_SIGNAL            0x0044  /**< Send a signal */
#define PSP_CD_WHODIED           0x0045  /**< Find out who sent a signal */


/** Messages to steer the daemons. */
#define PSP_CD_DAEMONSTART       0x0050  /**< Request to start remote daemon */
#define PSP_CD_DAEMONSTOP        0x0051  /**< Request to stop remote daemon */
#define PSP_CD_DAEMONRESET       0x0052  /**< Request to reset daemon */
#define PSP_CD_HWSTART           0x0053  /**< Request to start comm hardware */
#define PSP_CD_HWSTOP            0x0054  /**< Request to stop comm hardware */

/** Creation and handling of partitions. */
#define PSP_CD_CREATEPART        0x0060  /**< Bind a partition to a job */
#define PSP_CD_CREATEPARTNL      0x0061  /**< Partition request nodelist */
#define PSP_CD_PARTITIONRES      0x0062  /**< Reply partitions bind */
#define PSP_CD_GETNODES          0x0063  /**< Request nodes from a partition */
#define PSP_CD_NODESRES          0x0064  /**< Get nodes from a partition */

/** Client-client messages. These are fully transparent for the daemons. */
#define PSP_CC_MSG               0x0080  /**< Message between clients. */
#define PSP_CC_ERROR             0x0081  /**< Error in client communication. */

/** Error messages. */
#define PSP_CD_ERROR             0x00FF  /**< General error message */



    /******************************************************************/
    /* The IDs from 0x0100 on are reserved for daemon-daemon messages */
    /******************************************************************/



/** global reset actions. */
#define PSP_RESET_HW              0x0001

/**
 * Chunksize for PSP_CD_GETNODES, PSP_CD_CREATEPARTNL,
 * PSP_DD_GETPARTNL and PSP_DD_PROVIDEPARTNL messages
 */
#define NODES_CHUNK 512


/*--------------------------------------*/
/* Client-Daemon Protocol Message Types */
/*--------------------------------------*/

/** Message primitive. This is also the header of more complex messages. */
typedef struct {
    int16_t type;          /**< msg type */
    int16_t len;           /**< total length of the message */
    PStask_ID_t sender;    /**< sender of the message */ 
    PStask_ID_t dest;      /**< final destination of the message */
} DDMsg_t;

/** Typed message containing the type and nothing else. */
typedef struct {
    DDMsg_t header;        /**< message header */
    int32_t type;          /**< message (sub-)type */
} DDTypedMsg_t;

/** Buffer size (and thus maximum message size) */
#define BufMsgSize 8000

/**
 * Untyped buffer message. This is the largest message
 * possible. I.e. receiving a message of this type saves from
 * segmentation faults as long as the protocol is not messed up.
 */
typedef struct {
    DDMsg_t header;        /**< message header */
    char buf[BufMsgSize];  /**< message buffer */
} DDBufferMsg_t;

/**
 * Typed buffer message. Pretty much the same as #DDBufferMsg_t but
 * with an additional type (and a slightly smaller buffer)
 */
typedef struct {
    DDMsg_t header;        /**< message header */
    int32_t type;          /**< message (sub-)type */
    char buf[BufMsgSize-sizeof(int32_t)]; /**< message buffer */
} DDTypedBufferMsg_t;

/** Simple error message */
typedef struct {
    DDMsg_t header;        /**< message header */
    int32_t error;         /**< error number */
    PStask_ID_t request;   /**< request which caused the error */
} DDErrorMsg_t;

/** Initial message send by a potential client to the daemon. */
typedef struct {
    DDMsg_t header;        /**< header of the message */
    PStask_group_t group;  /**< process group of the task */
    uint32_t version;      /**< Protocol version spoken by the PS library */
    PStask_ID_t ppid;      /**< PID of the parent process (for TG_SPAWNER) */
#ifndef SO_PEERCRED
    pid_t pid;             /**< process id. Not used with UNIX sockets. */
    uid_t uid;             /**< user id. Not used with UNIX sockets. */
    gid_t gid;             /**< group id. Not used with UNIX sockets. */
#endif
} DDInitMsg_t;

/** Maximum number of options to set or get within one DDOption message. */
#define DDOptionMsgMax 16

/** Type for option's value within an option message */
typedef int32_t PSP_Optval_t;

/** Option container. A list of these is Used within @ref DDOptionMsg_t*/
typedef struct {
    PSP_Option_t option;   /**< option to be set/requested */
    PSP_Optval_t value;    /**< option's value */
} DDOption_t;

/** Option message used to set or get various options. */
typedef struct {
    DDMsg_t header;        /**< message header */
    char count;            /**< no of options in opt[] */
    DDOption_t opt[DDOptionMsgMax]; /**< array of option-value pairs */
} DDOptionMsg_t;

/** Signal message used to (de)register and send signals. */
typedef struct {
    DDMsg_t header;        /**< message header */
    int32_t signal;        /**< signal to be set or sent */
    int32_t param;         /**< additional parameter used as follows:
			      - PSP_CD_NOTIFYDEAD: unused
			      - PSP_CD_NOTIFYDEADRES: resulting error
			      - PSP_CD_RELEASE: unused
			      - PSP_CD_RELEASERES: resulting error
			      - PSP_CD_WHODIED: unused
			      - PSP_CD_SIGNAL: uid of the sender.
			      - PSP_DD_CHILDDEAD: unused. */
    char pervasive;        /**< flag to send signal to the whole task.
			      Only used within PSP_CD_SIGNAL messages. */
} DDSignalMsg_t;

/**
 * Types describing the content of PSP_INFO_LIST_TASKS responses.
 */
typedef struct {
    PStask_ID_t tid;       /**< tasks unique identifier */
    PStask_ID_t ptid;      /**< unique identifier of tasks parent-task */
    PStask_ID_t loggertid; /**< unique identifier of tasks logger-task */
    uid_t uid;             /**< user id of the task */
    PStask_group_t group;  /**< task group of the task */
    int32_t rank;          /**< rank of the task within process group */
    int32_t connected;     /**< flag if task has connected the daemon */
} PSP_taskInfo_t;


/**
 * Type describing the content of PSP_INFO_NODELIST and PSP_INFO_PARTITION
 * responses for clients prior to PSprotocolVersion 328. Later client
 * should not send requests of this type.
 */
typedef struct {
    int id;                /**< ID of this node */
    short up;              /**< Flag if node is up */
    short numCPU;          /**< Number of CPUs in this node */
    unsigned int hwStatus; /**< HW available on this node */
    float load[3];         /**< load on this node */
    short totalJobs;       /**< number of jobs */
    short normalJobs;      /**< number of jobs without logger, admin, etc.) */
    short maxJobs;         /**< maximum number of "normal" jobs */
} NodelistEntry_t;

/**
 * @brief Generate a string describing the message type.
 *
 * Generate a character string describing the message type @a
 * msgtype. The message type has to contain one of the PSP_CD_ or
 * PSP_CC_ message type constants.
 *
 * @param msgtype Message type the name should be generated for.
 *
 * @return A pointer to the '\\0' terminated character string
 * containing the name of the message type or a special message
 * containing @a msgtype if the type is unknown.
 */
char *PSP_printMsg(int msgtype);

/**
 * @brief Generate a string describing the info type.
 *
 * Generate a character string describing the info type @a
 * infotype. The info type has to contain one of the PSP_INFO_ info
 * type constants.
 *
 * @param infotype Info type the name should be generated for.
 *
 * @return A pointer to the '\\0' terminated character string
 * containing the name of the info type or a special message
 * containing @a infotype if the type is unknown.
 */
char *PSP_printInfo(PSP_Info_t infotype);


#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSPROTOCOL_H */
