/*
 * ParaStation
 *
 * Copyright (C) 2022-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "list.h"
#include "plugin.h"
#include "pscommon.h"
#include "psprotocol.h"
#include "pspluginprotocol.h"
#include "psserial.h"

#include "psidclient.h"
#include "psidcomm.h"
#include "psidhook.h"
#include "psidsession.h"
#include "psidtask.h"

#include "pluginmalloc.h"
#include "pluginpsconfig.h"

#include "rrcomm_common.h"

#include "rrcommconfig.h"
#include "rrcommforwarder.h"
#include "rrcommlog.h"
#include "rrcommproto.h"

/** psid plugin requirements */
char name[] = "rrcomm";
int version = 1;
int requiredAPI = 138;
plugin_dep_t dependencies[] = { { NULL, 0 } };

/** Flags of nodes without rrcomm plugin (due to PSP_CD_UNKNOWN message) */
static bool *ineptNds = NULL;

/** Mark node @a node to have no rrcomm plugin loaded */
static void markNodeInept(PSnodes_ID_t node)
{
    if (!PSC_validNode(node)) return;
    if (!ineptNds) ineptNds = calloc(PSC_getNrOfNodes(), sizeof(*ineptNds));
    if (ineptNds) ineptNds[node] = true;
}

/** Clear inept flag for node @a node */
static void clearNodeInept(PSnodes_ID_t node)
{
    if (!ineptNds || !PSC_validNode(node)) return;
    ineptNds[node] = false;
}

/** Check if node @a node is assumed to have plugin loaded */
static bool ineptNode(PSnodes_ID_t node)
{
    if (!PSC_validNode(node) || !ineptNds) return false;
    return ineptNds[node];
}

