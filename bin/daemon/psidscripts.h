/*
 * ParaStation
 *
 * Copyright (C) 2009-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Script-handling for the ParaStation daemon
 */
#ifndef __PSIDSCRIPTS_H
#define __PSIDSCRIPTS_H

#include <sys/types.h>

#include "config_parsing.h"

/** Information passed to script's callback function. */
typedef struct {
    int iofd;      /**< File-descriptor serving script's stdout/stderr. */
    void *info;    /**< Extra information to be passed to callback. */
} PSID_scriptCBInfo_t;

/**
 * @brief Script callback
 *
 * Callback used by @ref PSID_execScript() and @ref PSID_execFunc() to
 * handle the file-descriptor registered into the Selector. This
 * file-descriptor will deliver information concerning the result of
 * the script, i.e. the int return value of the system() call
 * executing the script.
 *
 * The first argument is the file-descriptor to handle. The second
 * argument will on the one hand contain the file-descriptor of the
 * stdout and stderr streams of the script. On the other hand some
 * optional pointer to extra information is included. This information
 * has to be provided to @ref PSID_execScript() or @ref
 * PSID_execFunc() as the last argument.
 *
 * The callback is responsible for cleaning up both file-descriptors
 * passed, i.e. the one passed as the first argument *and* the one
 * serving stdout/stderr within the @ref PSID_scriptCBInfo_t structure
 * the second argument is pointing to. Otherwise calling @ref
 * PSID_execScript() or @ref PSID_execFunc() with a callback
 * repeatedly might eat up the daemon's available file-descriptors.
 */
typedef int PSID_scriptCB_t(int, PSID_scriptCBInfo_t *);

/**
 * @brief Script environment preparation
 *
 * Function used to prepare the environment @ref PSID_execScript() and
 * @ref PSID_execFunc() are running in. The pointer argument might be
 * used to pass extra information to this function. This information
 * has to be provided to @ref PSID_execScript() or @ref
 * PSID_execFunc() as the last argument.
 */
typedef int PSID_scriptPrep_t(void *);

/**
 * @brief Execute a script and register for result.
 *
 * Execute a script defined by @a script and register the
 * callback-function @a cb to handle the output results
 * asynchronously. If no callback is passed, i.e. @a cb is NULL,
 * calling this function is blocking until the script has finished
 * execution. In this case some internal reporting via @ref PSID_log()
 * is used.
 *
 * Before executing @a script via @ref system(), the function @a prep
 * is called in order to setup some environment. If @a prep is NULL,
 * this step will be skipped. @a info might be used to pass extra
 * information to both, the callback-function @a cb within the @a info
 * field of the @ref PSID_scriptCBInfo_t argument and the
 * preparation-function @a prep.
 *
 * Calling this function without callback lets it act in a blocked
 * fashion. I.e. it will not return until @a script has finished
 * execution. In this case the script's exit-value is returned.
 *
 * @param script The script to be executed.
 *
 * @param prep Hook-function to setup the scripts environment.
 *
 * @param cb Callback to be registered within the Selector
 * facility. This will handle information passed back from the script.
 *
 * @param info Extra information to be passed to the environment setup
 * and callback functions.
 *
 * @return Upon failure -1 is returned. This function might fail due
 * to insufficient resources. If a callback @a cb is given, the pid of
 * the new process is returned upon success. Otherwise the exit-value
 * of @a script is returned.
 */
int PSID_execScript(char *script, PSID_scriptPrep_t prep, PSID_scriptCB_t cb,
		    void *info);

