/*
 * ParaStation
 *
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "plugin.h"
#include "pscommon.h"
#include "pscpu.h"
#include "psprotocol.h"
#include "pspluginprotocol.h"
#include "psserial.h"

#include "psidcomm.h"
#include "psidhook.h"
#include "psidhw.h"
#include "psidnodes.h"

#include "pluginmalloc.h"
#include "pluginpsconfig.h"

#include "rrcommconfig.h"
#include "rrcommlog.h"

/** psid plugin requirements */
char name[] = "rrcomm";
int version = 1;
int requiredAPI = 136;
plugin_dep_t dependencies[] = { { NULL, 0 } };

/** Packet types used within the RRComm protocol */
typedef enum {
    RRCOMM_DATA,     /**< Payload */
    RRCOMM_ERROR,    /**< Error signal */
} RRC_pkt_t;

/** Extended header of RRComm fragments */
typedef struct {
    uint32_t rank;   /**< Destination rank */
    // @todo we might have to provide namespace information here
} RRC_hdr_t;


static void handleRRCommData(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *rData)
{
    /* PSnodes_ID_t sender = PSC_getID(msg->header.sender); */
    /* char *ptr = rData->buf; */
    /* PSP_NodeInfo_t type = 0; // ensure higher bytes are all 0 */

    mdbg(RRCOMM_LOG_VERBOSE, "%s: handle message from %s\n", __func__,
	 PSC_printTID(msg->header.sender));

    /* getUint8(&ptr, &type); */
    /* while (type) { */
    /* 	mdbg(NODEINFO_LOG_VERBOSE, "%s: update type %d\n", __func__, type); */
    /* 	switch (type) { */
    /* 	case PSP_NODEINFO_CPUMAP: */
    /* 	    if (!handleCPUMapData(&ptr, sender)) return; */
    /* 	    break; */
    /* 	case PSP_NODEINFO_NUMANODES: */
    /* 	    if (!handleSetData(&ptr, sender, NULL, */
    /* 			       PSIDnodes_setCPUSets)) return; */
    /* 	    break; */
    /* 	case PSP_NODEINFO_GPU: */
    /* 	    if (!handleSetData(&ptr, sender, PSIDnodes_setNumGPUs, */
    /* 			       PSIDnodes_setGPUSets)) return; */
    /* 	    break; */
    /* 	case PSP_NODEINFO_NIC: */
    /* 	    if (!handleSetData(&ptr, sender, PSIDnodes_setNumNICs, */
    /* 			       PSIDnodes_setNICSets)) return; */
    /* 	    break; */
    /* 	case PSP_NODEINFO_REQ: */
    /* 	    sendNodeInfoData(sender); */
    /* 	    break; */
    /* 	case PSP_NODEINFO_DISTANCES: */
    /* 	    if (!handleDistanceData(&ptr, sender)) return; */
    /* 	    break; */
    /* 	case PSP_NODEINFO_CPU: */
    /* 	    if (!handleCPUData(&ptr, sender)) return; */
    /* 	    break; */
    /* 	default: */
    /* 	    mlog("%s: unknown type %d\n", __func__, type); */
    /* 	    return; */
    /* 	} */
    /* 	/\* Peek into next type *\/ */
    /* 	getUint8(&ptr, &type); */
    /* } */
}

static bool handleRRCommMsg(DDBufferMsg_t *msg)
{
    recvFragMsg((DDTypedBufferMsg_t *)msg, handleRRCommData);
    return true;
}

static bool evalValue(const char *key, const pluginConfigVal_t *val,
		      const void *info)
{
    if (!strcmp(key, "DebugMask")) {
	uint32_t mask = val ? val->val.num : 0;
	maskRRCommLogger(mask);
	mdbg(RRCOMM_LOG_VERBOSE, "debugMask set to %#x\n", mask);
    } else {
	mlog("%s: unknown key '%s'\n", __func__, key);
    }

    return true;
}


int initialize(FILE *logfile)
{
    /* init logging facility */
    initRRCommLogger(name, logfile);

    /* init configuration (depends on psconfig) */
    initRRCommConfig();

    /* Activate configuration values */
    pluginConfig_traverse(RRCommConfig, evalValue, NULL);

    if (!initSerial(0, sendMsg)) {
	mlog("%s: initSerial() failed\n", __func__);
	goto INIT_ERROR;
    }

    if (!PSID_registerMsg(PSP_PLUG_RRCOMM, handleRRCommMsg)) {
	mlog("%s: register 'PSP_PLUG_RRCOMM' handler failed\n", __func__);
	finalizeSerial();
	goto INIT_ERROR;
    }

    mlog("(%i) successfully started\n", version);

    return 0;

INIT_ERROR:
    //unregisterHooks(false);
    finalizeRRCommConfig();
    finalizeRRCommLogger();

    return 1;
}

void cleanup(void)
{
    PSID_clearMsg(PSP_PLUG_RRCOMM, handleRRCommMsg);
    finalizeSerial();
    finalizeRRCommConfig();

    mlog("...Bye.\n");

    /* release the logger */
    finalizeRRCommLogger();
}

char *help(char *key)
{
    StrBuffer_t strBuf = { .buf = NULL };

    addStrBuf("\tImplement rank routed communication\n\n", &strBuf);
    addStrBuf("\n# configuration options #\n\n", &strBuf);

    pluginConfig_helpDesc(RRCommConfig, &strBuf);

    return strBuf.buf;
}

char *set(char *key, char *val)
{
    const pluginConfigDef_t *thisDef = pluginConfig_getDef(RRCommConfig, key);

    if (!thisDef) return strdup(" Unknown option\n");

    if (!pluginConfig_addStr(RRCommConfig, key, val)
	|| !evalValue(key, pluginConfig_get(RRCommConfig, key), NULL)) {
	return strdup(" Illegal value\n");
    }

    return NULL;
}

char *unset(char *key)
{
    pluginConfig_remove(RRCommConfig, key);
    evalValue(key, NULL, RRCommConfig);

    return NULL;
}

char *show(char *key)
{
    StrBuffer_t strBuf = { .buf = NULL };

    if (!key) {
	/* Show the whole configuration */
	addStrBuf("\n", &strBuf);
	pluginConfig_traverse(RRCommConfig, pluginConfig_showVisitor,&strBuf);
    } else if (!pluginConfig_showKeyVal(RRCommConfig, key, &strBuf)) {
	addStrBuf(" '", &strBuf);
	addStrBuf(key, &strBuf);
	addStrBuf("' is unknown\n", &strBuf);
    }

    return strBuf.buf;
}