/*
 *               ParaStation3
 * pstask.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: pstask.h,v 1.2 2002/07/08 16:14:06 eicker Exp $
 *
 */
/**
 * @file
 * pstask: User-functions for interaction with ParaStation tasks.
 *
 * $Id: pstask.h,v 1.2 2002/07/08 16:14:06 eicker Exp $
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

/*----------------------------------------------------------------------
 * Task Group constants
 */
#define TG_ANY        0x0001
#define TG_ADMIN      0x0002
#define TG_RESET      0x0003
#define TG_RESETABORT 0x0004
#define TG_LOGGER     0x0005

/*----------------------------------------------------------------------
 * Task types
 */
struct PSsignal_t{
    long tid;
    int signal;
    struct PSsignal_t *next;
};

typedef struct PStask_T{
    struct PStask_T *link;       /* link to the next task */
    struct PStask_T *rlink;      /* link to the previous task */
    long tid;                    /* task identifier */
    long ptid;                   /* parent tid */
    uid_t uid;                   /* user id */
    gid_t gid;                   /* group id */
    long  group;                 /* task group number @see TG_* constants */
    int rank;                    /* rank given for spwaned childs */
    unsigned int loggernode;     /* the logging peer for any output */
    int loggerport;              /* the logging peer for any output */
    short fd;                    /* connection fd of the psid, otherwise -1 */
    long error;                  /* error number */
    int confirmed;               /* flag for reconnecting daemons */
    char *workingdir;            /* working directory */
    int argc;                    /* number of argv */
    char **argv;                 /* command line arguments */
    char **environ;              /* PS Environment, used before spawning */
    int childsignal;             /* the signal which is sent when a child dies
				    previor to PULC_clientinit() */
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
