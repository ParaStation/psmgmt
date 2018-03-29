/*
 * ParaStation
 *
 * Copyright (C) 2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2018 Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * Functions for handling the various informations about the nodes
 * with a ParaStation cluster
 */
#ifndef __PSNODES_H
#define __PSNODES_H

#include <stdint.h>
#include <sys/types.h>

/** Type to store unique node IDs in. This enables us to have 32168 nodes. */
typedef int16_t PSnodes_ID_t;

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
