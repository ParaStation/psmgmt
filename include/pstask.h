/*
 *               ParaStation3
 * pstask.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: pstask.h,v 1.6 2002/07/24 06:24:14 eicker Exp $
 *
 */
/**
 * @file
 * User-functions for interaction with ParaStation tasks.
 *
 * $Id: pstask.h,v 1.6 2002/07/24 06:24:14 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSTASK_H
#define __PSTASK_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** @todo Documentation */

/**
 * Task Group constants
 */
typedef enum {
    TG_ANY,       /**< A normal task */
    TG_ADMIN,     /**< Taskgroup for psiadmin (and GUI client) */
    TG_RESET,     /**< A normal task */
    TG_LOGGER     /**< A normal task */
} PStask_group_t;

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


/** Signal structure @todo */
typedef struct PSsig_T{
    long tid;                    /**< unique task identifier */
    int signal;                  /**< signal to send, or -1 for childsignal */
    struct PSsig_T *next;     /**< link to the next signal */
} PStask_sig_t;

/** Task structure @todo */
/* Member marked with C are (un)packed by PStask_encode()/PStask_decode() */
typedef struct PStask_T{
    struct PStask_T *next;         /**< link to the next task */
    struct PStask_T *prev;         /**< link to the previous task */

    long tid;                /*C*/ /**< unique task identifier */
    long ptid;               /*C*/ /**< unique identifier of parent task */
    uid_t uid;               /*C*/ /**< user id */
    gid_t gid;               /*C*/ /**< group id */
    PStask_group_t group;    /*C*/ /**< task group @see PStask_group_t */
    int rank;                /*C*/ /**< rank of task within task group */
    unsigned int loggernode; /*C*//*obsolete*/ /* the logging peer for any output */
    int loggerport;          /*C*//*obsolete*/ /* the logging peer for any output */
    long loggertid;          /*C*/ /**< unique identifier of the logger */
    short fd;                      /**< connection fd within psid */
    char *workingdir;        /*C*/ /**< working directory */
    int argc;                /*C*/ /**< num of arguments, length of @a argv */
    char **argv;             /*C*/ /**< command line arguments */
    char **environ;          /*C*/ /**< PS environment, used for spawning */
    int childsignal;               /**< the signal sent when a child dies */
    int pendingReleaseRes;         /**< num of pending RELEASERES messages */
    int released;                  /**< flag to mark released task, i.e. don't
				        send signal to parent on exit */

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
 * @brief Print a task structure in a string.
 *
 * Print the description of the task structure @a task into the
 * character array @a txt.
 *
 * @param txt Character array to print task description into.
 * @param size Size of the character array @a txt.
 * @param task Pointer to the task structure to print.
 *
 * @return No return value.
 * */
void PStask_snprintf(char *txt, size_t size, PStask_t *task);

/**
 * @todo
 * PStask_encode
 * encodes the task structure into a string, so it can be sent
 */
int PStask_encode(char *buffer, size_t size, PStask_t *task);

/**
 * @todo
 * PStask_decode
 * decodes the task structure from a string, maybe it was sent
 *
 * IN: buffer beginning with the data,
 * OUT: an initilized task structure
 */
int PStask_decode(char *buffer, PStask_t *task);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSTASK_H */
