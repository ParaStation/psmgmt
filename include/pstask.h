/*
 * ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file User-functions for interaction with ParaStation tasks.
 */
#ifndef __PSTASK_H
#define __PSTASK_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>

#include "list_t.h"
#include "pstaskid.h" // IWYU pragma: export

#include "pscpu.h"
#include "pssenddb_t.h"

/**
 * @brief Get the name of a PStask_group.
 *
 * Get the name of a PStask_group.
 *
 * @param taskgroup The PStask_group the name is wanted for.
 *
 * @return The name of the PStask_group or "UNKNOWN".
 */
const char* PStask_printGrp(PStask_group_t taskgroup);

/**
 * @brief Print statistics
 *
 * Print statistics concerning the usage of signal and reservation
 * structures.
 *
 * @return No return value.
 */
void PStask_printStat(void);

#include "pspartition.h"
#include "psreservation.h"

/* forward declaration */
typedef struct __task__ PStask_t;

/**
 * Different reasons why a task's spawn is delayed. These define the
 * different bits in the @ref delayReasons member of @ref PStask_t
 * that might be set in @ref PSIDHOOK_RECV_SPAWNREQ and have to be
 * cleared in the filter function of @ref PSIDspawn_startDelayedTasks()
 */
typedef enum {
    DELAY_RESINFO = 0x0001,        /**< delay triggered by missing CPUset */
    DELAY_PSSLURM = 0x0002,        /**< delay triggered by psslurm */
} PStask_delay_t;


/**
 * @brief Signal callback
 *
 * Callback to be executed upon SIGCHLD received from the
 * corresponding process. @a status will contain the exit status of
 * the process that triggered sending the SIGCHLD. @a task points to
 * the task structure describing this process.
 *
 * @param status Exit status determined via waitpid()
 *
 * @param task Task structure describing the process SIGCHLD was
 * received from
 *
 * @return No return value
 */
typedef void PStask_sigChldCB_t(int status, PStask_t *task);

