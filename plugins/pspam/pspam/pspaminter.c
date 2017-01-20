/*
 * ParaStation
 *
 * Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pspamuser.h"

#include "pspaminter.h"

void psPamAddUser(char *username, char *jobID, PSPAMState_t state)
{
    addUser(username, jobID, state);
}

void psPamSetState(char *username, char *jobID, PSPAMState_t state)
{
    setState(username, jobID, state);
}

void psPamDeleteUser(char *username, char *jobID)
{
    deleteUser(username, jobID);
}
