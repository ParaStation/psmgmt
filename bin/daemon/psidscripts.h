/*
 *               ParaStation
 *
 * Copyright (C) 2009 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * \file
 * Script-handling for the ParaStation daemon
 *
 * $Id$
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDSCRIPTS_H
#define __PSIDSCRIPTS_H

#include <stdio.h>
#include <time.h>

#include "psprotocol.h"
#include "logging.h"
#include "config_parsing.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Write complete buffer.
 *
 * Write the complete buffer @a buf of size @a count to the file
 * descriptor @a fd. Even if one or more trials to write to @a fd
 * fails due to e.g. timeouts, further writing attempts are made until
 * either a fatal error occurred or the whole buffer is sent.
 *
 * @param fd The file descriptor to send the buffer to.
 *
 * @param buf The buffer to send.
 *
 * @param count The number of bytes within @a buf to send.
 *
 * @return Upon success the number of bytes sent is returned,
 * i.e. usually this is @a count. Otherwise -1 is returned.
 */
int PSID_writeall(int fd, const void *buf, size_t count);

/**
 * @brief Read complete buffer.
 *
 * Read the complete buffer @a buf of size @a count from the file
 * descriptor @a fd. Even if one or more trials to read to @a fd fails
 * due to e.g. timeouts, further reading attempts are made until
 * either a fatal error occurred, an EOF is received or the whole
 * buffer is read.
 *
 * @param fd The file descriptor to read the buffer from.
 *
 * @param buf The buffer to read.
 *
 * @param count The maximum number of bytes to read.
 *
 * @return Upon success the number of bytes read is returned,
 * i.e. usually this is @a count if no EOF occurred. Otherwise -1 is
 * returned.
 */
int PSID_readall(int fd, void *buf, size_t count);

/** Information passed to script's callback function. */
typedef struct {
    int iofd;      /**< File-descriptor serving script's stdout/stderr. */
    void *info;    /**< Extra information to be passed to callback. */
} PSID_scriptCBInfo_t;

/**
 * @brief Script callback
 *
 * Callback used by @ref PSID_execScript() to handle the
 * file-descriptor registered into the Selector. This file-descriptor
 * will deliver information concerning the result of the script,
 * i.e. the int return value of the system() call executing the
 * script.
 *
 * The first argument is the file-descriptor to handle. The second
 * argument will on the one hand contain the file-descriptor of the
 * stdout and stderr streams of the script. On the other hand some
 * optional pointer to extra information is included. This information
 * has to be provided to @ref PSID_execScript() as the last argument.
 */
typedef int PSID_scriptCB_t(int, PSID_scriptCBInfo_t *);

/**
 * @brief Script environment preparation
 *
 * Function used to prepare the environment the script is running
 * in. The pointer argument might be used to pass extra information to
 * this function. This information has to be provieded to @ref
 * PSID_execScript() as the last argument.
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
 * preparational function @a prep.
 *
 * Calling this function without callback lets it act in a blocked
 * fashion. I.e. it will not return until @a script has finished
 * execution. In this case the scripts exit-value is returned.
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
 * to insufficient resources. If a callback @a cb is given, 0 is
 * returned upon success. Otherwise the exit-value of @a script is
 * returned.
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
 * information is used to decide, if the local node is capable to take
 * part in the cluster-action.
 *
 * - "nodeupscript" request the name of the script called by the
 * master daemon whenever a node becomes active in the concert of
 * daemons within a cluster.
 *
 * - "nodedownscript" request the name of the script called by the
 * master daemon whenever a node disappeares from the concert of
 * daemons within a cluster.
 *
 * Before registration @a script will be tested on existance and
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

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDSCRIPTS_H */
