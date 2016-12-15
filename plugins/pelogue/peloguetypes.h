/*
 * ParaStation
 *
 * Copyright (C) 2015-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PELOGUE__TYPES
#define __PELOGUE__TYPES

#include <stdbool.h>
#include <time.h>

#include "list.h"
#include "psnodes.h"
#include "pluginenv.h"
#include "pluginforwarder.h"

/** Types of pelogues currently handled */
typedef enum {
    PELOGUE_PROLOGUE = 1,  /**< prologue */
    PELOGUE_EPILOGUE,      /**< epilogue */
} PElogueType_t;

/** All information available on a running pelogue */
typedef struct {
    list_t next;        /**< used to put into list */
    char *plugin;       /**< name of the registering plugin */
    char *jobid;        /**< batch system's job ID */
    PElogueType_t type; /**< Type of pelogue to run */
    PSnodes_ID_t mainPElogue; /**< Node initiating the pelogue */
    char *scriptDir;    /**< directory containing all the scripts */
    char *tmpDir;       /**< Name of the pelogue's temp directory if any */
    char *rootHome;     /**< root's HOME; don't free() this!*/
    char *hostName;     /**< local hostname; don't free() this! */
    int32_t timeout;    /**< pelogue's timeout told to the forwarder */
    int32_t rounds;     /**< number of rounds for the pelogue (root / user) */
    env_t env;          /**< environment provided to the pelogue */
    uid_t uid;          /**< user ID under which a pelogue is exectued */
    gid_t gid;          /**< group ID under which a pelogue is exectued */
    time_t startTime;   /**< Job's pelogue starttime identifies answers */
    Forwarder_Data_t *fwData; /**< description of the forwarder to use */
    char **argv;        /**< argument vector of the pelogue actually started */
    int32_t signalFlag; /**< flag if any signal was sent to the pelogue */
    int32_t exit;       /**< pelogue's exit value */
} PElogueChild_t;


/** Various state a pelogue might be in */
typedef enum {
    PELOGUE_PENDING = 1,  /**< pelogue is started, no result yet */
    PELOGUE_DONE,         /**< pelogue finished succesfully */
    PELOGUE_FAILED,       /**< pelogue finished but failed */
    PELOGUE_TIMEDOUT,     /**< pelogue did not finish in time */
} PElogueState_t;

/** Collection of pelogue results associated to a job */
typedef struct {
    PSnodes_ID_t id;         /**< node this pelogues were running on */
    PElogueState_t prologue; /**< result of the corresponding prologue */
    PElogueState_t epilogue; /**< result of the corresponding epilogue */
} PElogueResList_t;

/**
 * @brief @doctodo
 *
 * @return No return value
 */
typedef void Pelogue_JobCb_Func_t (char *, int, int, PElogueResList_t *);

#endif  /* __PELOGUE__TYPES */
