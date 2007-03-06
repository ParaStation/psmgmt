/*
 *               ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2007 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file
 * User-functions for interaction with ParaStation tasks.
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSTASK_H
#define __PSTASK_H

#include <stdint.h>
#include <sys/types.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <time.h>

#include "psnodes.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** Task Group constants */
typedef enum {
    TG_ANY,       /**< normal task */
    TG_ADMIN,     /**< taskgroup for psiadmin (and GUI client) */
    TG_RESET,     /**< normal task */
    TG_LOGGER,    /**< special task, the logger */
    TG_FORWARDER, /**< special task, the forwarder */
    TG_SPAWNER,   /**< special task, the spawner (helper to spawn p4 jobs) */
    TG_GMSPAWNER, /**< special task, the gmspawner (helper to spawn GM) */
    TG_MONITOR,   /**< special task that monitors the daemon. Don't kill */
    TG_PSCSPAWNER,/**< special task, the pscspawner (helper to spawn PSC) @deprecated */
    TG_ADMINTASK, /**< admin-task, i.e. unaccounted task */
    TG_SERVICE,   /**< special task, used to spawn new PSC tasks */
    TG_ACCOUNT,   /**< accounter, will receive and log accounting info */
} PStask_group_t;

/** Type to store unique task IDs in */
typedef int32_t PStask_ID_t;

/**
 * @brief Get the name of a PStask_group.
 *
 * Get the name of a PStask_group.
 *
 * @param taskgroup The PStask_group the name is wanted for.
 *
 * @return The name of the PStask_group or "UNKNOWN".
 */
char *PStask_printGrp(PStask_group_t taskgroup);


/** Signal structure */
typedef struct PSsig_T{
    PStask_ID_t tid;          /**< unique task identifier */
    int32_t signal;           /**< signal to send, or -1 for childsignal */
    struct PSsig_T *next;     /**< link to the next signal */
} PStask_sig_t;

#include "pspartition.h"

/** Task structure */
/* Members marked with C are (un)packed by PStask_encode()/PStask_decode() */
typedef struct PStask_T{
    struct PStask_T *next;         /**< link to the next task */
    struct PStask_T *prev;         /**< link to the previous task */

    /*C*/ PStask_ID_t tid;         /**< unique task identifier */
    /*C*/ PStask_ID_t ptid;        /**< unique identifier of parent task */
    /*C*/ uid_t uid;               /**< user id */
    /*C*/ gid_t gid;               /**< group id */
    /*C*/ uint32_t aretty;         /**< flag stdin, stdout & stderr as tty */
    char interactive;              /**< stdin, stdout and stderr: all ttys */
    int stdin_fd;                  /**< helper fd during spawn */
    int stdout_fd;                 /**< helper fd during spawn */
    int stderr_fd;                 /**< helper fd during spawn */
    /*C*/ struct termios termios;  /**< parameters of the controlling tty */
    /*C*/ struct winsize winsize;  /**< window size of the controlling tty */
    /*C*/ PStask_group_t group;    /**< task group @see PStask_group_t */
    /*C*/ PStask_ID_t loggertid;   /**< unique identifier of the logger */
    PStask_ID_t forwardertid;      /**< unique identifier of the forwarder */
    /*C*/ int32_t rank;            /**< rank of task within task group */
    short fd;                      /**< connection fd within psid */
    /*C*/ char *workingdir;        /**< working directory */
    /*C*/ int32_t argc;            /**< num of args, length of @a argv */
    /*C*/ char **argv;             /**< command line arguments */
    /*C*/ char **environ;          /**< PS environment, used for spawning */
    int envSize;                   /**< Cur. size of environ (needed
				      during @ref PStask_decodeEnv()) */
    int relativesignal;            /**< the signal sent when a relative (i.e.
				      parent or child) dies */
    int pendingReleaseRes;         /**< num of pending RELEASERES messages */
    char released;                 /**< flag to mark released task, i.e. don't
				      send signal to parent on exit */
    char duplicate;                /**< flag to mark duplicate task, i.e. a
				      tasks that are fork()ed by a client */
    char suspended;                /**< flag to mark suspended tasks. */
    char removeIt;                 /**< flag to mark task to be removed (as
				      soon as all childs are released). */
    time_t killat;                 /**< flag a killed task, i.e. the time when
				      the task should really go away. */
    uint16_t protocolVersion;      /**< Protocol version the task speaks. */
    PStask_sig_t *childs;          /**< Childs of the task. Signal not used. */
    PSpart_request_t *request;     /**< Pointer to temp. partition request */
    uint32_t partitionSize;        /**< Size of the partition. */
    PSpart_option_t options;       /**< The partition's options. */
    PSpart_slot_t *partition;      /**< The actual partition. List of slots. */
    int nextRank;                  /**< Next rank to start within the task. */

    PStask_sig_t *signalSender;    /**< Tasks which sent signals */
    PStask_sig_t *signalReceiver;  /**< Tasks which want to receive signals */
    PStask_sig_t *assignedSigs;    /**< Tasks assigned to send signals */
} PStask_t;