/** Task structure */
/* Members marked C are handled by PStask_[en|de]code()/PStask_addToMsg() */
struct __task__ {
    list_t next;                   /**< used to put into managedTasks, etc. */
    /*C*/ PStask_ID_t tid;         /**< unique task identifier */
    /*C*/ PStask_ID_t ptid;        /**< unique identifier of parent task */
    /*C*/ uid_t uid;               /**< user id */
    /*C*/ gid_t gid;               /**< group id */
    /*C*/ uint32_t aretty;         /**< flag stdin, stdout & stderr as tty */
    bool interactive;              /**< stdin, stdout and stderr: all ttys */
    int stdin_fd;                  /**< helper fd during spawn */
    int stdout_fd;                 /**< helper fd during spawn */
    int stderr_fd;                 /**< helper fd during spawn */
    /*C*/ struct termios termios;  /**< parameters of the controlling tty */
    /*C*/ struct winsize winsize;  /**< window size of the controlling tty */
    /*C*/ PStask_group_t group;    /**< task group @see PStask_group_t */
    PStask_group_t childGroup;     /**< used by forwarder during spawn */
    /*C*/ PSrsrvtn_ID_t resID;     /**< reservation to be spawned in */
    /*C*/ PStask_ID_t loggertid;   /**< unique identifier of the logger */
    PStask_ID_t spawnertid;        /**< unique identifier of the spawner */
    PStask_t *forwarder;           /**< pointer to forwarder's task struct */
    /*C*/ int32_t rank;            /**< rank of task within task group */
    PSCPU_set_t CPUset;            /**< set of logical CPUs to pin to */
    PStask_ID_t partHolder;        /**< location of resource management */
    int32_t jobRank;               /**< rank w/in job (tasks w/ same spawner */
    short fd;                      /**< connection fd from/to the psid */
    /*C*/ char *workingdir;        /**< working directory */
    /*C*/ uint32_t argc;           /**< size of argv (w/o trailing NULL) */
    /*C*/ char **argv;             /**< command line arguments */
    /*C*/ char **environ;          /**< PS environment, used for spawning */
    /*C*/ uint32_t envSize;        /**< Size of environ (w/o trailing NULL) */
    int relativesignal;            /**< the signal sent when a relative (i.e.
				      parent or child) dies */
    int pendingReleaseRes;         /**< num of pending RELEASERES messages */
    int pendingReleaseErr;         /**< set to param!=0 in RELEASERES msg */
    int activeStops;               /**< Number of active SENDSTOPs */
    bool releaseAnswer;            /**< flag final RELEASERES to initiator */
    bool released;                 /**< flag to mark released task, i.e. don't
				      send signal to parent on exit */
    bool parentReleased;           /**< flag RELEASE msg sent to parent */
    bool duplicate;                /**< flag to mark duplicate task, i.e. a
				      tasks that are fork()ed by a client */
    bool suspended;                /**< flag to mark suspended tasks */
    bool removeIt;                 /**< flag to mark task to be removed (as
				      soon as all children are released) */
    bool deleted;                  /**< flag to mark deleted tasks. It
				      will be removed from the list of
				      managed tasks in the next round
				      of the main loop */
    bool obsolete;                 /**< flag tasks as obsolete, i.e. removed
				      from managed tasks but still referred
				      by a  selector */
    /*C*/ bool noParricide;        /**< flag to be set if kill signals should
				      not be forwarded to parents */
    PStask_delay_t delayReasons;   /**< reason to delay the spawn */
    time_t killat;                 /**< flag a killed task, i.e. the time when
				      the task should really go away */
    struct timeval started;        /**< Time the task structure was created */
    uint16_t protocolVersion;      /**< Protocol version the task speaks */
    list_t childList;              /**< Task's children (signal not used) */
    list_t releasedBefore;         /**< released children to be inherited */
    list_t deadBefore;             /**< dead children to be inherited */
    PSpart_request_t *request;     /**< Pointer to temp. partition request */
    PSpart_option_t options;       /**< Options to first create partition */
    uint32_t partitionSize;        /**< Number of slots in partition */
    PSpart_slot_t *partition;      /**< Actual partition (array of of slots) */
    uint32_t totalThreads;         /**< Size of @ref partThreads */
    PSpart_HWThread_t *partThrds;  /**< HW-threads forming the partition */
    int32_t usedThreads;           /**< Number of HW-threads currently in use */
    list_t sisterParts;            /**< Other partitions in the context of this
				      job, i.e. with the same loggertid */
    PSrsrvtn_ID_t nextResID;       /**< ID to be used for next reservation */
    list_t reservations;           /**< List of active reservations */
    list_t resRequests;            /**< List of reservation-requestd (FIFO) */
    int32_t activeChild;           /**< # of active children right now */
    int32_t numChild;              /**< Total # of children spawned over time */
    PSpart_slot_t *spawnNodes;     /**< Nodes the task can spawn to */
    uint32_t spawnNodesSize;       /**< Current size of @ref spawnNodes */
    uint32_t spawnNum;             /**< Amount of content of @ref spawnNodes */
    PStask_t *delegate;            /**< Delegate holding resources */
    int injectedEnv;               /**< Flag an injected environment into the
				      current spawn. Used by psmom, etc. */
    PStask_sigChldCB_t *sigChldCB; /**< Callback to be executed on SIGCHLD */
    void *info;                    /**< Generic info to be used by initiator */

    list_t signalSender;           /**< Tasks which sent signals */
    list_t signalReceiver;         /**< Tasks which want to receive signals */
    list_t assignedSigs;           /**< Tasks assigned to send signals */
    list_t keptChildren;           /**< Children kept during inheritance */
    uint16_t *resPorts;            /**< Reserved Ports for OpenMPI startup */
} /* PStask_t */;

/**
 * @brief Create a new task structure.
 *
 * A new task structure is created and initialized via @ref
 * PStask_init(). It may be removed with @ref PStask_delete().
 *
 * @return On success a pointer to the new task structure is
 * returned, or NULL otherwise
 *
 * @see PStask_init(), PStask_delete()
 */
PStask_t *PStask_new(void);

/**
 * @brief Initialize a task structure.
 *
 * Initialize the task structure @a task, i.e. set all member to
 * default values.
 *
 * @param task Pointer to the task structure to be initialized.
 *
 * @return On success true is returned; or false in case of error
 */
bool PStask_init(PStask_t *task);

/**
 * @brief Reinitialize a task structure
 *
 * Reinitialize the task structure @a task that was previously
 * used. All allocated strings and signal-lists shall be removed, all
 * links are reset to NULL.
 *
 * @param task Pointer to the task structure to be reinitialized
 *
 * @return On success true is returned; or false in case of error
 */
bool PStask_reinit(PStask_t *task);

