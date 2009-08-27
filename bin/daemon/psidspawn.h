/*
 *               ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2009 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * \file
 * Spawning of client processes and forwarding for the ParaStation daemon
 *
 * $Id$
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDSPAWN_H
#define __PSIDSPAWN_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Mark spawning task as deleted
 *
 * Mark tasks waiting to be spawned as deleted. This disables further
 * usage of these task-structures. Only tasks being spawned by a
 * parent-process located on node @a node are affected.
 *
 * The tasks will not be actually destroyed before calling @ref
 * cleanupSpawnTasks(). Nevertheless they will not be found by @ref
 * PStasklist_find().
 *
 * @return No return value
 */
void deleteSpawnTasks(PSnodes_ID_t node);

/**
 * @brief Cleanup spawning task marked as deleted
 *
 * Actually destroy task-structure waiting to be spawned but marked as
 * deleted. These tasks are expected to be marked via @ref
 * deleteSpawnTasks().
 *
 * @return No return value
 */
void cleanupSpawnTasks(void);

/**
 * @brief Initialize spawning stuff
 *
 * Initialize the spawning and forwarding framework. This registers
 * the necessary message handlers.
 *
 * @return No return value.
 */
void initSpawn(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDSPAWN_H */
