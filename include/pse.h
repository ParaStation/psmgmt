/*
 * Copyright (c) 1998 Regents of the University of Karlsruhe / Germany.
 * All rights reserved.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      @(#)pseinit.h    1.00 (Karlsruhe) 01/01/98
 *
 *      written by  Joachim Blum (blum@ira.uka.de)
 *                  Patrick Ohly (ohly@ira.uka.de)
 *      changed by  Farzad Safa (safa@planNET.de)
 *
 *
 * This is header file for the ParaStation Environment interface
 * It enables the programmer to use the ParaStation System similar
 * to MPI, where processes are addressed by ranks. 
 *
 * The rank to port translation has still to be done by hand, but
 * probably in the next version we will include communication calls
 * which directly use the rank instead of PSI-ports.
 * For now, please use PSI functions to communicate.
 */
#ifndef __PSE_H
#define __PSE_H

#include <psport.h>

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

#define DEBUG0(s) DEBUG((stderr, "[%d(%d)]: " s,\
                              s_nPSE_MyWorldRank,getpid()))

#define DEBUG1(s, arg1) DEBUG((stderr, "[%d(%d)]: " s,\
                              s_nPSE_MyWorldRank,getpid(), arg1))

#define DEBUG2(s, arg1, arg2) DEBUG((stderr, "[%d(%d)]: " s,\
                              s_nPSE_MyWorldRank,getpid(), arg1, arg2))

#define DEBUG3(s, arg1, arg2, arg3) DEBUG((stderr, "[%d(%d)]: " s,\
                              s_nPSE_MyWorldRank,getpid(), arg1, arg2, arg3))

#define EXIT(s, arg) {char reason[200];sprintf(reason, "[%d]: " s, s_nPSE_MyWorldRank, arg);\
                      PSE_SYexitall(reason,10);}
#define EXIT2(s, arg1, arg2) {char reason[200];sprintf(reason, "[%d]: " s, s_nPSE_MyWorldRank,\
                                      arg1, arg2);\
                      PSE_SYexitall(reason,10);}
#define EXIT3(s, arg1, arg2, arg3) {char reason[200];sprintf(reason, "[%d]: " s, s_nPSE_MyWorldRank,\
                                      arg1, arg2,arg3);\
                      PSE_SYexitall(reason,10);}

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PSE_Identity {
    int node;                  /**< PSHAL node id */
    int port;                  /**< PSPORT port number */
} PSE_Identity_t;

typedef struct PSE_Info {
    PSE_Identity_t myid;       /**< map[mygrank] for faster access */
    int mygrank;               /**< MPI global rank */
} PSE_Info_t;

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
void PSEinit(int NP, int Argc, char** Argv);

/***************************************************************************
 * void      PSEfinalize(void);
 *  
 *  PSEfinalize waits until all task in the task group have called PSEfinalize
 *  
 * PARAMETERS
 * RETURN
 */
void PSEfinalize(void);

void PSE_Abort(int nCode);

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

/***************************************************************************
 * int       PSEgetnodeno();
 *
 *  PSEgetnodeno  returns the number of the current node
 *
 * PARAMETERS
 * RETURN  node number
 */
int PSEgetnodeno(void);

/***************************************************************************
 * int       PSEgetportno();
 *
 *  PSEgetportno  returns the number of the current port
 *
 * PARAMETERS
 * RETURN  port number
 */
int PSEgetportno(void);

/***************************************************************************
 * PSPORT_t  PSEgetport(int nRank);
 *  
 *  PSEgetport  returns the global port identifier for a specific rank. 
 *  With this port identifier PSI functions such as PSIsend() can be called.
 *  
 * PARAMETERS
 *         nRank: the rank of the task of which the task identifier 
 *                should be returned
 * RETURN  >0 port identifier
 *         -1 if  rank is invalid
 */
PSP_PortH_t PSEgetport(void);

PSE_Identity_t * PSEgetmap(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif
