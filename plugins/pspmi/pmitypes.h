/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_PMI_TYPES
#define __PS_PMI_TYPES

#include "pstask.h"

/** Connection type of pmi */
typedef enum {
    PMI_DISABLED = 0,
    PMI_OVER_TCP,
    PMI_OVER_UNIX,
    PMI_FAILED,
} PMItype_t;

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
    int pmienvc;           /**< number of pmi environment variables */
    KVP_t *pmienvv;        /**< array of pmi environment variables */
} SpawnRequest_t;

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

/**
 * @brief Set task preparation function
 *
 * Register the new task preparation function @a spawnFunc to the
 * pspmi module. This function will be used within future PMI spawn
 * requests in order to prepare the task structure of the helper tasks
 * used to realize the actual spawn action.
 *
 * @param spawnFunc New preparation function to use
 *
 * @return No return value
 */
typedef void(psPmiSetFillSpawnTaskFunction_t)(fillerFunc_t spawnFunc);

/**
 * @brief Reset task preparation function
 *
 * Reset pspmi's task preparation function to the default one
 * utilizing mpiexec as a helper.
 *
 * @return No return value
 */
typedef void(psPmiResetFillSpawnTaskFunction_t)(void);

#endif  /* __PS_PMI_TYPES */
