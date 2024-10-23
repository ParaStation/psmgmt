/*
 * ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
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
#include <stddef.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>

#include "list_t.h"
#include "pstaskid.h" // IWYU pragma: export

#include "pscpu.h"
#include "psenv.h"
#include "psitems.h"
#include "pssenddb_t.h"
#include "psstrv.h"

/**
 * @brief Get the name of a PStask_group
 *
 * Get the name of a PStask_group.
 *
 * @param taskgroup PStask_group the name is wanted for
 *
 * @return The name of the PStask_group or "UNKNOWN"
 */
const char* PStask_printGrp(PStask_group_t taskgroup);

/**
 * @brief Print statistics
 *
 * Print statistics concerning the usage of signal, info, and
 * reservation structures.
 *
 * @return No return value
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

/** Different types of info items to be attached to a task */
typedef enum {
    TASKINFO_DRAINED = PSITEM_DRAINED, /**< Helper type for item handling */
    TASKINFO_UNUSED = PSITEM_IDLE,     /**< Helper type for item handling */
    TASKINFO_ALL = 0,                  /**< Helper type to travers all items */
    TASKINFO_FORWARDER = 1,            /**< Info points to ForwarderData_t */
    TASKINFO_STEP,                     /**< Info points to psslurm's Step_t */
    TASKINFO_JOB,                      /**< Info points to psslurm's Job_t */
} PStask_info_t;

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
    /*C*/ strv_t argV;             /**< command line arguments */
    /*C*/ env_t env;               /**< PS environment, used for spawning */
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
    bool deleted;                  /**< flag to mark deleted tasks. It will be
				      removed from the list of managed tasks
				      in the next round of the main loop */
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
    PStask_ID_t firstSpawner;      /**< first spawner requested a reservation */
    list_t resRequests;            /**< List of reservation requests (FIFO) */
    list_t reservations;           /**< List of active reservations */
    env_t extraJobData;            /**< Extra data distributed with job */
    int32_t activeChild;           /**< # of active children right now */
    int32_t numChild;              /**< Total # of children spawned over time */
    PSpart_slot_t *spawnNodes;     /**< Nodes the task can spawn to */
    uint32_t spawnNodesSize;       /**< Current size of @ref spawnNodes */
    uint32_t spawnNum;             /**< Amount of content of @ref spawnNodes */
    PStask_t *delegate;            /**< Delegate holding resources */
    PStask_sigChldCB_t *sigChldCB; /**< Callback to be executed on SIGCHLD */
    list_t info;                   /**< List of extra information items */

    list_t signalSender;           /**< Tasks which sent signals */
    list_t signalReceiver;         /**< Tasks which want to receive signals */
    list_t assignedSigs;           /**< Tasks assigned to send signals */
    list_t keptChildren;           /**< Children kept during inheritance */
} /* PStask_t */;

/**
 * @brief Create a new task structure
 *
 * A new task structure is created by allocating the corresponding
 * memory. Furthermore it is initialized ready for use. It shall be
 * removed with @ref PStask_delete().
 *
 * @return On success a pointer to the new task structure is
 * returned, or NULL otherwise
 *
 * @see PStask_delete()
 */
PStask_t *PStask_new(void);

/**
 * @brief Delete a task structure
 *
 * Delete the task structure @a task created via @ref
 * PStask_new(). First the task is cleaned up by @ref PStask_reinit(),
 * i.e. all allocated strings, signal-lists, etc. are
 * removed. Afterward the task itself is removed.
 *
 * @param task Pointer to the task structure to be deleted
 *
 * @return On success true is returned; or false in case of error
 *
 * @see PStask_new()
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
 * @brief Decode a task structure
 *
 * Decode a task structure sent via @ref PStask_addToMsg() and
 * received as a data blob of size @a len provided in @a data. The
 * decoded data is written to the task structure @a task is pointing
 * to.
 *
 * Before adding any data to @a task the structure will be cleaned
 * up. Thus, any information contained will get lost.
 *
 * @param data Data blob holding the encoded task structure
 *
 * @param len Size of @a data
 *
 * @param task Task structure to write to
 *
 * @return On success return true or false on error
 */
