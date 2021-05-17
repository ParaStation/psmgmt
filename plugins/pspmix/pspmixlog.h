/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_PMIX_LOG
#define __PS_PMIX_LOG

#include <pthread.h>

#include "logging.h"
#include "psidforwarder.h"

#include "pspmixtypes.h"

extern logger_t *pmixlogger;

extern pthread_mutex_t __mlock;

#define mlog(...) \
    do { \
	if (pmixlogger) { \
	    pthread_mutex_lock(&__mlock); \
	    logger_print(pmixlogger, -1, __VA_ARGS__); \
	    pthread_mutex_unlock(&__mlock); \
	} \
    } while(0)

#define mwarn(...) \
    do { \
	if (pmixlogger) { \
	    pthread_mutex_lock(&__mlock); \
	    if (pmixlogger) logger_warn(pmixlogger, -1, __VA_ARGS__); \
	    pthread_mutex_unlock(&__mlock); \
	} \
    } while(0)

#define mdbg(...) \
    do { \
	if (pmixlogger) { \
	    pthread_mutex_lock(&__mlock); \
	    if (pmixlogger) logger_print(pmixlogger, __VA_ARGS__); \
	    pthread_mutex_unlock(&__mlock); \
	} \
    } while(0)

#define elog(...) PSIDfwd_printMsgf(STDERR, __VA_ARGS__)
#define mset(flag) (logger_getMask(pmixlogger) & flag)

/** Various types of logging levels for more verbose logging */
typedef enum {
    PSPMIX_LOG_CALL    = 0x000001, /**< Log function calls */
    PSPMIX_LOG_ENV     = 0x000002, /**< Log environment stuff */
    PSPMIX_LOG_COMM    = 0x000004, /**< Log communication */
    PSPMIX_LOG_LOCK    = 0x000008, /**< Log service locking */
    PSPMIX_LOG_FENCE   = 0x000010, /**< Log fence stuff */
    PSPMIX_LOG_PROCMAP = 0x000020, /**< Log process mappings */
    PSPMIX_LOG_VERBOSE = 0x000100, /**< Other verbose stuff */
} PSPMIX_log_types_t;

/**
 * @brief Returns string representing sub-type of message type PSP_PLUG_PSPMIX
 *
 * @param type  the type
 *
 * @return Returns the string
 */
const char *pspmix_getMsgTypeString(PSP_PSPMIX_t type);

/**
 * @brief Init logging facility
 *
 * Init pspmi plugin's logging facility. If the filehandle @a
 * logfile is different from NULL, the corresponding file will be used
 * for logging. Otherwise the syslog facility is used.
 *
 * @param name Tag used for logging
 *
 * @param logfile File to use for logging
 *
 * @return No return value
 */
void pspmix_initLogger(char *name, FILE *logfile);

/**
 * @brief Set logger's debug mask
 *
 * Set the logger's debug mask to @a mask. @a mask is expected to be a
 * bit-field of type @ref PSPMIX_log_types_t.
 *
 * @param mask Bit-field of type @ref PSPMIX_log_types_t
 *
 * @return No return value
 */
void pspmix_maskLogger(int32_t mask);

#endif  /* __PS_PMIX_LOG */

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
