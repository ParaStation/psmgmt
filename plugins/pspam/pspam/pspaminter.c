/*
 * ParaStation
 *
 * Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2026 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pspaminter.h"

#include <stdbool.h>
#include <sys/types.h>

#include "pspamlog.h"
#include "pspamssh.h"
#include "pspamuser.h"

void psPamAddUser(char *username, char *jobID, PSPAMState_t state)
{
    fdbg(PSPAM_LOG_DEBUG, "(%s, %s, %s)\n", username, jobID, state2Str(state));
    addUser(username, jobID, state);
}

void psPamSetState(char *username, char *jobID, PSPAMState_t state)
{
    fdbg(PSPAM_LOG_DEBUG, "(%s, %s, %s)\n", username, jobID, state2Str(state));
    setState(username, jobID, state);
}

void psPamDeleteUser(char *username, char *jobID)
{
    fdbg(PSPAM_LOG_DEBUG, "user %s job %s\n", username, jobID);
    deleteUser(username, jobID);
}

bool psPamFindSessionForPID(pid_t pid)
{
    fdbg(PSPAM_LOG_DEBUG, "pid %d\n", pid);
    return findSessionForPID(pid);
}