bool PStask_decodeTask(void *data, size_t len, PStask_t *task);

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
 * @brief Add extra information to task
 *
 * Add the extra information @a info of type @a type to the task
 * structure @a task.
 *
 * @param task Task structure to add the information to
 *
 * @param type Type of information to be added
 *
 * @param info Actual information to add
 *
 * @return On success true is returned; or false in case of failure
 */
bool PStask_infoAdd(PStask_t *task, PStask_info_t type, void *info);

/**
 * @brief Get extra information from task
 *
 * Retrieve the first information of type @a type from the task
 * structure @a task.
 *
 * @param task Task structure to investigate
 *
 * @param type Type of information to retrieve
 *
 * @return On success, i.e. if a corresponding extra information was
 * added to @a task, this information is returned; or NULL otherwise
 */
void * PStask_infoGet(PStask_t *task, PStask_info_t type);

/**
 * @brief Remove extra information from task
 *
 * Remove the extra information @a info of type @a type from the task
 * structure @a task. If @a info is NULL, the first occurrence of
 * extra information of type @a type is removed.
 *
 * @param task Task structure to modify
 *
 * @param type Type of information to remove
 *
 * @param info Actual information to be removed from @a task; might be
 * NULL to evict the first information of type @a type
 *
 * @return On success, i.e. if a corresponding extra information was
 * removed from @a task, true is returned; or false otherwise
 */
bool PStask_infoRemove(PStask_t *task, PStask_info_t type, void *info);

/**
 * @brief Visitor function
 *
 * Visitor function used by @ref PStask_infoTraverse() in order to
 * visit each information object in a given task structure.
 *
 * The parameters are as follows: @a task points to the task structure
 * whose list of extra information is traversed, @a type provides the
 * type of information presented, and @a info is the information
 * itself.
 *
 * If the visitor function returns false, the traversal will be
 * interrupted and @ref PStask_infoTraverse() will return to its
 * calling function.
 */
typedef bool PStask_infoVisitor_t(const PStask_t *task,
				  const PStask_info_t type, const void *info);

/**
 * @brief Traverse task's extra information
 *
 * Traverse the task structure's @a task extra information by calling
 * @a visitor for each of the embodied information items of type @a
 * type. If @a type is TASKINFO_ALL, every extra information is
 * presented to @a visitor.
 *
 * If @a visitor returns false, the traversal will be stopped
 * immediately and false is returned to the calling function.
 *
 * @param task Task structure to investigate
 *
 * @param type Type of information to be presented; in case of
 * TASKINFO_ALL, every information item is presented
 *
 * @param visitor Visitor function to be called for each object
 *
 * @return If @a visitor returns false, traversal will be stopped and
 * false is returned; or true if no visitor returned false during the
 * traversal and no error occurred
 */
bool PStask_infoTraverse(PStask_t *task, PStask_info_t type,
			 PStask_infoVisitor_t visitor);

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

/**
 * @brief Garbage collection
 *
 * Do garbage collection on unused info structures. Since this module
 * will keep pre-allocated buffers for info structures its
 * memory-footprint might have grown after phases of heavy
 * usage. Thus, this function shall be called regularly in order to
 * free() info structures no longer required.
 *
 * @return No return value
 */
void PStask_gc(void);

/**
 * @brief Memory cleanup
 *
 * Cleanup all memory currently used by info structures. It will very
 * aggressively free all allocated memory most likely destroying
 * existing info lists. Thus, these should have been cleaned up
 * earlier. Currently this requires PSIDtask to be cleaned up.
 *
 * The purpose of this function is to cleanup before a fork()ed
 * process is handling other tasks, e.g. becoming a forwarder.
 *
 * @return No return value.
 */
void PStask_clearMem(void);

/**
 * @brief Initialize the info structure pool
 *
 * Initialize to pool of info structures. This must be called before
 * any function manipulating a task's extra information like @ref
 * PStask_infoAdd(), or @ref PStask_infoRemove().
 *
 * @return No return value
 *
 * @see PStask_infoAdd(), PStask_infoRemove()
 */
void PStask_init(void);

#endif  /* __PSTASK_H */
