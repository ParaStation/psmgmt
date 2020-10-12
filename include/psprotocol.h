/*
 * ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file ParaStation client-daemon high-level protocol.
 */
#ifndef __PSPROTOCOL_H
#define __PSPROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include "psnodes.h"
#include "pstaskid.h"

/** Unique version number of the high-level protocol */
#define PSProtocolVersion 342

/** The location of the UNIX socket used to contact the daemon. */
#define PSmasterSocketName "\0parastation.sock"

/** Environment used to pass number of service processes to logger */
#define ENV_NUM_SERVICE_PROCS    "__PSI_SERVICE_PROCS"

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
    PSP_OP_PROTOCOLVERSION,       /**< Node's PSProtocol version */
    PSP_OP_PROCLIMIT = 0x0003,    /**< Maximum number of processes */
    PSP_OP_OBSOLETE,              /**< Number of obsolete tasks */
    PSP_OP_PSIDDEBUG = 0x0005,    /**< psid's debug level */
    PSP_OP_PSIDSELECTTIME,        /**< Time (sec) in psid's select() */
    PSP_OP_MASTER = 0x0008,       /**< current master of the cluster */
    PSP_OP_DAEMONPROTOVERSION,    /**< Node's PSDaemonProtocol version */
    PSP_OP_PLUGINAPIVERSION,      /**< Node's version of the plugin API */

    PSP_OP_PLUGINUNLOADTMOUT=0x000c, /**< Timeout before evicting plugins */

    PSP_OP_LISTEND = 0x000f,      /**< no further opts; UID,GID,ACCT,etc. */

    PSP_OP_ADD_ACCT = 0x0010,     /**< add a new accounting process */
    PSP_OP_REM_ACCT,              /**< sign off accounting process */
    PSP_OP_ACCT,                  /**< list of accounting processes */
    PSP_OP_ACCTPOLL,              /**< accounter's poll interval */

    PSP_OP_STATUS_TMOUT = 0x0018, /**< status' timeout (in milli-seconds) */
    PSP_OP_STATUS_DEADLMT,        /**< status' dead-limit */
    PSP_OP_STATUS_BCASTS,         /**< status-broadcast limit */

    PSP_OP_RDPDEBUG = 0x0020,     /**< RDP's debug level */
    PSP_OP_RDPPKTLOSS,            /**< Packet loss within RDP (debugging) */
    PSP_OP_RDPMAXRETRANS,         /**< Max. re-transmissions in RDP */
    PSP_OP_RDPMAXACKPEND,         /**< Max. pending ACKs in RDP */
    PSP_OP_RDPRSNDTMOUT,          /**< RDP's resend-timeout */
    PSP_OP_RDPCLSDTMOUT,          /**< RDP's closed-timeout */
    PSP_OP_RDPTMOUT,              /**< RDP's registered timeout */
    PSP_OP_RDPRETRANS,            /**< RDP's total number of retransmissions */

    PSP_OP_MCASTDEBUG = 0x0028,   /**< MCast's debug level */
    PSP_OP_RDPSTATISTICS,         /**< Status of RDP statistics */

    PSP_OP_FREEONSUSP = 0x0038,   /**< Free suspended job's resources? */
    PSP_OP_NODESSORT = 0x003a,    /**< Sorting strategy for nodes */
    PSP_OP_OVERBOOK,              /**< (Dis-)Allow overbooking this node */
    PSP_OP_STARTER,               /**< (Dis-)Allow starting from this node */
    PSP_OP_RUNJOBS,               /**< (Dis-)Allow running on this node */
    PSP_OP_EXCLUSIVE,             /**< (Dis-)Allow assign node exclusively */

    PSP_OP_PINPROCS = 0x0048,     /**< Process-pinning is used on this node */
    PSP_OP_BINDMEM,               /**< Memory-binding is used on this node */
    PSP_OP_CLR_CPUMAP,            /**< clear CPU-map */
    PSP_OP_APP_CPUMAP,            /**< append an element to CPU-map */
    PSP_OP_CPUMAP,                /**< request full CPU-map (returns list)*/
    PSP_OP_ALLOWUSERMAP,          /**< allow user to influence CPU-mapping */
    PSP_OP_BINDGPUS,              /**< GPU-binding is used on this node */

    PSP_OP_SET_UID = 0x0050,      /**< set an exclusive user */
    PSP_OP_ADD_UID,               /**< add a new exclusive user */
    PSP_OP_REM_UID,               /**< remove an exclusive user */
    PSP_OP_UID,                   /**< list of exclusive users */
    PSP_OP_SET_GID,               /**< set an exclusive group */
    PSP_OP_ADD_GID,               /**< add a new exclusive group */
    PSP_OP_REM_GID,               /**< remove an exclusive group */
    PSP_OP_GID,                   /**< list of exclusive groups */
    /* admin users/groups are allowed to start unaccounted jobs */
    PSP_OP_SET_ADMUID,            /**< set an admin user */
    PSP_OP_ADD_ADMUID,            /**< add a new admin user */
    PSP_OP_REM_ADMUID,            /**< remove an admin user */
    PSP_OP_ADMUID,                /**< list of admin users */
    PSP_OP_SET_ADMGID,            /**< set an admin group */
    PSP_OP_ADD_ADMGID,            /**< add a new admin group */
    PSP_OP_REM_ADMGID,            /**< remove an admin group */
    PSP_OP_ADMGID,                /**< list of admin groups */

    PSP_OP_RL_AS = 0x0068,        /**< request RLIMIT_AS (address-space) */
    PSP_OP_RL_CORE,               /**< request RLIMIT_CORE (max core-size) */
    PSP_OP_RL_CPU,                /**< request RLIMIT_CPU (cpu-time limit) */
    PSP_OP_RL_DATA,               /**< request RLIMIT_DATA (max data-segm) */
    PSP_OP_RL_FSIZE,              /**< request RLIMIT_FSIZE (max file-size) */
    PSP_OP_RL_LOCKS,              /**< request RLIMIT_LOCKS (# of filelocks) */
    PSP_OP_RL_MEMLOCK,            /**< request RLIMIT_MEMLOCKS (max vm-size) */
    PSP_OP_RL_MSGQUEUE,           /**< request RLIMIT_MSGQUEUE (max bytes in
				     POSIX message queue) */
    PSP_OP_RL_NOFILE,             /**< request RLIMIT_NOFILE (# open files) */
    PSP_OP_RL_NPROC,              /**< request RLIMIT_NPROC (# user procs) */
    PSP_OP_RL_RSS,                /**< request RLIMIT_RSS (???) */
    PSP_OP_RL_SIGPENDING,         /**< request RLIMIT_SIGPENDING (max pending
				     signals) */
    PSP_OP_RL_STACK,              /**< request RLIMIT_STACK (max stack-size) */

    PSP_OP_SUPPL_GRPS = 0x0078,   /**< (Dis-)Enable setting suppl. groups */
    PSP_OP_MAXSTATTRY,            /**< Maximum stat() tries during spawn */
    PSP_OP_KILLDELAY,             /**< Delay before sending SIGKILL */
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
    PSP_INFO_LIST_HWSTATUS,       /**< List of hardware statuses */
    PSP_INFO_LIST_LOAD,           /**< List of load average values */
    PSP_INFO_LIST_ALLJOBS,        /**< List of job numbers (all jobs) */
    PSP_INFO_LIST_NORMJOBS,       /**< List of job numbers (normal jobs) */
    PSP_INFO_LIST_ALLOCJOBS = 0x1f,/**< List of allocated job slots (per node)*/
    PSP_INFO_LIST_EXCLUSIVE,      /**< List of flags of exclusive allocation */

    PSP_INFO_CMDLINE,             /**< Task's command line (if available) */
    PSP_INFO_RPMREV,              /**< Daemon's RPM revision */

    PSP_INFO_QUEUE_SEP,           /**< Queue separator (end of info item) */
    PSP_INFO_QUEUE_ALLTASK,       /**< Queue of tasks (all tasks) */
    PSP_INFO_QUEUE_NORMTASK,      /**< Queue of tasks (normal tasks tasks) */
    PSP_INFO_QUEUE_PARTITION,     /**< Queue of partitions */

    PSP_INFO_LIST_PARTITION,      /**< Task's list of allocated slots */
    PSP_INFO_LIST_MEMORY,         /**< List of total/used memory values */

    PSP_INFO_QUEUE_PLUGINS,       /**< Queue of plugins */

    PSP_INFO_STARTTIME,           /**< Node's start-time (sec since the epoch)*/

    PSP_INFO_STARTUPSCRIPT,       /**< Node's startup script */
    PSP_INFO_NODEUPSCRIPT,        /**< Node's script called upon new partner */
    PSP_INFO_NODEDOWNSCRIPT,      /**< Node's script called upon lost partner */

    PSP_INFO_QUEUE_ENVS,          /**< Queue of environment entries */
    PSP_INFO_RDPCONNSTATUS,       /**< Info on RDP connections */
    PSP_INFO_LIST_RESPORTS,       /**< Reserved ports for OpenMPI startup */
    PSP_INFO_LIST_RESNODES = 0x0032, /**< Get a reservation's node-list */
} PSP_Info_t;

