/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PSEXEC__TYPES
#define __PSEXEC__TYPES

#include <stdint.h>
#include <sys/types.h>

#include "list_t.h"
#include "pluginenv.h"
#include "psnodes.h"

/**
 * @doctodo
 */
typedef int psExec_Script_CB_t(uint32_t, int32_t, PSnodes_ID_t, uint16_t);

/** @doctodo */
typedef struct {
    list_t next;            /**< */
    uint32_t id;            /**< */
    uint16_t uID;           /**< */
    pid_t pid;              /**< */
    env_t env;              /**< */
    PSnodes_ID_t origin;    /**< */
    psExec_Script_CB_t *cb; /**< */
    char *execName;         /**< */
} Script_t;

/** Types of messages sent between psexec plugins */
typedef enum {
    PSP_EXEC_SCRIPT = 10,   /**< Initiate remote execution of script */
    PSP_EXEC_SCRIPT_RES,    /**< Result of remotely executed script */
} PSP_PSEXEC_t;

#endif