/**
 * @brief Register a script
 *
 * Register the script @a script to the configuration @a config acting
 * in the @a type role.
 *
 * @a type might have different values:
 *
 * - "startupscript" request the name of the script run during startup
 * of the daemon in order to test local features and stati.  This
 * information is used to decide if the local node is capable to take
 * part in the cluster-action.
 *
 * - "nodeupscript" request the name of the script called by the
 * master daemon whenever a node becomes active in the concert of
 * daemons within a cluster.
 *
 * - "nodedownscript" request the name of the script called by the
 * master daemon whenever a node disappears from the concert of
 * daemons within a cluster.
 *
 * Before registration @a script will be tested on existence and
 * x-flag. @a script might be an absolute or relative path. In the
 * latter case the script is searched relative to ParaStation's
 * installation-directory.
 *
 * @param config The configuration-structure used to store the script.
 *
 * @param type String describing the suggested role of the script.
 *
 * @param script Name of the script to register.
 *
 * @return If the script was registered successfully, 0 is
 * returned. Or -1 in case of failure.
 */
int PSID_registerScript(config_t *config, char *type, char *script);

/**
 * @brief Function to execute.
 *
 * Function to be executed via @ref PSID_execFunc(). The pointer
 * argument might be used to pass extra information to this
 * function. This information has to be provided to @ref
 * PSID_execFunc() as the last argument.
 */
typedef int PSID_scriptFunc_t(void *);


/**
 * @brief Execute a function and register for result.
 *
 * Execute the function @a func and register the callback-function @a
 * cb to handle the output results asynchronously. If no callback is
 * passed, i.e. @a cb is NULL, calling this function is blocking until
 * the function has finished execution. In this case some internal
 * reporting via @ref PSID_log() is used.
 *
 * Before actually calling @a func, the function @a prep is called in
 * order to setup some environment. If @a prep is NULL, this step will
 * be skipped. @a info might be used to pass extra information to
 * all, the callback-function @a cb within the @a info field of the
 * @ref PSID_scriptCBInfo_t argument, the preparation-function @a
 * prep and the actual function @a func.
 *
 * Calling this function without callback lets it act in a blocked
 * fashion. I.e. it will not return until @a func has finished
 * execution. In this case the function's exit-value is returned.
 *
 * @param func The function to be executed.
 *
 * @param prep Hook-function to setup the scripts environment.
 *
 * @param cb Callback to be registered within the Selector
 * facility. This will handle information passed back from the script.
 *
 * @param info Extra information to be passed to the environment setup
 * and callback functions.
 *
 * @return Upon failure -1 is returned. This function might fail due
 * to insufficient resources. If a callback @a cb is given, the pid of
 * the new process is returned upon success. Otherwise the
 * return-value of @a func is returned.
 */
int PSID_execFunc(PSID_scriptFunc_t func, PSID_scriptPrep_t prep,
		  PSID_scriptCB_t cb, void *info);

/**
 * @brief Cancel a registered callback
 *
 * Cancel the callback that was registered via @ref PSID_execScript()
 * or @ref PSID_execFunc() identified by the process ID @a pid
 * returned by the corresponding function.
 *
 * This might become necessary if the callback is located within a
 * module to be unloaded. If the callback is not cancelled before
 * unloading the module, a segmentation fault might be triggered as
 * soon as input is received on the corresponding file-descriptor and
 * the callback-function -- now located outside the valid address
 * range -- is called.
 *
 * @param pid The process ID to identify the callback to cancel.
 *
 * @return On success 0 is returned. Or -1 if an error occurred.
 */
int PSID_cancelCB(pid_t pid);

/**
 * @brief Set the number of callbacks to handle
 *
 * Set the number of callbacks the modules can handle to @a max. Since
 * registered callbacks are addressed by their file-descriptor, the
 * maximum has to be adapted each time RLIMIT_NOFILE is adapted.
 * Nevertheless, since old file-descriptor keep staying alive, only a
 * value of @a max larger than the previous maximum will have an
 * effect.
 *
 * The initial value is determined during the first registration of a
 * callback via sysconf(_SC_OPEN_MAX).
 *
 * @param max New maximum number of callbacks to handle
 *
 * @return On success 0 is returned. In case of failure -1 is returned
 * and errno is set appropriately.
 */
int PSIDscripts_setMax(int max);

#endif  /* __PSIDSCRIPTS_H */