/**
 * @brief Create a new task structure.
 *
 * A new task structure is created and initialized via @ref
 * PStask_init(). It may be removed with @ref PStask_delete().
 *
 * @return On success, a pointer to the new task structure is
 * returned, or NULL otherwise.
 *
 * @see PStask_init(), PStask_delete
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
 * @return On success, 1 is returned, or 0 otherwise.
 */
int PStask_init(PStask_t *task);

/**
 * @brief Reinitialize a task structure.
 *
 * Reinitialize the task structure @a task that was previously
 * used. All allocated strings and signallists shall be removed, all
 * links are reset to NULL.
 *
 * @param task Pointer to the task structure to be reinitialized.
 *
 * @return On success, 1 is returned, or 0 otherwise.
 */
int PStask_reinit(PStask_t *task);

/**
 * @brief Delete a task structure.
 *
 * Delete the task structure @a task created via @ref
 * PStask_new(). First the task is cleaned up by @ref PStask_reinit(),
 * i.e. all allocated strings and signallists are removed. Afterward
 * the task itself is removed.
 *
 * @param task Pointer to the task structure to be deleted.
 *
 * @return On success, 1 is returned, or 0 otherwise.
 */
int PStask_delete(PStask_t *task);

/**
 * @brief Clone a signal list.
 *
 * Create an exact clone of the signal list @a siglist and return a
 * pointer to first element of the cloned list.
 *
 * @param siglist The signal list to clone.
 *
 * @return On success, a pointer to the first element of the cloned
 * signal list is returned, or NULL otherwise. Beware of the fact that
 * the return value might also be NULL if the original @a siglist was
 * also NULl.
 */
PStask_sig_t *PStask_cloneSigList(PStask_sig_t *siglist);

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
 * returned, or NULL otherwise.
 *
 * @see PStask_new(), PStask_delete
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
 * @return No return value.
 */
void PStask_snprintf(char *txt, size_t size, PStask_t *task);

/**
 * @brief Encode a task structure.
 * @deprecated
 *
 * Encode the task structure @a task into the the buffer @a buffer of
 * size @a size. This enables the task to be sent to a remote node
 * where it can be decoded using the @ref PStask_decodeFull() function.
 *
 * This function encodes the full task structure, i.e. including the
 * argv part and the whole environment. This might lead to huge
 * packets sent to remote nodes on the hand and disables some task
 * structures to be sent at all.
 *
 * In order to circumvent such problems, a new family of encoding
 * functions (@ref PStask_encodeTask(), @ref PStask_encodeArgs(), @ref
 * PStask_encodeEnv()) was introduced.
 *
 * @param buffer The buffer used to encode the task structure.
 *
 * @param size The size of the buffer.
 *
 * @param task The task structure to encode.
 *
 * @return On success, the number of bytes written to the buffer are
 * returned. If the return value is larger than @a size, the buffer is
 * to small in order to encode the whole task. If this is the case the
 * new family of encoding functions might help.
 *
 * @see PStask_decodeFull(), PStask_encodeTask(), PStask_encodeArgs(),
 * PStask_encodeEnv()
 */
size_t PStask_encodeFull(char *buffer, size_t size, PStask_t *task);

/**
 * @brief Decode a task structure.
 *
 * Decode a task structure encoded by @ref PStask_encodeFull() and
 * stored within @a buffer and write it to the task structure @a task
 * is pointing to.
 *
 * @param buffer The buffer the encoded task strucure is stored in.
 *
 * @param task The task structure to write to.
 *
 * @return The number of chars within @a buffer used in order to
 * decode the task structure.
 *
 * @see PStask_encodeFull()
 */
int PStask_decodeFull(char *buffer, PStask_t *task);

