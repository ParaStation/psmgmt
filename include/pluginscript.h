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

#include "pluginforwarder.h"

#include "psenv.h"
#include "psstrv.h"

/**
 * @brief Callback reporting the result of an executed script. This is
 * only called if the script is started by @ref Script_exec() from
 * right inside the main daemon. In this case the script will be
 * executed from within a pluginforwarder and this callback will be
 * triggered once the pluginforwarder has reported the script's exit
 * status.
 *
 * This prevents the script from blocking the main psid.
 *
 * @param exitStatus Scripts exit status of the script
 *
 * @param info Pointer to script's info field
 */
typedef void Script_cbResult_t(int32_t exitStatus, void *info);

/**
 * @brief Callback which is invoked for every output line the script
 * produces. Lines without a terminating newline will be cached.
 *
 * @param line Output line from script's stdout/stderr
 *
 * @param info Pointer to script's info field
 */
typedef void Script_cbOutput_t(char *line, void *info);

/**
 * @brief Callback to prepare the script's environment before
 * privileges are dropped.
 *
 * @param info Pointer to script's info field
 */
typedef void Script_cbPrepPriv_t(void *);

/** Structure defining all parameter's of a script */
typedef struct {
    char *username;	    /**< optional username for the script */
    uid_t uid;		    /**< optional user ID of the script */
    gid_t gid;		    /**< optional group ID of the script */
    strv_t argV;	    /**< argument vector */
    char *cwd;		    /**< script working directory */
    int grace;		    /**< grace time when script is killed */
    int runtime;	    /**< runtime limit in seconds */
    pid_t childPid;	    /**< PID of the running child */
    void *info;		    /**< additional info pass to callbacks */
    int iofds[2];	    /**< I/O channel between parent and script */
    char *outBuf;
    Forwarder_Data_t *fwdata;	    /**< pluginforwarder data used if started in
				      main psid */
    Script_cbResult_t *cbResult;    /**< see @ref Script_cbResult_t */
    Script_cbOutput_t *cbOutput;    /**< see @ref Script_cbOutput_t */
    Script_cbPrepPriv_t *prepPriv;  /**< see @ref Script_cbPrepPriv_t */
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
