/*
 *               ParaStation3
 * psitask.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psitask.h,v 1.6 2002/02/19 09:31:06 eicker Exp $
 *
 */
/**
 * @file
 * psitask: User-functions for interaction with ParaStation tasks.
 *
 * $Id: psitask.h,v 1.6 2002/02/19 09:31:06 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSITASK_H
#define __PSITASK_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/*----------------------------------------------------------------------
 * Task Group constants
 */
#define TG_ANY        0x0001
#define TG_ADMIN      0x0002
#define TG_RESET      0x0003
#define TG_RESETABORT 0x0004
#define TG_LOGGER     0x0005

/*----------------------------------------------------------------------
 *   TaskOptions
 * Taskoptions are used to set the bits in the PSI_mychildoptions, which
 * influence the behaviour of a child being created.
 */
enum TaskOptions{
    TaskOption_ONESTDOUTERR   = 0x0001,
    /* use only one channel for stdout and stderr */
    TaskOption_SENDSTDHEADER  = 0x0002
    /* send a header (clientlogger_t) after connecting to stderr/stdout */
};

/*----------------------------------------------------------------------
 * Task types
 */
struct PSsignal_t{
    long tid;
    int signal;
    struct PSsignal_t* next;
};

#define PSTASKLIST_ENQUEUE(list,element) { element->link = list->link;\
					   element->rlink=list;\
					   list->link=element;\
					   element->link->rlink=element;}

typedef struct PStask_T{
    struct PStask_T* link;       /* link to the next task */
    struct PStask_T* rlink;      /* link to the previous task */
    long tid;                    /* task identifier */
    long ptid;                   /* parent tid */
    uid_t uid;                   /* user id */
    gid_t gid;                   /* group id */
    short nodeno;                /* node number of the executing node */
    long  group;                 /* task group number see TG_* constants */
    int rank;                    /* rank given for spwaned childs */
    unsigned int loggernode;     /* the logging peer for any output */
    int loggerport;              /* the logging peer for any output */
    short fd;                    /* connection fd of the psid, otherwise -1 */
    long error;                  /* error number */
    int confirmed;               /* flag for reconnecting daemons */
    long options;                /* options of this task */
    char* workingdir;            /* working directory */
    int argc;                    /* number of argv */
    char **argv;                 /* command line arguments */
    char **environ;              /* PS Environment, used before spawning */
    int childsignal;             /* the signal which is sent when a child dies
				    previor to PULC_clientinit() */
    struct PSsignal_t* signalsender;   /* List of tasks which sent signals */
    struct PSsignal_t* signalreceiver; /* List of tasks which want to receive
					  a signals when this task dies */
}PStask_t;

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
void PStask_sprintf(char *txt, PStask_t *task);

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


/*----------------------------------------------------------------------*/
/*
 * PStask_setsignalreceiver
 *
 *  adds the receiver TID to the list of tasks which shall receive a
 *  signal, when this task dies
 *  RETURN: void
 */
void PStask_setsignalreceiver(PStask_t *task, long sender, int signal);

/*----------------------------------------------------------------------*/
/*
 * PStask_getsignalreceiver
 *
 *  returns the tid of the task,which sent the signal
 *  removes the signalreceiver from the list
 *  RETURN: 0 if no such task exists
 *          >0 : tid of the receiver task
 */
long PStask_getsignalreceiver(PStask_t *task, int *signal);

/*----------------------------------------------------------------------*/
/*
 * PStask_setsignalsender
 *
 *  adds the sender TID to the list of tasks which has send a signal to this
 *  task due to the death of sender tid
 *  RETURN: void
 */
void PStask_setsignalsender(PStask_t *task, long sender, int signal);

/*----------------------------------------------------------------------*/
/*
 * PStask_getsignalsender
 *
 *  returns the tid of the task,which sent the signal
 *  removes the signalsender from the list
 *  RETURN: 0 if no such task exists
 *          >0 : tid of the sender task
 */
long PStask_getsignalsender(PStask_t *task, int *signal);


/*----------------------------------------------------------------------
 * Tasklist routines
 */

/*----------------------------------------------------------------------*/
/*
 * PStasklist_delete
 *
 *  deletes all tasks and the structure itself
 *  RETURN: 0 on success
 */
void PStasklist_delete(PStask_t **list);

/*----------------------------------------------------------------------*/
/*
 * PStasklist_enqueue
 *
 *  enqueus a task into a tasklist.
 *  RETURN: 0 on success
 */
int PStasklist_enqueue(PStask_t **list, PStask_t *newtask);

/*----------------------------------------------------------------------*/
/*
 * PStasklist_dequeue
 *
 *  dequeues a task from a tasklist.
 *  if tid==-1, the first task is dequeued otherwise exactly the
 *  task with TID==tid is dequeued
 *  RETURN: the removed task on success
 *          NULL if not found
 */
PStask_t* PStasklist_dequeue(PStask_t **list, long tid);

/*----------------------------------------------------------------------*/
/*
 * PStasklist_find
 *
 *  finds a task in a tasklist.
 *  RETURN: the task on success
 *          NULL if not found
 */
PStask_t* PStasklist_find(PStask_t *list, long tid);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSITASK_H */