/**
 * @brief Encode a task structure.
 *
 * Encode the task structure @a task into the the buffer @a buffer of
 * size @a size. This enables the task to be sent to a remote node
 * where it can be decoded using the @ref PStask_decodeTask() function.
 *
 * Beware of the fact that the argv and environment part of the task
 * structure is not encoded. In order to send these part they have to
 * be encoded separately using the @ref PStask_encodeArgs() and @ref
 * PStask_encodeEnv() functions.
 *
 * @param buffer The buffer used to encode the task structure.
 *
 * @param size The size of the buffer.
 *
 * @param task The task structure to encode.
 *
 * @return On success, the number of bytes written to the buffer are
 * returned. If the return value is larger than @a size, the buffer is
 * to small in order to encode the whole task.
 *
 * @see PStask_decodeTask(), PStask_encodeArgs(), PStask_encodeEnv()
 */
size_t PStask_encodeTask(char *buffer, size_t size, PStask_t *task);

/**
 * @brief Decode a task structure.
 *
 * Decode a task structure encoded by PStask_encodeTask() and stored
 * within @a buffer and write it to the task structure @a task is
 * pointing to.
 *
 * @param buffer The buffer the encoded task strucure is stored in.
 *
 * @param task The task structure to write to.
 *
 * @return The number of chars within @a buffer used in order to
 * decode the task structure.
 */
int PStask_decodeTask(char *buffer, PStask_t *task);

/**
 * @brief Encode argv part of task structure.
 *
 * Encode the argv-part of a task structure @a task into the the
 * buffer @a buffer of size @a size. This enables the task to be sent
 * to a remote node where this part can be decoded using the
 * @ref PStask_decodeArgs() function.
 *
 * The actual task structure might be encoded using the @ref
 * PStask_encodeTask() function.
 *
 * @param buffer The buffer used to encode the task structure.
 *
 * @param size The size of the buffer.
 *
 * @param task The task structure to encode.
 *
 * @return On success, the number of bytes written to the buffer are
 * returned. If the return value is larger than @a size, the buffer is
 * to small in order to encode the task's argv-part.
 *
 * @see PStask_encodeTask(), PStask_decodeArgs()
 */
size_t PStask_encodeArgs(char *buffer, size_t size, PStask_t *task);

/**
 * @brief Decode argv part of task structure.
 *
 * Decode the argv-part of a task structure encoded by
 * PStask_encodeArgs() and stored within @a buffer and write it to the
 * task structure @a task is pointing to.
 *
 * @param buffer The buffer the encoded argv-part is stored in.
 *
 * @param task The task structure to write to.
 *
 * @return The number of chars within @a buffer used in order to
 * decode the argv-part of the task structure.
 */
int PStask_decodeArgs(char *buffer, PStask_t *task);

/**
 * @brief Encode environment part of task structure.
 *
 * Encode the environment-part of a task structure @a task into the
 * the buffer @a buffer of size @a size. This enables the task to be
 * sent to a remote node where it can be decoded using the
 * PStask_decodeEnv() function.
 *
 * The actual task structure might be encoded using the @ref
 * PStask_encodeTask() function.
 *
 * Since the environment-part of a task structure might be
 * substantially larger than the buffer's size @a size, it might be
 * splitted into more than one messages and thus buffer contents. To
 * support this feature, a pointer to an integer within the calling
 * context @a cur has to be provided. The integer @a cur points to has
 * to be set to 0 before calling this function for the first time on a
 * given structure @a task. During consecutive calls with the same @a
 * task it has to be left untouched.
 *
 * @param buffer The buffer used to encode the task structure.
 *
 * @param size The size of the buffer.
 *
 * @param task The task structure containing the environment to encode.
 *
 * @param cur Pointer to an integer holding the internal status in
 * between consecutive calls.
 *
 * @return On success, the number of bytes written to the buffer are
 * returned. If the return value is larger than @a size, the buffer is
 * to small in order to encode the whole task.
 *
 * @see PStask_encodeTask() PStask_decodeEnv()
 */
size_t PStask_encodeEnv(char *buffer, size_t size, PStask_t *task, int *cur);

/**
 * @brief Decode environment part of task structure.
 *
 * Decode the environment-part of a task structure encoded by
 * PStask_encodeArgs() and stored within @a buffer and write it to the
 * task structure @a task is pointing to.
 *
 * @param buffer The buffer the encoded environment-part is stored in.
 *
 * @param task The task structure to write to.
 *
 * @return The number of chars within @a buffer used in order to
 * decode the environment-part of the task structure.
 */
int PStask_decodeEnv(char *buffer, PStask_t *task);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSTASK_H */
