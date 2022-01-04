/*
 * ParaStation
 *
 * Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Common funtions for mpiexec et al.
 */
#ifndef __STARTUP_COMMON_H
#define __STARTUP_COMMON_H

#include <stdbool.h>

#include "psnodes.h"
#include "cloptions.h"

/**
 * @brief Setup the environment forwarding mechanism
 *
 * Setup the environment to control forwarding of environment
 * variables. All information required to setup the environment is
 * expected either in the configuration @a conf parsed from the
 * command-line arguments or in the environment itself.
 *
 * @param conf Configuration as identified from command-line options
 *
 * @return No return value
 */
void setupEnvironment(Conf_t *conf);

/**
 * @brief Search slot-list by index
 *
 * Search the list of slots and return the nodeID indicated by
 * @a index. Successive identical nodeIDs will be skipped.
 *
 * Be aware of the fact that the list of slots forming the partition
 * is not identical to the list of nodes in the sense of nodes used by
 * specific ranks. In fact, the slots are a form of a compactified
 * list of nodes, i.e. there is just one slot for each physical node.
 *
 * @param conf Configuration as identified from command-line options
 *
 * @param index The index to find in the slot-list
 *
 * @return Returns the requested nodeID or the last nodeID in the
 * slot-list for invalid indexes. In case the slot-list is not
 * available, -1 is returned and thus the local node is adressed.
 */
PSnodes_ID_t getIDbyIdx(Conf_t *conf, int index);

/**
 * @brief Setup signal handlers
 *
 * Setup signal handlers and make them more or less verbose via the
 * flag @a verbose. For the time being only SIGTERM will be handled.
 *
 * @param verbose Flag signal handlers verbosity
 *
 * @return No return value
 */
void setupSighandler(bool verbose);

#endif /* __STARTUP_COMMON_H */