/** Messages concerning spawning of tasks. */
#define PSP_CD_SPAWNREQUEST      0x0020  /**< Spawn one or more processes */
#define PSP_CD_SPAWNSUCCESS      0x0021  /**< Reply on successful spawn */
#define PSP_CD_SPAWNFAILED       0x0022  /**< Reply on failed spawn */
#define PSP_CD_SPAWNFINISH       0x0023  /**< Reply after successful end of
					      spawned process */
#define PSP_CD_SPAWNREQ          0x0024  /**< Request to spawn a process */

/** Kind of content within #PSP_CD_SPAWNREQ message */
typedef enum {
    PSP_SPAWN_TASK = 0x0000,      /**< Content is task (and workdir) */
    PSP_SPAWN_ARG,                /**< Content is argument-vector */
    PSP_SPAWN_ENV,                /**< Content is chunk of environment */
    PSP_SPAWN_END,                /**< Content is last chunk of environment */
    PSP_SPAWN_LOC,                /**< Content is location to pin to */
    PSP_SPAWN_ENVCNTD,            /**< Content is continued single env-var */
    PSP_SPAWN_WDIRCNTD,           /**< Content is continued workdir */
    PSP_SPAWN_ARGCNTD,            /**< Content is continued argument-vector */
    PSP_SPAWN_ENV_CLONE,          /**< No content, trigger clone from sibling */
} PSP_Spawn_t;

