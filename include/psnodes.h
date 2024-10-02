/*
 * ParaStation
 *
 * Copyright (C) 2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Functions for handling the various informations about the
 * nodes with a ParaStation cluster
 */
#ifndef __PSNODES_H
#define __PSNODES_H

#include <stdint.h>

/**
 * Type to store unique node IDs in. This enables us to have > 2G nodes.
 *
 * There are some (negative) node IDs having a special meaning:
 *
 * - -1 denotes the local node. Especially in @ref PSC_getTID -1 is
 *   replaced by the actual local node ID.
 *
 * - -2 Marks a task ID to refer to an obsolete (local) task, i.e. a
 *    task structure not stored in the list @ref managedTasks but in
 *    @ref obsoleteTasks
 */
typedef int32_t PSnodes_ID_t;

/** Pseudo user ID to allow any user to run on a specific node */
#define PSNODES_ANYUSER (uid_t) -1

/** Pseudo user ID to allow any group to run on a specific node */
#define PSNODES_ANYGROUP (gid_t) -1

/** Pseudo number of processes to allow any job to run on a specific node */
#define PSNODES_ANYPROC -1

/** Possible modes for overbooking (per node) */
typedef enum {
    OVERBOOK_FALSE,   /**< No overbooking at all */
    OVERBOOK_TRUE,    /**< Complete overbooking */
    OVERBOOK_AUTO,    /**< Overbooking on user request */
} PSnodes_overbook_t;

#endif  /* __PSNODES_H */
