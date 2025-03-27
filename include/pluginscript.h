/*
 * ParaStation
 *
 * Copyright (C) 2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PLUGIN_LIB_SCRIPT
#define __PLUGIN_LIB_SCRIPT

#include <stdbool.h>
#include <sys/types.h>

#include "psenv.h"
#include "psstrv.h"

/** Structure defining all parameter's of a script */
typedef struct {
    char *username;	    /**< optional username for the script */
    uid_t uid;		    /**< optional user ID of the script */
    gid_t gid;		    /**< optional group ID of the script */
    strv_t argV;	    /**< argument vector */
    char *cwd;		    /**< script working directory */
    bool reclaimPriv;	    /**< reclaim root privileges before execution */
    int grace;		    /**< grace time when script is killed */
    int runtime;	    /**< runtime limit in seconds */
    pid_t childPid;	    /**< PID of the running child */
    void *info;		    /**< additional info pass to callbacks */
    int iofds[2];	    /**< I/O channel between parent and script */
    int (*cbOutput)(char *output, void *info);
			    /**< callback for each output line of the script */
    void (*prepPriv)(void *info);
			    /**< prepare script environment before user switch */
    void (*prepUser)(void *info);
			    /**< prepare script environment after user switch */
} Script_Data_t;

/**
 * @brief Create new script structure
 *
 * Allocate and initialize a new script structure. The username and current
 * working directory (cwd) of script structure will be freed using
 * @ref ufree().
 *
 * @param sPath Absolute path to the script
 *
 * @return Return an initialized script structure or NULL on error
 */
Script_Data_t *ScriptData_new(char *sPath);

/**
 * @brief Destroy a script structure
 *
 * Terminate a leftover script and free all used resources
 *
 * @param script Script structure to destroy
 */
void Script_destroy(Script_Data_t *script);

/**
 * @brief Execute a script
 *
 * @param script Script to execute
 *
 * @return Returns the status of child from waitpid() on success
 * otherwise -1 is returned
 */
int Script_exec(Script_Data_t *script);

#endif  /* __PLUGIN_LIB_SCRIPT */