/** Accounting messages */
#define PSP_CD_ACCOUNT           0x0025  /**< Accounting message */

/** Kind of event within #PSP_CD_ACCOUNT message */
typedef enum {
    PSP_ACCOUNT_QUEUE = 0x0000,   /**< New task queued */
    PSP_ACCOUNT_START,            /**< Start of task */
    PSP_ACCOUNT_SLOTS,            /**< Start of task, list of slots part */
    PSP_ACCOUNT_DELETE,           /**< Delete of task (end without spawn) */
    PSP_ACCOUNT_END,              /**< Task ends execution */
    PSP_ACCOUNT_CHILD,            /**< Another child is spawned */
    PSP_ACCOUNT_LOG,              /**< Special info only known by the logger */
} PSP_Account_t;

/** All the signal handling stuff. */
#define PSP_CD_NOTIFYDEAD        0x0040  /**< Register to get a signal */
#define PSP_CD_NOTIFYDEADRES     0x0041  /**< Reply registration's success */
#define PSP_CD_RELEASE           0x0042  /**< Cancel the signal */
#define PSP_CD_RELEASERES        0x0043  /**< Reply cancellation's success */
#define PSP_CD_SIGNAL            0x0044  /**< Send a signal */
#define PSP_CD_WHODIED           0x0045  /**< Find out who sent a signal */
#define PSP_CD_SIGRES            0x0046  /**< Any error occurred during sending signal? */

