/*
 *               ParaStation3
 * pse.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: pse.h,v 1.9 2002/07/18 13:05:23 eicker Exp $
 *
 */
/**
 * @file
 * ParaStation Programming Environment
 *
 * $Id: pse.h,v 1.9 2002/07/18 13:05:23 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSE_H
#define __PSE_H

#include <pshwtypes.h>

/* time limit within which parent process must receive the global
   portid of each spawned child process                           */
#define PSE_TIME_OUT         2000000

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** @todo Documentation */

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
void PSE_init(int NP, int *rank);

/**
 * @brief Set hwType for spawn
 *
 * @param hwType
 */
void PSE_setHWType(unsigned int hwType);

/**
 * @brief PSEspawn
 *
 * @todo
 *
 * @param
 * @param
 * @param
 *
 * @return
 */
void PSE_spawn(int Argc, char** Argv,
	      int *masternode, int *masterport, int rank);

/**
 * @brief Finish the actual process.
 *
 * Finish the actual process without shutting down all other processes
 * within the process group.
 *
 * @return No return value.
 *
 * @see PSEabort()
 */
void PSE_finalize(void);

/**
 * @brief Finish the actual process and shut down the whole process group.
 *
 * Finish the actual process and shut down all other processes within
 * the process group. @a nCode is returned to the calling process,
 * which is usually the daemon (or the forwarder in ParaStation4 @todo).
 *
 * @return No return value.
 *
 * @see PSEabort()
 */
void PSE_abort(int nCode);

/**
 * @brief Get the rank of the process.
 *
 * @todo
 *
 * @return On success, the actual rank of the process within the group
 * is returned, or -1, if an error occurred.
 */
int PSE_getRank(void);

/**
 * @brief Get the size of the process group.
 *
 * @todo
 *
 * @return On success, the actual size of the process group is
 * returned, or -1, if an error occurred.
 */
int PSE_getSize(void);

/* For compatibility with old versions, to be removed soon */
#define PSEinit      PSE_init
#define PSEspawn     PSE_spawn
#define PSEfinalize  PSE_finalize
#define PSEabort     PSE_abort
#define PSEgetmyrank PSE_getRank
#define PSEgetsize   PSE_getSize

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSE_H */