/**
 * @brief Delete a task structure
 *
 * Delete the task structure @a task created via @ref
 * PStask_new(). First the task is cleaned up by @ref PStask_reinit(),
 * i.e. all allocated strings and signal-lists are removed. Afterward
 * the task itself is removed.
 *
 * @param task Pointer to the task structure to be deleted
 *
 * @return On success true is returned; or false in case of error
 */
bool PStask_delete(PStask_t *task);

/**
 * @brief Destroy task structure
 *
 * Destroy the task structure @a task created via @ref
 * PStask_new(). Different from @ref PStask_delete() this will not
 * touch any signal list or reservation list but just release the
 * memory directly associated to the task structure (and the task
 * structure itself). The mentioned lists are assumed to be cleaned up
 * via the according *_clearMem() functions.
 *
 * @param task Pointer to the task structure to be destroyed
 *
 * @return On success true is returned; or false in case of error
 */
bool PStask_destroy(PStask_t *task);

/**
 * @brief Clone a task structure.
 *
 * Clone the task structure @a task. A new task structure is created
 * via @ref PStask_new() and initialized to be an exact copy of @a
 * task. The new task structure may be removed with @ref
 * PStask_delete().
 *
 * @param task Pointer to the task structure to be cloned.
 *
 * @return On success, a pointer to the new task structure is
 * returned, or NULL otherwise. In the latter case errno is set
 * appropriately.
 *
 * @see PStask_new(), PStask_delete()
 */
PStask_t *PStask_clone(PStask_t *task);

/**
 * @brief Print a task structure in a string.
 *
 * Print the description of the task structure @a task into the
 * character array @a txt. At most @a size characters will be written
 * into the character array @a txt.
 *
 * @param txt Character array to print the task description into.
 *
 * @param size Size of the character array @a txt.
 *
 * @param task Pointer to the task structure to print.
 *
 * @return No return value
 */
void PStask_snprintf(char *txt, size_t size, PStask_t *task);

/**
 * @brief Encode a task structure.
 *
 * Encode the task structure @a task into the buffer @a buffer of
 * size @a size. This enables the task to be sent to a remote node
 * where it can be decoded using the @ref PStask_decodeTask() function.
 *
 * Beware of the fact that the task's argument-vector and environment
 * are not encoded. In order to send these parts they have to be
 * encoded separately using the @ref PStask_encodeArgv() and @ref
 * PStask_encodeEnv() functions respectively.
 *
 * Sending the task includes sending its working-directory. Since this
 * information might be larger than the buffer, additional messages
 * might be necessary. In this case @a offset will point to the
 * remnant of the working-directory still to be sent.
 *
 * In the current implementation of the protocol the original messages
 * is flagged as PSP_SPAWN_TASK while messages holding additional
 * parts of the working directory are in PSP_SPAWN_WDIRCNTD messages.
 *
 *
 * @param buffer The buffer used to encode the task structure.
 *
 * @param size The size of the buffer.
 *
 * @param task The task structure to encode.
 *
 * @param offset Pointing to trailing part of working-directory upon
 * return.
 *
 * @return On success, the number of bytes written to the buffer are
 * returned. If the return value is larger than @a size, the buffer is
 * too small in order to encode the task. In this case the task
 * structure will *not* be encoded, i.e. the buffer remains empty.
 *
 * A value of @a offset different from NULL upon return flags that the
 * task's working-directory was not completely encoded. Additional
 * messages containing the trailing part have to be sent.
 *
 * @see PStask_decodeTask(), PStask_encodeArgv(), PStask_encodeEnv()
 */
size_t PStask_encodeTask(char *buffer, size_t size, PStask_t *task,
			 char **offset);

/**
 * @brief Decode a task structure
 *
 * Decode a task structure encoded by PStask_encodeTask() and stored
 * within @a buffer and write it to the task structure @a task is
 * pointing to. @a withWdir flags if also a string describing the
 * working directory of the task shall be fetched from @a buffer and
 * added to the task structure @a task. This string is expected to be
 * located right after the encoded task structure within @a buffer.
 *
 * @param buffer The buffer the encoded task structure is stored in
 *
 * @param task The task structure to write to
 *
 * @param withWDir Flag to also fetch the working directory from buffer
 *
 * @return The number of characters within @a buffer used in order to
 * decode the task structure
 */
int PStask_decodeTask(char *buffer, PStask_t *task, bool withWdir);

