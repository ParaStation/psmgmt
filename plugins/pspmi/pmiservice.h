/*
 * ParaStation
 *
 * Copyright (C) 2013 ParTec Cluster Competence Center GmbH, Munich
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
 *
 */

#ifndef __PS_PMI_SERVICE
#define __PS_PMI_SERVICE

/**
 * @brief Spawn a service process to start further compute processes.
 *
 * @param np The number of compute processes to spawn.
 *
 * @param c_argv Argument vector of the compute processes. The first argument is
 * the executable to start.
 *
 * @param c_argc The number of arguments.
 *
 * @param c_env A pointer to the environment vector to use.
 *
 * @param c_envc The number of elements in the environment vector.
 *
 * @param wdir The working directory of the new spawned processes. If NULL the
 * working directory of the spawning parent will be used.
 *
 * @param nType The type of node to execute the new processes on.
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
int spawnService(char *np, char **c_argv, int c_argc, char **c_env, int c_envc,
		    char *wdir, char *nType, int usize, int serviceRank,
		    char *kvsTmp);

#endif
