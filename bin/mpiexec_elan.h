/*
 *               ParaStation
 *
 * Copyright (C) 2007-2008 ParTec Cluster Competence Center GmbH, Munich
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
 * @brief Setup the environment for the given rank.
 *
 * @param rank The rank of the process to setup.
 *
 * @return Returns 1 on success and 0 on error.
 */
int setupELANProcsEnv(int rank);
