/*
 * ParaStation
 *
 * Copyright (C) 2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psgwcomm.h"

#include <stdint.h>
#include <stdio.h>

#include "pscommon.h"
#include "psenv.h"
#include "pspluginprotocol.h"
#include "psserial.h"
#include "pluginconfig.h"
#include "pluginmalloc.h"

#include "peloguetypes.h"

#include "psgwconfig.h"
#include "psgwlog.h"
#include "psgwrequest.h"
#include "psgwres.h"

typedef enum {
    PSP_PELOGUE_OE,	    /**< forward pelogue script stdout/stderr */
} PSP_PSGW_t;

int handlePelogueOE(void *pedata)
{
    PElogue_OEdata_t *oeData = pedata;
    PElogueChild_t *child = oeData->child;

    /* don't forward messages requested by psslurm or other plugins */
    int fwOE = getConfValueI(config, "PELOGUE_LOG_OE");
    if (!fwOE) return 0;

    /* forward output to leader */
    PS_SendDB_t data;

    initFragBuffer(&data, PSP_PLUG_PSGW, PSP_PELOGUE_OE);
    setFragDest(&data, PSC_getTID(child->mainPElogue, 0));

    /* allocation ID */
    addStringToMsg(child->jobid, &data);
    /* pelogue type */
    addInt8ToMsg(child->type, &data);
    /* output type */
    addInt8ToMsg(oeData->type, &data);
    /* message */
    addStringToMsg(oeData->msg, &data);

    sendFragMsg(&data);

    return 0;
}

static void handlePElogueOEMsg(DDTypedBufferMsg_t *msg, PS_DataBuffer_t data)
{
    int8_t PElogueType, msgType;

    /* allocation ID */
    char *jobid = getStringM(data);
    /* pelogue type */
    getInt8(data, &PElogueType);
    /* output type */
    getInt8(data, &msgType);
    /* message */
    char *msgData = getStringM(data);

    PSGW_Req_t *req = Request_find(jobid);
    if (!req) {
	flog("request for job %s not found\n", jobid);
	ufree(msgData);
	ufree(jobid);
	return;
    }

    char *cwd = envGet(req->env, "SLURM_SPANK_PSGW_CWD");
    char path[1024];
    snprintf(path, sizeof(path), "%s/JOB-%s-psgwd-n%i.%s", cwd, req->jobid,
	     PSC_getID(msg->header.sender),
	     msgType == PELOGUE_OE_STDOUT ? "out" : "err");

    writeErrorFile(req, msgData, path, false);

    ufree(msgData);
    ufree(jobid);
}

bool handlePSGWmsg(DDTypedBufferMsg_t *msg)
{
    char sender[48], dest[48];

    snprintf(sender, sizeof(sender), "%s", PSC_printTID(msg->header.sender));
    snprintf(dest, sizeof(dest), "%s", PSC_printTID(msg->header.dest));

    fdbg(PSGW_LOG_DEBUG, "msg type:(%i) [%s->%s]\n", msg->type, sender, dest);

    switch (msg->type) {
	case PSP_PELOGUE_OE:
	    recvFragMsg(msg, handlePElogueOEMsg);
	    break;
	default:
	    flog("received unknown msg type: %i [%s -> %s]\n",
		 msg->type, sender, dest);
    }
    return true;
}
