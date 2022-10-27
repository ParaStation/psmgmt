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
#include <string.h>

#include "list.h"
#include "plugin.h"
#include "pscommon.h"
#include "psprotocol.h"
#include "pspluginprotocol.h"
#include "psserial.h"

#include "psidclient.h"
#include "psidcomm.h"
#include "psidsession.h"
#include "psidtask.h"

#include "pluginmalloc.h"
#include "pluginpsconfig.h"

#include "rrcommconfig.h"
#include "rrcommforwarder.h"
#include "rrcommlog.h"
#include "rrcommproto.h"

/** psid plugin requirements */
char name[] = "rrcomm";
int version = 1;
int requiredAPI = 136;
plugin_dep_t dependencies[] = { { NULL, 0 } };

static bool handleRRCommMsg(DDTypedBufferMsg_t *msg)
{
    if (msg->header.dest != PSC_getMyTID()) {
	/* no messsage for me => forward */
	fdbg(RRCOMM_LOG_VERBOSE, "%s->", PSC_printTID(msg->header.sender));
	mdbg(RRCOMM_LOG_VERBOSE, "%s", PSC_printTID(msg->header.dest));
	if (PSC_getID(msg->header.dest) != PSC_getMyID()) {
	    sendMsg(msg);
	} else {
	    PSIDclient_send((DDMsg_t *)msg);
	}
	return true;
    }

    if (msg->type != RRCOMM_DATA) {
	flog("no routing for non-data packets -> drop\n");
	return true;
    }

    /* Take a peek into the header to determine the next destination */
    RRComm_hdr_t *hdr;
    size_t used = 0, hdrSize;
    fetchFragHeader(msg, &used, NULL, NULL, (void **)&hdr, &hdrSize);
    if (hdrSize != sizeof(*hdr)) {
	flog("broken extra header from %s (%zd/%zd)\n",
	     PSC_printTID(msg->header.sender), hdrSize, sizeof(*hdr));
	return PSID_dropMsg((DDBufferMsg_t *)msg);
    }

    if (PSC_getID(msg->header.sender) == PSC_getMyID()) {
	/* determine destination node */
	PSsession_t *session = PSID_findSessionByLoggerTID(hdr->loggerTID);
	if (!session) {
	    flog("no session for %s!?\n", PSC_printTID(hdr->loggerTID));
	    return PSID_dropMsg((DDBufferMsg_t *)msg);
	}

	PSjob_t *job = PSID_findJobInSession(session, hdr->spawnerTID);
	if (!job) {
	    flog("no job for %s!?\n", PSC_printTID(hdr->spawnerTID));
	    return PSID_dropMsg((DDBufferMsg_t *)msg);
	}

	list_t *r;
	list_for_each(r, &job->resInfos) {
	    PSresinfo_t *res = list_entry(r, PSresinfo_t, next);
	    bool found = false;
	    if (hdr->dest < res->minRank || hdr->dest > res->maxRank) continue;
	    for (uint32_t e = 0; e < res->nEntries; e++) {
		if (hdr->dest < res->entries[e].firstrank
		    || hdr->dest > res->entries[e].lastrank) continue;
		msg->header.dest = PSC_getTID(res->entries[e].node, 0);
		found = true;
		break;
	    }
	    if (found) break;
	}

	if (msg->header.dest != PSC_getMyTID()) {
	    /* rank lives on a remote node => forward */
	    sendMsg(msg);
	    return true;
	}
    }

    list_t *t;
    list_for_each(t, &managedTasks) {
	PStask_t *task = list_entry(t, PStask_t, next);
	if (task->rank != hdr->dest || task->loggertid != hdr->loggerTID
	    || task->spawnertid != hdr->spawnerTID) continue;
	if (!task->forwarder || task->deleted) continue;
	msg->header.dest = task->forwarder->tid;
	break;
    }

    if (msg->header.dest != PSC_getMyTID()) {
	/* destination forwarder identified => forward */
	PSIDclient_send((DDMsg_t *)msg);
	return true;
    }

    return PSID_dropMsg((DDBufferMsg_t *)msg);
}

static bool dropRRCommMsg(DDTypedBufferMsg_t *msg)
{
    RRComm_hdr_t *hdr;
    uint8_t fragType;
    size_t used = 0, hdrSize;
    fetchFragHeader(msg, &used, &fragType, NULL, (void **)&hdr, &hdrSize);

    /* Silently drop non-data fragments and all fragments but the last */
    if (msg->type != RRCOMM_DATA || fragType != FRAGMENT_END) return true;

    DDTypedBufferMsg_t answer = {
	.header = {
	    .type = PSP_PLUG_RRCOMM,
	    .sender = msg->header.dest,
	    .dest = msg->header.sender,
	    .len = 0, /* to be set by PSP_putTypedMsgBuf */ },
	.type = RRCOMM_ERROR,
	.buf = { '\0' } };
    /* Add all information we have concerning the message */
    PSP_putTypedMsgBuf(&answer, "hdr", hdr, sizeof(*hdr));

    sendMsg(&answer);
    return true;
}

static bool registerMsgHandlers(void)
{
    if (!PSID_registerMsg(PSP_PLUG_RRCOMM, (handlerFunc_t)handleRRCommMsg)) {
	flog("register 'PSP_PLUG_RRCOMM' handler failed\n");
	return false;
    }

    if (!PSID_registerDropper(PSP_PLUG_RRCOMM, (handlerFunc_t)dropRRCommMsg)) {
	flog("register 'PSP_PLUG_RRCOMM' dropper failed\n");
	return false;
    }
    return true;
}

static void removeMsgHandlers(bool verbose)
{
    if (!PSID_clearMsg(PSP_PLUG_RRCOMM, (handlerFunc_t)handleRRCommMsg)) {
	if (verbose) flog("clear 'PSP_PLUG_RRCOMM' handler failed\n");
    }
    if (!PSID_clearDropper(PSP_PLUG_RRCOMM, (handlerFunc_t)dropRRCommMsg)) {
	if (verbose) flog("clear 'PSP_PLUG_RRCOMM' dropper failed\n");
    }
}

static bool evalValue(const char *key, const pluginConfigVal_t *val,
		      const void *info)
{
    if (!strcmp(key, "DebugMask")) {
	uint32_t mask = val ? val->val.num : 0;
	maskRRCommLogger(mask);
	mdbg(RRCOMM_LOG_VERBOSE, "debugMask set to %#x\n", mask);
    } else {
	flog("unknown key '%s'\n", key);
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
	flog("initSerial() failed\n");
	goto INIT_ERROR;
    }

    if (!registerMsgHandlers()) goto INIT_ERROR;

    if (!attachRRCommForwarderHooks()) goto INIT_ERROR;

    mlog("(%i) successfully started\n", version);

    return 0;

INIT_ERROR:
    detachRRCommForwarderHooks(false);
    removeMsgHandlers(false);
    finalizeSerial();
    finalizeRRCommConfig();
    finalizeRRCommLogger();

    return 1;
}

void cleanup(void)
{
    detachRRCommForwarderHooks(true);
    removeMsgHandlers(true);
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
