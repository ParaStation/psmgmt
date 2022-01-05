/*
 * ParaStation
 *
 * Copyright (C) 2009-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
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

#include <stdbool.h>
#include <sys/time.h>
#include <sys/types.h>

#include "config_parsing.h"

/**
 * @brief Script callback
 *
 * Callback used by @ref PSID_execScript() and @ref PSID_execFunc() to
 * handle the return-value and output of the script or function.
 *
 * The first argument is the returned int value of the executed
 * function or from the @ref system() function executing the script.
 *
 * The second argument flags that executing the script or function ran
 * into a timeout, thus, invalidating the first argument. The output
 * provided via the file-descriptor in the third argument might be
 * incomplete or spoiled by error messages due to the SIGKILL sent to
 * the corresponding process group before calling the callback.
 *
 * The third argument provides the file-descriptor of the stdout and
 * stderr streams of the script or function.
 *
 * The fourth argument points to the optional extra information that
 * can be provided to @ref PSID_execScript() or @ref PSID_execFunc()
 * as the last argument
 *
 * The callback is responsible for cleaning the file-descriptor
 * serving stdout/stderr provided as the third argument. Otherwise
 * calling @ref PSID_execScript(), or @ref PSID_execFunc() with a
 * callback repeatedly might eat up the daemon's available
 * file-descriptors.
 */
typedef void PSID_scriptCB_t(int, bool, int, void *);

/**
 * @brief Script environment preparation
 *
 * Function used to prepare the environment @ref PSID_execScript() and
 * @ref PSID_execFunc() are running in. The pointer argument might be
 * used to pass extra information to this function. This information
 * has to be provided to @ref PSID_execScript() or @ref
 * PSID_execFunc() as the last argument.
 */
typedef void PSID_scriptPrep_t(void *);

/**
 * @brief Execute a script and register for result
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
 * information to both, the callback-function @a cb and the
 * preparation-function @a prep.
 *
 * If a @a timeout is provided, the allowed time for @a script to
 * execute is limited to the committed time. When the timeout expires
 * the process group of the controlling process, i.e. the script
 * process itself and the controlling process are killed with SIGKILL.
 * After this the callback @a cb is called with the @ref timedOut flag
 * set.
 *
 * Calling this function without callback lets it act in a blocked
 * fashion. I.e. it will not return until @a script has finished
 * execution. The timeout functionality is not available! In this case
 * the script's exit-value is returned.
 *
 * @param script The script to be executed
 *
 * @param prep Hook-function to setup the scripts environment
 *
 * @param cb Callback to be registered within the Selector
 * facility; this will handle information passed back from the script
 *
 * @param timeout Amount of time committed to execute @a script
 *
 * @param info Extra information to be passed to the environment setup
 * and callback functions
 *
 * @return Upon failure -1 is returned. This function might fail due
 * to insufficient resources. If a callback @a cb is provided, the pid
 * of the observer process executing @ref system() is returned upon
 * success. It might be used to cancel the callback via @ref
 * PSID_cancelCB(). Otherwise the exit-value of @a script is returned.
 */
int PSID_execScript(char *script, PSID_scriptPrep_t prep, PSID_scriptCB_t cb,
		    struct timeval *timeout, void *info);

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
 * @param config The configuration-structure used to store the script
 *
 * @param type String describing the suggested role of the script
 *
 * @param script Name of the script to register
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
 * @brief Execute a function and register for result
 *
 * Execute the function @a func and register the callback-function @a
 * cb to handle the output results asynchronously. If no callback is
 * passed, i.e. @a cb is NULL, calling this function is blocking until
 * the function has finished execution. In this case some internal
 * reporting via @ref PSID_log() is used.
 *
 * Before actually calling @a func, the function @a prep is called in
 * order to setup some environment. If @a prep is NULL, this step will
 * be skipped. @a info might be used to pass extra information to all,
 * the callback-function @a cb, the preparation-function @a prep, and
 * the actual function @a func.
 *
 * If a @a timeout is provided, the allowed time for @a func to
 * execute is limited to the committed time. When the timeout expires
 * the process executing the function is killed with SIGKILL. After
 * this the callback @a cb is called with the @ref timedOut flag set.
 *
 * Calling this function without callback lets it act in a blocked
 * fashion. I.e. it will not return until @a func has finished
 * execution. The timeout functionality is not available! In this case
 * the function's exit-value is returned.
 *
 * @param func The function to be executed
 *
 * @param prep Hook-function to setup the scripts environment
 *
 * @param cb Callback to be registered within the Selector
 * facility; this will handle information passed back from the function
 *
 * @param timeout Amount of time committed to execute @a func
 *
 * @param info Extra information to be passed to the environment setup
 * and callback functions
 *
 * @return Upon failure -1 is returned. This function might fail due
 * to insufficient resources. If a callback @a cb is provided, the pid
 * of the new process is returned upon success. It might be used to
 * cancel the callback via @ref PSID_cancelCB(). Otherwise the
 * return-value of @a func is returned.
 */
int PSID_execFunc(PSID_scriptFunc_t func, PSID_scriptPrep_t prep,
		  PSID_scriptCB_t cb, struct timeval *timeout, void *info);

/**
 * @brief Cancel a registered callback
 *
 * Cancel the callback that was registered via @ref PSID_execScript()
 * or @ref PSID_execFunc() identified by the process ID @a pid
 * returned by the corresponding function.
 *
 * This might become necessary if the callback is located within a
 * module to be unloaded. If the callback is not canceled before
 * unloading the module, a segmentation fault might be triggered as
 * soon as input is received on the corresponding file-descriptor and
 * the callback-function -- now located outside the valid address
 * range -- is called.
 *
 * @param pid Process ID to identify the callback to cancel
 *
 * @return On success 0 is returned; or -1 if an error occurred
 */
int PSID_cancelCB(pid_t pid);

/**
 * @brief Initialize script handling
 *
 * Initialize the asynchronous script / function handling
 * framework. This includes setting up the pool for callback info
 * blobs and its corresponding helpers.
 *
 * @return On success true is returned; or false in case of error
 */
bool PSIDscripts_init(void);

/**
 * @brief Print statistics
 *
 * Print statistics concerning the usage of internal callback info blobs.
 *
 * @return No return value
 */
void PSIDscripts_printStat(void);

#endif  /* __PSIDSCRIPTS_H */
