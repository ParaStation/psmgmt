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

/* forward declaration */
typedef struct scriptData Script_Data_t;

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
 * @param script Script structure of finalized script
 */
typedef void Script_cbResult_t(int32_t exitStatus, Script_Data_t *script);

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
struct scriptData {
    char *username;	    /**< optional username for the script */
    uid_t uid;		    /**< optional user ID of the script */
    gid_t gid;		    /**< optional group ID of the script */
    strv_t argV;	    /**< argument vector */
    char *cwd;		    /**< script working directory */
    int grace;		    /**< grace time when script is killed */
    int runtime;	    /**< runtime limit in seconds */
    pid_t childPid;	    /**< PID of the running child */
    void *info;		    /**< additional info pass to callbacks */
    int iofd;		    /**< I/O channel between parent and script */
    char *outBuf;
    Forwarder_Data_t *fwdata;	    /**< pluginforwarder data used if started in
				      main psid */
    Script_cbResult_t *cbResult;    /**< see @ref Script_cbResult_t */
    Script_cbOutput_t *cbOutput;    /**< see @ref Script_cbOutput_t */
    Script_cbPrepPriv_t *prepPriv;  /**< see @ref Script_cbPrepPriv_t */
};

/**
 * @brief Create new script structure
 *
 * Allocate and initialize a new script structure.
 *
 * @param sPath Absolute path to the script
 *
 * @return Return an initialized script structure or NULL on error
 */
Script_Data_t *ScriptData_new(char *sPath);

/**
 * @brief Destroy a script structure
 *
 * Terminate a leftover script and free all used resources.  The username and
 * current working directory (cwd) of script structure will be freed using
 * @ref ufree().
 *
 * @param script Script structure to destroy
 */
void Script_destroy(Script_Data_t *script);

/**
 * @brief Execute a script
 *
 * Execute a script with given argument vector @ref argV. The script can be
 * executed under a different user by specifying @ref username, @ref uid and
 * @ref gid. If the change is requested by a non root user,
 * PSC_switchEffectiveUser() will try to reclaim privileges beforehand. The
 * working directory might be changed by setting @ref cwd. The ownership of cwd
 * and username is transferred to the pluginscript facility and are supposed to
 * be allocated using @a malloc(). A maximal @ref runtime and @ref grace
 * period can limit the time the script might execute.
 *
 * @param script Script to execute
 *
 * @return Returns the status of child from waitpid() on success
 * otherwise -1 is returned
 */
int Script_exec(Script_Data_t *script);

#endif  /* __PLUGIN_LIB_SCRIPT */
