/*
 * Copyright (c) 1995 Regents of the University of Karlsruhe / Germany.
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
 *      @(#)PSI.h    1.00 (Karlsruhe) 03/11/97
 *
 *      ParaStation User Level Protocol
 *
 *      written by Joachim Blum
 *
 *
 * This is the key module for the ParaStationProtocol.
 * It manages the protocol independet protocol support functions
 *
 *  History
 *
 *   970311 Joe Creation: used psp.h and changed some things
 */
#ifndef __PSI_H
#define __PSI_H

#include <sys/types.h>

#include "psitask.h"

extern int PSI_msock;          /* master socket for connection to PSI daemon */
extern int PSI_mygroup;        /* the group id of the process */

extern short PSI_nrofnodes;    /* total number of nodes */
extern unsigned short PSI_myid;/* my node number */

extern long PSI_mytid;         /* The Task ID */
extern int PSI_mypid;          /* The Process ID */

extern int PSI_masternode;     /* number of my masternode (or -1) */
extern int PSI_masterport;     /* port of my master process */
extern int PSI_myrank;         /* rank inside my process-group */

extern char *PSI_hoststatus;

extern long PSI_options;

#define PSI_isoption(op) (PSI_options & op)
int PSI_setoption(long option,char value);

extern enum TaskOptions PSI_mychildoptions;

extern char * PSI_installdir;

/***************************************************************************
 *       PSI_clientinit()
 *
 *       MUST be call by every client process. It does all the necessary
 *       work to initialize the shm and contact the local daemon.
 */
int PSI_clientinit(unsigned short protocol);

/***************************************************************************
 *       PSI_clientexit()
 *
 *   reconfigs all variable so that a PSI_clientinit() will be successful
 */
int PSI_clientexit(void);

/****************************************
 *  PSI_gettid()
 *  returns the TID. This is necessary to have unique Task Identifiers in
 *  the cluster .The TID of a Task in the Cluster is a combination 
 *  of the Node number and the local pid on the node.
 *  If node=-1, the local node is used.
 */
long PSI_gettid(short node, pid_t pid);

/****************************************
 *  PSI_getnode()
 *  return the value of the node number of a TID. The TID of a Task in the 
 *  Cluster is a combination of the Node number and the local pid
 *  on the node.
 */
unsigned short PSI_getnode(long tid);

/****************************************
 *  PSI_getnrofnodes()
 * returns the number of nodes
 */
short PSI_getnrofnodes(void);

/****************************************
 *  PSI_getpid()
 *  returns the value of the local PID of the OS. The TID of a Task in the 
 *  Cluster is a combination of the Node number and the local pid
 *  on the node.
 */
pid_t PSI_getpid(long tid);

/***************************************************************************
 *       PSI_startdaemon()
 *
 *       starts the daemon via the inetd
 */
int PSI_startdaemon(unsigned long hostaddr);

/***************************************************************************
 *       PSI_mastersocket()
 */
int PSI_mastersocket(unsigned long hostaddr);

/*----------------------------------------------------------------------*/
/* 
 * PSI_notifydead()
 *  
 *  PSI_notifydead requests the signal sig, when the child with task
 *  identifier tid dies.
 *  
 * PARAMETERS
 *  tid: the task identifier of the child whose death shall be signaled to you
 *  sig: the signal which should be sent to you when the child dies
 * RETURN  0 on success
 *         -1 on error
 */
int PSI_notifydead(long tid, int sig);

/*----------------------------------------------------------------------*/
/*
 * PSI_release()
 *
 *  PSI_release() helps parent to survive if task is exiting. Quite usefull
 *       this in PSE_finalize().
 *
 * PARAMETERS
 *  tid: the task identifier of the task that should *not* kill the parent
 *
 * RETURN  0 on success
 *         -1 on error
 */
int PSI_release(long tid);

/*----------------------------------------------------------------------*/
/* 
 * PSI_whodied()
 *  
 *  PSI_whodied asks the ParaStation system which child's death caused the 
 *  last signal to be delivered to you.
 *  
 * PARAMETERS
 * RETURN  0 on success
 *         -1 on error
 */
long PSI_whodied(int sig);

/*----- Working area ---------------------------------------------------*/
/* 
 * PSI_getload()
 *  
 *  PSI_getload asks the ParaStation system of the load for a given node.
 *  
 * PARAMETERS
 *         nodenr the number of the node to be asked.
 * RETURN  the load of the given node
 *         -1 on error
 */
double PSI_getload(unsigned short nodenr);

/*
 * PSI_getNumberOfProcs(int node)
 *   Get number of running procs on node node
 *   Admin procs count as 0.01 procs :-)
 */
double PSI_getNumberOfProcs(int node);

char * PSI_LookupInstalldir(void);
void PSI_SetInstalldir(char *installdir);

#endif 