/**
 * @brief Send task structure
 *
 * Send task structure @a task via the serialization layer utilizing
 * the data buffer @a msg. Only the core members of @a task will be
 * sent. Further parts like the argument vector or the environment are
 * omitted and have to be added explicitly via @ref addStringArray().
 *
 * @a msg has to be setup before in order to provide the message type,
 * the destination address, etc.
 *
 * @param msg Data buffer used for sending
 *
 * @param task Task structure to be sent
 *
 * @return On success true is returned; or false in case of error
 */
bool PStask_addToMsg(PStask_t *task, PS_SendDB_t *msg);

/**
 * @brief Encode argv part of task structure.
 *
 * Encode the argument-vector @a argv into the buffer @a buffer of
 * size @a size. This enables the argument-vector to be sent to a
 * remote node where the argument-vector shall be decoded using the
 * @ref PStask_decodeArgv() and PStask_decodeArgvAppend() functions.
 *
 * The actual task structure might be encoded using the @ref
 * PStask_encodeTask() function.
 *
 * Since both, the argument-vector as whole and single arguments,
 * might be substantially larger than the buffer's size @a size, it
 * might be split into more than one messages and, thus, buffer
 * contents. To support this feature, a pointer to an integer within
 * the calling context @a cur has to be provided. The integer @a cur
 * points to has to be set to 0 before calling this function for the
 * first time on a given @a argv. The same is true for the pointer @a
 * offset points to. During consecutive calls with the same @a argv
 * they have to be left untouched.
 *
 * In order to make correct use of this function the chunks of the
 * argument-vector have to be sent using different message types. The
 * type of message is determined by the @a offset parameter. If it is
 * different from NULL after a call, there will exists trailing parts
 * of the current argument which can be accessed by further calls to
 * this function. This trailing parts have to be flagged and handled
 * by the @ref PStask_decodeArgvAppend() function and have to be send
 * in strict order. As soon as @a offset is NULL again, all further
 * calls will give normal chunks unless @a offset is different from
 * NULL.
 *
 * In the current implementation of the protocol the messages are
 * flagged as PSP_SPAWN_ARG and PSP_SPAWN_ARGCNTD respectively.
 *
 * @param buffer The buffer used to encode the arguments.
 *
 * @param size The size of the buffer.
 *
 * @param argv The argument-vector to encode.
 *
 * @param cur Pointer to an integer holding the internal status in
 * between consecutive calls. Do not modify between consecutive
 * calls. The integer @a cur is pointing to has to be set to 0 before
 * doing the first call. This integer will be -1 after the last part
 * of the last argument is encoded.
 *
 * @param offset Pointer to an char pointer holding an offset for
 * trailing chunks of an environment variable. Do not modify between
 * consecutive calls. The pointer @a offset is pointing to has to be
 * set to NULL before doing the first call.
 *
 * @return On success, the number of bytes written to the buffer are
 * returned. Or 0, if an error occurs.
 *
 * @see PStask_encodeTask(), PStask_decodeArgv(), PStask_decodeArgvAppend()
 */
size_t PStask_encodeArgv(char *buffer, size_t size, char **argv, int *cur,
			 char **offset);

/**
 * @brief Decode argument-vector.
 *
 * Decode the argument-vector of a task structure encoded by
 * PStask_encodeArgv() and stored within @a buffer and write it to the
 * task structure @a task is pointing to.
 *
 * @param buffer The buffer holding the encoded argument-vector.
 *
 * @param task The task structure to write to.
 *
 * @return The number of characters used within @a buffer in order to
 * decode the argument-vector.
 */
int PStask_decodeArgv(char *buffer, PStask_t *task);

/**
 * @brief Decode trailing part of an argument.
 *
 * Decode a trailing part of an argument encoded by
 * PStask_encodeArgv() and stored within @a buffer and write it to the
 * task structure @a task is pointing to.
 *
 * While @ref PStask_decodeArgv() only can handle complete arguments
 * or the first parts of such, this function appends trailing parts of
 * an argument to an existing head.
 *
 * Trailing parts can be determined by the value of the @a offset
 * parameter of the @ref PStask_encodeArgv() function upon return.
 *
 * @param buffer The buffer the encoded argument-part is stored in.
 *
 * @param task The task structure to write to.
 *
 * @return The number of characters within @a buffer used in order to
 * decode the argument.
 */
int PStask_decodeArgvAppend(char *buffer, PStask_t *task);

/**
 * @brief Encode environment.
 *
 * Encode the environment @a env into the the buffer @a buffer of size
 * @a size. This enables the environment to be sent to a remote node
 * where it shall be decoded using the PStask_decodeEnv() and
 * PStask_decodeEnvAppend() functions.
 *
 * The actual task structure might be encoded using the @ref
 * PStask_encodeTask() function.
 *
 * Since both, the environment as whole and single environment's
 * key-value pairs, might be substantially larger than the buffer's
 * size @a size, it might be split into more than one messages and,
 * thus, buffer contents. To support this feature, a pointer to an
 * integer within the calling context @a cur has to be provided. The
 * integer @a cur points to has to be set to 0 before calling this
 * function for the first time on a given @a env. The same is true for
 * the pointer @a offset points to. During consecutive calls upon the
 * same @a env they have to be left untouched.
 *
 * Since the environment might be substantially larger than the
 * buffer's size @a size, it might be split into more than one
 * messages and thus buffer contents. To support this feature, a
 * pointer to an integer within the calling context @a cur has to be
 * provided. The integer @a cur points to has to be set to 0 before
 * calling this function for the first time on a given environment @a
 * env. The same is true for the pointer @a offset points to. During
 * consecutive calls with the same @a env they have to be left
 * untouched.
 *
 * In order to make correct use of this function the chunk of the
 * environment have to be sent using different message types. The type
 * of message is determined by the @a offset parameter. If it is
 * different from NULL after a call, there will exists trailing parts
 * of the current environment which can be accessed by further calls
 * to this function. This trailing parts have to be flagged and
 * handled by the @ref PStask_decodeEnvAppend() function and have to
 * be send in strict order. As soon as @a offset is NULL again, all
 * further calls will give normal chunks unless @a offset is different
 * from NULL.
 *
 * In the current implementation of the protocol the messages are
 * flagged as PSP_SPAWN_ENV and PSP_SPAWN_ENVCNTD respectively.
 *
 * @param buffer The buffer used to encode the environment.
 *
 * @param size The size of the buffer.
 *
 * @param env The environment to encode.
 *
 * @param cur Pointer to an integer holding the internal status in
 * between consecutive calls. Do not modify between consecutive calls.
 * The integer @a cur is pointing to has to be set to 0 before doing
 * the first call. This integer will be -1 after the last part of the
 * last argument is encoded.
 *
 * @param offset Pointer to an char pointer holding an offset for
 * trailing chunks of an environment variable. Do not modify between
 * consecutive calls. The pointer @a offset is pointing to has to be
 * set to NULL before doing the first call.
 *
 * @return On success, the number of bytes written to the buffer are
 * returned. Or 0, if an error occurs.
 *
 * @see PStask_decodeEnv() PStask_decodeEnvAppend()
 */
size_t PStask_encodeEnv(char *buffer, size_t size, char **env, int *cur,
			char **offset);

/**
 * @brief Decode environment.
 *
 * Decode the environment of a task structure encoded by
 * PStask_encodeEnv() and stored within @a buffer and write it to the
 * task structure @a task is pointing to.
 *
 * @param buffer The buffer holding the encoded environment.
 *
 * @param task The task structure to write to.
 *
 * @return The number of characters within @a buffer used in order to
 * decode the environment.
 */
int PStask_decodeEnv(char *buffer, PStask_t *task);

/**
 * @brief Decode trailing part of an environment key-value pair.
 *
 * Decode a trailing part of an environment key-value pair encoded by
 * PStask_encodeEnv() and stored within @a buffer and write it to the
 * task structure @a task is pointing to.
 *
 * While @ref PStask_decodeEnv() only can handle complete key-value
 * pairs or the first part of such, this function appends trailing
 * parts of an environment key-value pair to an existing head.
 *
 * Trailing parts can be determined by the value of the @a offset
 * parameter of the @ref PStask_encodeEnv() function upon return.
 *
 * @param buffer The buffer the encoded environment-part is stored in.
 *
 * @param task The task structure to write to.
 *
 * @return The number of characters within @a buffer used in order to
 * decode the key-value pair.
 */
int PStask_decodeEnvAppend(char *buffer, PStask_t *task);

/**
 * @brief Get reservation ID
 *
 * Get an unused reservation ID for task @a task.
 *
 * @param task The task providing the unique sequence
 *
 * @return The new reservation ID
 */
PSrsrvtn_ID_t PStask_getNextResID(PStask_t *task);

#endif  /* __PSTASK_H */