/** Messages to steer the daemons. */
#define PSP_CD_DAEMONSTART       0x0050  /**< Request to start remote daemon */
#define PSP_CD_DAEMONSTOP        0x0051  /**< Request to stop remote daemon */
#define PSP_CD_DAEMONRESET       0x0052  /**< Request to reset daemon */
#define PSP_CD_HWSTART           0x0053  /**< Request to start comm hardware */
#define PSP_CD_HWSTOP            0x0054  /**< Request to stop comm hardware */
#define PSP_CD_PLUGIN            0x0055  /**< Request to (un-)load a plugin */
#define PSP_CD_PLUGINRES         0x0056  /**< Result of the last plugin msg */
#define PSP_CD_ENV               0x0057  /**< Request (un-)set an environment */
#define PSP_CD_ENVRES            0x0058  /**< Result of an environment msg */

/** Kind of action/information within #PSP_CD_PLUGIN/#PSP_PLUGIN_RES message */
typedef enum {
    PSP_PLUGIN_LOAD = 0x0000,     /**< Load a new plugin */
    PSP_PLUGIN_REMOVE,            /**< Unload plugin */
    PSP_PLUGIN_FORCEREMOVE,       /**< Unload plugin forcefully */
    PSP_PLUGIN_AVAIL,             /**< Report available plugins */
    PSP_PLUGIN_HELP,              /**< Show plugin's help message */
    PSP_PLUGIN_SET,               /**< Modify plugin's internal state */
    PSP_PLUGIN_UNSET,             /**< Modify plugin's internal state */
    PSP_PLUGIN_SHOW,              /**< Report on plugin's internal state */
    PSP_PLUGIN_LOADTIME,          /**< Report plugin's load-time */
} PSP_Plugin_t;

/** Kind of action within #PSP_CD_ENV message */
typedef enum {
    PSP_ENV_SET = 0x0000,         /**< Set an environment variable */
    PSP_ENV_UNSET,                /**< Unset an environment variable */
} PSP_Env_t;

/** Creation and handling of partitions. */
#define PSP_CD_CREATEPART        0x0060  /**< Bind a partition to a job */
#define PSP_CD_CREATEPARTNL      0x0061  /**< Partition request nodelist */
#define PSP_CD_PARTITIONRES      0x0062  /**< Reply partitions bind */
#define PSP_CD_GETNODES          0x0063  /**< Request nodes from a partition */
#define PSP_CD_NODESRES          0x0064  /**< Get nodes from a partition */
#define PSP_CD_GETRANKNODE       0x0065  /**< Req node of rank from partition */
#define PSP_CD_GETRESERVATION    0x0066  /**< Request reservation of slots */
#define PSP_CD_RESERVATIONRES    0x0067  /**< Reservation result */
#define PSP_CD_GETSLOTS          0x0068  /**< Request slots from reservation */
#define PSP_CD_SLOTSRES          0x0069  /**< Slots got from a reservation */

/** Flow-control to loggers and forwarders. */
#define PSP_CD_SENDSTOP          0x0070  /**< Stop sending further packets */
#define PSP_CD_SENDCONT          0x0071  /**< Continue sending packets */

/** Client-client messages. These are fully transparent for the daemons. */
#define PSP_CC_MSG               0x0080  /**< Message between clients. */
#define PSP_CC_ERROR             0x0081  /**< Error in client communication. */
#define PSP_CC_PSI_MSG           0x0082  /**< Rank-routed message. */
#define PSP_CC_PSI_ERROR         0x0083  /**< Error in rank-routed comm. */

