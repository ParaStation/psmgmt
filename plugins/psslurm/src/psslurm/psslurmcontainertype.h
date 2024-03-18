/*
 * ParaStation
 *
 * Copyright (C) 2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_CONTAINER_TYPE
#define __PS_SLURM_CONTAINER_TYPE

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#include "pluginjson.h"
#include "pstask.h"

/** holding container runtime commands */
typedef struct {
    char *query;	/**< runtime cmd to query a container */
    char *create;	/**< currently unsupported */
    char *start;	/**< currently unsupported */
    char *kill;		/**< runtime cmd to kill (stop) a container */
    char *delete;	/**< runtime cmd to delete a container */
    char *run;		/**< runtime cmd to create and start a container */
    char *envExclude;	/**< currently unsupported */
} Container_CMDs_t;

/** holding a container definition */
typedef struct {
    char *bundle;	    /**< path to OCI bundle */
    char *rootfs;	    /**< container root filesystem */
    char *spoolDir;	    /**< user specific spool directory */
    char *spoolJobDir;	    /**< job specific spool directory */
    psjson_t configObj;	    /**< json configuration */
    Container_CMDs_t cmds;  /**< runtime commands */
    uint32_t jobid;	    /**< unique job identifier */
    uint32_t stepid;	    /**< unique step identifier */
    char *username;	    /**< username of job owner */
    uid_t uid;		    /**< user ID */
    gid_t gid;		    /**< group ID */
    int32_t rank;	    /**< task rank */
    pid_t rankPID;	    /**< task PID */
} Slurm_Container_t;

#endif  /* __PS_SLURM_CONTAINER_TYPE */
