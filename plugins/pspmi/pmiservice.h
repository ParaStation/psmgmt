/*
 * ParaStation
 *
 * Copyright (C) 2013-2015 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 * Stephan Krempel <krempel@par-tec.com>
 *
 */

#ifndef __PS_PMI_SERVICE
#define __PS_PMI_SERVICE

/**
 * @brief Spawn a service process to start further compute processes.
 *
 * Enable to spawn processes using multiple executables (as defined by
 * MPI_Comm_spawn_multiple). All arrays passed have to match each other
 * in a way that the same index in each array belongs to the same executable.
 *
 * @param np The total number of compute processes to spawn.
 *
 * @param nps Array of number of compute processes to spawn.
 *
 * @param c_argvs Array of argument vectors of the compute processes.
 *               The first argument in each array is the executable
 *               for this index.
 *
 * @param c_argcs Array of the number of arguments.
 *
 * @param c_envvs Array of pointers to the environment vectors to use.
 *              Attention: Due to internal limitations, currently only the
 *                         first vector (with index 0) is used for all
 *                         processes to be spawned.
 *
 * @param c_envcs Array of the numbers of elements in the environment vectors.
 *
 * @param wdirs Array of working directories of the spawned processes.
 *              If NULL, the working directory of the spawning parent
 *              will be used.
 *
 * @param tpps Array of threads per process of the spawned processes.
 *
 * @param nTypes Array of the type of node to execute the new processes on.
 *
 * @param paths Array of paths were the executable should be searched.
 *
 * @param ts The length of each of the arrays above.
 *
 * @param usize The universe size of the current job.
 *
 * @param serviceRank The rank of the spawned service process to use. This
 * information should be requested from the logger.
 *
 * @param kvsTmp The KVS mask to use. Must be a uniq string in the current job.
 *
 * @return Returns 1 on success or 0 on error.
 */
int spawnService(int np, char *nps[], char **c_argvs[], int c_argcs[],
		 char **c_envvs[], int c_envcs[], char *wdirs[], char *tpps[],
		 char *nTypes[], char *paths[], int ts, int usize,
		 int serviceRank, char *kvsTmp);

#endif
