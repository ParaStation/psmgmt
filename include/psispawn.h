#ifndef __PSISPAWN_H__
#define __PSISPAWN_H__

/*----------------------------------------------------------------------*/
/*
 * PSIspawn()
 *
 *  creates a new process on ParaStation node DSTNODE.
 *  The WORKINGDIR can either be absolute or realativ to the actual
 *  working directory of the spawning process.
 *  ARGC is the number of arguments and ARGV are the arguments as known
 *  from main(argc,argv)
 *  ERROR returns an errorcode if the spawning failed.
 *  RETURN -1 on failure
 *         TID of the new process on success
 */
long PSI_spawn(short dstnode, char *workingdir, int argc, char **argv,
	       int masternode, int masterport, int rank, int *error);

/*----------------------------------------------------------------------*/
/*
 * PSIspawnM()
 *
 *  creates count new processes on ParaStation node DSTNODES[].
 *  The WORKINGDIR can either be absolute or realativ to the actual
 *  working directory of the spawning process.
 *  ARGC is the number of arguments and ARGV are the arguments as known
 *  from main(argc,argv)
 *  ERROR[] returns an errorcode if the spawning failed.
 *  TIDS[]  returns the tids of the new processes on success
 *  RETURN -1 on failure
 *         >0 nr of processes on success
 */
int PSI_spawnM(int count, short* dstnodes, char *workingdir, int argc,
	       char **argv, int masternode, int masterport, int rank,
	       int *errors, long *tids);

/*----------------------------------------------------------------------*/
/*
 * PSIisalive(long tid)
 *
 *  checks if a task is still running
 *
 *  RETURN 0 if the task is not alive
 *         1 if the task is alive
 */
int PSI_isalive(long tid);

/*------------------------------------------------------------
 * PSI_LSF()
 *
 * Check for the presence of LSF-Parallel. And if present, modify
 * ENV_NODE_HOSTS
 */
void PSI_LSF(void);

/*------------------------------------------------------------
 * PSI_RemoteArgs(int Argc,char **Argv,int &RArgc,char ***RArgv)
 *
 * Modify Args of remote tasks.
 */
void PSI_RemoteArgs(int Argc,char **Argv,int *RArgc,char ***RArgv);

/*------------------------------------------------------------
 * PSIGetPartition()
 *
 * Set the available node numbers in an array.
 * and sorts this array due to the enrivonment variable
 * If no partition is given, the whole cluster is used
 * If no sorting algorithm is given the parition is used unsorted
 *
 * RETURN:  the number of nodes in the partition or -1 on error.
 */
short PSI_getPartition(void);

/*
 * PSI_do_spawn()
 *
 *  creates COUNT new processes on ParaStation node DSTNODES[].
 *  The WORKINGDIR can either be absolute or realativ to the actual
 *  working directory of the spawning process.
 *  ARGC is the number of arguments and ARGV are the arguments as known
 *  from main(argc,argv)
 *  ERROR[] returns an errorcode if the spawning failed.
 *  TIDS[] returns an tids if the spawning is successful.
 *  RETURN -1 on failure
 *         nr of processes spawned on success
 */
int PSI_dospawn(int count, short *dstnodes, char *workingdir, int argc,
		char **argv, int masternode, int masterport, int firstrank,
		int *errors, long* tids);

/*
 * PSI_kill()
 *
 *  kill a task on any node of the cluster.
 *  TID is the task identifier of the task, which shall receive the signal
 *  SIGNAL is the signal to be sent to the task
 */
int PSI_kill(long tid, short signal);

#endif
