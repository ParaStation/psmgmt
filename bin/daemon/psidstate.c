/*
 * ParaStation
 *
 * Copyright (C) 2008-2018 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psidstate.h"

#include <stdbool.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <signal.h>

#include "pscommon.h"
#include "psprotocol.h"

#include "mcast.h"
#include "rdp.h"

#include "psidutil.h"
#include "psidcomm.h"
#include "psidclient.h"
#include "psidstatus.h"
#include "psidhw.h"
#include "psidnodes.h"
#include "psidplugin.h"
#include "psidhook.h"

/**
 * The internal status of the ParaStation daemon. This is used for the
 * functionality provided by PSID_shutdown() and PSID_reset(). This
 * status might be requested from the outside via
 * PSID_getDaemonState().
 *
 * @see PSID_shutdown(), PSID_reset(), PSID_getDaemonState()
 */
static PSID_DaemonState_t daemonState = PSID_STATE_NONE;

PSID_DaemonState_t PSID_getDaemonState(void)
{
    return daemonState;
}

void PSID_shutdown(void)
{
    static int phase = -1;
    int numPlugins;

    phase++;
    PSID_log(-1, "%s(%d)\n", __func__, phase);

    switch (phase) {
    case 0:
	daemonState |= PSID_STATE_SHUTDOWN;
	PSID_registerLoopAct(PSID_shutdown);
	PSIDhook_call(PSIDHOOK_SHUTDOWN, NULL);
	PSID_disableMasterSock();
	if (!PSIDclient_killAll(SIGTERM, false))
	    /* no clients => proceed immediately to next phase */
	    PSID_shutdown();
	break;
    case 1:
	if (!PSIDclient_killAll(SIGTERM, false)) PSID_shutdown();
	break;
    case 2:
	if (!PSIDclient_killAll(SIGKILL, false)) PSID_shutdown();
	break;
    case 3:
	if (!PSIDclient_killAll(SIGTERM, true)) PSID_shutdown();
	break;
    case 4:
	if (!PSIDclient_killAll(SIGKILL, true)) PSID_shutdown();
	break;
    case 5:
	PSIDplugin_setUnloadTmout(2);
	PSIDplugin_forceUnloadAll();
	if (!PSIDplugin_getNum()) PSID_shutdown();
	break;
    case 6:
	numPlugins = PSIDplugin_getNum();
	if (numPlugins) {
	    PSID_log(-1, "    Still %d plugins\n", numPlugins);
	    /* Stay in this phase */
	    phase--;
	    return;
	}
	if (!PSID_config->useMCast) {
	    releaseStatusTimer();
	} else {
	    exitMCast();
	}
	PSID_stopAllHW(); /* must be here due to RDP-broadcasting HW change */
	send_DAEMONSHUTDOWN(); /* shuts down the RDP connections */
	exitRDP();
	PSID_unregisterLoopAct(PSID_shutdown);
	PSID_shutdownMasterSock(); /* used for locking => ALAP */
	PSID_log(-1, "%s: good bye\n", __func__);
	PSID_finalizeLogs();
	exit(0);
    default:
	PSID_log(-1, "%s: unknown phase %d\n", __func__, phase);
    }
}

void PSID_reset(void)
{
    static int phase = -1;
    int num;

    phase++;
    PSID_log(-1, "%s(%d)\n", __func__, phase);

    if (!PSIDclient_getNum(false)) {
	/* reset the hardware if demanded */
	if (daemonState & PSID_STATE_RESET_HW) {
	    PSID_log(-1, "%s: resetting hardware", __func__);
	    PSID_stopAllHW();
	    PSID_startAllHW();
	}
	/* reset the state */
	daemonState &= ~(PSID_STATE_RESET | PSID_STATE_RESET_HW);
	if (phase) PSID_unregisterLoopAct(PSID_reset);
	phase = -1;
	PSID_log(-1, "%s: done\n", __func__);
	return;
    }

    switch (phase) {
    case 0:
	daemonState |= PSID_STATE_RESET;
	PSID_registerLoopAct(PSID_reset);
	if (!PSIDclient_killAll(SIGTERM, false))
	    /* no clients => proceed immediately to next phase */
	    PSID_reset();
	break;
    case 1:
	if (!PSIDclient_killAll(SIGTERM, false)) PSID_reset();
	break;
    case 2:
	num = PSIDclient_killAll(SIGKILL, false);
	if (num) {
	    PSID_log(-1, "%s: still %d clients\n", __func__, num);
	    /* Stay in this phase */
	    phase--;
	    return;
	}
	PSID_reset();
	break;
    default:
	PSID_log(-1, "%s: unknown phase %d\n", __func__, phase);
    }
}

