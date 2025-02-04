/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_PMIX_LOG
#define __PS_PMIX_LOG

#include <stdint.h>
#include <stdio.h>
#include <pthread.h>

#include "logging.h"
#include "pstask.h"

#include "psidforwarder.h"  // IWYU pragma: keep // for PSIDfwd_printMsgf()

#include "pspmixtypes.h"

extern logger_t pmixlogger;

extern pthread_mutex_t __mlock;

#define mlog(...)					\
    do {						\
	if (pmixlogger) {				\
	    pthread_mutex_lock(&__mlock);		\
	    logger_print(pmixlogger, -1, __VA_ARGS__);	\
	    pthread_mutex_unlock(&__mlock);		\
	}						\
    } while(0)

#define mwarn(...)					\
    do {						\
	if (pmixlogger) {				\
	    pthread_mutex_lock(&__mlock);		\
	    logger_warn(pmixlogger, -1, __VA_ARGS__);	\
	    pthread_mutex_unlock(&__mlock);		\
	}						\
    } while(0)

#define mdbg(...)					\
    do {						\
	if (pmixlogger) {				\
	    pthread_mutex_lock(&__mlock);		\
	    logger_print(pmixlogger, __VA_ARGS__);	\
	    pthread_mutex_unlock(&__mlock);		\
	}						\
    } while(0)

#define elog(...) PSIDfwd_printMsgf(STDERR, __VA_ARGS__)
#define mset(flag) (logger_getMask(pmixlogger) & (flag))

#if defined __GNUC__ && __GNUC__ < 8
#define fdbg(mask, format, ...)						\
    mdbg(mask, "%s: " format, __func__, ##__VA_ARGS__)
#define fwarn(eno, format, ...)					\
    mwarn(eno, "%s: " format, __func__, ##__VA_ARGS__)
#else
#define fdbg(mask, format, ...)						\
    mdbg(mask, "%s: " format, __func__ __VA_OPT__(,) __VA_ARGS__)
#define fwarn(eno, format, ...)					\
    mwarn(eno, "%s: " format, __func__ __VA_OPT__(,) __VA_ARGS__)
#endif

#define flog(...) fdbg(-1, __VA_ARGS__)

/** Various types of logging levels for more verbose logging */
typedef enum {
    PSPMIX_LOG_CALL    = 0x000001, /**< Log function calls */
    PSPMIX_LOG_ENV     = 0x000002, /**< Log environment stuff */
    PSPMIX_LOG_COMM    = 0x000004, /**< Log communication */
    PSPMIX_LOG_LOCK    = 0x000008, /**< Log service locking */
    PSPMIX_LOG_FENCE   = 0x000010, /**< Log fence stuff */
    PSPMIX_LOG_PROCMAP = 0x000020, /**< Log process mappings */
    PSPMIX_LOG_MODEX   = 0x000040, /**< Log modex data send/receive/forward */
    PSPMIX_LOG_INFOARR = 0x000080, /**< Log passed info arrays */
    PSPMIX_LOG_CLIENTS = 0x000100, /**< Log clients connected/disconnected */
    PSPMIX_LOG_PSET    = 0x000200, /**< Log pset creation */
    PSPMIX_LOG_SPAWN   = 0x000400, /**< Log (re)spawn stuff */
    PSPMIX_LOG_JOB     = 0x000800, /**< Log job and reservations */
    PSPMIX_LOG_VERBOSE = 0x100000, /**< Other verbose stuff */
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
 * @brief Returns string representing a PMIx job
 *
 * @param job  the job
 *
 * @return Returns the string
 */
const char *pspmix_jobStr(PspmixJob_t *job);

/**
 * @brief Returns string representing a PMIx job
 *
 * @param sessID   session id
 * @param jobID    job id
 *
 * @return Returns the string
 */
const char *pspmix_jobIDsStr(PStask_ID_t sessID, PStask_ID_t jobID);

/**
 * @brief Init logging facility
 *
 * Init pspmix plugin's logging facility. If the filehandle @a
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

/**
 * @brief Finalize logging facility
 *
 * Finalize pspmix plugin's logging facility.
 *
 * @return No return value
 */
void pspmix_finalizeLogger(void);

#endif  /* __PS_PMIX_LOG */

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
