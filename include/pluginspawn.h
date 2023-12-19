/*
 * ParaStation
 *
 * Copyright (C) 2015 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2023-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PLUGIN_LIB_SPAWN
#define __PLUGIN_LIB_SPAWN

#include <stdbool.h>

#include "psenv.h"
#include "pstask.h"

/** Simple key-value pair */
typedef struct {
    char *key;
    char *value;
} KVP_t;

/** Structure holding all information on a single spawn */
typedef struct {
    int np;         /**< number of processes */
    int argc;       /**< number of arguments */
    char **argv;    /**< array of arguments */
    int preputc;    /**< number of preput values */
    KVP_t *preputv; /**< array of preput key-value-pairs */
    int infoc;      /**< number of info values */
    KVP_t *infov;   /**< array of info key-value-pairs */
} SingleSpawn_t;

/**
 * Structure holding information on a complex spawn consisting of
 * multiple single spawns.
 */
typedef struct {
    int num;               /**< number of single spawns */
    SingleSpawn_t *spawns; /**< array of single spawns */
    env_t env;             /**< environment variables */
    void *data;            /**< custom data pointer */
} SpawnRequest_t;

/**
 * @brief Create an empty spawn request
 *
 * @param num Number of single spawns the request is capable to store
 *
 * @return SpawnRequest_t structure, NULL on error with errno set
 */
SpawnRequest_t *initSpawnRequest(int num);

/**
 * @brief Copy spawn request and all its contents
 *
 * The @a data pointer is just copied, so the pointer of the copied struct
 * will point to the same memory as that in the original struct.
 *
 * @param req Spawn request to copy
 *
 * @return Returns a duplicate of the spawn request
 */
SpawnRequest_t *copySpawnRequest(SpawnRequest_t *req);

/**
 * @brief Free spawn request
 *
 * The @a data pointer is not freed, the caller needs to take care of doing
 * that in the right way.
 *
 * @param req Spawn request to free
 */
void freeSpawnRequest(SpawnRequest_t *req);

/**
 * @brief Create and initialize new task for the spawnee
 *
 * This creates a new tast and copies all relevant values
 * from the spawners task to it.
 *
 * @param spawner   task of the process that initialized the spawn
 *
 * @param filter    function to filter out environment variables
 *		    needs to return true for all environment variables that
 *		    shall be included into the spawnee's environment
 *
 *  @returns Returns the spawnee's new task or NULL if PStask_new() failed
 */
PStask_t* initSpawnTask(PStask_t *spawner, bool filter(const char*));

/**
 * Prepare task structure for actual spawn
 *
 * Prepare the task structure @a task used in order to start a helper
 * task that will realize the actual spawn of processes as requested
 * in @a req. @a usize determines the universe size of the new PMI job
 * to be created.
 *
 * @param req Information on the PMI spawn to be realized
 *
 * @param usize Universe size of the new PMI job to be created
 *
 * @param task Task strucuture to be prepared
 *
 * @return Return 1 on succes, 0 if an error occurred or -1 if not
 * feeling responsible for this distinct spawn. In the latter case
 * pspmi's default filler function will be called instead.
 */
typedef int (fillerFunc_t)(SpawnRequest_t *req, int usize, PStask_t *task);

#endif  /* __PLUGIN_LIB_SPAWN */
