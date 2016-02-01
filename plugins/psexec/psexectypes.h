/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PSEXEC__TYPES
#define __PSEXEC__TYPES

typedef enum {
    PSP_EXEC_SCRIPT = 10,
    PSP_EXEC_SCRIPT_RES,
} PSP_PSEXEC_t;

typedef int psExec_Script_CB_t(uint32_t, int32_t, PSnodes_ID_t, uint16_t);

typedef struct {
    uint32_t id;
    uint16_t uID;
    pid_t pid;
    env_t env;
    PSnodes_ID_t origin;
    psExec_Script_CB_t *cb;
    char *execName;
    struct list_head list;
} Script_t;

#endif
