/*
 * ParaStation
 *
 * Copyright (C) 2015 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_PMI_CLIENT_SPAWN
#define __PS_PMI_CLIENT_SPAWN

typedef struct {
    char *key;
    char *value;
} KVP_t;

typedef struct {
    int np;         /**< number of processes */
    int argc;       /**< number of arguments */
    char **argv;    /**< array of arguments */
    int preputc;    /**< number of preput values */
    KVP_t *preputv; /**< array of preput key-value-pairs */
    int infoc;      /**< number of info values */
    KVP_t *infov;   /**< array of info key-value-pairs */
} SingleSpawn_t;

typedef struct {
    int num;               /**< number of single spawns */
    SingleSpawn_t *spawns; /**< array of single spawns */
    int pmienvc;           /**< number of pmi environment variables */
    KVP_t *pmienvv;        /**< array of pmi environment variables */
} SpawnRequest_t;

/**
 * @brief Initialize empty spawn request
 *
 * @param num Number of single spawns to have space in the request
 *
 * @return SpawnRequest_t structure, NULL on error with errno set
 */
SpawnRequest_t *initSpawnRequest(int num);

/**
 * @brief Copy spawn request and all its contents
 *
 * @param req Spawn request to copy
 *
 * @return Returns a duplicate of the spawn request
 */
SpawnRequest_t *copySpawnRequest(SpawnRequest_t *req);

/**
 * @brief Free spawn request
 *
 * @param req Spawn request to free
 */
void freeSpawnRequest(SpawnRequest_t *req);

#endif  /* __PS_PMI_CLIENT_SPAWN */
