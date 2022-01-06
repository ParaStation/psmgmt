/*
 * ParaStation
 *
 * Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurminter.h"

#include "psslurmmsg.h"
#include "psslurmproto.h"

slurmdHandlerFunc_t psSlurmRegMsgHandler(int msgType,
					 slurmdHandlerFunc_t handler)
{
    return registerSlurmdMsg(msgType, handler);
}

slurmdHandlerFunc_t psSlurmClrMsgHandler(int msgType)
{
    return clearSlurmdMsg(msgType);
}


Slurm_Msg_t * psSlurmDupMsg(Slurm_Msg_t *msg)
{
    return dupSlurmMsg(msg);
}

void psSlurmReleaseMsg(Slurm_Msg_t *msg)
{
    releaseSlurmMsg(msg);
}