/** Error messages */
#define PSP_CD_UNKNOWN           0x00FE  /**< Message reporting unknown types
					    somewhere in the forwarding chain */
#define PSP_CD_ERROR             0x00FF  /**< General error message */


    /******************************************************************/
    /* The IDs from 0x0100 on are reserved for daemon-daemon messages */
    /******************************************************************/

    /***********************************************************/
    /* The IDs from 0x0200 on are reserved for plugin messages */
    /***********************************************************/

/** global reset actions. */
#define PSP_RESET_HW              0x0001

/**
 * Chunk-size for PSP_CD_GETNODES, PSP_CD_CREATEPARTNL and
 * PSP_DD_GETPARTNL messages
 */
#define NODES_CHUNK 256


/*--------------------------------------*/
/* Client-Daemon Protocol Message Types */
/*--------------------------------------*/

/** Message primitive. This is also the header of more complex messages. */
typedef struct {
    int16_t type;          /**< message type */
    uint16_t len;          /**< total length of the message */
    PStask_ID_t sender;    /**< sender of the message */
    PStask_ID_t dest;      /**< final destination of the message */
} DDMsg_t;

/** Typed message containing the type and nothing else. */
typedef struct {
    DDMsg_t header;        /**< message header */
    int32_t type;          /**< message (sub-)type */
} DDTypedMsg_t;

/**
 * Buffer size (and thus maximum message size). 1456 is derived
 * from 1500 (MTU) - 20 (IP header) - 8 (UDP header) - 16 (RDP header)
 */
#define BufMsgSize (1456-sizeof(DDMsg_t))

/**
 * Untyped buffer message. This is the largest message
 * possible. I.e. receiving a message of this type saves from
 * segmentation faults as long as the protocol is not messed up.
 */
typedef struct {
    DDMsg_t header;        /**< message header */
    char buf[BufMsgSize];  /**< message buffer */
} DDBufferMsg_t;

/** Buffer size of typed message. */
#define BufTypedMsgSize (BufMsgSize-sizeof(int32_t))

/**
 * Typed buffer message. Pretty much the same as #DDBufferMsg_t but
 * with an additional type (and a slightly smaller buffer)
 */
typedef struct {
    DDMsg_t header;        /**< message header */
    int32_t type;          /**< message (sub-)type */
    char buf[BufTypedMsgSize]; /**< message buffer */
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

/** Signal message used to (un)register and send signals. */
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
    char answer;           /**< flag to send answer on signal delivery
			      or release.  Only used within
			      PSP_CD_SIGNAL and PSP_CD_RELEASE
			      messages. */
} DDSignalMsg_t;

/**
 * Type describing the content of PSP_INFO_QUEUE_ALLTASK and
 * PSP_INFO_QUEUE_NORMTASK responses.
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

/**
 * @brief Determine string length
 *
 * Determine the length of the character string @a str in a way
 * compatible with the needs of @ref PSP_putMsgBuf and @ref
 * PSP_putTypedMsgBuf. I.e. the terminating '\0'-character counts and
 * at the same time @a str might be NULL.
 *
 * @param str Pointer to '\0'-terminated character-string to analyze.
 *
 * @return The length of the string including the terminating '\0'
 * character is returned. Or 0, if @a str is NULL.
 */
size_t PSP_strLen(const char *str);

