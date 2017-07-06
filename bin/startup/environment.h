/*
 * ParaStation
 *
 * Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Handle evironment setup  of mpiexec et al.
 */
#ifndef __ENVIRONMENT_H
#define __ENVIRONMENT_H

#include "cloptions.h"

/**
 * @doctodo
 */
/**
 * @brief Set up the environment forwarding mechanism
 *
 * Set up the environment to control forwarding of environment
 * variables. All information required to setup the environment is
 * expected either in the configuration @a conf parsed from the
 * command-line arguments or in the environment itself.
 *
 * @param conf Configuration as identified from command-line options
 *
 * @return No return value
 */
void setupEnvironment(Conf_t *conf);

#endif /* __ENVIRONMENT_H */
