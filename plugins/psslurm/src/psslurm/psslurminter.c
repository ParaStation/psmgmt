/*
 * ParaStation
 *
 * Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "psslurmproto.h"

#include "psslurminter.h"

slurmdHandlerFunc_t psSlurmRegisterMsg(int msgType, slurmdHandlerFunc_t handler)
{
    return registerSlurmdMsg(msgType, handler);
}

slurmdHandlerFunc_t psSlurmClearMsg(int msgType)
{
    return clearSlurmdMsg(msgType);
}