/**
 * @brief Put data into message buffer
 *
 * Put some data referenced by @a data of size @a size into the
 * payload-buffer @ref buf of the message @a msg of type @ref
 * DDBufferMsg_t. The data are placed with an offset defined by the
 * @ref len member of @a msg's header. Upon success, i.e. if the data
 * fitted into the remainder of @a msg's buffer, the @ref len entry of
 * @a msg's header is updated and 1 is returned. Otherwise, an
 * error-message is put out and 0 is returned. In the latter case the
 * len member of @a msg is not updated.
 *
 * @a funcName and @a dataName are used in order the create the
 * error-message. It shall describe the calling function and the type
 * of content to be added to @a msg's buffer.
 *
 * @param msg Message to be modified
 *
 * @param funcName Name of the calling function
 *
 * @param dataName Description of the data @a data to be added to @a
 * msg.
 *
 * @param data Pointer to the data to put into the message @a msg.
 *
 * @param size Amount of data to be put into the message @a msg.
 *
 * @return Upon success, @a true is returned. Or @a false if an error
 * occurred. This is mainly due to insufficient space within @a msg.
 */
bool PSP_putMsgBuf(DDBufferMsg_t *msg, const char *funcName,
		   const char *dataName, const void *data, size_t size);

/**
 * @brief Try to put data into message buffer
 *
 * This function shows basically the same behavior as @ref
 * PSP_putMsgBuf(). The main difference is suppressing an error message
 * in case @a msg does not contain sufficient space for the data.
 *
 * @param msg Message to be modified
 *
 * @param funcName Name of the calling function
 *
 * @param dataName Description of the data @a data to be added to @a
 * msg.
 *
 * @param data Pointer to the data to put into the message @a msg.
 *
 * @param size Amount of data to be put into the message @a msg.
 *
 * @return Upon success, @a true is returned. Or @a false if an error
 * occurred. This is mainly due to insufficient space within @a msg.
 */
bool PSP_tryPutMsgBuf(DDBufferMsg_t *msg, const char *funcName,
		      const char *dataName, const void *data, size_t size);

/**
 * @brief Get data from message buffer
 *
 * Fetch data from the payload-buffer @ref buf of the message @a msg
 * of type @ref DDBufferMsg_t. An amount of data as given by @a size
 * will be stored to @a data. The data is fetched with an offset given
 * by @a used . At the same time @a used is updated to point right
 * after the fetched data. Thus, subsequent calls will fetch
 * successive data from the payload buffer.
 *
 * Upon success, i.e. if the data is available in the message
 * (i.e. its @ref len is large enough) and @a data is different from
 * NULL, 1 is returned. Otherwise, an error-message is put out and 0
 * is returned. In the latter case @a used is not updated.
 *
 * @a funcName and @a dataName are used in order the create the
 * error-message. It shall describe the calling function and the type
 * of content to be fetched from @a msg's buffer.
 *
 * @param msg Message to fetch data from
 *
 * @param used Counter used to track the data offset
 *
 * @param funcName Name of the calling function
 *
 * @param dataName Description of @a data to be fetched from to @a
 * msg.
 *
 * @param data Pointer to the data to get from the message @a msg.
 *
 * @param size Amount of data to be fetched from the message @a msg.
 *
 * @return Upon success, @a true is returned. Or @a false if an error
 * occurred. This is mainly due to insufficient data available in @a msg.
 */
bool PSP_getMsgBuf(DDBufferMsg_t *msg, size_t *used, const char *funcName,
		   const char *dataName, void *data, size_t size);

/**
 * @brief Try to get data from message buffer
 *
 * This function shows basically the same behavior as @ref
 * PSP_getMsgBuf(). The main difference is suppressing an error message
 * in case @a msg does not contain sufficient amount of data.
 *
 * @param msg Message to fetch data from
 *
 * @param used Counter used to track the data offset
 *
 * @param funcName Name of the calling function
 *
 * @param dataName Description of @a data to be fetched from to @a
 * msg.
 *
 * @param data Pointer to the data to get from the message @a msg.
 *
 * @param size Amount of data to be fetched from the message @a msg.
 *
 * @return Upon success, @a true is returned. Or @a false if an error
 * occurred. This is mainly due to insufficient data available in @a msg.
 *
 * @see PSP_getMsgBuf()
 */