/**
 * @brief Handle a PSP_CD_DAEMONSTART message
 *
 * Handle the message @a msg of type PSP_CD_DAEMONSTART.
 *
 * This message type is used in order to trigger the startup of a
 * remote daemon via (x)inetd.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_DAEMONSTART(DDBufferMsg_t *msg)
{
    PSnodes_ID_t starter = PSC_getID(msg->header.dest);
    PSnodes_ID_t node = *(PSnodes_ID_t *) msg->buf;

    PSID_log(PSID_LOG_STATUS, "%s: received (starter=%d node=%d)\n",
	     __func__, starter, node);

    if (!PSID_checkPrivilege(msg->header.sender)) {
	PSID_log(-1, "%s: task %s not allowed to start daemons\n", __func__,
		 PSC_printTID(msg->header.sender));
    } else if (starter==PSC_getMyID()) {
	if (PSC_validNode(node)) {
	    if (!PSIDnodes_isUp(node)) {
		in_addr_t addr = PSIDnodes_getAddr(node);
		if (addr != INADDR_ANY)	PSC_startDaemon(addr);
	    } else {
		PSID_log(-1, "%s: node %d already up\n", __func__, node);
	    }
	}
    } else {
	if (PSIDnodes_isUp(starter)) {
	    /* forward message */
	    sendMsg(&msg);
	} else {
	    PSID_log(-1, "%s: starter %d is down\n", __func__, starter);
	}
    }
    return true;
}

/**
 * @brief Handle a PSP_CD_DAEMONSTOP message
 *
 * Handle the message @a msg of type PSP_CD_DAEMONSTOP.
 *
 * If the local node is the final destination of the message, it will
 * be stopped using @ref shutdownNode(). Otherwise @a msg will be
 * forwarded to the correct destination.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_DAEMONSTOP(DDMsg_t *msg)
{
    if (!PSID_checkPrivilege(msg->sender)) {
	PSID_log(-1, "%s: task %s not allowed to stop daemons\n", __func__,
		 PSC_printTID(msg->sender));
    } else if (PSC_getID(msg->dest) == PSC_getMyID()) {
	if (!(PSID_getDaemonState() & PSID_STATE_SHUTDOWN)) PSID_shutdown();
    } else {
	sendMsg(msg);
    }
    return true;
}

/**
 * @brief Handle a PSP_CD_DAEMONRESET message
 *
 * Handle the message @a msg of type PSP_CD_DAEMONRESET.
 *
 * If the local node is the final destination of the message, it will
 * be reseted using @ref doReset(). Otherwise @a msg will be forwarded
 * to the correct destination.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_DAEMONRESET(DDBufferMsg_t *msg)
{
    if (!PSID_checkPrivilege(msg->header.sender)) {
	PSID_log(-1, "%s: task %s not allowed to reset daemons\n", __func__,
		 PSC_printTID(msg->header.sender));
    } else if (PSC_getID(msg->header.dest) == PSC_getMyID()) {
	if (*(int *)msg->buf & PSP_RESET_HW) daemonState |= PSID_STATE_RESET_HW;
	/* Resetting my node */
	if (!(PSID_getDaemonState() & PSID_STATE_RESET)) PSID_reset();
    } else {
	sendMsg(msg);
    }
    return true;
}

void initState(void)
{
    PSID_log(PSID_LOG_VERB, "%s()\n", __func__);

    PSID_registerMsg(PSP_CD_DAEMONSTART, msg_DAEMONSTART);
    PSID_registerMsg(PSP_CD_DAEMONSTOP, (handlerFunc_t) msg_DAEMONSTOP);
    PSID_registerMsg(PSP_CD_DAEMONRESET, msg_DAEMONRESET);
}
