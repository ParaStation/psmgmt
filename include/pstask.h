/*
 *               ParaStation3
 * pstask.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: pstask.h,v 1.4 2002/07/11 16:48:28 eicker Exp $
 *
 */
/**
 * @file
 * pstask: User-functions for interaction with ParaStation tasks.
 *
 * $Id: pstask.h,v 1.4 2002/07/11 16:48:28 eicker Exp $
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
 * @todo
 *
 */
char *PStask_groupMsg(PStask_group_t tg);

/*----------------------------------------------------------------------
 * Task types
 */
struct PSsignal_t{
    long tid;
    int signal;
    struct PSsignal_t *next;
};

typedef struct PStask_T{
    struct PStask_T *next;          /**< link to the next task */
    struct PStask_T *prev;          /**< link to the previous task */

    long tid;                /*C*/  /**< unique task identifier */
    long ptid;               /*C*/  /**< unique identifier of parent task */
    uid_t uid;               /*C*/  /**< user id */
    gid_t gid;               /*C*/  /**< group id */
    PStask_group_t group;    /*C*/  /**< task group @see PStask_group_t */
    int rank;                /*C*/  /**< rank of task within task group */
    unsigned int loggernode; /*C*/  /* the logging peer for any output */
    int loggerport;          /*C*/  /* the logging peer for any output */
    short fd;                       /**< connection fd within psid */
    int confirmed;     /* obsolete */  /* flag for reconnecting daemons */
    char *workingdir;        /*C*/  /* working directory */
    int argc;                /*C*/  /**< number of argument, length of argv */
    char **argv;             /*C*/  /**< command line arguments */
    char **environ;          /*C*/  /**< PS environment, used for spawning */
    int childsignal;                /**< the signal sent when a child dies */

    struct PSsignal_t *signalsender;   /* List of tasks which sent signals */
    struct PSsignal_t *signalreceiver; /* List of tasks which want to receive
					  a signals when this task dies */
} PStask_t;

/*----------------------------------------------------------------------
 * Task routines
 */

/*----------------------------------------------------------------------*/
/*
 * returns a new task structure
 */
PStask_t* PStask_new(void);

/*----------------------------------------------------------------------*/
/*
 * initializes a task structure
 */
int PStask_init(PStask_t *task);

/*----------------------------------------------------------------------*/
/*
 * reinitializes a task structure that was previously used
 * the allocated strings shall be removed
 */
int PStask_reinit(PStask_t *task);

/*----------------------------------------------------------------------*/
/*
 * deletes a task structure and all strings associated with it
 */
int PStask_delete(PStask_t * task);

/*----------------------------------------------------------------------*/
/*
 * prints the task structure in a string
 */
void PStask_snprintf(char *txt, size_t size, PStask_t *task);

/*----------------------------------------------------------------------*/
/*
 * PStask_encode
 * encodes the task structure into a string, so it can be sent
 */
int PStask_encode(char *buffer,PStask_t *task);

/*----------------------------------------------------------------------*/
/*
 * PStask_decode
 * decodes the task structure from a string, maybe it was sent
 *
 * IN: buffer beginning with the data,
 * OUT: an initilized task structure
 */
int PStask_decode(char *buffer,PStask_t *task);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSTASK_H */