bool PSP_tryGetMsgBuf(DDBufferMsg_t *msg, size_t *used, const char *funcName,
		      const char *dataName, void *data, size_t size);

/**
 * @brief Put data into message buffer
 *
 * Put some data referenced by @a data of size @a size into the
 * payload-buffer @ref buf of the message @a msg of type @ref
 * DDTypedBufferMsg_t. The data are placed with an offset defined by
 * the @ref len member of @a msg's header. Upon success, i.e. if the
 * data fitted into the remainder of @a msg's buffer, the @ref len
 * entry of @a msg's header is updated and 1 is returned. Otherwise,
 * an error-message is put out and 0 is returned. In the latter case
 * the len member of @a msg is not updated.
 *
 * @a funcName and @a dataName are used in order the create the
 * error-message. It shall describe the calling function and the type
 * of content to be added to @a msg's buffer.
 *
 * @param msg Message to be modified
 *
 * @param funcName Name of the calling function
 *
 * @param dataName Description of the data @a data to be added to @a
 * msg.
 *
 * @param data Pointer to the data to put into the message @a msg.
 *
 * @param size Amount of data to be put into the message @a msg.
 *
 * @return Upon success, @a true is returned. Or @a false if an error
 * occurred. This is mainly due to insufficient space within @a msg.
 */
bool PSP_putTypedMsgBuf(DDTypedBufferMsg_t *msg, const char *funcName,
			const char *dataName, const void *data, size_t size);

/**
 * @brief Try to put data into message buffer
 *
 * This function shows basically the same behavior as @ref
 * PSP_putTypedMsgBuf(). The main difference is suppressing an error message
 * in case @a msg does not contain sufficient space for the data.
 *
 * @param msg Message to be modified
 *
 * @param funcName Name of the calling function
 *
 * @param dataName Description of the data @a data to be added to @a
 * msg.
 *
 * @param data Pointer to the data to put into the message @a msg.
 *
 * @param size Amount of data to be put into the message @a msg.
 *
 * @return Upon success, @a true is returned. Or @a false if an error
 * occurred. This is mainly due to insufficient space within @a msg.
 *
 * @see PSP_putTypedMsgBuf()
 */
bool PSP_tryPutTypedMsgBuf(DDTypedBufferMsg_t *msg, const char *funcName,
			   const char *dataName, const void *data, size_t size);

/**
 * @brief Get data from message buffer
 *
 * Fetch data from the payload-buffer @ref buf of the message @a msg
 * of type @ref DDTypedBufferMsg_t. An amount of data as given by @a size
 * will be stored to @a data. The data is fetched with an offset given
 * by @a used . At the same time @a used is updated to point right
 * after the fetched data. Thus, subsequent calls will fetch
 * successive data from the payload buffer.
 *
 * Upon success, i.e. if the data is available in the message
 * (i.e. its @ref len is large enough) and @a data is different from
 * NULL, 1 is returned. Otherwise, an error-message is put out and 0
 * is returned. In the latter case @a used is not updated.
 *
 * @a funcName and @a dataName are used in order the create the
 * error-message. It shall describe the calling function and the type
 * of content to be fetched from @a msg's buffer.
 *
 * @param msg Message to fetch data from
 *
 * @param used Counter used to track the data offset
 *
 * @param funcName Name of the calling function
 *
 * @param dataName Description of @a data to be fetched from to @a
 * msg.
 *
 * @param data Pointer to the data to get from the message @a msg.
 *
 * @param size Amount of data to be fetched from the message @a msg.
 *
 * @return Upon success, @a true is returned. Or @a false if an error
 * occurred. This is mainly due to insufficient data available in @a msg.
 */
bool PSP_getTypedMsgBuf(DDTypedBufferMsg_t *msg, size_t *used,
			const char *funcName, const char *dataName,
			void *data, size_t size);

#endif /* __PSPROTOCOL_H */
