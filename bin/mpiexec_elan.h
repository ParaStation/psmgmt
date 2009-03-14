/*
 *               ParaStation
 *
 * Copyright (C) 2007-2009 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file mpiexec_elan.h Adds support for the Quadrics ELAN library libelan
 * in order to start applications build against their MPI in a ParaStation
 * cluster.
 *
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */


/**
 * @brief Load the libelan and init the
 * function pointer into it.
 *
 * @return Returns 1 on success and 0 on error.
 */
int initELAN(void);

/**
 * @brief Unload libelan.
 *
 * @return No return value.
 */
void closeELAN(void);

/**
 * @brief Prepare the elan environment.
 *
 * @param np The number of process to start over
 * libelan.
 *
 * @param verbose Set verbose mode, ouput whats going on.
 *
 * @return Returns 1 on success and 0 on error.
 */
int setupELANEnv(int np, int verbose);

/**
 * @brief Setup per rank environment
 *
 * Setup the rank-specific ELAN environment for the rank @a rank.
 *
 * This function returns a pointer to a statuc character-array. Upon
 * return it contains a environment of the form "name=value". It might
 * be used within a function registered via @ref
 * PSI_registerRankEnvFunc() in order to prepare a per rank
 * environement within a PSI_spawn*() function.
 *
 * @param rank Rank of the process the environment is prepared for.
 *
 * @return A pointer to a static character-array is returned. Thus the
 * value might get changed by subsequent calls this function.
 */
char * setupELANNodeEnv(int rank);
