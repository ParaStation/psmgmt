/*
 *               ParaStation3
 * pse.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: pse.h,v 1.5 2002/02/08 10:45:06 eicker Exp $
 *
 */
/**
 * @file
 * ParaStation Programming Environment
 *
 * $Id: pse.h,v 1.5 2002/02/08 10:45:06 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSE_H
#define __PSE_H

/* time limit within which parent process must receive the global
   portid of each spawned child process                           */
#define PSE_TIME_OUT         2000000

/* time in which parent process makes a pause */
#define PSE_TIME_SLEEP       100

/* these are tags used with the PSISend/Receive functions */
#define PSE_InitExit_Tag     3   /* for starting/terminating processes */

/* hier aendern, wenn wieder DEBUG Meldungden kommen sollen */
#ifndef DEBUG
#ifdef DODEBUG
#include <stdio.h>
#define DEBUG(args) {fprintf args ;}
#else
#define DEBUG(args) 
#endif
#endif

#define DEBUG0(s) DEBUG((stderr, "[%d(%d)]: "s,\
                              worldRankPSE,getpid()))

#define DEBUG1(s, arg1) DEBUG((stderr, "[%d(%d)]: "s,\
                              worldRankPSE,getpid(), arg1))

#define DEBUG2(s, arg1, arg2) DEBUG((stderr, "[%d(%d)]: "s,\
                              worldRankPSE,getpid(), arg1, arg2))

#define DEBUG3(s, arg1, arg2, arg3) DEBUG((stderr, "[%d(%d)]: "s,\
                              worldRankPSE,getpid(), arg1, arg2, arg3))

#define EXIT(s, arg) {char reason[200];sprintf(reason,"[%d]: "s,\
                              worldRankPSE, arg);\
                      PSE_SYexitall(reason,10);}
#define EXIT2(s, arg1, arg2) {char reason[200];sprintf(reason,"[%d]: "s,\
                              worldRankPSE, arg1, arg2);\
                      PSE_SYexitall(reason,10);}
#define EXIT3(s, arg1, arg2, arg3) {char reason[200];sprintf(reason,"[%d]: "s,\
                              worldRankPSE, arg1, arg2,arg3);\
                      PSE_SYexitall(reason,10);}

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/***************************************************************************
 * void      PSEinit(int NP, int Argc, char** Argv);
 *  
 *  PSEinit spawns and initializes a task group.
 *  All tasks in a PSE program has to call PSEinit at the beginning.
 *  If the calling task has no ParaStation parent task, the task declares
 *  itself as the master of a new task group and spawns NP-1 other tasks with
 *  Argv. The other tasks are spawned on the other nodes with the specified
 *  strategy (see below). Inside a task group, the system can apply specific
 *  scheduling strategies. 
 *  After PSEinit, other PSE functions can be used.
 *  To finialize the task group call PSEfinalize or PSEkillmachine.
 *
 *  Spawn strategy:
 *    The environment variable PSI_NODES (if set) declares the possible 
 *      nodes to be used for spawning. If it is not set, all ParaStation 
 *      nodes are used. (e.g. setenv PSI_NODES 4,6,3 )
 *    The environment variable PSI_NODES_SORT declares the strategy how
 *      the available nodes should be sorted before spawning. After sorting
 *      the nodes, new task are spawned in a round robin fashion in this
 *      sorted node list. (e.g. setenv PSI_NODES_SORT LOAD )
 *      Possible values are: 
 *      LOAD : nodes are sorted by their actual load
 *      NONE : no sorting is done.
 *    
 *  
 * PARAMETERS
 *         nRank: the rank of the task of which the task identifier 
 *                should be returned
 * RETURN  >0 task identifier
 *         -1 if  rank is invalid
 * SEE     
 *         PSEfinalize, PSEkillmachine, PSIspawn, PSEgetmyrank
 */
void PSEinit(int NP, int *rank);

void PSEspawn(int Argc, char** Argv,
	      int *masternode, int *masterport, int rank);

/***************************************************************************
 * void      PSEfinalize(void);
 *  
 *  PSEfinalize waits until all task in the task group have called PSEfinalize
 *  
 * PARAMETERS
 * RETURN
 */
void PSEfinalize(void);

void PSEabort(int nCode);

/***************************************************************************
 * void      PSEkillmachine(void);
 *  
 *  PSEkillmachine kills all other tasks in the own task group.
 *  
 * PARAMETERS
 * RETURN
 */
void PSEkillmachine(void);

/***************************************************************************
 * int       PSEgetmyrank();
 *  
 *  PSEgetmyrank  returns the rank of this task
 *  
 * PARAMETERS
 * RETURN  >=0 rank of the task
 *         -1  not in a task group
 */
int PSEgetmyrank(void);

/***************************************************************************
 * int       PSEgetsize();
 *  
 *  PSEgetsize  returns the number of tasks addressable with the PSE interface
 *  
 * PARAMETERS
 * RETURN  >0 number of tasks in task group
 *         -1 not in a task group
 */
int PSEgetsize(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSE_H */