static bool handleRRCommMsg(DDTypedBufferMsg_t *msg)
{
    clearNodeInept(PSC_getID(msg->header.sender)); // just in case...
    if (msg->header.dest != PSC_getMyTID()) {
	/* no message for me => forward */
	fdbg(msg->type == RRCOMM_ERROR ? RRCOMM_LOG_ERR : RRCOMM_LOG_VERBOSE,
	     "type %d %s", msg->type, PSC_printTID(msg->header.sender));
	mdbg(msg->type == RRCOMM_ERROR ? RRCOMM_LOG_ERR : RRCOMM_LOG_VERBOSE,
	     " -> %s\n", PSC_printTID(msg->header.dest));
	if (PSC_getID(msg->header.dest) != PSC_getMyID()) {
	    sendMsg(msg);
	} else {
	    if (PSIDclient_send((DDMsg_t *)msg) < 0 && errno != EWOULDBLOCK) {
		PSID_dropMsg((DDBufferMsg_t *)msg);
	    }
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
	PSsession_t *session = PSID_findSessionByID(hdr->loggerTID);
	if (!session) {
	    flog("no session for %s!?\n", PSC_printTID(hdr->loggerTID));
	    return PSID_dropMsg((DDBufferMsg_t *)msg);
	}

	PSjob_t *job = PSID_findJobInSession(session, hdr->destJob);
	if (!job) {
	    flog("no job for %s!?\n", PSC_printTID(hdr->destJob));
	    return PSID_dropMsg((DDBufferMsg_t *)msg);
	}

	list_t *r;
	list_for_each(r, &job->resInfos) {
	    PSresinfo_t *res = list_entry(r, PSresinfo_t, next);
	    bool found = false;
	    if (hdr->dest < res->minRank || hdr->dest > res->maxRank) continue;
	    for (uint32_t e = 0; e < res->nEntries; e++) {
		if (hdr->dest < res->entries[e].firstRank
		    || hdr->dest > res->entries[e].lastRank) continue;
		if (ineptNode(res->entries[e].node)) {
		    return PSID_dropMsg((DDBufferMsg_t *)msg);
		}
		msg->header.dest = PSC_getTID(res->entries[e].node, 0);
		found = true;
		break;
	    }
	    if (found) break;
	}

	if (msg->header.dest != PSC_getMyTID()) {
	    /* rank lives on a remote node => forward */
	    fdbg(RRCOMM_LOG_COMM, "%s-fwd->", PSC_printTID(msg->header.sender));
	    mdbg(RRCOMM_LOG_COMM, "%s\n", PSC_printTID(msg->header.dest));
	    sendMsg(msg);
	    return true;
	}
    }

    list_t *t;
    list_for_each(t, &managedTasks) {
	PStask_t *task = list_entry(t, PStask_t, next);
	if (task->rank != hdr->dest || task->loggertid != hdr->loggerTID
	    || task->spawnertid != hdr->destJob) continue;
	if (!task->forwarder || task->deleted) continue;
	msg->header.dest = task->forwarder->tid;
	break;
    }

    if (msg->header.dest != PSC_getMyTID()) {
	/* destination forwarder identified => forward */
	fdbg(RRCOMM_LOG_COMM, "%s-fwd->", PSC_printTID(msg->header.sender));
	mdbg(RRCOMM_LOG_COMM, "%s\n", PSC_printTID(msg->header.dest));
	if (PSIDclient_send((DDMsg_t *)msg) >= 0 || errno == EWOULDBLOCK)
	    return true;
    }

    return PSID_dropMsg((DDBufferMsg_t *)msg);
}

static bool handleUnknownMsg(DDBufferMsg_t *msg)
{
    size_t used = 0;

    /* original dest */
    PStask_ID_t dest;
    PSP_getMsgBuf(msg, &used, "dest", &dest, sizeof(dest));

    /* original type */
    int16_t type;
    PSP_getMsgBuf(msg, &used, "type", &type, sizeof(type));

    if (type != PSP_PLUG_RRCOMM) return false; // fallback to other handler

    /* RRComm message */
    flog("delivery failed; make sure '%s' plugin is active on node %d\n",
	 name, PSC_getID(msg->header.sender));

    markNodeInept(PSC_getID(msg->header.sender));
    return true;
}

static bool dropRRCommMsg(DDBufferMsg_t *msg)
{
    return dropHelper((DDTypedBufferMsg_t *)msg, sendMsg);
}

static bool registerMsgHandlers(void)
{
    if (!PSID_registerMsg(PSP_PLUG_RRCOMM, (handlerFunc_t)handleRRCommMsg)) {
	flog("register PSP_PLUG_RRCOMM handler failed\n");
	return false;
    }

    if (!PSID_registerMsg(PSP_CD_UNKNOWN, handleUnknownMsg)) {
	flog("register PSP_CD_UNKNOWN handler failed\n");
	return false;
    }

    if (!PSID_registerDropper(PSP_PLUG_RRCOMM, dropRRCommMsg)) {
	flog("register PSP_PLUG_RRCOMM dropper failed\n");
	return false;
    }
    return true;
}

static void removeMsgHandlers(bool verbose)
{
    if (!PSID_clearMsg(PSP_PLUG_RRCOMM, (handlerFunc_t)handleRRCommMsg)) {
	if (verbose) flog("clear PSP_PLUG_RRCOMM handler failed\n");
    }
    if (!PSID_clearMsg(PSP_CD_UNKNOWN, handleUnknownMsg)) {
	if (verbose) flog("clear PSP_CD_UNKNOWN handler failed\n");
    }
    if (!PSID_clearDropper(PSP_PLUG_RRCOMM, dropRRCommMsg)) {
	if (verbose) flog("clear PSP_PLUG_RRCOMM dropper failed\n");
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

static int hookClearMem(void *data)
{
    free(ineptNds);
    ineptNds = NULL;

    return 0;
}

static bool attachRRCommHooks(void)
{
    if (!PSIDhook_add(PSIDHOOK_CLEARMEM, hookClearMem)) {
	flog("attaching PSIDHOOK_CLEARMEM failed\n");
	return false;
    }

    return true;
}

static void detachRRCommHooks(bool verbose)
{
    if (!PSIDhook_del(PSIDHOOK_CLEARMEM, hookClearMem)) {
	if (verbose) flog("unregister PSIDHOOK_CLEARMEM failed\n");
    }
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

    if (!attachRRCommHooks()) goto INIT_ERROR;

    if (!attachRRCommForwarderHooks()) goto INIT_ERROR;

    mlog("(%i) successfully started\n", version);

    return 0;

INIT_ERROR:
    detachRRCommForwarderHooks(false);
    detachRRCommHooks(false);
    removeMsgHandlers(false);
    finalizeSerial();
    finalizeRRCommConfig();
    finalizeRRCommLogger();

    return 1;
}

void cleanup(void)
{
    detachRRCommForwarderHooks(true);
    detachRRCommHooks(true);
    removeMsgHandlers(true);
    finalizeSerial();
    finalizeRRCommConfig();
    hookClearMem(NULL);

    mlog("...Bye.\n");

    /* release the logger */
    finalizeRRCommLogger();
}

char *help(char *key)
{
    StrBuffer_t strBuf = { .buf = NULL };

    addStrBuf("\tImplement rank routed communication\n\n", &strBuf);
    addStrBuf("\tNodes assumed to have no loaded plugin are displayed"
	      " under key 'inept'\n", &strBuf);
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

static void showInept(StrBuffer_t *strBuf)
{
    char line[80];

    bool first = true;
    addStrBuf("\tinept node(s): ", strBuf);

    if (ineptNds) {
	for (PSnodes_ID_t n = 0; n < PSC_getNrOfNodes(); n++) {
	    if (!ineptNode(n)) continue;
	    if (!first) addStrBuf(", ", strBuf);
	    snprintf(line, sizeof(line), "%d", n);
	    addStrBuf(line, strBuf);
	    first = false;
	}
    }
    if (first) addStrBuf("<none>", strBuf);
    addStrBuf("\n", strBuf);
}


char *show(char *key)
{
    StrBuffer_t strBuf = { .buf = NULL };

    if (!key) {
	/* Show the whole configuration */
	addStrBuf("\n", &strBuf);
	pluginConfig_traverse(RRCommConfig, pluginConfig_showVisitor,&strBuf);
    } else if (!strcmp(key, "inept")) {
	showInept(&strBuf);
    } else if (!pluginConfig_showKeyVal(RRCommConfig, key, &strBuf)) {
	addStrBuf(" '", &strBuf);
	addStrBuf(key, &strBuf);
	addStrBuf("' is unknown\n", &strBuf);
    }

    return strBuf.buf;
}
